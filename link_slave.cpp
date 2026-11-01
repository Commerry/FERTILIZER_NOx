// ============================================================================
//  link_slave.cpp
// ============================================================================
#include "link_slave.h"
#include "config.h"
#include "link_protocol.h"
#include "actuators.h"
#include "logbook.h"
#include "settings.h"      // settings_get() -- ใช้ในการทยอยส่ง $K (paced config dump)

Telemetry g_tlm;

static LinkReader s_rx;
static LinkWriter s_tx;              // ตัวเดียว ใช้ซ้ำ -- ประหยัด RAM (128 B ต่อตัว)

static uint32_t s_last_tlm   = 0;
static uint32_t s_last_rx_ms = 0;
static uint32_t s_seq        = 0;

// --- การทยอยส่ง config ($K) เป็นชุด ๆ (paced) ---
//   เดิมยิง 58 เฟรมรวดเดียว -> (ก) จอ RX buffer ล้น เฟรมท้าย (PIN) หาย -> config ไม่ sync
//   -> จอขอ $G ซ้ำไม่จบ -> ลิงก์ตัน ACK timeout คำสั่งหาย   (ข) Nano บล็อก ~300ms ต่อ $G
//   แก้: ส่งทีละ KV_BATCH key เว้นจังหวะ KV_GAP_MS ให้จอ drain ทัน -- ไม่บล็อก loop
static bool     s_kvActive = false;
static uint8_t  s_kvNext   = 0;
static uint32_t s_kvLastMs = 0;
#define KV_BATCH    4
#define KV_GAP_MS   25

void link_begin_config_dump()      // เรียกจาก link_on_get_config() ($G เข้ามา)
{
    s_kvActive = true;
    s_kvNext   = 0;
    s_kvLastMs = 0;                // ส่งชุดแรกทันทีในรอบ link_task ถัดไป
}

static void kv_pump()
{
    if (!s_kvActive) return;
    if ((uint32_t)(millis() - s_kvLastMs) < KV_GAP_MS) return;
    s_kvLastMs = millis();
    for (uint8_t i = 0; i < KV_BATCH && s_kvActive; i++) {
        link_send_kv(s_kvNext, settings_get(s_kvNext));
        if (++s_kvNext >= CFG_KEY_COUNT) s_kvActive = false;
    }
}

// ---------------------------------------------------------------------------
static void send_frame()
{
    const char *f = s_tx.finish();
    if (f) LINK_SERIAL.write(f, s_tx.length());
}

static void send_ack(long seq, uint8_t err)
{
    s_tx.begin(LNK_T_ACK);
    s_tx.addLong(seq);
    if (err == LNK_ERR_NONE) {
        s_tx.addStr("OK");
    } else {
        s_tx.addStr("ERR");
        s_tx.addLong(err);
    }
    send_frame();
}

void link_send_kv(uint8_t key, float value)
{
    s_tx.begin(LNK_T_KV);
    s_tx.addLong(key);
    s_tx.addFloat(value, 4);         // 4 ตำแหน่ง -- scale/offset ต้องการความละเอียด
    send_frame();
}

void link_send_raw(long seq, const uint16_t *words, uint8_t n)
{
    s_tx.begin(LNK_T_RAW);
    s_tx.addLong(seq);
    for (uint8_t i = 0; i < n; i++) s_tx.addHex16(words[i]);
    send_frame();
}

void link_send_log(uint8_t seq, uint32_t ms, uint8_t type, uint8_t a, uint8_t b)
{
    s_tx.begin(LNK_T_LOG);
    s_tx.addLong(seq);
    s_tx.addLong((long)ms);
    s_tx.addLong(type);
    s_tx.addLong(a);
    s_tx.addLong(b);
    send_frame();
}

// ---------------------------------------------------------------------------
static void send_telemetry()
{
    s_tx.begin(LNK_T_DATA);
    s_tx.addLong((long)(++s_seq));
    s_tx.addFloat(g_tlm.o2a, 2);
    s_tx.addFloat(g_tlm.o2b, 2);
    s_tx.addFloat(g_tlm.pressure, 3);
    s_tx.addLong(act_relay_bits());
    s_tx.addLong(act_get_valve());
    s_tx.addLong(g_tlm.mode);

    uint8_t flags = g_tlm.errflags;
    if (act_is_estop()) flags |= LNK_F_ESTOP;
    s_tx.addLong(flags);

    s_tx.addLong(g_tlm.runState);
    s_tx.addLong(g_tlm.rvZone);
    s_tx.addLong((long)g_tlm.elapsed);

    send_frame();
}

// ---------------------------------------------------------------------------
static void handle(const LinkFrame &f)
{
    s_last_rx_ms = millis();

    switch (f.type) {

    case LNK_T_CMD: {                       // $C,<seq>,<CMD>,<a1>,<a2>
        long    seq = f.asLong(0);
        uint8_t err = link_on_command(f.asStr(1), f.asLong(2), f.asLong(3));
        send_ack(seq, err);
        break;
    }

    case LNK_T_SET: {                       // $S,<seq>,<key>,<value>
        long    seq = f.asLong(0);
        long    key = f.asLong(1);
        uint8_t err = (key < 0 || key >= CFG_KEY_COUNT)
                          ? LNK_ERR_BAD_KEY
                          : link_on_set_config((uint8_t)key, f.asFloat(2));
        send_ack(seq, err);
        break;
    }

    case LNK_T_GET: {                       // $G,<seq>
        long seq = f.asLong(0);

        // *** ต้อง ACK ก่อน แล้วค่อยยิง $K ***
        //
        // ACK แปลว่า "รับคำสั่งแล้ว" ไม่ใช่ "ทำงานเสร็จแล้ว"
        // ถ้าเรียงกลับกัน: 58 เฟรม x ~20 ไบต์ = 1160 ไบต์ @38400 = ~300 ms
        // แต่จอรอ ACK แค่ LINK_ACK_TIMEOUT_MS (200 ms) -> มันจะ retry $G
        // แล้วเรายิง config ซ้ำอีก 4 ชุด + จอขึ้น ACK timeout ทั้งที่ทุกอย่างปกติ
        send_ack(seq, LNK_ERR_NONE);
        link_on_get_config();               // แล้วค่อยทยอยยิง $K ทีละ key
        break;
    }

    case LNK_T_MB: {                        // $M,<seq>,<slave>,<fc>,<reg>,<arg>
        long    seq = f.asLong(0);          // fc 3/4: arg=จำนวน · fc 6: arg=ค่าที่เขียน
        uint8_t err = link_on_modbus_raw((uint8_t)f.asLong(1),
                                         (uint8_t)f.asLong(2),
                                         (uint16_t)f.asLong(3),
                                         (uint16_t)f.asLong(4),
                                         seq);
        // ถ้าสำเร็จ link_on_modbus_raw จะส่ง $R เองแล้ว -- ตรงนี้ตอบ ACK ตามหลัง
        send_ack(seq, err);
        break;
    }

    case LNK_T_LOGACK:                      // $P,<seq> -- ESP32 รับ log ถึง seq นี้แล้ว
        logbook_ack((uint8_t)f.asLong(0));  // *** ไม่ต้องตอบ ACK กลับ *** (มันคือ ack เอง)
        break;                              //     ถ้าตอบจะวน ack ไม่จบ

    default:
        // เฟรมที่ Nano ไม่สนใจ (เช่น $D ที่สะท้อนกลับมา) -- ทิ้งเงียบ ๆ
        break;
    }
}

// ---------------------------------------------------------------------------
// ลิงก์จอเป็น Hardware UART (D0/D1) -- LINK_SERIAL = Serial (ดู link_slave.h)
//
// ข้อแลกที่รู้กัน: ระหว่าง SoftwareSerial คุย Modbus (ปิด interrupt ~1ms/byte)
// ไบต์จากจอที่เข้ามาพอดีช่วงนั้นอาจหาย -> เฟรมตก CRC -> จอ retry เอง
// (โปรโตคอลออกแบบมาให้ทนหลุดตั้งแต่แรก -- CRC + ACK + ส่งซ้ำ)
//
// *** ห้ามมี Serial.print() debug ที่ไหนเลย -- Serial คือสายจอ ***

void link_begin()
{
    LINK_SERIAL.begin(LINK_BAUD);
    s_last_rx_ms = 0;
    s_last_tlm   = millis();
}

void link_task()
{
    // 1) รับ -- อ่านให้หมด buffer ในรอบเดียว ไม่งั้น HW UART buffer (64 B) ล้น
    LinkFrame f;
    while (LINK_SERIAL.available()) {
        if (s_rx.feed(LINK_SERIAL.read(), f)) handle(f);
    }

    // 2) ทยอยส่ง config ($K) ถ้ามี $G ค้างอยู่ -- เว้นจังหวะ ไม่บล็อก
    kv_pump();

    // 3) ส่ง telemetry ตามจังหวะ
    uint32_t now = millis();
    if (now - s_last_tlm >= LINK_TELEMETRY_MS) {
        s_last_tlm = now;
        send_telemetry();
    }
}

bool link_is_up()
{
    // ยังไม่เคยได้รับอะไรเลย = ยังไม่ขึ้น
    if (s_last_rx_ms == 0) return false;
    return (millis() - s_last_rx_ms) < LINK_LOSS_MS;
}

uint32_t link_rx_ok()      { return s_rx.rx_ok; }
uint32_t link_rx_crc_err() { return s_rx.rx_crc_err; }
