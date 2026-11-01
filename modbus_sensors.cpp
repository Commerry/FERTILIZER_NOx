// ============================================================================
//  modbus_sensors.cpp  --  Modbus RTU master บน SoftwareSerial (D2/D3), 2 บัสแยก
//
//  *** เขียน RTU เองทั้งหมด ไม่ใช้ไลบรารี ModbusMaster แล้ว ***
//
//  เหตุผล: ModbusMaster ตั้ง timeout ไว้ 2000 ms เป็น "static const" ในตัวไลบรารี
//  แก้จากข้างนอกไม่ได้เลย  ถ้าเซนเซอร์ตัวไหนไม่ตอบ (สายหลุด / ตั้ง slave ID ผิด /
//  ยังไม่ได้เสียบ) loop() จะค้าง 2 วินาที ต่อการ poll หนึ่งครั้ง
//  -> ลิงก์จอขาดเกิน LINK_LOSS_MS -> จอขึ้น OFFLINE ทั้งที่ Nano ยังดีอยู่
//
//  ของเราคุม timeout เองได้ (g_cfg.timeoutMs -- ตั้งจากหน้าจอ, default 300 ms)
//
//  ---------------------------------------------------------------------------
//  ทำไม "2 บัส" -- ดู config.h ข้อ 2 ฉบับเต็ม  สรุปสั้น:
//  pressure (24V) หลังตอบครั้งแรกมันขับบัสค้าง (driver hold) O2 เลยพูดไม่ขึ้น
//  -> แยก: DE1 (D6) = บัส O2  /  DE2 (D7) = บัส pressure  โดย RO/DI ใช้ร่วมกัน
//
//  กติกา complement (ห้ามพลาด): DE+RE ของแต่ละโมดูลพ่วงกัน
//    ขา LOW  = เปิดหูฟัง  -> RO โมดูลนั้นขับสาย D2
//    ขา HIGH = ปิดหู+ขับบัสตัวเองค้างที่ idle (DI ว่าง = mark)
//    *** ห้ามให้สองขา LOW พร้อมกัน  ไม่งั้น RO สองตัวชนกันบน D2 ***
//  ตอนส่ง: เปิด HIGH ทั้งคู่ได้ (เฟรมออกทั้งสองบัส -- ตัวที่ address ไม่ตรงจะเมิน)
//  ตอนรอตอบ: เฉพาะบัสเป้าหมาย LOW อีกตัวค้าง HIGH
//
//  ---------------------------------------------------------------------------
//  3 เรื่องที่ RS485 half-duplex พลาดบ่อย -- พลาดข้อไหนก็อ่านไม่ได้ทั้งนั้น
//
//  1) ปล่อย DE เร็วไป = ตัดหางเฟรมตัวเอง
//     (SoftwareSerial.write() บิตแบงจนจบ stop bit แล้วค่อย return -- ปลอดภัย
//      แต่เผื่อ guard สั้น ๆ ก่อนสลับ DE อยู่ดี)
//
//  2) ปล่อย DE ช้าไป = ไปชนกับคำตอบของ slave
//     แต่ Modbus บังคับ slave ให้เงียบอย่างน้อย T3.5 ก่อนตอบ -> เผื่อ 60us พอ
//
//  3) ต้องเงียบครบ T3.5 ก่อนขึ้นเฟรมใหม่
//     ไม่งั้น slave เหมาว่าเฟรมใหม่เป็นหางของเฟรมเก่า -> CRC ไม่ผ่าน แล้วเงียบใส่
// ============================================================================
#include "modbus_sensors.h"
#include <SoftwareSerial.h>
#include "config.h"
#include "link_protocol.h"

static float    s_val[CFG_SENSOR_COUNT] = { 0, 0, 0 };
static bool     s_ok[CFG_SENSOR_COUNT]  = { false, false, false };
static uint8_t  s_idx       = 0;        // poll ทีละตัว วนไป
static uint32_t s_lastPoll  = 0;
static uint32_t s_busIdleAt = 0;        // สายจะว่างพอให้ยิงเฟรมใหม่เมื่อไหร่ (T3.5)
static bool     s_busQuiet  = true;     // ไม่มีใครตอบเลยสักตัว

// SoftwareSerial ตัวเดียวของทั้งเฟิร์มแวร์ (ลิงก์จอย้ายไป HW UART แล้ว
// -> ไม่มีปัญหา listen() แย่งกัน)
static SoftwareSerial s_mb(PIN_RS485_RO, PIN_RS485_DI);   // (RX, TX)

// เลือกบัสจาก slave id: id ของ pressure -> บัส 2, ที่เหลือ -> บัส 1 (O2)
// อิงจาก config จริง (ตั้งจากจอได้) ไม่ hardcode เลข 3
static inline uint8_t mb_bus_pin(uint8_t slave)
{
    return (slave == g_cfg.sensor[CFG_SENSOR_PRESS].slaveId)
               ? PIN_RS485_DE2 : PIN_RS485_DE1;
}

// idle state: จอดหูที่บัส O2-B (DE3=LOW) เพื่อดักเฟรม FF ที่มันพ่นเอง
// -- ยังถูกกติกา complement (มีตัวเดียวที่ LOW)
static inline void mb_idle_state()
{
    digitalWrite(PIN_RS485_DE1, HIGH);
    digitalWrite(PIN_RS485_DE2, HIGH);
    digitalWrite(PIN_RS485_DE3, LOW);
}

// ---------------------------------------------------------------------------
void mb_begin()
{
    pinMode(PIN_RS485_DE1, OUTPUT);
    pinMode(PIN_RS485_DE2, OUTPUT);
    pinMode(PIN_RS485_DE3, OUTPUT);
    mb_idle_state();

    s_mb.begin(g_cfg.mbBaud);
    s_mb.listen();                      // SoftwareSerial RX พร้อมฟัง (มี pullup ในตัว)

    s_lastPoll  = millis();
    s_busIdleAt = millis();
}

#if O2B_PASSIVE_FF
// ---------------------------------------------------------------------------
//  ตัวดักเฟรม FF ของ O2#2 (active upload):
//     FF 01 07 01 <HI> <LO> 00 00 00 <CS>   CS = (sum byte1..8 - 1) & 0xFF
//  เดินทุก byte ที่เข้ามาตอนบัสว่างผ่านตัวนี้ -- เจอเฟรมถูก = ได้ค่า O2B ฟรี ๆ
// ---------------------------------------------------------------------------
static uint8_t  s_ffBuf[10];
static uint8_t  s_ffLen     = 0;
static uint32_t s_ffLastOk  = 0;        // เฟรมดีล่าสุดเมื่อไหร่ (เอาไว้ตัดสิน s_ok)
static bool     s_ffEver    = false;    // เคยได้เฟรม FF ที่ถูกต้องแล้วหรือยัง
//   กัน O2B ขึ้น "OK" หลอกช่วง 12 วิแรกหลังบูต (now-0 < 12000 = จริง) ทั้งที่ยังไม่มีเฟรมเข้าเลย

static void ff_feed(uint8_t b)
{
    if (s_ffLen == 0 && b != 0xFF) return;          // รอหัวเฟรม
    s_ffBuf[s_ffLen++] = b;
    if (s_ffLen < 10) return;
    s_ffLen = 0;

    // ตรวจโครง + checksum
    if (s_ffBuf[1] != 0x01 || s_ffBuf[2] != 0x07) return;
    uint8_t sum = 0;
    for (uint8_t i = 1; i <= 8; i++) sum = (uint8_t)(sum + s_ffBuf[i]);
    if ((uint8_t)(sum - 1) != s_ffBuf[9]) return;

    uint16_t raw = (uint16_t)(((uint16_t)s_ffBuf[4] << 8) | s_ffBuf[5]);
    if (raw > 3000) return;                          // เกิน 300% = ขยะ
    s_val[CFG_SENSOR_O2B] = raw * 0.1f;
    s_ffLastOk = millis();
    s_ffEver   = true;
}

// เทของที่ค้างในบัฟเฟอร์เข้า parser -- เรียกบ่อย ๆ จาก mb_task
static void ff_drain()
{
    while (s_mb.available()) ff_feed((uint8_t)s_mb.read());
}
#endif

// เคลียร์ RX ก่อนเริ่ม transaction -- ถ้าเปิดโหมดดักฟัง เอาไปเข้า parser ก่อนทิ้ง
static inline void mb_flush_rx()
{
#if O2B_PASSIVE_FF
    ff_drain();
#else
    while (s_mb.available()) s_mb.read();
#endif
}

// ---------------------------------------------------------------------------
//  CRC-16/MODBUS  (poly 0xA001 = 0x8005 กลับบิต, init 0xFFFF)
// ---------------------------------------------------------------------------
static uint16_t mb_crc(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*b++);
        for (uint8_t i = 0; i < 8; i++)
            c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

// T3.5 = เวลา 3.5 ตัวอักษร (1 ตัวอักษร = 11 บิต รวม start/stop)
// 3.5 x 11 = 38.5 บิต  ->  ms = 38500 / baud   (ปัดขึ้น 1 กันเศษหาย)
static inline uint16_t mb_t35_ms()
{
    uint32_t b = g_cfg.mbBaud ? g_cfg.mbBaud : RS485_BAUD_DEFAULT;
    return (uint16_t)(38500UL / b) + 1;
}

// ---------------------------------------------------------------------------
//  ถอดค่าดิบ -> เลขจริง
//
//  เซนเซอร์จีนมี word order ไม่เหมือนกัน (ABCD vs CDAB) ซึ่งเป็นสาเหตุยอดฮิต
//  ที่อ่านค่าออกมาแล้วเป็นเลขมั่ว -> เลยเปิดให้เลือกจากหน้าจอได้
// ---------------------------------------------------------------------------
float mb_decode(const uint16_t *w, uint8_t nRegs, uint8_t dtype, float scale, float offset)
{
    (void)nRegs;
    uint32_t u32;
    float    raw;

    switch (dtype) {
    case LNK_DT_U16:
        raw = (float)w[0];
        break;

    case LNK_DT_S16:
        raw = (float)(int16_t)w[0];
        break;

    case LNK_DT_U32_ABCD:
        u32 = ((uint32_t)w[0] << 16) | w[1];
        raw = (float)u32;
        break;

    case LNK_DT_U32_CDAB:
        u32 = ((uint32_t)w[1] << 16) | w[0];
        raw = (float)u32;
        break;

    case LNK_DT_F32_ABCD:
        u32 = ((uint32_t)w[0] << 16) | w[1];
        memcpy(&raw, &u32, 4);
        break;

    case LNK_DT_F32_CDAB:
        u32 = ((uint32_t)w[1] << 16) | w[0];
        memcpy(&raw, &u32, 4);
        break;

    default:
        raw = 0.0f;
        break;
    }

    return raw * scale + offset;
}

// ---------------------------------------------------------------------------
//  อ่าน register ดิบ -- ใช้ทั้งตอน poll ปกติ และตอน Scanner
// ---------------------------------------------------------------------------
uint8_t mb_raw_read(uint8_t slave, uint8_t fc, uint16_t reg, uint8_t n, uint16_t *out)
{
    if (n < 1 || n > 8) return 0;
    if (fc != 4) fc = 3;                       // รองรับ FC3 (holding) กับ FC4 (input)

    const uint8_t dePin = mb_bus_pin(slave);        // บัสของ slave ตัวนี้

    // ---- 1) รอสายว่างครบ T3.5 ก่อน แล้วค่อยขึ้นเฟรมใหม่ ----
    while ((int32_t)(millis() - s_busIdleAt) < 0) { /* รอ */ }
    mb_flush_rx();                                  // เคลียร์ค้าง (เฟรม FF เข้า parser ก่อน)

    // ---- 2) ประกอบเฟรมคำขอ ----
    uint8_t req[8];
    req[0] = slave;
    req[1] = fc;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = 0;
    req[5] = n;
    uint16_t c = mb_crc(req, 6);
    req[6] = (uint8_t)(c & 0xFF);              // Modbus ส่ง CRC ไบต์ต่ำก่อน
    req[7] = (uint8_t)(c >> 8);

    // ---- 3) ส่ง (ยก HIGH ทุกโมดูล = ปิดหูหมด, เฟรมออกทุกบัส --
    //          ตัวที่ address ไม่ตรงเมินเอง ตามสเปค Modbus) ----
    digitalWrite(PIN_RS485_DE1, HIGH);
    digitalWrite(PIN_RS485_DE2, HIGH);
    digitalWrite(PIN_RS485_DE3, HIGH);
    delayMicroseconds(50);                     // ให้ transceiver ตั้งตัว
    s_mb.write(req, 8);                        // บิตแบง block จน stop bit สุดท้ายออกจริง
    delayMicroseconds(60);
    digitalWrite(dePin, LOW);                  // เปิดหูเฉพาะบัสเป้าหมาย (อีกขาค้าง HIGH)

    // ---- 4) รอคำตอบ:  [addr][fc][byteCount][data...][crcLo][crcHi] ----
    const uint8_t need = (uint8_t)(5 + 2 * n);      // n<=8 -> ไม่เกิน 21 ไบต์
    uint8_t  rsp[21];
    uint8_t  got      = 0;
    uint32_t deadline = millis() + g_cfg.timeoutMs;

    while (got < need) {
        if ((int32_t)(millis() - deadline) >= 0) {  // เงียบจนหมดเวลา -> เลิก
            mb_idle_state();                        // กลับ idle (เปิดหูบัส O2)
            s_busIdleAt = millis() + mb_t35_ms();
            return 0;
        }
        if (!s_mb.available()) continue;

        uint8_t b = (uint8_t)s_mb.read();

        // ต้องจับ "หัวเฟรม" ให้ตรงก่อน ไม่งั้นขยะบนสาย 1 ไบต์
        // จะทำให้ทุกไบต์หลังจากนั้นเลื่อนตำแหน่งหมด แล้ว CRC ไม่มีทางผ่าน
        switch (got) {
        case 0:
            // บัสแยกกันแล้ว: ไบต์ช่วง transaction = ของบัสที่ poll อยู่เท่านั้น
            // ห้ามป้อนเข้า ff parser (นั่นของบัส O2-B ตอน idle เท่านั้น)
            if (b != slave) continue;                       // ยังไม่ใช่หัวเฟรมของเรา
            break;
        case 1:
            if (b == (uint8_t)(fc | 0x80)) {                // slave ตอบ exception
                mb_idle_state();
                s_busIdleAt = millis() + mb_t35_ms();
                return 0;
            }
            if (b != fc) { got = 0; continue; }
            break;
        case 2:
            if (b != (uint8_t)(2 * n)) { got = 0; continue; }   // byte count ไม่ตรง
            break;
        }
        rsp[got++] = b;
    }

    mb_idle_state();                           // ได้ครบแล้ว -> กลับ idle (เปิดหูบัส O2)
    s_busIdleAt = millis() + mb_t35_ms();

    // ---- 5) เช็ค CRC ----
    uint16_t want = (uint16_t)(rsp[need - 2] | ((uint16_t)rsp[need - 1] << 8));
    if (mb_crc(rsp, (uint8_t)(need - 2)) != want) return 0;

    for (uint8_t i = 0; i < n; i++)
        out[i] = (uint16_t)(((uint16_t)rsp[3 + 2 * i] << 8) | rsp[4 + 2 * i]);

    return n;
}

// ---------------------------------------------------------------------------
//  เขียน register เดียว (FC06) -- ใช้ตั้ง Slave ID / เขียนค่า config ของเซนเซอร์
//
//  คืน true ถ้าเซนเซอร์ตอบ echo ถูกต้อง (FC06 ตอบกลับด้วยเฟรมเดิมเป๊ะ)
//
//  *** กรณีตั้ง ID: ดาต้าชีตบอกว่าเซนเซอร์ตอบด้วย "address เดิม" ก่อน
//      แล้วค่อยเปลี่ยนเป็น ID ใหม่หลังส่งคำตอบ ***
//  -> เรายิงไปที่ ID เดิม เซนเซอร์ echo กลับด้วย ID เดิม + reg/val ที่เขียน -> ตรง -> true
//     (ID ใหม่จะมีผลรอบถัดไป)
// ---------------------------------------------------------------------------
bool mb_raw_write(uint8_t slave, uint16_t reg, uint16_t val)
{
    const uint8_t dePin = mb_bus_pin(slave);        // บัสของ slave ตัวนี้

    // 1) รอสายว่างครบ T3.5 แล้วเคลียร์ขยะค้าง
    while ((int32_t)(millis() - s_busIdleAt) < 0) { /* รอ */ }
    mb_flush_rx();

    // 2) ประกอบเฟรม FC06:  [addr][06][regHi][regLo][valHi][valLo][crcLo][crcHi]
    uint8_t req[8];
    req[0] = slave;
    req[1] = 0x06;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = (uint8_t)(val >> 8);
    req[5] = (uint8_t)(val & 0xFF);
    uint16_t c = mb_crc(req, 6);
    req[6] = (uint8_t)(c & 0xFF);
    req[7] = (uint8_t)(c >> 8);

    // 3) ส่ง (กติกาเดียวกับ mb_raw_read)
    digitalWrite(PIN_RS485_DE1, HIGH);
    digitalWrite(PIN_RS485_DE2, HIGH);
    digitalWrite(PIN_RS485_DE3, HIGH);
    delayMicroseconds(50);
    s_mb.write(req, 8);
    delayMicroseconds(60);
    digitalWrite(dePin, LOW);                  // เปิดหูเฉพาะบัสเป้าหมาย

    // 4) รอ echo (FC06 ตอบด้วย 8 ไบต์เดิม)
    uint8_t  rsp[8];
    uint8_t  got      = 0;
    uint32_t deadline = millis() + g_cfg.timeoutMs;

    while (got < 8) {
        if ((int32_t)(millis() - deadline) >= 0) {
            mb_idle_state();
            s_busIdleAt = millis() + mb_t35_ms();
            return false;
        }
        if (!s_mb.available()) continue;

        uint8_t b = (uint8_t)s_mb.read();
        switch (got) {
        case 0:
            if (b != slave) continue;                       // จับหัวเฟรมให้ตรงก่อน
            break;
        case 1:
            if (b == (uint8_t)(0x06 | 0x80)) {              // exception
                mb_idle_state();
                s_busIdleAt = millis() + mb_t35_ms();
                return false;
            }
            if (b != 0x06) { got = 0; continue; }
            break;
        }
        rsp[got++] = b;
    }

    mb_idle_state();                           // จบ -> กลับ idle (เปิดหูบัส O2)
    s_busIdleAt = millis() + mb_t35_ms();

    // 5) เช็ค CRC + echo ต้องตรง reg/val ที่เขียน
    uint16_t want = (uint16_t)(rsp[6] | ((uint16_t)rsp[7] << 8));
    if (mb_crc(rsp, 6) != want) return false;

    uint16_t rReg = ((uint16_t)rsp[2] << 8) | rsp[3];
    uint16_t rVal = ((uint16_t)rsp[4] << 8) | rsp[5];
    return (rReg == reg && rVal == val);
}

// ---------------------------------------------------------------------------
//  poll ทีละตัว -- ไม่ยิงรวดเดียว 3 ตัว เพราะช่วง SoftwareSerial ทำงาน
//  interrupt ปิด ~1ms/byte -> เฟรมจากจอ (HW UART) เสี่ยงตกหล่นนานเกิน
//  ยิงทีละตัวต่อรอบ = เปิดหน้าต่างให้ลิงก์จอหายใจระหว่าง poll
// ---------------------------------------------------------------------------
void mb_task()
{
    uint32_t now = millis();

#if O2B_PASSIVE_FF
    ff_drain();                      // ดักเฟรม FF ของ O2-B ที่พ่นเข้ามาตอนจอดหูบัส DE3
    // หน้าต่างความสด 12 วิ: ตอน poll O1/PRESS (โดยเฉพาะ O1 timeout 300ms) หูปิดจากบัส D7
    // เฟรม FF ช่วงนั้นหายเป็นชุด -- 6 วิยังแคบไป ทำให้กะพริบเวลา O1 กระพริบ
    // FF พ่นทุก ~0.6-1.2 วิ -> 12 วิ = เผื่อพลาดต่อเนื่องได้ ~10 เฟรมยังไม่หลุด
    s_ok[CFG_SENSOR_O2B] = s_ffEver && (uint32_t)(now - s_ffLastOk) < 12000UL;
#endif

    if (now - s_lastPoll < g_cfg.pollMs) return;
    s_lastPoll = now;

#if O2B_PASSIVE_FF
    if (s_idx == CFG_SENSOR_O2B)     // O2B ไม่ต้อง poll -- ได้ค่าจากการดักฟังแล้ว
        s_idx = (uint8_t)((s_idx + 1) % CFG_SENSOR_COUNT);
#endif

    const SensorCfg &c = g_cfg.sensor[s_idx];
    uint16_t w[2] = { 0, 0 };

    uint8_t got = mb_raw_read(c.slaveId, c.fc, c.reg, c.nRegs, w);

    if (got == c.nRegs) {
        s_val[s_idx] = mb_decode(w, c.nRegs, c.dtype, c.scale, c.offset);
        s_ok[s_idx]  = true;
    } else {
        s_ok[s_idx] = false;         // ค่าเก่าค้างไว้ -- ไม่กระโดดเป็น 0 บนจอ
    }

    // มีสักตัวตอบไหม
    s_busQuiet = !(s_ok[0] || s_ok[1] || s_ok[2]);

    s_idx = (uint8_t)((s_idx + 1) % CFG_SENSOR_COUNT);
}

// ---------------------------------------------------------------------------
float mb_value(uint8_t i) { return (i < CFG_SENSOR_COUNT) ? s_val[i] : 0.0f; }
bool  mb_ok(uint8_t i)    { return (i < CFG_SENSOR_COUNT) ? s_ok[i]  : false; }

uint8_t mb_errflags()
{
    uint8_t f = 0;
    if (!s_ok[CFG_SENSOR_O2A])   f |= LNK_F_O2A_FAIL;
    if (!s_ok[CFG_SENSOR_O2B])   f |= LNK_F_O2B_FAIL;
    if (!s_ok[CFG_SENSOR_PRESS]) f |= LNK_F_PR_FAIL;
    if (s_busQuiet)              f |= LNK_F_MB_BUS;
    return f;
}
