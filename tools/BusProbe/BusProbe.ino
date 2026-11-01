// ============================================================================
//  BusProbe  --  จับผิด: ใครตอบ id ไหนบ้าง (ต่อทั้ง 2 ตัว เปิดไฟปกติ)
//
//  ทฤษฎี: pressure (อดีต id1) อาจยังแอบตอบ id1 -> ชนกับ O2 -> O2 เลยดูเงียบ
//
//  ยิงทีละคำถาม แล้วพิมพ์ "ทุก byte ดิบ" ที่ได้กลับมา:
//    id1 reg0 : O2 ตอบ = ค่า 0 หรือค่าคงที่ / pressure ตอบ = 3 (address ของมัน!)
//    id1 reg6 : O2 ตอบ = ~209 (20.9%)
//    id3 reg0 : pressure ตอบ = 3  (ตัวเทียบ)
//    id6 reg0 : "ต้องเงียบ" (ไม่มีใครเป็น id6) -- ถ้ามีตอบ = มีตัว promiscuous!
//
//  ขา RO=D2 DI=D3 DE=D7 RE=D8
// ============================================================================
#include <SoftwareSerial.h>

#define PIN_RO 2
#define PIN_DI 3
#define PIN_DE 7
#define PIN_RE 8

SoftwareSerial mod(PIN_RO, PIN_DI);

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

// ยิง FC03 1 register -> เก็บ "ทุก byte" 250ms -> คืน rawlen (echo query โผล่ด้วย = ปกติ)
static void probe(uint8_t id, uint16_t reg)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;

    while (mod.available()) mod.read();
    txMode(); delay(3);
    mod.write(q, 8); mod.flush();
    rxMode();

    rawlen = 0;
    uint32_t dl = millis() + 250;
    while ((long)(millis() - dl) < 0) {
        if (mod.available() && rawlen < sizeof(rawbuf)) rawbuf[rawlen++] = mod.read();
    }
    txMode();

    Serial.print(F("ask id")); Serial.print(id);
    Serial.print(F(" reg")); Serial.print(reg);
    Serial.print(F("  -> raw[")); Serial.print(rawlen); Serial.print(F("]:"));
    for (uint8_t k = 0; k < rawlen; k++) {
        Serial.print(' ');
        if (rawbuf[k] < 16) Serial.print('0');
        Serial.print(rawbuf[k], HEX);
    }
    Serial.println();
    delay(300);                          // เว้นให้บัสเงียบสนิทก่อนคำถามถัดไป
}

void setup()
{
    pinMode(PIN_DE, OUTPUT); pinMode(PIN_RE, OUTPUT);
    digitalWrite(PIN_RE, LOW);           // receiver เปิดค้าง (เห็น echo ตัวเองด้วย -- อ่านข้ามได้)
    txMode();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== BUS PROBE : ใครตอบ id ไหน ==="));
    Serial.println(F("(8 byte แรกของทุกบรรทัด = echo คำถามตัวเอง ให้ดู byte หลังจากนั้น)"));
}

void loop()
{
    probe(1, 0);     // O2 ควรตอบ -- ถ้าค่า data = 00 03 = pressure แอบตอบ!
    probe(1, 6);     // O2 ควรตอบ ~209 (0x00D1)
    probe(3, 0);     // pressure ควรตอบ 3 (ตัวเทียบ)
    probe(6, 0);     // ต้องเงียบ (echo 8 byte เท่านั้น) -- มีเกิน = promiscuous!
    Serial.println(F("---------- วนใหม่ 3 วิ ----------"));
    delay(3000);
}
