// ============================================================================
//  RS485Gateway  --  อ่านหลายอุปกรณ์บนบัส RS485 เดียว (โพลทีละตัว/รอบ)
//
//  ตารางอุปกรณ์: id / baud / register / ชื่อ  -> ไล่อ่าน "ทีละตัว/รอบ" (round-robin)
//  ทุก query มี idle ยาวคั่นก่อน (เหมือนอ่านเดี่ยว = จังหวะที่พิสูจน์แล้วว่าได้)
//  จังหวะ DE ใช้ค่าที่พิสูจน์แล้ว: txMode -> delay(2) -> write -> flush -> รับทันที
//
//  ขา RO=D2 DI=D3 DE=D7 RE=D8
//
//  FAIL raw[0]:            = เงียบ (ตัวนั้นไม่ตอบเลย)
//  FAIL raw[7]: 01 03 ..   = มีตอบแต่เฟรม/CRC ไม่ตรง
// ============================================================================
#include <SoftwareSerial.h>

#define PIN_RO 2
#define PIN_DI 3
#define PIN_DE 7
#define PIN_RE 8

SoftwareSerial mod(PIN_RO, PIN_DI);

struct Dev { uint8_t id; long baud; uint16_t reg; uint8_t cnt; float scale; const char *name; const char *unit; };
Dev devs[] = {
    { 1, 9600, 6, 1, 0.1f,  "O2#1 ", "%"   },   // O2 reg6 x0.1
    { 3, 9600, 4, 1, 0.01f, "PRESS", "kPa" },   // pressure reg4 /100 (dp=2)
    // { 2, 9600, 6, 1, 0.1f, "O2#2 ", "%" },   // <- ตัวใหม่มาค่อยเปิด
};
const uint8_t NDEV = sizeof(devs) / sizeof(devs[0]);
long    curBaud = 0;
uint8_t idx = 0;                       // ตัวชี้ round-robin

// RE ตรึง LOW ตลอด (receiver เปิดค้าง) -> ขา RX ไม่ลอยตอน idle-drive ยาว ๆ
// เราจะได้ยิน echo ของ query ตัวเอง -- ตัวสแกนเฟรมข้ามมันเองได้ ไม่เป็นไร
static inline void txMode() { digitalWrite(PIN_DE, HIGH); }
static inline void rxMode() { digitalWrite(PIN_DE, LOW);  }

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

uint8_t rawbuf[48];
uint8_t rawlen;

// ยิง FC03 -> เก็บทุก byte ที่กลับมา -> สแกนหาเฟรมที่ถูก
//
// *** ACTIVE IDLE: master ขับบัสค้างที่ mark ตลอดตอนว่าง (DE เปิดค้าง) ***
//   เหตุ: pressure (24V) มี bias ภายในดึงบัสค้างขั้วผิดตอนว่าง -> O2 เห็น break ตลอด
//         เลยไม่เคยได้ยิน query. ไดรเวอร์เราแรงกว่า bias -> ตรึง idle ที่ถูกให้ O2
//   ปล่อยบัส (rxMode) เฉพาะช่วงรอคำตอบ แล้วรีบกลับมาขับต่อ
static bool readReg(uint8_t id, uint16_t reg, uint8_t cnt, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, cnt };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;

    while (mod.available()) mod.read();
    txMode(); delay(20);                                          // ขับ mark ยาว ๆ ให้ UART ของ O2 หลุดจาก break ก่อน
    mod.write(q, 8); mod.flush(); rxMode();                       // ปล่อยบัสเฉพาะช่วงรอคำตอบ

    rawlen = 0;
    uint32_t hard = millis() + 300, quiet = millis() + 300;
    while ((long)(millis() - hard) < 0 && (long)(millis() - quiet) < 0) {
        if (mod.available()) {
            if (rawlen < sizeof(rawbuf)) rawbuf[rawlen++] = mod.read();
            quiet = millis() + 15;
        }
    }

    txMode();                                                     // ได้คำตอบ/หมดเวลาแล้ว กลับมาขับ idle ต่อทันที

    uint8_t need = 3 + 2 * cnt + 2;
    for (uint8_t s = 0; s + need <= rawlen; s++) {
        if (rawbuf[s] == id && rawbuf[s + 1] == 3 && rawbuf[s + 2] == 2 * cnt) {
            uint16_t got = rawbuf[s + need - 2] | ((uint16_t)rawbuf[s + need - 1] << 8);
            if (crc16(&rawbuf[s], need - 2) == got) {
                for (uint8_t i = 0; i < cnt; i++)
                    out[i] = ((uint16_t)rawbuf[s + 3 + 2 * i] << 8) | rawbuf[s + 4 + 2 * i];
                return true;
            }
        }
    }
    return false;
}

void setup()
{
    pinMode(PIN_DE, OUTPUT); pinMode(PIN_RE, OUTPUT);
    digitalWrite(PIN_RE, LOW);          // receiver เปิดค้างถาวร
    txMode();                           // ACTIVE IDLE: ขับบัสค้างตั้งแต่บูต
    Serial.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== RS485 GATEWAY (active-idle drive) ==="));
}

void loop()
{
    // อ่านทุกตัวในตาราง (เว้นจังหวะ T3.5 ระหว่างตัว) แล้วพิมพ์รวมบรรทัดเดียว = ดูสดง่าย
    Serial.print('t'); Serial.print(millis() / 1000); Serial.print(F("s  "));
    for (uint8_t i = 0; i < NDEV; i++) {
        Dev &d = devs[i];
        if (d.baud != curBaud) { mod.begin(d.baud); curBaud = d.baud; delay(10); }

        uint16_t v; bool ok = false;
        for (uint8_t t = 0; t < 2 && !ok; t++) { ok = readReg(d.id, d.reg, d.cnt, &v); if (!ok) delay(8); }

        Serial.print(d.name); Serial.print('=');
        if (ok) { Serial.print(v * d.scale, 2); Serial.print(d.unit); }
        else    { Serial.print(F("----")); }
        Serial.print(F("  |  "));
        delay(20);                      // เว้น T3.5 ก่อนยิงตัวถัดไป
    }
    Serial.println();
    delay(500);
}
