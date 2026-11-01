// ============================================================================
//  BusReadAll  --  อ่าน O2 #1 (id 1) + Pressure (id 3) พร้อมกันบนบัส RS485 เดียว
//
//  ขาเดิม: RO=D2 DI=D3 DE=D7 RE=D8
//  เช็คว่า 2 ตัวอยู่บัสเดียวกันได้ ไม่ชน + ค่ามาครบ
//
//  O2  : id 1, reg 6  (ค่า x0.1 = %)
//  Pres: id 3, reg 4 (S16) / 10^reg3 + หน่วยจาก reg2   (หรือ float reg7-8)
//
//  อัป -> Serial Monitor 9600 -> อ่านทุก ~0.8 วิ:
//    "O2#1 = 20.9 %   |   PRESS = 0.00 kPa"
// ============================================================================
#include <SoftwareSerial.h>
#include <string.h>

#define PIN_RO 2
#define PIN_DI 3
#define PIN_DE 7
#define PIN_RE 8

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

static bool rdN(uint8_t id, uint16_t reg, uint8_t cnt, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, cnt };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    delay(5);                    // <-- idle >=3.5 char (Modbus T3.5) กันเฟรมชนกันตอนสลับ slave
    while (mod.available()) mod.read();
    txMode(); delay(2); mod.write(q, 8); mod.flush(); rxMode();
    uint8_t need = 3 + 2 * cnt + 2, r[16]; if (need > sizeof(r)) return false;
    uint8_t n = 0; uint32_t dl = millis() + 250;
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

// อ่านซ้ำได้ถึง 3 ครั้ง กันพลาดตอนบัสมาร์จินอล (เว้น 8ms ระหว่างครั้ง)
static bool rdRetry(uint8_t id, uint16_t reg, uint8_t cnt, uint16_t *out)
{
    for (uint8_t t = 0; t < 3; t++) { if (rdN(id, reg, cnt, out)) return true; delay(8); }
    return false;
}

void setup()
{
    pinMode(PIN_DE, OUTPUT); pinMode(PIN_RE, OUTPUT); rxMode();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== BUS READ : O2 #1 (id1) + PRESSURE (id3) ==="));
}

void loop()
{
    uint16_t o2, raw, unit = 0, dp = 0;

    // ---- O2 #1 ----
    Serial.print(F("O2#1 = "));
    if (rdRetry(1, 6, 1, &o2)) { Serial.print(o2 * 0.1f, 1); Serial.print(F(" %")); }
    else                       Serial.print(F("no reply"));

    Serial.print(F("   |   PRESS = "));
    // ---- Pressure id3 ----
    if (rdRetry(3, 4, 1, &raw)) {
        rdRetry(3, 2, 1, &unit); rdRetry(3, 3, 1, &dp);
        float v = (int16_t)raw; for (uint8_t k = 0; k < dp && k < 4; k++) v /= 10.0f;
        Serial.print(v, (int)dp); Serial.print(' '); Serial.print(unitName(unit));
    } else {
        Serial.print(F("no reply"));
    }
    Serial.println();
    delay(800);
}
