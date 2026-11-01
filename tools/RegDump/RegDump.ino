// ============================================================================
//  RegDump  --  อ่าน register 0-40 ของ pressure (id3) ทีละตัว หา register ซ่อน
//
//  อ่านอย่างเดียว (FC03) ปลอดภัย 100% ไม่เขียนอะไร
//  ตัวไหนอ่านได้ = มีจริง -> พิมพ์ค่า  |  ตัวไหน exception/เงียบ = ไม่มี
//
//  ขา RO=D2 DI=D3 DE=D7 RE=D8  (O2 เสียบค้างได้ ไม่รบกวน -- ถามแต่ id3)
// ============================================================================
#include <SoftwareSerial.h>

#define PIN_RO 2
#define PIN_DI 3
#define PIN_DE 7
#define PIN_RE 8
#define DEV_ID 3

SoftwareSerial mod(PIN_RO, PIN_DI);

static inline void txMode() { digitalWrite(PIN_DE, HIGH); }
static inline void rxMode() { digitalWrite(PIN_DE, LOW);  }

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) { c ^= *b++; for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xA001) : (c >> 1); }
    return c;
}

// อ่าน 1 register (ครั้งเดียว) ; คืน: 1=ได้ค่า, 0=เงียบ, 2=exception
static uint8_t rd1_once(uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { DEV_ID, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = c; q[7] = c >> 8;
    while (mod.available()) mod.read();
    txMode(); delay(20);                          // pre-drive ยาว = จังหวะเดียวกับ gateway ที่อ่านนิ่ง
    mod.write(q, 8); mod.flush(); rxMode();

    uint8_t r[12], n = 0;
    uint32_t dl = millis() + 250;
    while ((long)(millis() - dl) < 0 && n < sizeof(r)) {
        if (mod.available()) r[n++] = mod.read();
    }
    txMode();

    // ตอบปกติ: [id][03][02][hi][lo][crc][crc]
    for (uint8_t s = 0; s + 7 <= n; s++) {
        if (r[s] == DEV_ID && r[s + 1] == 3 && r[s + 2] == 2) {
            uint16_t got = r[s + 5] | ((uint16_t)r[s + 6] << 8);
            if (crc16(&r[s], 5) == got) { *out = ((uint16_t)r[s + 3] << 8) | r[s + 4]; return 1; }
        }
        if (r[s] == DEV_ID && r[s + 1] == 0x83) return 2;   // exception FC03
    }
    return 0;
}

// retry 3 ครั้ง -- เอาผลที่ "ไม่เงียบ" ครั้งแรก
static uint8_t rd1(uint16_t reg, uint16_t *out)
{
    for (uint8_t t = 0; t < 3; t++) {
        uint8_t st = rd1_once(reg, out);
        if (st != 0) return st;
        delay(30);
    }
    return 0;
}

void setup()
{
    pinMode(PIN_DE, OUTPUT); pinMode(PIN_RE, OUTPUT);
    digitalWrite(PIN_RE, LOW);
    txMode();
    Serial.begin(9600); mod.begin(9600); delay(300);
    Serial.println();
    Serial.println(F("=== PRESSURE (id3) REGISTER DUMP 0-40 ==="));

    for (uint16_t reg = 0; reg <= 40; reg++) {
        uint16_t v;
        uint8_t st = rd1(reg, &v);
        Serial.print(F("reg ")); Serial.print(reg);
        if (st == 1) {
            Serial.print(F(" = ")); Serial.print(v);
            Serial.print(F("  (0x"));
            if (v < 0x1000) Serial.print('0');
            if (v < 0x100)  Serial.print('0');
            if (v < 0x10)   Serial.print('0');
            Serial.print(v, HEX); Serial.println(')');
        }
        else if (st == 2) Serial.println(F(" : exception (ไม่มี/ห้ามอ่าน)"));
        else              Serial.println(F(" : เงียบ"));
        delay(60);
    }
    Serial.println(F("=== จบ ==="));
}

void loop() { }
