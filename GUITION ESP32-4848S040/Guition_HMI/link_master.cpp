// ============================================================================
//  link_master.cpp
// ============================================================================
#include "link_master.h"
#include "board_pins.h"
#include "link_protocol.h"
#include "app_state.h"
#include "sdlog.h"
#include "rtclock.h"

AppState g_app;

// de-dup ของ $L: Nano ส่ง record ซ้ำจนกว่าจะได้ ack -> ประมวลผลเฉพาะตัวใหม่
static uint8_t s_lastLogSeq = 0;
static bool    s_haveLogSeq = false;

static LinkReader s_rx;
static LinkLogCb  s_logCb = nullptr;
static LinkRawCb  s_rawCb = nullptr;
static LinkKvCb   s_kvCb  = nullptr;

// ---------------------------------------------------------------------------
//  คิวคำสั่ง
//
//  ส่งทีละคำสั่ง แล้วรอ ACK ก่อนส่งตัวถัดไป -- เรียบง่ายและตามลำดับเสมอ
//  ถ้าไม่ได้ ACK ใน LINK_ACK_TIMEOUT_MS จะส่งซ้ำ สูงสุด LINK_ACK_RETRIES ครั้ง
// ---------------------------------------------------------------------------
#define CMD_QUEUE_LEN   8

struct PendingCmd {
    char     frame[LINK_MAX_FRAME];
    uint16_t len;
    uint32_t seq;
};

static PendingCmd s_queue[CMD_QUEUE_LEN];
static uint8_t    s_qHead = 0;      // ตัวที่กำลังส่ง
static uint8_t    s_qTail = 0;      // ตัวถัดไปที่จะเขียนลง
static uint8_t    s_qCount = 0;

static bool     s_waitingAck = false;
static uint32_t s_sentAtMs   = 0;
static uint8_t  s_retries    = 0;
static uint32_t s_nextSeq    = 1;

// ---------------------------------------------------------------------------
static void queue_push(LinkWriter &w, uint32_t seq)
{
    const char *f = w.finish();
    if (!f) return;                                  // เฟรมยาวเกิน -- ทิ้ง
    if (s_qCount >= CMD_QUEUE_LEN) return;           // คิวเต็ม -- ทิ้งคำสั่งเก่าสุดไม่ได้
                                                     // เพราะลำดับสำคัญ จึงทิ้งตัวใหม่แทน
    PendingCmd &c = s_queue[s_qTail];
    memcpy(c.frame, f, w.length());
    c.frame[w.length()] = '\0';
    c.len = w.length();
    c.seq = seq;

    s_qTail = (uint8_t)((s_qTail + 1) % CMD_QUEUE_LEN);
    s_qCount++;
}

static void transmit(const PendingCmd &c)
{
    Serial.write((const uint8_t *)c.frame, c.len);
    g_app.txFrames++;
    if (s_logCb) s_logCb(c.frame, true);
}

// ---------------------------------------------------------------------------
//  ถอดรหัส log record -- ชื่อที่คนอ่านออก
// ---------------------------------------------------------------------------
static const char *dev_name(uint8_t i)
{
    static const char *N[4] = { "CP", "CB", "FB", "FP" };
    return (i < 4) ? N[i] : "?";
}

static const char *zone_name(uint8_t z)
{
    switch (z) {
    case LNK_RVZ_MAXOUT: return "MAX OUT";
    case LNK_RVZ_ROUGH:  return "ROUGH";
    case LNK_RVZ_MEDIUM: return "MEDIUM";
    case LNK_RVZ_FINE:   return "FINE";
    default:             return "OFF";
    }
}

static const char *alarm_name(uint8_t bit)
{
    switch (bit) {
    case LNK_F_O2A_FAIL:    return "O2 MAIN FAIL";
    case LNK_F_O2B_FAIL:    return "O2 CHECK FAIL";
    case LNK_F_PR_FAIL:     return "PRESSURE FAIL";
    case LNK_F_MB_BUS:      return "RS485 BUS DOWN";
    case LNK_F_ESTOP:       return "E-STOP";
    case LNK_F_CFG_BAD:     return "EEPROM CORRUPT";
    case LNK_F_O2_MISMATCH: return "O2 SENSOR MISMATCH";
    default:                return "UNKNOWN";
    }
}

// ตอบ $P,<seq> -- ส่งตรง ไม่ผ่านคิวคำสั่ง (มันคือ ack เอง ไม่ต้องการ ack กลับ)
static void send_logack(uint8_t seq)
{
    LinkWriter w;
    w.begin(LNK_T_LOGACK);
    w.addLong(seq);
    const char *fr = w.finish();
    if (fr) Serial.write((const uint8_t *)fr, w.length());
}

// ประมวลผล 1 log record -> เขียน SD (EVENTS + ALARMS ถ้าเข้าข่าย)
static void log_record(uint32_t ms, uint8_t type, uint8_t a, uint8_t b)
{
    uint32_t epoch = clock_now();
    uint32_t up    = ms / 1000UL;
    char dt[24];
    clock_str_datetime(dt, epoch);

    const char *ev;
    char detail[40];
    detail[0] = '\0';
    bool        isAlarm  = false;
    const char *alName   = nullptr;
    bool        alSet    = true;

    switch (type) {
    case LNK_LOG_BOOT:  ev = "BOOT";          break;
    case LNK_LOG_START: ev = "START";         break;
    case LNK_LOG_STOP:  ev = "STOP";          break;
    case LNK_LOG_ESTOP: ev = "E-STOP";
        isAlarm = true; alName = "E-STOP"; alSet = true; break;
    case LNK_LOG_MODE:  ev = "MODE";
        strcpy(detail, a == LNK_MODE_AUTO ? "AUTO" : "MANUAL"); break;
    case LNK_LOG_ZONE:  ev = "ZONE";
        strcpy(detail, zone_name(a)); break;
    case LNK_LOG_RELAY: ev = "RELAY";
        snprintf(detail, sizeof(detail), "%s %s", dev_name(a), b ? "ON" : "OFF"); break;
    case LNK_LOG_ALARM: ev = "ALARM";
        alName = alarm_name(a); alSet = (b != 0); isAlarm = true;
        snprintf(detail, sizeof(detail), "%s %s", alName, alSet ? "SET" : "CLEAR"); break;
    case LNK_LOG_SAVED: ev = "CONFIG SAVED";  break;
    default:            ev = "?";             break;
    }

    char line[SDLOG_LINE_MAX];
    snprintf(line, sizeof(line), "%s,%lu,%s,%s", dt, (unsigned long)up, ev, detail);
    sdlog_push(LOGF_EVENT, epoch, line);

    // alarm/error -> ลง ALARMS ด้วย (ซับเซ็ตแยกไฟล์ให้หาง่าย)
    if (isAlarm) {
        snprintf(line, sizeof(line), "%s,%lu,%s,%s",
                 dt, (unsigned long)up, alName, alSet ? "SET" : "CLEAR");
        sdlog_push(LOGF_ALARM, epoch, line);
    }
}

// ---------------------------------------------------------------------------
//  จัดการเฟรมที่รับมา
// ---------------------------------------------------------------------------
static void handle(const LinkFrame &f, const char *raw)
{
    switch (f.type) {

    // $D,<seq>,<o2a>,<o2b>,<press>,<bits>,<valve>,<mode>,<err>,<runState>,<rvZone>,<elapsed>
    case LNK_T_DATA: {
        g_app.seq         = (uint32_t)f.asLong(0);
        g_app.o2a         = f.asFloat(1);
        g_app.o2b         = f.asFloat(2);
        g_app.pressure    = f.asFloat(3);
        g_app.relayBits   = (uint8_t)f.asLong(4);
        g_app.valve       = (uint8_t)f.asLong(5);
        g_app.mode        = (uint8_t)f.asLong(6);
        g_app.errflags    = (uint8_t)f.asLong(7);
        g_app.runState    = (uint8_t)f.asLong(8);
        g_app.rvZone      = (uint8_t)f.asLong(9);
        g_app.elapsed     = (uint32_t)f.asLong(10);
        g_app.lastFrameMs = millis();
        g_app.linkUp      = true;
        break;
    }

    case LNK_T_ACK: {           // $A,<seq>,OK  |  $A,<seq>,ERR,<code>
        uint32_t seq = (uint32_t)f.asLong(0);
        if (s_waitingAck && s_qCount > 0 && s_queue[s_qHead].seq == seq) {
            // ACK ตรงกับตัวที่กำลังรออยู่ -> เอาออกจากคิว
            s_qHead      = (uint8_t)((s_qHead + 1) % CMD_QUEUE_LEN);
            s_qCount--;
            s_waitingAck = false;
            s_retries    = 0;
        }
        break;
    }

    case LNK_T_RAW: {           // $R,<seq>,<hex>,<hex>...
        if (s_rawCb) {
            uint16_t w[8];
            uint8_t  n = 0;
            for (uint8_t i = 1; i < f.count && n < 8; i++)
                w[n++] = (uint16_t)strtoul(f.asStr(i), nullptr, 16);
            s_rawCb(w, n);
        }
        break;
    }

    case LNK_T_KV: {            // $K,<key>,<value>
        if (s_kvCb) s_kvCb((uint8_t)f.asLong(0), f.asFloat(1));
        break;
    }

    case LNK_T_LOG: {           // $L,<seq>,<ms>,<type>,<a>,<b>
        uint8_t  seq  = (uint8_t)f.asLong(0);
        uint32_t ms   = (uint32_t)f.asLong(1);
        uint8_t  type = (uint8_t)f.asLong(2);
        uint8_t  a    = (uint8_t)f.asLong(3);
        uint8_t  b    = (uint8_t)f.asLong(4);

        // *** ack ก่อน แล้วค่อยประมวลผล *** -- ack = "รับแล้ว" ไม่ใช่ "เขียน SD เสร็จ"
        // ถ้าไม่มีการ์ดก็ยัง ack เพื่อให้ Nano ทิ้ง record ได้ ไม่ส่งซ้ำไม่จบ
        send_logack(seq);

        // de-dup: ตัวซ้ำ (ack หาย -> Nano ส่งซ้ำ) ข้ามไป  แต่ BOOT ทำเสมอ
        // (Nano รีบูต -> seq รีเซ็ต -> ต้องยอมรับ ไม่งั้นเหตุการณ์แรกหลังบูตหาย)
        bool dup = (s_haveLogSeq && seq == s_lastLogSeq && type != LNK_LOG_BOOT);
        s_lastLogSeq = seq;
        s_haveLogSeq = true;
        if (!dup) log_record(ms, type, a, b);
        break;
    }

    default:
        break;
    }

    if (s_logCb) s_logCb(raw, false);
}

// ---------------------------------------------------------------------------
void link_begin()
{
    // UART0 บนขา header ของบอร์ด (GPIO43/44)
    // *** ขาเดียวกับชิพ USB -- ห้าม Serial.print debug ระหว่างลิงก์ทำงาน ***

    // *** setRxBufferSize ต้องมา "ก่อน" begin เสมอ ***
    //   บน ESP32 Arduino core พอ UART driver ติดตั้งแล้ว (หลัง begin) การเรียกนี้
    //   เป็น no-op (log error + return) -> buffer ค้างที่ default 256 ไบต์
    //   ของเดิมเรียกหลัง begin = ไม่มีผล -> นี่คือสาเหตุจริงที่ $K ท้าย ๆ (PIN) หาย
    //   -> config ไม่ sync -> จอขอ $G ซ้ำไม่จบ -> ลิงก์ตัน ACK timeout คำสั่งหาย
    //   2048 > burst (~1160 ไบต์) เผื่อ LVGL วาดจอค้างยาว link_task ไม่ได้ drain
    //   (คู่กับฝั่ง Nano ที่ทยอยส่ง $K เป็นชุด ๆ -> กันล้นอีกชั้น)
    Serial.setRxBufferSize(2048);
    Serial.begin(LINK_BAUD, SERIAL_8N1, NANO_RX, NANO_TX);
}

void link_task()
{
    // 1) รับ
    static char lineBuf[LINK_MAX_FRAME];
    static uint16_t lineLen = 0;

    LinkFrame f;
    while (Serial.available()) {
        int c = Serial.read();

        // เก็บบรรทัดดิบไว้โชว์ในหน้า Diagnostics
        // '$' = เริ่มเฟรมใหม่ -> ทิ้งขยะที่มาก่อนหน้า (เช่น boot log ของ ESP32)
        // ไม่งั้น log จะโชว์ "ets Jul 29...$D,1,20.9" ปนกันมั่ว
        if (c == '$') {
            lineLen = 0;
            lineBuf[lineLen++] = '$';
        } else if (c == '\n') {
            lineBuf[lineLen] = '\0';
        } else if (lineLen < LINK_MAX_FRAME - 1 && c != '\r') {
            lineBuf[lineLen++] = (char)c;
        }

        if (s_rx.feed(c, f)) {
            handle(f, lineBuf);
            lineLen = 0;
        } else if (c == '\n') {
            lineLen = 0;
        }
    }

    g_app.rxFrames  = s_rx.rx_ok;
    g_app.crcErrors = s_rx.rx_crc_err;

    // ไม่ได้ยินเสียง Nano นานเกินไป -> ถือว่าลิงก์หลุด
    if (g_app.linkUp && (millis() - g_app.lastFrameMs) > LINK_STALE_MS)
        g_app.linkUp = false;

    // 2) ส่ง / retry
    uint32_t now = millis();

    if (s_qCount == 0) return;

    if (!s_waitingAck) {
        transmit(s_queue[s_qHead]);
        s_waitingAck = true;
        s_sentAtMs   = now;
        s_retries    = 0;
        return;
    }

    if (now - s_sentAtMs >= LINK_ACK_TIMEOUT_MS) {
        if (s_retries < LINK_ACK_RETRIES) {
            s_retries++;
            transmit(s_queue[s_qHead]);
            s_sentAtMs = now;
        } else {
            // ยอมแพ้กับคำสั่งนี้ -- ทิ้งไป ไม่งั้นคิวจะตันตลอดกาล
            g_app.ackTimeouts++;
            s_qHead      = (uint8_t)((s_qHead + 1) % CMD_QUEUE_LEN);
            s_qCount--;
            s_waitingAck = false;
            s_retries    = 0;
        }
    }
}

// ---------------------------------------------------------------------------
//  คำสั่งต่าง ๆ
// ---------------------------------------------------------------------------
static LinkWriter s_w;      // ตัวเดียวใช้ซ้ำ -- ประกอบเฟรมแล้ว push เข้าคิวทันที

void link_cmd_relay(uint8_t idx, bool on)
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_RELAY);
    s_w.addLong(idx);
    s_w.addLong(on ? 1 : 0);
    queue_push(s_w, seq);
}

void link_cmd_valve(uint8_t percent)
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_VALVE);
    s_w.addLong(percent);
    queue_push(s_w, seq);
}

void link_cmd_mode(uint8_t mode)
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_MODE);
    s_w.addLong(mode);
    queue_push(s_w, seq);
}

void link_cmd_estop()
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_ESTOP);
    queue_push(s_w, seq);
}

void link_cmd_start()
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_START);
    queue_push(s_w, seq);
}

void link_cmd_stop()
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_STOP);
    queue_push(s_w, seq);
}

void link_cmd_save()
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_CMD);
    s_w.addLong((long)seq);
    s_w.addStr(LNK_CMD_SAVE);
    queue_push(s_w, seq);
}

void link_cmd_get_config()
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_GET);
    s_w.addLong((long)seq);
    queue_push(s_w, seq);
}

void link_cmd_set_config(uint8_t key, float value)
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_SET);
    s_w.addLong((long)seq);
    s_w.addLong(key);
    s_w.addFloat(value, 4);
    queue_push(s_w, seq);
}

// ---------------------------------------------------------------------------
//  สแนปช็อตเซนเซอร์ลง DATA เป็นช่วง -- ESP32 ทำเอง (มีค่าครบจาก telemetry อยู่แล้ว
//  ไม่ต้องกวน Nano/ลิงก์)  เก็บเฉพาะตอนลิงก์ขึ้น (ไม่งั้นได้ค่าค้าง)
//
//  relays คอลัมน์ = 4 หลัก เรียง CP CB FB FP (1 = ทำงาน)
// ---------------------------------------------------------------------------
void link_data_tick()
{
    static uint32_t s_last = 0;
    uint32_t now = millis();
    if (now - s_last < (uint32_t)LOG_DATA_INTERVAL_S * 1000UL) return;
    s_last = now;

    if (!g_app.linkUp) return;

    uint32_t epoch = clock_now();
    char dt[24];
    clock_str_datetime(dt, epoch);

    uint8_t r = g_app.relayBits;
    char line[SDLOG_LINE_MAX];
    snprintf(line, sizeof(line),
             "%s,%lu,%.2f,%.2f,%.2f,%u,%c%c%c%c,%s,%s,%s",
             dt, (unsigned long)g_app.elapsed,
             g_app.o2a, g_app.o2b, g_app.pressure, g_app.valve,
             (r & 1) ? '1' : '0', (r & 2) ? '1' : '0',
             (r & 4) ? '1' : '0', (r & 8) ? '1' : '0',
             g_app.mode == LNK_MODE_AUTO ? "AUTO" : "MAN",
             g_app.runState == LNK_RUN_RUNNING ? "RUN" : "IDLE",
             zone_name(g_app.rvZone));
    sdlog_push(LOGF_DATA, epoch, line);
}

void link_cmd_modbus_raw(uint8_t slave, uint8_t fc, uint16_t reg, uint8_t n)
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_MB);
    s_w.addLong((long)seq);
    s_w.addLong(slave);
    s_w.addLong(fc);
    s_w.addLong(reg);
    s_w.addLong(n);
    queue_push(s_w, seq);
}

// เขียน register เดียว (FC06) -- ใช้ตั้ง Slave ID
// ใช้เฟรม $M เดิม แต่ fc=6 และ field สุดท้าย = "ค่าที่เขียน" (ไม่ใช่จำนวน)
// Nano ตอบ $R กลับมาพร้อมค่าที่เขียน -> เข้า s_rawCb เหมือนอ่าน
void link_cmd_modbus_write(uint8_t slave, uint16_t reg, uint16_t value)
{
    uint32_t seq = s_nextSeq++;
    s_w.begin(LNK_T_MB);
    s_w.addLong((long)seq);
    s_w.addLong(slave);
    s_w.addLong(6);
    s_w.addLong(reg);
    s_w.addLong(value);
    queue_push(s_w, seq);
}

void link_set_log_cb(LinkLogCb cb) { s_logCb = cb; }
void link_set_raw_cb(LinkRawCb cb) { s_rawCb = cb; }
void link_set_kv_cb(LinkKvCb cb)   { s_kvCb  = cb; }
