// ============================================================================
//  O2SetID4 v2 --  ตั้ง id เซนเซอร์ O2 -> 4  ลองทุกกระบวนท่า
//
//  ท่า 1: FC06 เขียน reg24 = 4          (มาตรฐาน)
//  ท่า 2: FC16 (0x10) เขียน reg24 = 4   (บางเจ้ารับแต่ FC16)
//  แต่ละท่า: เขียน -> รอ -> เช็ค id4 ตอบไหม
//  ก่อนเริ่ม: อ่าน reg24 โชว์ (ควรเป็น 1)
//
//  *** เสียบเซนเซอร์ O2 ตัวเดียวบนบัส D7 ***
//  ขา: RO=D2 DI=D3 | DE บัส O2=D7 | D6 ค้าง HIGH   Serial @9600
// ============================================================================
#include <SoftwareSerial.h>

#define NEW_ID 4
SoftwareSerial mod(2, 3);

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

static void txFrame(const uint8_t *f, uint8_t n)
{
    while (mod.available()) mod.read();
    digitalWrite(7, HIGH); delay(2);
    mod.write(f, n);
    delayMicroseconds(60);
    digitalWrite(7, LOW);
}

static uint8_t rxCollect(uint8_t *buf, uint8_t max, uint16_t ms)
{
    uint8_t n = 0;
    uint32_t dl = millis() + ms;
    while ((long)(millis() - dl) < 0) {
        if (mod.available() && n < max) buf[n++] = mod.read();
    }
    digitalWrite(7, HIGH);
    return n;
}

static bool rd(uint8_t id, uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    txFrame(q, 8);
    uint8_t buf[32];
    uint8_t n = rxCollect(buf, sizeof(buf), 220);
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

static void dumpHex(const uint8_t *b, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) { if (b[i] < 16) Serial.print('0'); Serial.print(b[i], HEX); Serial.print(' '); }
    Serial.println();
}

void setup()
{
    pinMode(6, OUTPUT); pinMode(7, OUTPUT);
    digitalWrite(6, HIGH); digitalWrite(7, HIGH);
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== SET ID -> 4 (v2: FC06 + FC16) ==="));

    uint16_t v = 0;
    uint8_t cur = 0;
    for (uint8_t id = 1; id <= 8 && !cur; id++) if (rd(id, 6, &v)) cur = id;
    if (!cur) { Serial.println(F("!! ไม่เจอเซนเซอร์ -- reset ลองใหม่")); return; }

    Serial.print(F("เจอ id=")); Serial.print(cur);
    Serial.print(F("  O2=")); Serial.print(v * 0.1f, 1); Serial.println('%');
    uint16_t a24 = 0xFFFF;
    if (rd(cur, 24, &a24)) { Serial.print(F("reg24 (address) = ")); Serial.println(a24); }

    // ---- ท่า 1: FC06 ----
    Serial.println(F("[1] FC06 reg24 = 4 ..."));
    uint8_t w6[8] = { cur, 0x06, 0, 24, 0, NEW_ID };
    uint16_t c = crc16(w6, 6); w6[6] = c; w6[7] = c >> 8;
    txFrame(w6, 8);
    uint8_t rbuf[16];
    uint8_t rn = rxCollect(rbuf, sizeof(rbuf), 300);
    Serial.print(F("    ตอบ ")); Serial.print(rn); Serial.print(F(" bytes: ")); dumpHex(rbuf, rn);
    delay(400);
    if (rd(NEW_ID, 6, &v)) { Serial.println(F(">>> สำเร็จด้วย FC06! id=4")); goto live; }
    Serial.println(F("    id4 ยังเงียบ"));

    // ---- ท่า 2: FC16 ----
    Serial.println(F("[2] FC16 reg24 = 4 ..."));
    {
        uint8_t w16[11] = { cur, 0x10, 0, 24, 0, 1, 2, 0, NEW_ID };
        uint16_t c2 = crc16(w16, 9); w16[9] = c2; w16[10] = c2 >> 8;
        txFrame(w16, 11);
        rn = rxCollect(rbuf, sizeof(rbuf), 300);
        Serial.print(F("    ตอบ ")); Serial.print(rn); Serial.print(F(" bytes: ")); dumpHex(rbuf, rn);
    }
    delay(400);
    if (rd(NEW_ID, 6, &v)) { Serial.println(F(">>> สำเร็จด้วย FC16! id=4")); goto live; }
    Serial.println(F("    id4 ยังเงียบ -- ตัวนี้ล็อก id ถาวร (บอกผม มีแผน C)"));

live: ;
}

void loop()
{
    uint16_t v;
    Serial.print(F("id1: "));
    if (rd(1, 6, &v)) { Serial.print(v * 0.1f, 1); Serial.print('%'); } else Serial.print(F("--"));
    Serial.print(F("   id4: "));
    if (rd(4, 6, &v)) { Serial.print(v * 0.1f, 1); Serial.print('%'); } else Serial.print(F("--"));
    Serial.println();
    delay(900);
}
