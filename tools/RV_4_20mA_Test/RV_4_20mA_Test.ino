// ============================================================================
//  RV_4_20mA_Test  --  ทดสอบ/คาลิเบรตสาย RV:  Nano D9 PWM -> RC -> V->I -> วาล์ว
//
//  ต่อก่อนทดสอบ:
//    D9 --[1k]--+--> VIN+ โมดูล V->I
//               |
//            10uF 25V (+ บน, - ลง GND)
//               |
//              GND (ร่วม Nano)
//    โมดูล VCC=24V, GND=24V- (กราวด์ร่วม), OUT -> วาล์ว signal+
//
//  *** ถอดสายจอ (D0/D1) ก่อนอัปโหลด -- แล้ว Serial ว่างให้ test ใช้ ***
//
//  วิธีวัด:  มัลติมิเตอร์โหมด mA ต่ออนุกรมที่ OUT ของโมดูล (หรือในลูปวาล์ว)
//
//  คำสั่งผ่าน Serial Monitor (115200):
//    พิมพ์ตัวเลข 0-100 + Enter = ตั้ง % วาล์ว (ดู mA ที่วัดได้)
//    s = สวีปอัตโนมัติ 0->100->0 วนไปเรื่อย ๆ
//    x = หยุดสวีป ค้างค่าล่าสุด
//    0 / f = ปิดสุด(0%) / เปิดสุด(100%) ลัด
// ============================================================================
#include <Arduino.h>

#define PIN_RV 9            // D9 = Timer1/OC1A (PWM ~490Hz)

static uint8_t  s_pct   = 0;
static bool     s_sweep = false;

static void setPct(uint8_t pct)
{
    if (pct > 100) pct = 100;
    s_pct = pct;
    analogWrite(PIN_RV, map(pct, 0, 100, 0, 255));
    Serial.print(F("RV = "));
    Serial.print(pct);
    Serial.print(F(" %  (PWM duty "));
    Serial.print(map(pct, 0, 100, 0, 255));
    Serial.print(F("/255)  -> คาดหวัง ~"));
    // 0%=4mA, 100%=20mA (เชิงเส้น) -- ไว้เทียบกับที่วัดได้
    Serial.print(4.0f + (pct / 100.0f) * 16.0f, 1);
    Serial.println(F(" mA"));
}

void setup()
{
    pinMode(PIN_RV, OUTPUT);
    analogWrite(PIN_RV, 0);
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println(F("=== RV 4-20mA TEST ==="));
    Serial.println(F("พิมพ์ 0-100 = ตั้ง % | s=สวีป | x=หยุด | f=100% | วัด mA ที่ OUT"));
    setPct(0);
}

static void handleSerial()
{
    static char buf[8];
    static uint8_t n = 0;

    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == 's' || c == 'S') { s_sweep = true;  Serial.println(F(">> สวีป ON")); n = 0; continue; }
        if (c == 'x' || c == 'X') { s_sweep = false; Serial.println(F(">> สวีป OFF")); n = 0; continue; }
        if (c == 'f' || c == 'F') { s_sweep = false; setPct(100); n = 0; continue; }

        if (c >= '0' && c <= '9') {
            if (n < sizeof(buf) - 1) buf[n++] = c;
        } else if (c == '\n' || c == '\r') {
            if (n > 0) {
                buf[n] = 0;
                s_sweep = false;
                setPct((uint8_t)atoi(buf));
                n = 0;
            }
        }
    }
}

void loop()
{
    handleSerial();

    if (s_sweep) {
        static uint32_t last = 0;
        static int8_t   dir  = +5;
        if (millis() - last > 1500) {      // ขยับ 5% ทุก 1.5 วิ ให้ทันดูวาล์ว+วัด
            last = millis();
            int v = (int)s_pct + dir;
            if (v >= 100) { v = 100; dir = -5; }
            if (v <= 0)   { v = 0;   dir = +5; }
            setPct((uint8_t)v);
        }
    }
}
