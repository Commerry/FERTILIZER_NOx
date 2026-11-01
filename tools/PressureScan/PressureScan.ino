// ============================================================================
//  PressureScan  --  หา gas pressure sensor บน RS485 + อ่านค่า
//
//  ต่อขาเดิม (ชุดเดียวกับที่อ่าน O2 #1 ได้):
//        MAX485  RO -> D2    DI -> D3    DE -> D7    RE -> D8
//        A -> เซนเซอร์ 485A     B -> เซนเซอร์ 485B     VCC/GND ตามสเปคเซนเซอร์
//
//  *** สแกนทีละตัว: เสียบ "pressure ตัวเดียว" (ถอด O2 #1 ชั่วคราว) ***
//      ไม่งั้นสแกนจะไปเจอ O2 #1 (id=1) ก่อน แล้วหยุด
//
//  register ของ pressure (จาก datasheet):
//        reg 0   = address (ID)          <- ตั้ง ID เขียนที่ reg 0  (คนละที่กับ O2 ที่ reg 24)
//        reg 1   = baud
//        reg 4   = ค่า S16 (signed)
//        reg 7-8 = ค่า float (ABCD)
//
//  อัป -> Serial Monitor 9600 -> อ่านผล:
//    ">>> FOUND id=.. baud=.. | reg0(addr)=.. reg4(S16)=.. float=.."  = เจอ
//    "... ไม่ตอบ"  ทุก baud/id  = ไม่เจอ (สลับ A/B / เช็คไฟ / ลอง FC04)
// ============================================================================
#include <SoftwareSerial.h>
#include <string.h>

#define PIN_RO 2
#define PIN_DI 3
#define PIN_DE 7
#define PIN_RE 8

SoftwareSerial mod(PIN_RO, PIN_DI);
const long BAUDS[] = { 9600, 4800, 19200 };

static inline void txMode() { digitalWrite(PIN_DE, HIGH); digitalWrite(PIN_RE, HIGH); }
static inline void rxMode() { digitalWrite(PIN_DE, LOW);  digitalWrite(PIN_RE, LOW);  }

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*b++);
        for (uint8_t i = 0; i < 8; i++)
            c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

// FC03 อ่าน cnt register (1..4) -> out[] ; true = ตอบ CRC ถูก
static bool rdN(uint8_t id, uint16_t reg, uint8_t cnt, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, cnt };
    uint16_t c = crc16(q, 6); q[6] = (uint8_t)c; q[7] = (uint8_t)(c >> 8);
    while (mod.available()) mod.read();
    txMode(); delay(2);
    mod.write(q, 8); mod.flush();
    rxMode();

    uint8_t need = 3 + 2 * cnt + 2;              // id,fc,bc + data + crc
    uint8_t r[16]; if (need > sizeof(r)) return false;
    uint8_t n = 0; uint32_t dl = millis() + 200;
    while (n < need) {
        if ((long)(millis() - dl) >= 0) return false;
        if (!mod.available()) continue;
        uint8_t b = (uint8_t)mod.read();
        if (n == 0 && b != id)          continue;
        if (n == 1 && b != 3)     { n = 0; continue; }
        if (n == 2 && b != 2 * cnt){ n = 0; continue; }
        r[n++] = b;
    }
    if (crc16(r, need - 2) != (uint16_t)(r[need - 2] | ((uint16_t)r[need - 1] << 8)))
        return false;
    for (uint8_t i = 0; i < cnt; i++)
        out[i] = ((uint16_t)r[3 + 2 * i] << 8) | r[4 + 2 * i];
    return true;
}

// reg2 Unit code -> ข้อความ (ตาม datasheet)
static const __FlashStringHelper* unitName(uint16_t u)
{
    switch (u) {
        case 0: return F("MPa");
        case 1: return F("kPa");
        case 2: return F("Pa");
        case 3: return F("Bar");
        case 4: return F("mbar");
        case 5: return F("kg/cm2");
        case 6: return F("Psi");
        case 7: return F("mH2O");
        case 8: return F("mmH2O");
        default: return F("?");
    }
}

void setup()
{
    pinMode(PIN_DE, OUTPUT);
    pinMode(PIN_RE, OUTPUT);
    rxMode();
    Serial.begin(9600);
    delay(300);
    Serial.println();
    Serial.println(F("=== GAS PRESSURE SENSOR SCAN (RS485) ==="));
    Serial.println(F("RO=D2 DI=D3 DE=D7 RE=D8 | reg0=addr reg4=val reg7-8=float"));
    Serial.println(F("*** เสียบ pressure ตัวเดียว (ถอด O2 #1 ก่อน) ***"));
}

void loop()
{
    for (uint8_t bi = 0; bi < 3; bi++) {
        mod.begin(BAUDS[bi]);
        delay(60);
        Serial.print(F("baud ")); Serial.print(BAUDS[bi]); Serial.print(F(" : "));

        for (uint8_t id = 1; id <= 8; id++) {
            uint16_t v, unit = 0, dp = 0, raw = 0, a[2];
            if (rdN(id, 0, 1, &v)) {                 // ตอบ reg0 = เจอตัวตน
                rdN(id, 2, 1, &unit);                // หน่วย
                rdN(id, 3, 1, &dp);                  // จำนวนทศนิยม
                rdN(id, 4, 1, &raw);                 // ค่าดิบ S16
                float val = (int16_t)raw;
                for (uint8_t k = 0; k < dp && k < 4; k++) val /= 10.0f;

                Serial.println();
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                Serial.print  (F(">>> FOUND  id=")); Serial.print(id);
                Serial.print  (F("  baud="));        Serial.print(BAUDS[bi]);
                Serial.print  (F("  addr(r0)="));    Serial.println(v);
                // ค่าจริง = reg4 / 10^reg3  พร้อมหน่วยจาก reg2 (ตาม datasheet)
                Serial.print  (F("    PRESSURE = ")); Serial.print(val, (int)dp);
                Serial.print  (' '); Serial.print(unitName(unit));
                Serial.print  (F("   (raw r4=")); Serial.print((int16_t)raw);
                Serial.print  (F(", dp="));        Serial.print(dp);
                Serial.print  (F(", unit#"));      Serial.print(unit); Serial.println(')');
                if (rdN(id, 7, 2, a)) {             // ทางเลือก: float ตรง ๆ (ABCD)
                    uint32_t u = ((uint32_t)a[0] << 16) | a[1];
                    float f; memcpy(&f, &u, 4);
                    Serial.print(F("    float(r7-8) = ")); Serial.println(f, 3);
                }
                Serial.println(F(">>> เจอ pressure! จด id/baud ไว้"));
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                while (1) { }
            }
            Serial.print('.');
        }
        Serial.println(F(" ไม่ตอบ"));
        mod.end();
    }

    Serial.println(F("*** ไม่เจอทุก baud/id -> 1)สลับ A<->B  2)เช็คไฟตามสเปค  3)อาจใช้ FC04 ***"));
    Serial.println(F("---- วนใหม่ 3 วิ ----"));
    Serial.println();
    delay(3000);
}
