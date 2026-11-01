// ============================================================================
//  LineDoctor  --  ตรวจสุขภาพสาย RS485 ทีละจุด (ไม่สแกน -- รายงานข้อเท็จจริง)
//
//  ต่อตามที่ผู้ใช้ยืนยัน:  RO(ทั้งคู่)->D2  DI(ทั้งคู่)->D3
//                          DE+RE โมดูล pressure -> D6   โมดูล O2 -> D7
//
//  ตรวจ 3 อย่าง:
//   [1] ระดับ idle ของ D2 เมื่อเปิดหูทีละโมดูล
//         100% HIGH = บัสนั้น idle ปกติ (มีคน bias/ขับ mark)
//         LOW/แกว่ง = บัสลอย/กลับขั้ว/receiver ไม่ทำงาน
//   [2] ยิงข้ามโมดูล: ขับผ่านโมดูลหนึ่ง ฟังอีกโมดูล
//         ได้ byte = สองบัสเชื่อมถึงกันจริง (A/B ยังพ่วงกันอยู่!) + ฮาร์ดแวร์ทำงาน
//         เงียบ    = บัสแยกกันจริง (ตามแผน) หรือเส้นทางเสีย
//   [3] ยิง Modbus id1/id2/id3 ทีละบัส แล้วโชว์ byte ดิบทุกตัวที่กลับมา
//
//  Serial Monitor @ 9600
// ============================================================================
#include <SoftwareSerial.h>

#define DE_PRESS 6
#define DE_O2    7

SoftwareSerial mod(2, 3);    // RX=D2(<-RO) TX=D3(->DI)

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

// [1] วัด idle: เปิดหูโมดูลเดียว แล้วอ่าน D2 500 ครั้ง -> คืน % ที่เป็น HIGH
static uint8_t idlePct(uint8_t deListen, uint8_t deOther)
{
    digitalWrite(deOther, HIGH);
    digitalWrite(deListen, LOW);
    delay(5);
    uint16_t hi = 0;
    for (uint16_t i = 0; i < 500; i++) { if (digitalRead(2)) hi++; delayMicroseconds(100); }
    digitalWrite(deListen, HIGH);
    return (uint8_t)(hi / 5);
}

// [2] ยิงข้าม: ขับ driver ของ deTx แล้วฟังผ่าน receiver ของ deRx -> นับ byte ที่ได้
static uint8_t crossEcho(uint8_t deTx, uint8_t deRx)
{
    while (mod.available()) mod.read();
    digitalWrite(deTx, HIGH);        // driver on (หูโมดูลนี้ปิด)
    digitalWrite(deRx, LOW);         // หูอีกโมดูลเปิด
    delay(3);
    const uint8_t pat[4] = { 0x55, 0xAA, 0x55, 0xAA };
    mod.write(pat, 4);
    delay(30);
    uint8_t n = 0;
    while (mod.available()) { mod.read(); n++; }
    digitalWrite(deRx, HIGH);
    return n;
}

// [3] ยิง Modbus แล้วเทกลับทุก byte ดิบ
static void probeRaw(uint8_t de, uint8_t deOther, uint8_t id)
{
    uint8_t q[8] = { id, 3, 0, (id == 3) ? (uint8_t)4 : (uint8_t)6, 0, 1 };  // pressure reg4 / O2 reg6
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    digitalWrite(deOther, HIGH);
    digitalWrite(de, HIGH); delay(2);
    mod.write(q, 8);
    delayMicroseconds(60);
    digitalWrite(de, LOW);
    uint8_t buf[24], n = 0;
    uint32_t dl = millis() + 250;
    while ((long)(millis() - dl) < 0) { if (mod.available() && n < sizeof(buf)) buf[n++] = mod.read(); }
    digitalWrite(de, HIGH);

    Serial.print(F("    id")); Serial.print(id);
    Serial.print(F(" -> raw[")); Serial.print(n); Serial.print(F("]:"));
    for (uint8_t k = 0; k < n; k++) { Serial.print(' '); if (buf[k] < 16) Serial.print('0'); Serial.print(buf[k], HEX); }
    Serial.println();
}

void setup()
{
    pinMode(DE_PRESS, OUTPUT); pinMode(DE_O2, OUTPUT);
    digitalWrite(DE_PRESS, HIGH); digitalWrite(DE_O2, HIGH);
    Serial.begin(9600);
    mod.begin(9600);
    mod.listen();
    delay(300);
    Serial.println();
    Serial.println(F("=== LINE DOCTOR ==="));
}

void loop()
{
    // [1] idle แต่ละบัส
    Serial.print(F("[1] idle D2: bus-PRESS(D6)=")); Serial.print(idlePct(DE_PRESS, DE_O2));
    Serial.print(F("% HIGH   bus-O2(D7)="));        Serial.print(idlePct(DE_O2, DE_PRESS));
    Serial.println(F("% HIGH   (100=ดี, 0/แกว่ง=บัสลอยหรือกลับขั้ว)"));

    // [2] ยิงข้าม
    Serial.print(F("[2] cross: drive-O2->hear-PRESS = ")); Serial.print(crossEcho(DE_O2, DE_PRESS));
    Serial.print(F(" bytes   drive-PRESS->hear-O2 = "));   Serial.print(crossEcho(DE_PRESS, DE_O2));
    Serial.println(F(" bytes   (>0 = สองบัสยังเชื่อมถึงกัน!)"));

    // [3] modbus ดิบ
    Serial.println(F("[3] modbus raw:"));
    Serial.println(F("  bus-O2 (D7):"));
    probeRaw(DE_O2, DE_PRESS, 1);
    probeRaw(DE_O2, DE_PRESS, 2);
    Serial.println(F("  bus-PRESS (D6):"));
    probeRaw(DE_PRESS, DE_O2, 3);
    Serial.println(F("--------------------------------------"));
    delay(2500);
}
