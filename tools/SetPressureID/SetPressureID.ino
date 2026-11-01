// ============================================================================
//  SetPressureID  --  ตั้ง Slave ID ของ gas pressure sensor  (1 -> 3)
//
//  ขาเดิม: RO=D2 DI=D3 DE=D7 RE=D8   *** เสียบ pressure ตัวเดียว ***
//  datasheet: reg0 = Slave Address (R/W) เขียนผ่าน FC06 มีผลทันที (comm param)
//             ตอนเขียน address เซนเซอร์ตอบด้วย id เดิมก่อน แล้วสลับเป็น id ใหม่
//
//  อัป -> Serial Monitor 9600:
//    ">>> OK! ID now = 3"  = สำเร็จ  แล้ว loop จะอ่านค่า pressure ที่ id 3 โชว์สด
// ============================================================================
#include <SoftwareSerial.h>
#include <string.h>

#define PIN_RO 2
#define PIN_DI 3
#define PIN_DE 7
#define PIN_RE 8
#define OLD_ID 1
#define NEW_ID 3

SoftwareSerial mod(PIN_RO, PIN_DI);

static inline void txMode() { digitalWrite(PIN_DE, HIGH); digitalWrite(PIN_RE, HIGH); }
static inline void rxMode() { digitalWrite(PIN_DE, LOW);  digitalWrite(PIN_RE, LOW);  }

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

static const __FlashStringHelper* unitName(uint16_t u)
{
    switch (u) {
        case 0: return F("MPa");  case 1: return F("kPa");   case 2: return F("Pa");
        case 3: return F("Bar");  case 4: return F("mbar");  case 5: return F("kg/cm2");
        case 6: return F("Psi");  case 7: return F("mH2O");  case 8: return F("mmH2O");
        default: return F("?");
    }
}

// FC03 อ่าน cnt register -> out[] ; true = ตอบ CRC ถูก
static bool rdN(uint8_t id, uint16_t reg, uint8_t cnt, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, cnt };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    txMode(); delay(2); mod.write(q, 8); mod.flush(); rxMode();
    uint8_t need = 3 + 2 * cnt + 2, r[16]; if (need > sizeof(r)) return false;
    uint8_t n = 0; uint32_t dl = millis() + 200;
    while (n < need) {
        if ((long)(millis() - dl) >= 0) return false;
        if (!mod.available()) continue;
        uint8_t b = mod.read();
        if (n == 0 && b != id) continue;
        if (n == 1 && b != 3) { n = 0; continue; }
        if (n == 2 && b != 2 * cnt) { n = 0; continue; }
        r[n++] = b;
    }
    if (crc16(r, need - 2) != (uint16_t)(r[need - 2] | ((uint16_t)r[need - 1] << 8))) return false;
    for (uint8_t i = 0; i < cnt; i++) out[i] = ((uint16_t)r[3 + 2 * i] << 8) | r[4 + 2 * i];
    return true;
}

// FC06 เขียน 1 register ; true = ได้ echo กลับ
static bool wr(uint8_t id, uint16_t reg, uint16_t val)
{
    uint8_t q[8] = { id, 6, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    txMode(); delay(2); mod.write(q, 8); mod.flush(); rxMode();
    uint8_t n = 0; uint32_t dl = millis() + 300;      // เก็บ echo (ใช้ id เดิม)
    while (n < 8) { if ((long)(millis() - dl) >= 0) break; if (mod.available()) { mod.read(); n++; } }
    return n >= 8;
}

void setup()
{
    pinMode(PIN_DE, OUTPUT); pinMode(PIN_RE, OUTPUT); rxMode();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== SET PRESSURE ID : 1 -> 3 ==="));

    uint16_t a;
    if (!rdN(OLD_ID, 0, 1, &a)) {
        Serial.println(F("!! ไม่เจอ id=1 -- เช็คสาย/เสียบ pressure ตัวเดียว/A-B"));
        return;
    }
    Serial.print(F("เจอ id=")); Serial.print(OLD_ID); Serial.print(F("  (reg0=")); Serial.print(a); Serial.println(')');

    Serial.print(F("เขียน reg0 = ")); Serial.print(NEW_ID); Serial.println(F(" ..."));
    wr(OLD_ID, 0, NEW_ID);
    delay(300);

    if (rdN(NEW_ID, 0, 1, &a) && a == NEW_ID) {
        Serial.print(F(">>> OK! ID now = ")); Serial.println(a);
    } else {
        Serial.println(F("!! verify id=3 ไม่ผ่าน -- ลองรีเซ็ตแล้วอ่านใหม่"));
    }
}

void loop()
{
    uint16_t unit = 0, dp = 0, raw = 0;
    if (rdN(NEW_ID, 4, 1, &raw)) {
        rdN(NEW_ID, 2, 1, &unit); rdN(NEW_ID, 3, 1, &dp);
        float v = (int16_t)raw; for (uint8_t k = 0; k < dp && k < 4; k++) v /= 10.0f;
        Serial.print(F("id3  PRESSURE = ")); Serial.print(v, (int)dp);
        Serial.print(' '); Serial.println(unitName(unit));
    } else {
        Serial.println(F("id3  no reply"));
    }
    delay(1000);
}
