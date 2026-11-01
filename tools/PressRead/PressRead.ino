// ============================================================================
//  PressRead  --  อ่าน "pressure (id3) อย่างเดียว" -- ใช้พิสูจน์ว่าเป็นโค้ดหรือฮาร์ดแวร์
//
//  ***  ให้ O2 ยังเสียบอยู่บนบัส  ***  (แค่ไม่ query O2)
//    อ่านได้  = ไม่ใช่ฮาร์ดแวร์บัส -> ปัญหาอยู่ที่จังหวะ query 2 ตัวใน BusReadAll
//    เงียบ    = O2 เสียบอยู่บล็อกบัส = ฮาร์ดแวร์ (ขั้ว A/B / common-mode / O2 คาบัส)
//
//  ขา RO=D2 DI=D3 DE=D7 RE=D8
// ============================================================================
#include <SoftwareSerial.h>

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

static bool rdN(uint8_t id, uint16_t reg, uint8_t cnt, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, cnt };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    txMode(); delay(2); mod.write(q, 8); mod.flush(); rxMode();
    uint8_t need = 3 + 2 * cnt + 2, r[16]; if (need > sizeof(r)) return false;
    uint8_t n = 0; uint32_t dl = millis() + 150;
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

void setup()
{
    pinMode(PIN_DE, OUTPUT); pinMode(PIN_RE, OUTPUT); rxMode();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== PRESS-ONLY READ (O2 ยังเสียบอยู่) ==="));
}

void loop()
{
    uint16_t raw, dp = 0;
    if (rdN(3, 4, 1, &raw)) {
        rdN(3, 3, 1, &dp);
        float v = (int16_t)raw; for (uint8_t k = 0; k < dp && k < 4; k++) v /= 10.0f;
        Serial.print(F("PRESS id3 = ")); Serial.print(v, (int)dp); Serial.println(F("  (OK)"));
    } else {
        Serial.println(F("PRESS id3 = no reply"));
    }
    delay(700);
}
