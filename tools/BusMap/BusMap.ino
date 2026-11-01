// ============================================================================
//  BusMap v3  --  แผนที่ 3 บัส: D6 / D7 / D8 อะไรอยู่ขาไหน
//
//  ต่อบัสละรอบ: [1] ฟัง 2.5 วิ (นับเฟรม FF)  [2] สแกน id1-8
//  ขา: RO=D2 DI=D3 | EN สามตัว = D6, D7, D8
//  Serial Monitor @ 9600
// ============================================================================
#include <SoftwareSerial.h>

SoftwareSerial mod(2, 3);
const uint8_t ENS[3] = { 6, 7, 8 };

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

static void allHigh() { for (uint8_t i = 0; i < 3; i++) digitalWrite(ENS[i], HIGH); }

static uint16_t listenFF(uint8_t de, uint16_t ms)
{
    allHigh();
    digitalWrite(de, LOW);
    while (mod.available()) mod.read();
    uint16_t ffCount = 0;
    uint8_t ff[10]; uint8_t fl = 0;
    uint32_t dl = millis() + ms;
    while ((long)(millis() - dl) < 0) {
        if (!mod.available()) continue;
        uint8_t b = (uint8_t)mod.read();
        if (fl == 0 && b != 0xFF) continue;
        ff[fl++] = b;
        if (fl == 10) {
            fl = 0;
            if (ff[1] == 0x01 && ff[2] == 0x07) {
                ffCount++;
                uint16_t raw = ((uint16_t)ff[4] << 8) | ff[5];
                Serial.print(F("      FF: O2=")); Serial.print(raw * 0.1f, 1); Serial.println('%');
            }
        }
    }
    allHigh();
    return ffCount;
}

static bool rd1(uint8_t de, uint8_t id, uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    allHigh(); delay(2);
    mod.write(q, 8);
    delayMicroseconds(60);
    digitalWrite(de, LOW);
    uint8_t buf[32], n = 0;
    uint32_t dl = millis() + 200;
    while ((long)(millis() - dl) < 0) {
        if (mod.available() && n < sizeof(buf)) buf[n++] = mod.read();
    }
    allHigh();
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
    for (uint8_t i = 0; i < 3; i++) pinMode(ENS[i], OUTPUT);
    allHigh();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== BUS MAP v3 : D6 / D7 / D8 ==="));
}

void loop()
{
    for (uint8_t i = 0; i < 3; i++) {
        uint8_t de = ENS[i];
        Serial.print(F("---- EN = D")); Serial.print(de); Serial.println(F(" ----"));
        uint16_t ff = listenFF(de, 2500);
        Serial.print(F("   เฟรม FF: ")); Serial.print(ff); Serial.println(F(" เฟรม/2.5วิ"));
        bool any = false;
        for (uint8_t id = 1; id <= 8; id++) {
            uint16_t v;
            uint16_t reg = (id == 3) ? 4 : 6;
            if (rd1(de, id, reg, &v)) {
                Serial.print(F("   id")); Serial.print(id);
                Serial.print(F(" ตอบ ค่า=")); Serial.print(v);
                if (reg == 6) { Serial.print(F(" (")); Serial.print(v * 0.1f, 1); Serial.print(F("%)")); }
                Serial.println();
                any = true;
            }
        }
        if (!any) Serial.println(F("   Modbus: เงียบ"));
    }
    Serial.println(F("================ วนใหม่ ================"));
    delay(1500);
}
