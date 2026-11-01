// ============================================================================
//  PWMTest  --  ทดสอบขา D9 แบบ "หรี่ไฟ" : ไล่ขึ้น 0->100% แล้วลง 100->0% ลื่น ๆ
//
//  ขยับ 1% ทุก 60ms  ->  ขึ้นสุด ~6 วิ ลงสุด ~6 วิ วนไม่หยุด
//  วาล์ว/โหลดควรแรงขึ้นช้า ๆ แล้วเบาลงช้า ๆ เหมือนหรี่ไฟ
//  มิเตอร์คร่อมโหลด: เข็ม/เลขไต่ 0 -> 5V แล้วไหลกลับ 5 -> 0V
//
//  พิมพ์เลข 0-100 = หยุดค้างที่ค่านั้น   |   พิมพ์ a = กลับโหมดหรี่อัตโนมัติ
//  Serial Monitor @ 9600
// ============================================================================
#define PIN_PWM 9

bool     autoMode = true;
int8_t   dir      = 1;      // +1 ขึ้น / -1 ลง
int16_t  pct      = 0;
uint32_t lastTick = 0;

static void applyPct(int16_t p)
{
    analogWrite(PIN_PWM, (uint16_t)p * 255 / 100);
}

void setup()
{
    pinMode(PIN_PWM, OUTPUT);
    analogWrite(PIN_PWM, 0);
    Serial.begin(9600);
    delay(300);
    Serial.println();
    Serial.println(F("=== PWM FADE TEST D9 : 0->100->0% วนเหมือนหรี่ไฟ ==="));
    Serial.println(F("พิมพ์เลข 0-100 = ค้างค่า | a = กลับโหมดหรี่"));
    lastTick = millis();
}

void loop()
{
    if (Serial.available()) {
        String s = Serial.readStringUntil('\n');
        s.trim();
        if (s.length()) {
            if (s[0] == 'a' || s[0] == 'A') {
                autoMode = true;
                Serial.println(F(">> โหมดหรี่อัตโนมัติ"));
            } else {
                int v = s.toInt();
                if (v >= 0 && v <= 100) {
                    autoMode = false;
                    pct = (int16_t)v;
                    applyPct(pct);
                    Serial.print(F(">> ค้างที่ ")); Serial.print(v); Serial.println(F(" %"));
                }
            }
        }
    }

    if (autoMode && millis() - lastTick >= 60) {     // 1% ทุก 60ms
        lastTick = millis();
        pct += dir;
        if (pct >= 100) { pct = 100; dir = -1; Serial.println(F("ถึง 100% -> ไล่ลง")); }
        if (pct <= 0)   { pct = 0;   dir = +1; Serial.println(F("ถึง 0%   -> ไล่ขึ้น")); }
        applyPct(pct);
    }
}
