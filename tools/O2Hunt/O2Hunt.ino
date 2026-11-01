// ============================================================================
//  O2Hunt  --  ล่าเซนเซอร์ O2 ตัวเก่า: ทุก baud x id 1-16 บนบัส O2 (D7)
//
//  ขา: RO=D2 DI=D3 | DE+RE บัส O2 = D7 | บัส pressure D6 ค้าง HIGH
//  Serial Monitor @ 9600
// ============================================================================
#include <SoftwareSerial.h>

SoftwareSerial mod(2, 3);
const long BAUDS[3] = { 9600, 4800, 19200 };

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

static bool rd(uint8_t id, uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    digitalWrite(7, HIGH); delay(2);
    mod.write(q, 8);
    delayMicroseconds(60);
    digitalWrite(7, LOW);
    uint8_t buf[24], n = 0;
    uint32_t dl = millis() + 160;
    while ((long)(millis() - dl) < 0) {
        if (mod.available() && n < sizeof(buf)) buf[n++] = mod.read();
    }
    digitalWrite(7, HIGH);
    for (uint8_t s = 0; s + 7 <= n; s++) {
        if (buf[s] == id && buf[s + 1] == 3 && buf[s + 2] == 2) {
            uint16_t got = buf[s + 5] | ((uint16_t)buf[s + 6] << 8);
            if (crc16(&buf[s], 5) == got) {
                *out = ((uint16_t)buf[s + 3] << 8) | buf[s + 4];
                return true;
            }
        }
    }
    return false;
}

void setup()
{
    pinMode(6, OUTPUT); pinMode(7, OUTPUT);
    digitalWrite(6, HIGH); digitalWrite(7, HIGH);
    Serial.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== O2 HUNT : 3 baud x id 1-16 บนบัส D7 ==="));
}

void loop()
{
    for (uint8_t bi = 0; bi < 3; bi++) {
        mod.begin(BAUDS[bi]);
        delay(50);
        Serial.print(F("baud ")); Serial.print(BAUDS[bi]); Serial.print(F(" : "));
        bool found = false;
        for (uint8_t id = 1; id <= 16; id++) {
            uint16_t v;
            if (rd(id, 6, &v)) {
                Serial.println();
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                Serial.print  (F(">>> เจอ!  baud=")); Serial.print(BAUDS[bi]);
                Serial.print  (F("  id=")); Serial.print(id);
                Serial.print  (F("  O2=")); Serial.print(v * 0.1f, 1); Serial.println('%');
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                found = true;
                break;
            }
            Serial.print('.');
        }
        if (!found) Serial.println(F(" ไม่ตอบ"));
        mod.end();
    }
    Serial.println(F("---- วนใหม่ ----"));
    delay(1200);
}
