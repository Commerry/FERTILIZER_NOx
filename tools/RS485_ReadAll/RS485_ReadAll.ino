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
//  RS485_ReadAll  --  ทดสอบอ่านเซนเซอร์ 3 ตัวพร้อมกันบนบัสเดียว
//
//  ใช้ "หลัง" ตั้ง slave ID ครบแล้วด้วย RS485_SetID
//
//  ทดสอบกับของจริงล้วน ๆ ไม่มีค่าจำลอง
//  ถ้าตัวไหนอ่านไม่ได้ จะขึ้น FAIL พร้อมบอกสาเหตุ ไม่แกล้งทำเป็นมีค่า
//
//  สิ่งที่ต้องดู:
//    - err ของแต่ละตัวต้องอยู่ที่ 0 หรือใกล้ 0
//    - O2 ในอากาศปกติต้องได้ ~20.9 %
//    - ถ้า err ไต่ขึ้นเรื่อย ๆ = สายรบกวน / ไม่มี terminator / GND ไม่ถึงกัน
//
//  การต่อสาย: เหมือน RS485_SetID  (D2=DE, D8=RO, D9=DI)
//  Serial Monitor: 115200
// ============================================================================
#include <AltSoftSerial.h>

#define PIN_DE   2

// ---------------------------------------------------------------------------
//  *** แก้ตรงนี้ ***  เอาค่าที่ได้จาก RS485_SetID (probe / regs) มาใส่
// ---------------------------------------------------------------------------
#define BUS_BAUD      9600
#define POLL_MS       1000     // อ่านครบ 1 รอบทุกกี่ ms
#define TIMEOUT_MS    400

// ชนิดข้อมูลที่ decode จาก register
#define DT_U16        0        // 1 word ไม่มีเครื่องหมาย
#define DT_S16        1        // 1 word มีเครื่องหมาย (ค่าติดลบได้)
#define DT_U32_ABCD   2        // 2 word, word สูงมาก่อน
#define DT_U32_CDAB   3        // 2 word, word ต่ำมาก่อน
#define DT_F32_ABCD   4        // float 2 word, word สูงมาก่อน
#define DT_F32_CDAB   5        // float 2 word, word ต่ำมาก่อน (พบบ่อยสุดในของจีน)

struct Sensor {
    const char *name;
    uint8_t     id;       // slave ID (ตั้งด้วย RS485_SetID)
    uint8_t     fc;       // 3 = holding, 4 = input
    uint16_t    reg;      // register แรก
    uint8_t     nRegs;    // 1 หรือ 2
    uint8_t     dtype;    // DT_*
    float       scale;    // ค่าจริง = raw * scale + offset
    float       offset;
    const char *unit;
};

// ค่าเริ่มต้นเป็นแค่ "เดา" -- ต้องแก้ให้ตรงกับที่หาเจอจริง
static Sensor SENSORS[] = {
    { "O1 (oxygen)",   1, 3, 0, 1, DT_U16, 0.1f,  0.0f, "%"   },
    { "O2 (oxygen)",   2, 3, 0, 1, DT_U16, 0.1f,  0.0f, "%"   },
    { "PT (pressure)", 3, 3, 0, 1, DT_U16, 0.01f, 0.0f, "bar" },
};
#define SENSOR_COUNT (sizeof(SENSORS) / sizeof(SENSORS[0]))

// ---------------------------------------------------------------------------
AltSoftSerial rs485;

static uint8_t s_rsp[32];
static uint8_t s_rspLen  = 0;
static uint8_t s_excCode = 0;

static uint32_t s_ok[SENSOR_COUNT];
static uint32_t s_err[SENSOR_COUNT];
static float    s_val[SENSOR_COUNT];
static bool     s_good[SENSOR_COUNT];
static int8_t   s_last[SENSOR_COUNT];

#define MB_OK          0
#define MB_TIMEOUT    -1
#define MB_BAD_CRC    -2
#define MB_EXCEPTION  -3
#define MB_SHORT      -4

// ---------------------------------------------------------------------------
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

static int8_t mb_read(uint8_t id, uint8_t fc, uint16_t reg, uint8_t n, uint16_t *out)
{
    uint8_t req[8];
    req[0] = id;
    req[1] = fc;
    req[2] = (uint8_t)(reg >> 8);
    req[3] = (uint8_t)(reg & 0xFF);
    req[4] = 0;
    req[5] = n;

    uint16_t crc = mb_crc16(req, 6);
    req[6] = (uint8_t)(crc & 0xFF);
    req[7] = (uint8_t)(crc >> 8);

    while (rs485.available()) rs485.read();

    digitalWrite(PIN_DE, HIGH);
    delayMicroseconds(100);
    rs485.write(req, 8);
    rs485.flush();                 // รอ byte สุดท้ายออกสายจริงก่อนสลับทิศ
    delayMicroseconds(100);
    digitalWrite(PIN_DE, LOW);

    uint32_t t0 = millis();
    while (!rs485.available())
        if (millis() - t0 > TIMEOUT_MS) return MB_TIMEOUT;

    uint16_t gap = (uint16_t)((35UL * 1000UL) / BUS_BAUD) + 2;
    if (gap < 3) gap = 3;

    s_rspLen = 0;
    uint32_t last = millis();
    while (millis() - last < gap) {
        if (rs485.available()) {
            if (s_rspLen < sizeof(s_rsp)) s_rsp[s_rspLen++] = (uint8_t)rs485.read();
            else                          rs485.read();
            last = millis();
        }
    }

    if (s_rspLen < 4) return MB_SHORT;

    uint16_t got  = (uint16_t)s_rsp[s_rspLen - 2] | ((uint16_t)s_rsp[s_rspLen - 1] << 8);
    if (got != mb_crc16(s_rsp, (uint8_t)(s_rspLen - 2))) return MB_BAD_CRC;

    if (s_rsp[1] & 0x80) { s_excCode = s_rsp[2]; return MB_EXCEPTION; }

    if (s_rsp[2] != (uint8_t)(n * 2)) return MB_SHORT;

    for (uint8_t i = 0; i < n; i++)
        out[i] = ((uint16_t)s_rsp[3 + i * 2] << 8) | s_rsp[4 + i * 2];

    return MB_OK;
}

// ---------------------------------------------------------------------------
static float decode(const uint16_t *w, uint8_t dtype, float scale, float offset)
{
    uint32_t u32;
    float    raw;

    switch (dtype) {
    case DT_S16:      raw = (float)(int16_t)w[0];                      break;
    case DT_U32_ABCD: u32 = ((uint32_t)w[0] << 16) | w[1];
                      raw = (float)u32;                                break;
    case DT_U32_CDAB: u32 = ((uint32_t)w[1] << 16) | w[0];
                      raw = (float)u32;                                break;
    case DT_F32_ABCD: u32 = ((uint32_t)w[0] << 16) | w[1];
                      memcpy(&raw, &u32, 4);                           break;
    case DT_F32_CDAB: u32 = ((uint32_t)w[1] << 16) | w[0];
                      memcpy(&raw, &u32, 4);                           break;
    default:          raw = (float)w[0];                               break;
    }

    return raw * scale + offset;
}

static void print_err(int8_t rc)
{
    switch (rc) {
    case MB_TIMEOUT:   Serial.print(F("ไม่ตอบ"));         break;
    case MB_BAD_CRC:   Serial.print(F("CRC เพี้ยน"));      break;
    case MB_SHORT:     Serial.print(F("เฟรมสั้น"));        break;
    case MB_EXCEPTION: Serial.print(F("exc "));
                       Serial.print(s_excCode);            break;
    default:           Serial.print(F("ok"));              break;
    }
}

// ---------------------------------------------------------------------------
void setup()
{
    pinMode(PIN_DE, OUTPUT);
    digitalWrite(PIN_DE, LOW);

    Serial.begin(115200);
    rs485.begin(BUS_BAUD);
    delay(300);

    Serial.println();
    Serial.println(F("=== RS485 -- อ่าน 3 เซนเซอร์พร้อมกัน (ของจริง ไม่มีค่าจำลอง) ==="));
    Serial.print(F("bus: "));
    Serial.print(BUS_BAUD);
    Serial.println(F(" baud 8N1"));
    Serial.println();

    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        Serial.print(F("  "));
        Serial.print(SENSORS[i].name);
        Serial.print(F("  ->  id "));
        Serial.print(SENSORS[i].id);
        Serial.print(F("  fc "));
        Serial.print(SENSORS[i].fc);
        Serial.print(F("  reg "));
        Serial.print(SENSORS[i].reg);
        Serial.print(F("  x"));
        Serial.println(SENSORS[i].scale, 4);
    }
    Serial.println();
    Serial.println(F("ถ้าตัวไหน FAIL ตลอด -> เช็ค id/reg ด้วย RS485_SetID (probe / regs)"));
    Serial.println();
}

void loop()
{
    static uint32_t last = 0;
    static uint32_t round = 0;

    if (millis() - last < POLL_MS) return;
    last = millis();
    round++;

    // --- อ่านทีละตัว วนไปตามลำดับ ---
    // ห้ามยิงพร้อมกัน -- บัส RS485 เป็น half-duplex คุยได้ทีละคน
    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        Sensor  &s = SENSORS[i];
        uint16_t w[2] = { 0, 0 };

        int8_t rc = mb_read(s.id, s.fc, s.reg, s.nRegs, w);
        s_last[i] = rc;

        if (rc == MB_OK) {
            s_val[i]  = decode(w, s.dtype, s.scale, s.offset);
            s_good[i] = true;
            s_ok[i]++;
        } else {
            s_good[i] = false;
            s_err[i]++;
        }

        delay(30);   // เว้นจังหวะให้ transceiver ปล่อยสายก่อนยิงตัวถัดไป
    }

    // --- พิมพ์ผล ---
    Serial.print(F("["));
    Serial.print(round);
    Serial.print(F("]  "));

    for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
        Serial.print(SENSORS[i].name);
        Serial.print(F(" = "));

        if (s_good[i]) {
            Serial.print(s_val[i], 2);
            Serial.print(' ');
            Serial.print(SENSORS[i].unit);
        } else {
            Serial.print(F("FAIL ("));
            print_err(s_last[i]);
            Serial.print(')');
        }
        Serial.print(F("   |   "));
    }
    Serial.println();

    // สรุปสถิติทุก 10 รอบ -- ดูว่าบัสนิ่งหรือรวน
    if (round % 10 == 0) {
        Serial.print(F("      สถิติ:  "));
        for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
            Serial.print(SENSORS[i].name);
            Serial.print(F(" ok="));
            Serial.print(s_ok[i]);
            Serial.print(F(" err="));
            Serial.print(s_err[i]);
            Serial.print(F("   "));
        }
        Serial.println();

        bool anyErr = false;
        for (uint8_t i = 0; i < SENSOR_COUNT; i++) if (s_err[i]) anyErr = true;
        if (anyErr) {
            Serial.println(F("      มี error -> เช็ค: terminator 120 ohm ปลายบัส 2 ข้าง /"));
            Serial.println(F("                        GND ของเซนเซอร์ถึง GND ของ Nano ไหม /"));
            Serial.println(F("                        สาย A-B สลับกันหรือเปล่า"));
        }
        Serial.println();
    }
}
