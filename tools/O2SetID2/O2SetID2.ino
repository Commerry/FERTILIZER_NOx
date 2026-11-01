// ============================================================================
//  O2SetID2  --  ตั้ง ID เซนเซอร์ O2 "ตัวใหม่" : 1 -> 2   (FC06 เขียน reg24)
//
//  *** เสียบเฉพาะตัวใหม่ ตัวเดียว บนบัส O2! ตัวเก่าต้องถอด A/B ออก ***
//      (สองตัวเป็น id1 เหมือนกัน -- เสียบคู่แล้วเขียน = โดนทั้งคู่)
//
//  ผังขาจริง:  RO=D2  DI=D3  |  บัส O2: DE+RE = D7  |  บัส pressure: D6 (ค้าง HIGH)
//
//  ทำอัตโนมัติ: หา id ที่ตอบ -> เขียน reg24 = 2 -> ยืนยัน id2 -> โชว์สถานะสด
//  Serial Monitor @ 9600
// ============================================================================
#include <SoftwareSerial.h>

#define DE_PRESS 6
#define DE_O2    7
#define NEW_ID   2

SoftwareSerial mod(2, 3);

static inline void o2Tx() { digitalWrite(DE_O2, HIGH); }
static inline void o2Rx() { digitalWrite(DE_O2, LOW);  }

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

// FC03 อ่าน 1 reg -- ข้ามเฟรม FF ที่พ่นแทรก (สแกนหาหัวเฟรมที่ตรง)
static bool rd(uint8_t id, uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    o2Tx(); delay(2);
    mod.write(q, 8);
    delayMicroseconds(60);
    o2Rx();

    uint8_t buf[32], n = 0;
    uint32_t dl = millis() + 250;
    while ((long)(millis() - dl) < 0) {
        if (mod.available() && n < sizeof(buf)) buf[n++] = mod.read();
    }
    o2Tx();

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

// FC06 เขียน 1 reg (ไม่เช็ค echo -- ยืนยันด้วยการอ่านซ้ำ)
static void wr(uint8_t id, uint16_t reg, uint16_t val)
{
    uint8_t q[8] = { id, 6, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    o2Tx(); delay(2);
    mod.write(q, 8);
    delayMicroseconds(60);
    o2Rx();
    uint32_t dl = millis() + 300;
    while ((long)(millis() - dl) < 0) { if (mod.available()) mod.read(); }
    o2Tx();
}

void setup()
{
    pinMode(DE_PRESS, OUTPUT); pinMode(DE_O2, OUTPUT);
    digitalWrite(DE_PRESS, HIGH);      // ปิดหูบัส pressure ค้าง
    o2Tx();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== SET ID ตัวใหม่ -> 2   (เสียบตัวใหม่ตัวเดียว!) ==="));

    // 1) หา id ที่ตอบ
    uint8_t found = 0; uint16_t v = 0;
    for (uint8_t id = 1; id <= 8 && !found; id++)
        if (rd(id, 6, &v)) found = id;

    if (!found) { Serial.println(F("!! ไม่เจอเลย -- เช็ค A/B / ไฟ / โมดูล D7 -- กด reset ลองใหม่")); return; }

    Serial.print(F("เจอ id=")); Serial.print(found);
    Serial.print(F("  O2=")); Serial.print(v * 0.1f, 1); Serial.println(F(" %"));

    if (found == NEW_ID) { Serial.println(F(">> เป็น id2 อยู่แล้ว")); return; }

    // 2) เขียน id ใหม่
    Serial.println(F("เขียน reg24 = 2 ..."));
    wr(found, 24, NEW_ID);
    delay(400);

    // 3) ยืนยัน
    uint16_t v2 = 0;
    bool okNew = rd(NEW_ID, 6, &v2);
    bool okOld = rd(found, 6, &v2) && found != NEW_ID;   // id เดิมต้องเงียบแล้ว

    if (okNew && !okOld) {
        Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
        Serial.println(F(">>> สำเร็จ! ตัวใหม่ = id 2"));
        Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
    } else if (okNew && okOld) {
        Serial.println(F("!! id2 ตอบ แต่ id เดิมก็ยังตอบ ?! -- มี 2 ตัวบนบัส? ถอดตัวเก่าออกแล้วกด reset"));
    } else {
        Serial.println(F("!! เขียนไม่ติด (เซนเซอร์ไม่รับ FC06?) -- กด reset ลองซ้ำ"));
        Serial.println(F("   ถ้าซ้ำแล้วยังไม่ได้ = ตัวนี้ล็อก id -- ต้องย้าย 'ตัวเก่า' ไป id อื่นแทน (บอกผม)"));
    }
}

void loop()
{
    // สถานะสด: id1 / id2 ใครตอบ + ค่า
    uint16_t v;
    Serial.print(F("id1: "));
    if (rd(1, 6, &v)) { Serial.print(v * 0.1f, 1); Serial.print('%'); } else Serial.print(F("--"));
    Serial.print(F("   id2: "));
    if (rd(2, 6, &v)) { Serial.print(v * 0.1f, 1); Serial.print('%'); } else Serial.print(F("--"));
    Serial.println();
    delay(900);
}
