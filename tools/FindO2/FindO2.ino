// ============================================================================
//  FindO2 v2 --  หาเซนเซอร์ + จับผิด "RO/DI สลับกัน" อัตโนมัติ
//
//  ลองทุกชุด: {ขาปกติ RX=D2,TX=D3  หรือ สลับ RX=D3,TX=D2}
//           x {บัส DE=D6 หรือ DE=D7}   x  id 1..8  (อ่าน reg6)
//
//  ผล:
//    ">>> FOUND ปกติ DE=D7 id=1"   = สายถูก O2 อยู่บัส D7
//    ">>> FOUND สลับ ..."          = RO/DI บัดกรีสลับกัน! (แก้สายหรือใช้โค้ดสลับ)
//    เงียบหมด                       = ปัญหาไฟ/A/B/จุดบัดกรี ไม่ใช่ทิศสาย
//
//  Serial Monitor @ 9600
// ============================================================================
#include <SoftwareSerial.h>

SoftwareSerial modN(2, 3);   // ปกติ : RX=D2(<-RO) TX=D3(->DI)
SoftwareSerial modS(3, 2);   // สลับ : RX=D3       TX=D2   (กรณีบัดกรีสลับ)

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

// อ่าน reg6 1 register ; sw = ตัวที่ active อยู่ ; de = ขา DE บัสเป้าหมาย, อีกขาค้าง HIGH
static bool rd(SoftwareSerial &sw, uint8_t de, uint8_t deOther, uint8_t id, uint16_t *out)
{
    uint8_t q[8] = { id, 3, 0, 6, 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (sw.available()) sw.read();
    digitalWrite(deOther, HIGH);
    digitalWrite(de, HIGH); delay(2);
    sw.write(q, 8);
    delayMicroseconds(60);
    digitalWrite(de, LOW);
    uint8_t r[8], n = 0; uint32_t dl = millis() + 180;
    bool ok = false;
    while (n < 7) {
        if ((long)(millis() - dl) >= 0) break;
        if (!sw.available()) continue;
        uint8_t b = (uint8_t)sw.read();
        if (n == 0 && b != id) continue;
        if (n == 1 && b != 3) { n = 0; continue; }
        r[n++] = b;
    }
    if (n == 7 && crc16(r, 5) == (uint16_t)(r[5] | ((uint16_t)r[6] << 8))) {
        *out = (uint16_t)(((uint16_t)r[3] << 8) | r[4]);
        ok = true;
    }
    digitalWrite(de, HIGH);
    return ok;
}

static void scanPair(SoftwareSerial &sw, const __FlashStringHelper *name)
{
    sw.begin(9600);
    sw.listen();
    delay(30);
    const uint8_t des[2] = { 6, 7 };
    for (uint8_t d = 0; d < 2; d++) {
        Serial.print('['); Serial.print(name);
        Serial.print(F(" DE=D")); Serial.print(des[d]); Serial.print(F("] "));
        bool found = false;
        for (uint8_t id = 1; id <= 8; id++) {
            uint16_t v;
            if (rd(sw, des[d], des[d ^ 1], id, &v)) {
                Serial.println();
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                Serial.print  (F(">>> FOUND ")); Serial.print(name);
                Serial.print  (F(" DE=D"));  Serial.print(des[d]);
                Serial.print  (F("  id=")); Serial.print(id);
                Serial.print  (F("  val=")); Serial.println(v);
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                found = true;
                break;
            }
            Serial.print('.');
        }
        if (!found) Serial.println(F(" ไม่ตอบ"));
    }
    sw.end();
}

void setup()
{
    pinMode(6, OUTPUT); pinMode(7, OUTPUT);
    digitalWrite(6, HIGH); digitalWrite(7, HIGH);
    Serial.begin(9600);
    delay(300);
    Serial.println();
    Serial.println(F("=== FIND O2 v2 : ปกติ vs RO/DI สลับ ==="));
}

void loop()
{
    scanPair(modN, F("ปกติ"));
    scanPair(modS, F("สลับ"));
    Serial.println(F("---------- วนใหม่ 2 วิ ----------"));
    delay(2000);
}
