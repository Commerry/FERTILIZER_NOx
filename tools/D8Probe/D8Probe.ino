// ============================================================================
//  D8Probe  --  เจาะบัส D8 (MAX3485): ลอง EN ทั้ง 2 ขั้ว ฟัง+ถาม
//
//  [1] ฟัง 3 วิ โดย EN=LOW   (ขั้วปกติ: LOW = ฟัง)
//  [2] ฟัง 3 วิ โดย EN=HIGH  (เผื่อโมดูลกลับขั้ว)
//  [3] ถาม id1 โดยส่งตอน EN=HIGH ฟังตอน EN=LOW  (ปกติ)
//  [4] ถาม id1 โดยส่งตอน EN=LOW  ฟังตอน EN=HIGH (กลับขั้ว)
//  ทุกโหมด: โชว์จำนวน byte ดิบที่เข้ามา
//
//  D6/D7 ค้าง HIGH ตลอด    Serial @9600
// ============================================================================
#include <SoftwareSerial.h>

SoftwareSerial mod(2, 3);

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

static uint16_t listenRaw(uint8_t enLevel, uint16_t ms, bool show)
{
    digitalWrite(6, HIGH); digitalWrite(7, HIGH);
    digitalWrite(8, enLevel);
    while (mod.available()) mod.read();
    uint16_t n = 0; uint8_t col = 0;
    uint32_t dl = millis() + ms;
    while ((long)(millis() - dl) < 0) {
        if (!mod.available()) continue;
        uint8_t b = (uint8_t)mod.read(); n++;
        if (show && col < 48) {
            if (b < 16) Serial.print('0');
            Serial.print(b, HEX); Serial.print(' ');
            col++;
        }
    }
    if (show && col) Serial.println();
    digitalWrite(8, HIGH);
    return n;
}

static void query(uint8_t txLevel, uint8_t rxLevel)
{
    uint8_t q[8] = { 1, 3, 0, 6, 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    digitalWrite(6, HIGH); digitalWrite(7, HIGH);
    digitalWrite(8, txLevel); delay(2);
    mod.write(q, 8);
    delayMicroseconds(60);
    digitalWrite(8, rxLevel);
    uint8_t buf[24], n = 0;
    uint32_t dl = millis() + 250;
    while ((long)(millis() - dl) < 0) {
        if (mod.available() && n < sizeof(buf)) buf[n++] = mod.read();
    }
    digitalWrite(8, HIGH);
    Serial.print(F("   ได้ ")); Serial.print(n); Serial.print(F(" bytes: "));
    for (uint8_t k = 0; k < n && k < 16; k++) { if (buf[k] < 16) Serial.print('0'); Serial.print(buf[k], HEX); Serial.print(' '); }
    Serial.println();
}

void setup()
{
    pinMode(6, OUTPUT); pinMode(7, OUTPUT); pinMode(8, OUTPUT);
    digitalWrite(6, HIGH); digitalWrite(7, HIGH); digitalWrite(8, HIGH);
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== D8 PROBE (MAX3485) ==="));
}

void loop()
{
    Serial.println(F("[1] ฟัง 3 วิ EN=LOW:"));
    uint16_t a = listenRaw(LOW, 3000, true);
    Serial.print(F("   รวม ")); Serial.print(a); Serial.println(F(" bytes"));

    Serial.println(F("[2] ฟัง 3 วิ EN=HIGH:"));
    uint16_t b = listenRaw(HIGH, 3000, true);
    Serial.print(F("   รวม ")); Serial.print(b); Serial.println(F(" bytes"));

    Serial.println(F("[3] ถาม id1 (ส่ง HIGH / ฟัง LOW):"));
    query(HIGH, LOW);

    Serial.println(F("[4] ถาม id1 (ส่ง LOW / ฟัง HIGH):"));
    query(LOW, HIGH);

    Serial.println(F("=============== วนใหม่ ==============="));
    delay(1500);
}
