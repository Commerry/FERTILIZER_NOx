// ############################################################################
//  !!! อ่านก่อนใช้ -- เครื่องมือนี้ใช้ผังสายคนละแบบกับตัวโปรแกรมจริง !!!
//
//  โปรแกรมจริง (Arduinonano_Display_O2_pressure) วาง RS485 ไว้บน HW UART:
//        MAX485 RO -> D0    DI -> D1    DE/RE -> D6
//
//  แต่เครื่องมือนี้ต้องคุยกับคุณผ่าน Serial Monitor (พิมพ์ scan / probe / setid)
//  ซึ่ง Serial Monitor ก็ใช้ D0/D1 -- ขาเดียวกันเป๊ะ  ใช้พร้อมกันไม่ได้
//  จึงต้องย้าย RS485 ไปอยู่ soft serial ชั่วคราว
//
//  *** ก่อนรันเครื่องมือนี้ ให้ย้าย 3 เส้น ***
//        RO :  D0  ->  D8
//        DI :  D1  ->  D9
//        DE :  D6  ->  D2
//  เสร็จแล้วย้ายกลับที่เดิม แล้วเอาค่าที่ได้ไปกรอกที่จอ SETUP -> SENSORS
// ############################################################################
// ============================================================================
//  RS485_SetID  --  เครื่องมือตั้ง Slave ID ของเซนเซอร์ Modbus RTU
//
//  ใช้ก่อนเอาเซนเซอร์ไปต่อรวมบัสเดียวกัน
//  เพราะเซนเซอร์จีนส่วนใหญ่มาจากโรงงานด้วย ID = 1 เหมือนกันหมด
//  ถ้าเอาไปต่อรวมกันเลย มันจะตอบพร้อมกันแล้วชนกันบนสาย -> อ่านไม่ได้สักตัว
//
//  *** ต่อเซนเซอร์ทีละตัวเท่านั้นตอนตั้ง ID ***
//
//  ------------------------------------------------------------------------
//  ทำไมไม่ใช้ ModbusMaster:
//    ไลบรารีนั้นล็อก response timeout ไว้ที่ 2000 ms แบบ static const
//    เปลี่ยนตอน runtime ไม่ได้ -> scan 247 ID จะกินเวลา 8 นาที
//    เขียน Modbus RTU เองตรงนี้ทำให้คุม timeout ได้ (80 ms -> scan ~20 วิ)
//    และเห็น byte ดิบ ๆ ซึ่งจำเป็นมากตอนไล่ปัญหากับของจริง
//  ------------------------------------------------------------------------
//
//  การต่อสาย (เหมือนโปรเจกต์หลัก):
//    Nano D2  -> MAX485 DE + RE (ต่อเข้าด้วยกัน)
//    Nano D8  <- MAX485 RO      (AltSoftSerial RX -- ขาตายตัว)
//    Nano D9  -> MAX485 DI      (AltSoftSerial TX -- ขาตายตัว)
//    MAX485 A/B -> เซนเซอร์  (+ 120 ohm ปลายบัส ถ้าสายยาว)
//
//    D0/D1 ว่าง -> ใช้คุยกับ Serial Monitor ได้เลย
//    (นี่คือเหตุผลที่โปรเจกต์นี้เลือก AltSoftSerial ตั้งแต่แรก
//     SoftwareSerial จะปิด interrupt จน Serial Monitor เพี้ยน)
//
//  วิธีใช้:
//    เปิด Serial Monitor  ->  115200 baud  ->  ตั้ง "Newline"
//    พิมพ์  help  แล้ว Enter
// ============================================================================
#include <AltSoftSerial.h>

// --- ขา ---------------------------------------------------------------------
#define PIN_DE        2      // MAX485 DE + RE
// AltSoftSerial ใช้ D8 (RX) / D9 (TX) ตายตัว -- กำหนดเองไม่ได้

// --- ค่าตั้งต้น --------------------------------------------------------------
#define SCAN_TIMEOUT_MS   80    // ตอน scan ใช้สั้น ๆ ให้เร็ว
#define NORMAL_TIMEOUT_MS 400   // ตอนอ่าน/เขียนปกติ เผื่อเซนเซอร์ช้า

AltSoftSerial rs485;

static uint32_t g_baud    = 9600;    // baud ที่ใช้คุยอยู่ตอนนี้
static uint16_t g_timeout = NORMAL_TIMEOUT_MS;

// baud ที่เซนเซอร์ Modbus มักตั้งมาจากโรงงาน
static const uint32_t BAUD_LIST[] = { 9600, 4800, 19200, 38400 };
#define BAUD_COUNT (sizeof(BAUD_LIST) / sizeof(BAUD_LIST[0]))

// ---------------------------------------------------------------------------
//  Modbus RTU -- เขียนเอง
// ---------------------------------------------------------------------------
#define MB_OK          0
#define MB_TIMEOUT    -1
#define MB_BAD_CRC    -2
#define MB_EXCEPTION  -3
#define MB_SHORT      -4

static uint8_t s_rsp[64];      // global กัน stack ล้น
static uint8_t s_rspLen = 0;
static uint8_t s_excCode = 0;

static uint16_t mb_crc16(const uint8_t *buf, uint8_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
    }
    return crc;
}

// ช่องว่างระหว่างเฟรม Modbus = 3.5 ตัวอักษร (11 บิต/ตัว)
static uint16_t frame_gap_ms()
{
    uint16_t g = (uint16_t)((35UL * 1000UL) / g_baud) + 2;   // +2 กันเหนียว
    return (g < 3) ? 3 : g;
}

// ส่ง request แล้วรอ response
//   คืน MB_OK พร้อมข้อมูลใน s_rsp / s_rspLen  (ยังไม่ตัด header)
static int8_t mb_txrx(uint8_t *req, uint8_t reqLen, uint16_t timeoutMs)
{
    // เติม CRC ท้าย request
    uint16_t crc = mb_crc16(req, reqLen);
    req[reqLen++] = (uint8_t)(crc & 0xFF);          // CRC ส่ง low byte ก่อน
    req[reqLen++] = (uint8_t)(crc >> 8);

    // ล้างขยะที่ค้างในบัฟเฟอร์รับก่อน
    while (rs485.available()) rs485.read();

    // --- ส่ง ---
    digitalWrite(PIN_DE, HIGH);
    delayMicroseconds(100);              // ให้ transceiver ตั้งตัว
    rs485.write(req, reqLen);
    rs485.flush();                       // ต้องรอ byte สุดท้ายไหลออกสายจริง ๆ
    delayMicroseconds(100);              // ไม่งั้นจะตัดหางเฟรมตัวเอง
    digitalWrite(PIN_DE, LOW);           // กลับเป็นโหมดรับ

    // --- รอไบต์แรก ---
    uint32_t t0 = millis();
    while (!rs485.available()) {
        if (millis() - t0 > timeoutMs) return MB_TIMEOUT;
    }

    // --- อ่านจนกว่าสายจะเงียบครบ 1 frame gap ---
    s_rspLen = 0;
    uint16_t gap = frame_gap_ms();
    uint32_t last = millis();

    while (millis() - last < gap) {
        if (rs485.available()) {
            if (s_rspLen < sizeof(s_rsp)) s_rsp[s_rspLen++] = (uint8_t)rs485.read();
            else                          rs485.read();       // ล้นก็ทิ้ง
            last = millis();
        }
    }

    if (s_rspLen < 4) return MB_SHORT;                        // สั้นเกินกว่าจะเป็นเฟรม

    // --- ตรวจ CRC ---
    uint16_t got  = (uint16_t)s_rsp[s_rspLen - 2] | ((uint16_t)s_rsp[s_rspLen - 1] << 8);
    uint16_t want = mb_crc16(s_rsp, (uint8_t)(s_rspLen - 2));
    if (got != want) return MB_BAD_CRC;

    // --- exception? (bit7 ของ function code ถูกตั้ง) ---
    if (s_rsp[1] & 0x80) {
        s_excCode = s_rsp[2];
        return MB_EXCEPTION;
    }

    return MB_OK;
}

// อ่าน holding (fc=3) หรือ input (fc=4) register
static int8_t mb_read(uint8_t id, uint8_t fc, uint16_t reg, uint8_t n,
                      uint16_t *out, uint16_t timeoutMs)
{
    uint8_t req[8];
    req[0] = id;
    req[1] = fc;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = 0;
    req[5] = n;

    int8_t rc = mb_txrx(req, 6, timeoutMs);
    if (rc != MB_OK) return rc;

    uint8_t bc = s_rsp[2];                       // byte count
    if (bc != (uint8_t)(n * 2)) return MB_SHORT;

    for (uint8_t i = 0; i < n; i++)
        out[i] = ((uint16_t)s_rsp[3 + i * 2] << 8) | s_rsp[4 + i * 2];

    return MB_OK;
}

// เขียน register เดียว (FC06)
static int8_t mb_write(uint8_t id, uint16_t reg, uint16_t val, uint16_t timeoutMs)
{
    uint8_t req[8];
    req[0] = id;
    req[1] = 0x06;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = (uint8_t)(val >> 8);
    req[5] = (uint8_t)(val & 0xFF);

    return mb_txrx(req, 6, timeoutMs);
}

// ---------------------------------------------------------------------------
//  ตัวช่วยพิมพ์
// ---------------------------------------------------------------------------
static void print_err(int8_t rc)
{
    switch (rc) {
    case MB_TIMEOUT:   Serial.print(F("ไม่ตอบ (timeout)")); break;
    case MB_BAD_CRC:   Serial.print(F("CRC ไม่ตรง -- สายรบกวน / baud ผิด")); break;
    case MB_SHORT:     Serial.print(F("เฟรมสั้นผิดปกติ")); break;
    case MB_EXCEPTION:
        Serial.print(F("exception code "));
        Serial.print(s_excCode);
        if (s_excCode == 2) Serial.print(F("  (register ไม่มีอยู่จริง)"));
        if (s_excCode == 3) Serial.print(F("  (ค่าที่ส่งไปไม่ถูกต้อง)"));
        break;
    default:           Serial.print(F("ok")); break;
    }
}

static void print_raw()
{
    Serial.print(F("      raw:"));
    for (uint8_t i = 0; i < s_rspLen; i++) {
        Serial.print(' ');
        if (s_rsp[i] < 0x10) Serial.print('0');
        Serial.print(s_rsp[i], HEX);
    }
    Serial.println();
}

static void set_baud(uint32_t b)
{
    g_baud = b;
    rs485.end();
    rs485.begin(b);
    delay(20);
}

// ---------------------------------------------------------------------------
//  scan -- กวาดหา slave ที่ baud ปัจจุบัน
//
//  นับว่า "เจอ" ทั้งกรณีตอบปกติ และกรณีตอบ exception
//  เพราะ exception = มีเซนเซอร์อยู่จริง แค่ register ที่เราถามไม่มี
// ---------------------------------------------------------------------------
static uint8_t do_scan(bool quiet)
{
    uint8_t found = 0;
    uint16_t w[2];

    Serial.print(F("scan ที่ "));
    Serial.print(g_baud);
    Serial.println(F(" baud  (ID 1-247) ..."));

    for (uint8_t id = 1; id <= 247; id++) {
        if (!quiet && (id % 40 == 0)) { Serial.print(F("  ... ")); Serial.println(id); }

        int8_t rc = mb_read(id, 3, 0, 1, w, SCAN_TIMEOUT_MS);
        if (rc == MB_TIMEOUT) rc = mb_read(id, 4, 0, 1, w, SCAN_TIMEOUT_MS);  // ลอง input reg ด้วย

        if (rc == MB_OK || rc == MB_EXCEPTION) {
            found++;
            Serial.print(F("  >>> เจอ ID "));
            Serial.print(id);
            if (rc == MB_EXCEPTION) Serial.print(F("   (ตอบ exception -- มีตัวตนแน่นอน)"));
            else                    { Serial.print(F("   reg0 = ")); Serial.print(w[0]); }
            Serial.println();
        }
    }

    Serial.print(F("เสร็จ -- เจอทั้งหมด "));
    Serial.print(found);
    Serial.println(F(" ตัว"));
    if (found == 0)
        Serial.println(F("  ไม่เจอเลย -> ลอง 'scanall' (กวาดทุก baud) หรือเช็คสาย A/B ว่าสลับกันไหม"));
    if (found > 1)
        Serial.println(F("  *** เจอหลายตัว! ตอนตั้ง ID ต้องต่อทีละตัวเท่านั้น ***"));

    return found;
}

// ---------------------------------------------------------------------------
//  probe -- หา register ที่เก็บ "ที่อยู่ของตัวเอง"
//
//  วิธี: อ่าน register ที่ผู้ผลิตนิยมใช้ แล้วดูว่าตัวไหนมีค่า "เท่ากับ ID ปัจจุบัน"
//        ถ้าเจอ -> นั่นแหละคือ register ที่ต้องเขียนเพื่อเปลี่ยน ID
// ---------------------------------------------------------------------------
static const uint16_t PROBE_REGS[] PROGMEM = {
    0x0000, 0x0001, 0x0002,
    0x000F, 0x0010,
    0x001E, 0x0032, 0x0064,
    0x00FB,
    0x0100, 0x0101, 0x0102,
    0x0200,
    0x03E8,
    0x07D0, 0x07D1,        // 2000/2001 -- พบบ่อยที่สุดในเซนเซอร์จีน
    0x0FA0,
    0x1000, 0x1001,
    0x2000, 0x4000,
};
#define PROBE_COUNT (sizeof(PROBE_REGS) / sizeof(PROBE_REGS[0]))

static void do_probe(uint8_t id)
{
    Serial.print(F("probe slave "));
    Serial.print(id);
    Serial.println(F("  -- หา register ที่เก็บ address ของตัวเอง"));
    Serial.println(F("  reg      hex     value    หมายเหตุ"));

    uint8_t hits = 0;

    for (uint8_t i = 0; i < PROBE_COUNT; i++) {
        uint16_t reg = pgm_read_word(&PROBE_REGS[i]);
        uint16_t w[1];

        int8_t rc = mb_read(id, 3, reg, 1, w, g_timeout);
        if (rc != MB_OK) continue;                 // อ่านไม่ได้ = ไม่ใช่ ข้ามไป

        Serial.print(F("  "));
        Serial.print(reg);
        Serial.print(F("\t0x"));
        Serial.print(reg, HEX);
        Serial.print(F("\t"));
        Serial.print(w[0]);

        if (w[0] == id) {
            Serial.print(F("\t<<< น่าจะเป็น ADDRESS ตัวนี้ (ค่าตรงกับ ID)"));
            hits++;
        }
        Serial.println();
    }

    Serial.println();
    if (hits == 0) {
        Serial.println(F("  ไม่เจอ register ที่ค่าตรงกับ ID"));
        Serial.println(F("  -> ลอง 'regs <id> 0 20' ไล่ดู register ช่วงอื่น"));
        Serial.println(F("  -> หรือเปิดคู่มือเซนเซอร์หา 'device address register'"));
    } else {
        Serial.println(F("  เอา register ที่มาร์ก <<< ไปใช้กับคำสั่ง setid"));
        Serial.print(F("  เช่น:  setid "));
        Serial.print(id);
        Serial.println(F(" <reg> 2"));
    }
}

// ---------------------------------------------------------------------------
//  regs -- dump register ต่อเนื่อง (ไว้ไล่หาค่า O2 ด้วย)
// ---------------------------------------------------------------------------
static void do_regs(uint8_t id, uint16_t start, uint8_t count, uint8_t fc)
{
    Serial.print(F("dump slave "));
    Serial.print(id);
    Serial.print(F("  fc="));
    Serial.print(fc);
    Serial.print(F("  reg "));
    Serial.print(start);
    Serial.print(F(" .. "));
    Serial.println(start + count - 1);
    Serial.println(F("  reg      hex     u16      x0.1     x0.01"));

    for (uint16_t i = 0; i < count; i++) {
        uint16_t reg = start + i;
        uint16_t w[1];

        int8_t rc = mb_read(id, fc, reg, 1, w, g_timeout);

        Serial.print(F("  "));
        Serial.print(reg);
        Serial.print(F("\t0x"));
        Serial.print(reg, HEX);
        Serial.print(F("\t"));

        if (rc != MB_OK) {
            print_err(rc);
            Serial.println();
            continue;
        }

        Serial.print(w[0]);
        Serial.print(F("\t"));
        Serial.print(w[0] * 0.1f, 1);
        Serial.print(F("\t"));
        Serial.print(w[0] * 0.01f, 2);

        // O2 ในอากาศปกติ = 20.9 % -> ช่วยมาร์กให้
        float v1 = w[0] * 0.1f;
        float v2 = w[0] * 0.01f;
        if (v1 > 19.5f && v1 < 22.0f) Serial.print(F("   <<< x0.1 ได้ ~20.9 = น่าจะเป็นค่า O2!"));
        if (v2 > 19.5f && v2 < 22.0f) Serial.print(F("   <<< x0.01 ได้ ~20.9 = น่าจะเป็นค่า O2!"));

        Serial.println();
    }
}

// ---------------------------------------------------------------------------
//  setid -- เขียน ID ใหม่ แล้วยืนยันผล
// ---------------------------------------------------------------------------
static void do_setid(uint8_t oldId, uint16_t reg, uint8_t newId)
{
    if (newId < 1 || newId > 247) {
        Serial.println(F("ID ใหม่ต้องอยู่ระหว่าง 1-247"));
        return;
    }

    uint16_t w[1];

    // 1) อ่านค่าเดิมก่อน -- ถ้าอ่านไม่ได้ แปลว่า register ผิด อย่าเพิ่งเขียน
    Serial.print(F("อ่านค่าเดิมที่ reg "));
    Serial.print(reg);
    Serial.print(F(" ... "));

    int8_t rc = mb_read(oldId, 3, reg, 1, w, g_timeout);
    if (rc != MB_OK) {
        print_err(rc);
        Serial.println();
        Serial.println(F("*** ยกเลิก -- อ่าน register นี้ไม่ได้ อย่าเสี่ยงเขียนมั่ว ***"));
        Serial.println(F("    ลอง 'probe <id>' หา register ที่ถูกต้องก่อน"));
        return;
    }
    Serial.print(F("ได้ค่า "));
    Serial.println(w[0]);

    if (w[0] != oldId)
        Serial.println(F("  (เตือน: ค่าเดิมไม่ตรงกับ ID ปัจจุบัน -- register นี้อาจไม่ใช่ address)"));

    // 2) เขียน
    Serial.print(F("เขียน ID ใหม่ = "));
    Serial.print(newId);
    Serial.print(F(" ... "));

    rc = mb_write(oldId, reg, newId, g_timeout);

    // เซนเซอร์บางตัวเปลี่ยน ID ทันทีแล้วตอบกลับด้วย "ID ใหม่"
    // ทำให้ฝั่งเราเห็นเป็น timeout/CRC เพี้ยน -- ไม่ใช่ error จริง
    if (rc == MB_OK) Serial.println(F("ตอบรับแล้ว"));
    else {
        print_err(rc);
        Serial.println(F("  (อาจไม่ใช่ปัญหา -- บางตัวเปลี่ยน ID ทันทีจนตอบกลับด้วย ID ใหม่)"));
    }

    // 3) ยืนยัน
    delay(600);
    Serial.print(F("ตรวจสอบ: อ่าน reg "));
    Serial.print(reg);
    Serial.print(F(" ที่ ID ใหม่ ("));
    Serial.print(newId);
    Serial.print(F(") ... "));

    rc = mb_read(newId, 3, reg, 1, w, g_timeout);
    if (rc == MB_OK && w[0] == newId) {
        Serial.println();
        Serial.println(F("*** สำเร็จ! เซนเซอร์ตัวนี้เป็น ID "));
        Serial.print(F("    "));
        Serial.print(newId);
        Serial.println(F(" แล้ว -- ถอดออก แล้วเอาตัวถัดไปมาต่อ"));
        return;
    }

    print_err(rc);
    Serial.println();
    Serial.println(F("  ยังยืนยันไม่ได้ -> ลองปิด-เปิดไฟเซนเซอร์ แล้วสั่ง 'scan' ใหม่"));
    Serial.println(F("  (บางรุ่นต้อง power cycle ก่อน ID ใหม่ถึงจะมีผล)"));
}

// ---------------------------------------------------------------------------
//  console
// ---------------------------------------------------------------------------
static void print_help()
{
    Serial.println();
    Serial.println(F("=========================================================="));
    Serial.println(F("  RS485 Modbus -- เครื่องมือตั้ง Slave ID"));
    Serial.println(F("=========================================================="));
    Serial.println(F("  scan                     กวาดหา slave (ID 1-247) ที่ baud ปัจจุบัน"));
    Serial.println(F("  scanall                  กวาดทุก baud (9600/4800/19200/38400)"));
    Serial.println(F("  probe <id>               หา register ที่เก็บ address ของตัวเอง"));
    Serial.println(F("  regs <id> <start> <n>    dump register (มาร์กค่าที่ได้ ~20.9 ให้)"));
    Serial.println(F("  regs4 <id> <start> <n>   เหมือนบน แต่ใช้ FC04 (input register)"));
    Serial.println(F("  read <id> <fc> <reg> <n> อ่าน register แบบเจาะจง"));
    Serial.println(F("  write <id> <reg> <val>   เขียน register (FC06) -- ระวัง!"));
    Serial.println(F("  setid <id> <reg> <new>   ตั้ง ID ใหม่ + ยืนยันผล"));
    Serial.println(F("  baud <b>                 เปลี่ยน baud ที่ใช้คุย"));
    Serial.println(F("  help"));
    Serial.println();
    Serial.println(F("  *** ตอนตั้ง ID ให้ต่อเซนเซอร์ทีละตัวเท่านั้น ***"));
    Serial.println();
    Serial.println(F("  ขั้นตอนปกติ:"));
    Serial.println(F("    1) ต่อ O2 ตัวแรกตัวเดียว -> scan       (ได้ ID เดิม เช่น 1)"));
    Serial.println(F("    2) probe 1                             (ได้ register ของ address)"));
    Serial.println(F("    3) setid 1 <reg> 1                     (ตั้งเป็น 1)"));
    Serial.println(F("    4) เปลี่ยนเป็น O2 ตัวที่สอง -> scan -> setid 1 <reg> 2"));
    Serial.println(F("    5) เปลี่ยนเป็น Pressure     -> scan -> setid 1 <reg> 3"));
    Serial.println(F("    6) ต่อทั้ง 3 ตัวเข้าบัสเดียวกัน -> scan -> ต้องเจอ 1, 2, 3"));
    Serial.println();
    Serial.print(F("  baud ตอนนี้: "));
    Serial.println(g_baud);
    Serial.println();
}

static void handle(char *line)
{
    char *cmd = strtok(line, " ");
    if (!cmd) return;

    // แปลง argument (ไม่มีก็เป็น 0)
    char *a1 = strtok(NULL, " ");
    char *a2 = strtok(NULL, " ");
    char *a3 = strtok(NULL, " ");
    char *a4 = strtok(NULL, " ");

    long v1 = a1 ? atol(a1) : 0;
    long v2 = a2 ? atol(a2) : 0;
    long v3 = a3 ? atol(a3) : 0;
    long v4 = a4 ? atol(a4) : 0;

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        print_help();
    }
    else if (!strcmp(cmd, "scan")) {
        do_scan(false);
    }
    else if (!strcmp(cmd, "scanall")) {
        uint32_t keep = g_baud;
        for (uint8_t i = 0; i < BAUD_COUNT; i++) {
            set_baud(BAUD_LIST[i]);
            if (do_scan(true) > 0) {
                Serial.print(F(">>> เจอที่ baud "));
                Serial.print(g_baud);
                Serial.println(F(" -- ใช้ baud นี้ต่อได้เลย"));
                return;
            }
        }
        set_baud(keep);
        Serial.println(F("ไม่เจอเลยสักตัวในทุก baud"));
        Serial.println(F("  -> เช็คสาย A/B สลับกันไหม / มีไฟเลี้ยงเซนเซอร์ไหม / GND ถึงกันไหม"));
    }
    else if (!strcmp(cmd, "probe") && a1) {
        do_probe((uint8_t)v1);
    }
    else if (!strcmp(cmd, "regs") && a3) {
        do_regs((uint8_t)v1, (uint16_t)v2, (uint8_t)v3, 3);
    }
    else if (!strcmp(cmd, "regs4") && a3) {
        do_regs((uint8_t)v1, (uint16_t)v2, (uint8_t)v3, 4);
    }
    else if (!strcmp(cmd, "read") && a4) {
        uint16_t w[8];
        uint8_t  n = (uint8_t)v4;
        if (n < 1 || n > 8) { Serial.println(F("n ต้องอยู่ระหว่าง 1-8")); return; }

        int8_t rc = mb_read((uint8_t)v1, (uint8_t)v2, (uint16_t)v3, n, w, g_timeout);
        if (rc != MB_OK) { print_err(rc); Serial.println(); print_raw(); return; }

        for (uint8_t i = 0; i < n; i++) {
            Serial.print(F("  reg "));
            Serial.print(v3 + i);
            Serial.print(F(" = "));
            Serial.print(w[i]);
            Serial.print(F("  (0x"));
            Serial.print(w[i], HEX);
            Serial.println(F(")"));
        }
        print_raw();
    }
    else if (!strcmp(cmd, "write") && a3) {
        Serial.print(F("เขียน slave "));
        Serial.print(v1);
        Serial.print(F(" reg "));
        Serial.print(v2);
        Serial.print(F(" = "));
        Serial.print(v3);
        Serial.print(F(" ... "));
        int8_t rc = mb_write((uint8_t)v1, (uint16_t)v2, (uint16_t)v3, g_timeout);
        print_err(rc);
        Serial.println();
    }
    else if (!strcmp(cmd, "setid") && a3) {
        do_setid((uint8_t)v1, (uint16_t)v2, (uint8_t)v3);
    }
    else if (!strcmp(cmd, "baud") && a1) {
        set_baud((uint32_t)v1);
        Serial.print(F("baud = "));
        Serial.println(g_baud);
    }
    else {
        Serial.println(F("ไม่รู้จักคำสั่ง -- พิมพ์ help"));
    }
}

// ---------------------------------------------------------------------------
void setup()
{
    pinMode(PIN_DE, OUTPUT);
    digitalWrite(PIN_DE, LOW);        // เริ่มที่โหมดรับเสมอ

    Serial.begin(115200);
    rs485.begin(g_baud);

    delay(300);
    print_help();
    Serial.print(F("> "));
}

void loop()
{
    static char    buf[48];
    static uint8_t len = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            buf[len] = '\0';
            Serial.println();
            if (len) handle(buf);
            len = 0;
            Serial.print(F("> "));
            continue;
        }

        if (len < sizeof(buf) - 1) {
            buf[len++] = c;
            Serial.print(c);          // echo ให้เห็นว่าพิมพ์อะไรไป
        }
    }
}
