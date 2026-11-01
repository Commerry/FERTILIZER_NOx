// ============================================================================
//  ValveTest  --  เทส wiring บอลวาล์ว RV: 2 relay H-bridge + feedback 2 จุด
//
//  ต่อก่อน (ตรงกับ config.h ของโปรแกรมหลัก):
//    D4  -> Relay เปิด (OPEN)
//    D5  -> Relay ปิด  (CLOSE)
//    A2  <- เทา  全开 (เปิดสุด)   -- INPUT_PULLUP, LOW = ถึงแล้ว
//    A3  <- ดำ   关闭 (ปิดสุด)    -- INPUT_PULLUP, LOW = ถึงแล้ว
//    ขาว (COM feedback) -> GND
//    แดง/เหลือง(วาล์ว) -> ผ่าน H-bridge 2 relay (ดูผังในแชต)
//
//  *** ถอดสายจอ (D0/D1) ก่อนอัปโหลด -- Serial ว่างให้เทสใช้ ***
//  โมดูลรีเลย์ = high-level trigger (ติดเมื่อ HIGH) ตรงกับ RELAY_ACTIVE_LOW=0
//
//  คำสั่ง Serial (115200):
//    o = ขับเปิด   c = ขับปิด   s = หยุด (ทั้งคู่ OFF)
//    t = จับเวลา: วิ่งปิดสุด -> เปิดสุด พิมพ์เวลาที่ใช้ (วัด T อัตโนมัติ)
//  สถานะ feedback พิมพ์ทุก 500ms อัตโนมัติ
// ============================================================================
#include <Arduino.h>

#define PIN_OPEN   4
#define PIN_CLOSE  5
#define PIN_FB_OPEN   A2     // เทา
#define PIN_FB_CLOSED A3     // ดำ

#define RELAY_ACTIVE_LOW 0
static inline uint8_t lvl(bool on) {
#if RELAY_ACTIVE_LOW
    return on ? LOW : HIGH;
#else
    return on ? HIGH : LOW;
#endif
}

static inline bool fbOpen()   { return digitalRead(PIN_FB_OPEN)   == LOW; }
static inline bool fbClosed() { return digitalRead(PIN_FB_CLOSED) == LOW; }

static int8_t s_dir = 0;     // -1 ปิด, 0 หยุด, +1 เปิด

static void drive(int8_t dir)
{
    if (dir > 0)      { digitalWrite(PIN_CLOSE, lvl(false)); digitalWrite(PIN_OPEN,  lvl(true));  }
    else if (dir < 0) { digitalWrite(PIN_OPEN,  lvl(false)); digitalWrite(PIN_CLOSE, lvl(true));  }
    else              { digitalWrite(PIN_OPEN,  lvl(false)); digitalWrite(PIN_CLOSE, lvl(false)); }
    s_dir = dir;
    Serial.print(F(">> "));
    Serial.println(dir > 0 ? F("เปิด (OPEN)") : dir < 0 ? F("ปิด (CLOSE)") : F("หยุด (STOP)"));
}

void setup()
{
    digitalWrite(PIN_OPEN,  lvl(false)); pinMode(PIN_OPEN,  OUTPUT); digitalWrite(PIN_OPEN,  lvl(false));
    digitalWrite(PIN_CLOSE, lvl(false)); pinMode(PIN_CLOSE, OUTPUT); digitalWrite(PIN_CLOSE, lvl(false));
    pinMode(PIN_FB_OPEN,   INPUT_PULLUP);
    pinMode(PIN_FB_CLOSED, INPUT_PULLUP);
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("=== VALVE TEST (2-relay H-bridge) ==="));
    Serial.println(F("o=เปิด c=ปิด s=หยุด t=จับเวลาเต็มสโตรก"));
}

// จับเวลา: ปิดสุดก่อน แล้ววิ่งเปิดจนชนลิมิตเปิด พิมพ์เวลา
static void measureStroke()
{
    Serial.println(F("[T] วิ่งปิดสุดก่อน..."));
    drive(-1);
    uint32_t t0 = millis();
    while (!fbClosed() && millis() - t0 < 10000UL) delay(10);
    drive(0);
    delay(500);

    Serial.println(F("[T] วิ่งเปิด จับเวลา..."));
    drive(+1);
    uint32_t ts = millis();
    while (!fbOpen() && millis() - ts < 10000UL) delay(10);
    uint32_t dt = millis() - ts;
    drive(0);

    Serial.print(F("[T] เวลาเปิดเต็ม = "));
    Serial.print(dt);
    Serial.print(F(" ms"));
    if (!fbOpen()) Serial.print(F("  (!! ไม่ชนลิมิตเปิด -- timeout/เช็คสาย)"));
    Serial.println();
}

void loop()
{
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == 'o' || c == 'O') drive(+1);
        else if (c == 'c' || c == 'C') drive(-1);
        else if (c == 's' || c == 'S') drive(0);
        else if (c == 't' || c == 'T') measureStroke();
    }

    // auto-stop ที่ปลายทาง (กันมอเตอร์ฝืนถ้าลิมิตในตัวไม่ตัด)
    if (s_dir > 0 && fbOpen())   drive(0);
    if (s_dir < 0 && fbClosed()) drive(0);

    static uint32_t last = 0;
    if (millis() - last > 500) {
        last = millis();
        Serial.print(F("fb: เปิดสุด="));
        Serial.print(fbOpen() ? F("YES") : F("no"));
        Serial.print(F("  ปิดสุด="));
        Serial.print(fbClosed() ? F("YES") : F("no"));
        Serial.print(F("  dir="));
        Serial.println(s_dir > 0 ? F("OPEN") : s_dir < 0 ? F("CLOSE") : F("stop"));
    }
}
