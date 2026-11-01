// ============================================================================
//  O2Tame  --  "ปราบ" O2 #2 ที่พ่นเฟรม FF เอง (active upload)
//
//  ทำ 4 เฟสวนไป:
//   [1] ฟังเงียบ ๆ 4 วิ  -> เทเฟรม FF ที่มันพ่น (ดูโครงสร้าง+ค่า)
//   [2] ส่งคำสั่ง Winsen "สลับเป็นโหมดถาม-ตอบ (Q&A)"  FF 01 78 41 .. 46
//       แล้วฟัง 3 วิ -> ถ้าเงียบ = สลับสำเร็จ
//   [3] ลองถามแบบ Winsen Q&A:  FF 01 86 .. 79  -> รอ FF 86 <hi> <lo> ..
//   [4] ลองถาม Modbus id1..8 reg6 -> เผื่อโหมด Q&A ทำให้ Modbus ใช้ได้
//
//  บัส O2 = โมดูล DE+RE -> D7  (โมดูล pressure D6 ค้าง HIGH)
//  RO->D2  DI->D3   Serial Monitor @ 9600
// ============================================================================
#include <SoftwareSerial.h>

#define DE_PRESS 6
#define DE_O2    7

SoftwareSerial mod(2, 3);

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

static void o2Listen() { digitalWrite(DE_PRESS, HIGH); digitalWrite(DE_O2, LOW); }
static void o2Drive()  { digitalWrite(DE_PRESS, HIGH); digitalWrite(DE_O2, HIGH); }

// เทออกทุก byte ที่เข้ามาภายใน ms -- คืนจำนวน (พิมพ์ hex ให้ด้วยถ้า show)
static uint8_t dump(uint16_t ms, bool show)
{
    uint32_t dl = millis() + ms;
    uint8_t n = 0, col = 0;
    while ((long)(millis() - dl) < 0) {
        if (!mod.available()) continue;
        uint8_t b = (uint8_t)mod.read(); n++;
        if (show) {
            if (b == 0xFF && col) { Serial.println(); col = 0; }   // ขึ้นเฟรมใหม่
            if (b < 16) Serial.print('0');
            Serial.print(b, HEX); Serial.print(' ');
            if (++col >= 16) { Serial.println(); col = 0; }
        }
    }
    if (show && col) Serial.println();
    return n;
}

static void sendCmd(const uint8_t *c9, const __FlashStringHelper *name)
{
    Serial.print(F(">> ส่ง ")); Serial.println(name);
    while (mod.available()) mod.read();
    o2Drive(); delay(3);
    mod.write(c9, 9);
    delayMicroseconds(80);
    o2Listen();
}

void setup()
{
    pinMode(DE_PRESS, OUTPUT); pinMode(DE_O2, OUTPUT);
    digitalWrite(DE_PRESS, HIGH); digitalWrite(DE_O2, HIGH);
    Serial.begin(9600);
    mod.begin(9600);
    mod.listen();
    delay(200);
    Serial.println();
    Serial.println(F("=== O2 TAME : จับภาษา O2 #2 ==="));
}

void loop()
{
    // [1] ฟังเฉย ๆ -- มันพ่นอะไรออกมา
    Serial.println(F("[1] ฟัง 4 วิ (โหมดพ่นเอง?):"));
    o2Listen();
    uint8_t n1 = dump(4000, true);
    Serial.print(F("    รวม ")); Serial.print(n1); Serial.println(F(" bytes"));

    // [2] สั่งสลับเป็น Q&A mode (Winsen)
    const uint8_t QA[9] = { 0xFF, 0x01, 0x78, 0x41, 0x00, 0x00, 0x00, 0x00, 0x46 };
    sendCmd(QA, F("switch->Q&A  FF 01 78 41 ... 46"));
    Serial.println(F("[2] ฟัง 3 วิ (ควรเงียบถ้าสลับติด):"));
    uint8_t n2 = dump(3000, true);
    Serial.print(F("    รวม ")); Serial.print(n2);
    Serial.println(n2 == 0 ? F(" bytes  << เงียบ = สลับสำเร็จ!") : F(" bytes  << ยังพ่น = ไม่ติด"));

    // [3] ถามแบบ Q&A
    const uint8_t ASK[9] = { 0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79 };
    sendCmd(ASK, F("Q&A read  FF 01 86 ... 79"));
    Serial.println(F("[3] คำตอบ:"));
    dump(400, true);

    // [4] ลอง Modbus reg6 id1..8
    Serial.print(F("[4] Modbus: "));
    bool found = false;
    for (uint8_t id = 1; id <= 8 && !found; id++) {
        uint8_t q[8] = { id, 3, 0, 6, 0, 1 };
        uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
        while (mod.available()) mod.read();
        o2Drive(); delay(2);
        mod.write(q, 8);
        delayMicroseconds(60);
        o2Listen();
        uint8_t r[8], n = 0; uint32_t dl = millis() + 180;
        while (n < 7 && (long)(millis() - dl) < 0) {
            if (!mod.available()) continue;
            uint8_t b = (uint8_t)mod.read();
            if (n == 0 && b != id) continue;
            if (n == 1 && b != 3) { n = 0; continue; }
            r[n++] = b;
        }
        if (n == 7 && crc16(r, 5) == (uint16_t)(r[5] | ((uint16_t)r[6] << 8))) {
            Serial.print(F("ตอบ! id=")); Serial.print(id);
            Serial.print(F(" O2=")); Serial.print((((uint16_t)r[3] << 8) | r[4]) * 0.1f, 1); Serial.println(F(" %"));
            found = true;
        } else Serial.print('.');
    }
    if (!found) Serial.println(F(" ไม่ตอบ"));
    Serial.println(F("======================================"));
    delay(1500);
}
