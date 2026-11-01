// ============================================================================
//  RS485_O2_Test  --  LIVE MONITOR  (คอนโซลสะอาด ดูสถานะสดได้)
//
//    RS485 บน SoftwareSerial:  RO -> D8 (RX) · DI -> D9 (TX)   <- ย้ายมาจาก D0/D1
//    DE+RE:                    D6  (เหมือนเดิม)
//    คอนโซล -> Hardware Serial (D0/D1) @9600  = สะอาด (ไม่มี RS485 ปน)
//
//  *** ย้ายสาย MAX485 (แค่ 2 เส้น) ***
//        RO : D0 -> D8      DI : D1 -> D9      (DE/RE คงไว้ที่ D6)
//        A/B และไฟ ไม่ต้องแตะ
//
//  เปิด Serial Monitor ตั้ง baud = 9600 -> เห็นสถานะสด ทุก 0.6 วิ
//    "OK  id=1  O2=20.9 %  addr=1"     = เจอ อ่านได้
//    "... no reply ..."                = ไม่เจอ (ขยับสายดูได้เลย)
//  -> ขยับ/กด แต่ละจุด ดูว่าอันไหนทำให้ขึ้น OK / หลุด = เจอจุดหลวม
// ============================================================================
#include <SoftwareSerial.h>

#define PIN_DE   6
SoftwareSerial mod(8, 9);        // RX=D8 (<-RO) , TX=D9 (->DI)

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

static bool rd(uint8_t id, uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = (uint8_t)c; q[7] = (uint8_t)(c >> 8);
    while (mod.available()) mod.read();
    digitalWrite(PIN_DE, HIGH); delay(2);
    mod.write(q, 8); mod.flush();
    digitalWrite(PIN_DE, LOW);
    uint8_t r[8], n = 0; uint32_t dl = millis() + 200;
    while (n < 7) {
        if ((long)(millis() - dl) >= 0) return false;
        if (!mod.available()) continue;
        uint8_t b = (uint8_t)mod.read();
        if (n == 0 && b != id) continue;
        if (n == 1 && b != 3) { n = 0; continue; }
        r[n++] = b;
    }
    if (crc16(r, 5) != (uint16_t)(r[5] | ((uint16_t)r[6] << 8))) return false;
    *out = (uint16_t)(((uint16_t)r[3] << 8) | r[4]);
    return true;
}

void setup()
{
    pinMode(PIN_DE, OUTPUT);
    digitalWrite(PIN_DE, LOW);
    Serial.begin(9600);
    mod.begin(9600);
    delay(300);
    Serial.println();
    Serial.println(F("=== O2 LIVE MONITOR (RS485 on D8/D9, DE=D6) ==="));
    Serial.println(F("wiggle wires -> watch OK / no reply change"));
}

void loop()
{
    uint16_t o2, addr;

    // ลอง id 1 ก่อน (ตัวที่รู้ว่าดี) แล้วไล่ 2..8 เผื่อถูกตั้ง id อื่น
    uint8_t found = 0;
    if (rd(1, 6, &o2)) found = 1;
    else for (uint8_t id = 2; id <= 8; id++) { if (rd(id, 6, &o2)) { found = id; break; } }

    if (found) {
        rd(found, 24, &addr);
        Serial.print(F("OK   id=")); Serial.print(found);
        Serial.print(F("  O2=")); Serial.print(o2 * 0.1f, 1);
        Serial.print(F(" %   addr(r24)=")); Serial.println(addr);
    } else {
        Serial.println(F("... no reply ...   (check A/B, power, DE=D6)"));
    }
    delay(600);
}
