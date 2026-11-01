// ============================================================================
//  SensorHealthCheck  --  "เซนเซอร์เสียหรือเปล่า?" ไล่ทุก baud x ทุก ID อัตโนมัติ
//
//  *** ต่อสายตามรูปที่ส่งมา (DE/RE แยกขากัน) ***
//        MAX485  RO -> D2   (เขียว)   = ขารับ  (RX)
//        MAX485  DI -> D3   (น้ำตาล)  = ขาส่ง  (TX)
//        MAX485  DE -> D7   (ม่วง)
//        MAX485  RE -> D8   (น้ำเงิน)
//        MAX485  A  -> เซนเซอร์ 485A     B -> เซนเซอร์ 485B
//        VCC -> 5V   GND -> GND   (เซนเซอร์ JXM-02 0-30% ใช้ไฟ 5V)
//
//   ถ้าขาไม่ตรงกับที่ต่อจริง แก้ 4 #define ข้างล่างให้ตรง แล้วอัปใหม่
//
//  อัป -> เปิด Serial Monitor ตั้ง baud = 9600 -> อ่านผล:
//    ">>> ALIVE!  baud=.. id=.. O2=..%"   = เซนเซอร์ดี (จด baud/id ที่เจอไว้)
//    "*** NO REPLY ทุก baud/id ***"       = ไม่ตอบเลย -> ทำตามขั้นตอนที่พรินต์บนจอ
// ============================================================================
#include <SoftwareSerial.h>

// ---- แก้ตรงนี้ถ้าขาไม่ตรงกับที่ต่อจริง --------------------------------------
#define PIN_RO   2      // MAX485 RO  -> Arduino  (ขารับ RX)
#define PIN_DI   3      // MAX485 DI  -> Arduino  (ขาส่ง TX)
#define PIN_DE   7      // MAX485 DE  -> Arduino
#define PIN_RE   8      // MAX485 RE  -> Arduino
// ---------------------------------------------------------------------------

SoftwareSerial mod(PIN_RO, PIN_DI);           // (RX=RO, TX=DI)

const long BAUDS[] = { 9600, 4800, 19200 };   // 9600 = ค่าโรงงานปกติ ลองก่อน

// DE=HIGH + RE=HIGH -> โหมดส่ง   ,   DE=LOW + RE=LOW -> โหมดรับ
static inline void txMode() { digitalWrite(PIN_DE, HIGH); digitalWrite(PIN_RE, HIGH); }
static inline void rxMode() { digitalWrite(PIN_DE, LOW);  digitalWrite(PIN_RE, LOW);  }

static uint16_t crc16(const uint8_t *b, uint8_t n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*b++);
        for (uint8_t i = 0; i < 8; i++)
            c = (c & 1) ? (uint16_t)((c >> 1) ^ 0xA001) : (uint16_t)(c >> 1);
    }
    return c;
}

// อ่าน 1 register (FC03) ที่ baud ปัจจุบันของ mod ; true = ได้คำตอบ CRC ถูก
static bool rd(uint8_t id, uint16_t reg, uint16_t *out)
{
    uint8_t q[8] = { id, 3, (uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), 0, 1 };
    uint16_t c = crc16(q, 6); q[6] = (uint8_t)c; q[7] = (uint8_t)(c >> 8);
    while (mod.available()) mod.read();
    txMode(); delay(2);
    mod.write(q, 8); mod.flush();
    rxMode();
    uint8_t r[8], n = 0; uint32_t dl = millis() + 150;
    while (n < 7) {
        if ((long)(millis() - dl) >= 0) return false;
        if (!mod.available()) continue;
        uint8_t b = (uint8_t)mod.read();
        if (n == 0 && b != id) continue;
        if (n == 1 && b != 3) { n = 0; continue; }
        r[n++] = b;
    }
    if (crc16(r, 5) != (uint16_t)(r[5] | ((uint16_t)r[6] << 8))) return false;
    *out = (uint16_t)(((uint16_t)r[3] << 8) | r[4]);
    return true;
}

void setup()
{
    pinMode(PIN_DE, OUTPUT);
    pinMode(PIN_RE, OUTPUT);
    rxMode();                               // เริ่มที่โหมดรับ
    Serial.begin(9600);
    delay(300);
    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("   O2 SENSOR HEALTH CHECK"));
    Serial.println(F("   RO=D2  DI=D3  DE=D7  RE=D8"));
    Serial.println(F("   ไล่ทุก baud x ทุก ID ให้อัตโนมัติ"));
    Serial.println(F("========================================"));
}

void loop()
{
    for (uint8_t bi = 0; bi < 3; bi++) {
        mod.begin(BAUDS[bi]);
        delay(60);
        Serial.print(F("ลอง baud ")); Serial.print(BAUDS[bi]); Serial.print(F(" : "));

        for (uint8_t id = 1; id <= 8; id++) {
            uint16_t o2;
            if (rd(id, 6, &o2)) {                 // เจอ! อ่านค่า O2 ได้
                uint16_t addr = 0; rd(id, 24, &addr);
                Serial.println();
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                Serial.print  (F(">>> ALIVE!  baud=")); Serial.print(BAUDS[bi]);
                Serial.print  (F("  id="));            Serial.print(id);
                Serial.print  (F("  O2="));            Serial.print(o2 * 0.1f, 1);
                Serial.print  (F(" %   addr(r24)=")); Serial.println(addr);
                Serial.println(F(">>> เซนเซอร์ดี! จด baud/id ไว้เลย"));
                Serial.println(F(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>"));
                while (1) { }                     // เจอแล้ว หยุดค้างไว้
            }
            Serial.print('.');                    // ยังไม่เจอ id นี้
        }
        Serial.println(F(" ไม่ตอบ"));
        mod.end();
    }

    Serial.println(F("*** NO REPLY ทุก baud/id  ->  ทำตามนี้ทีละข้อ: ***"));
    Serial.println(F("  1) สลับสาย A <-> B แล้วกด reset  (สาเหตุอันดับ 1)"));
    Serial.println(F("  2) วัดไฟเลี้ยงเซนเซอร์ 5V ตรงไหม / มี LED ติดไหม"));
    Serial.println(F("  3) เอาเซนเซอร์ 'ตัวที่เคยอ่านได้' มาเสียบชุดนี้แทน:"));
    Serial.println(F("       - ตัวดีขึ้น ALIVE แต่ตัวนี้ไม่  = ตัวนี้เสียจริง"));
    Serial.println(F("       - ตัวดีก็ไม่ขึ้นด้วย            = สาย/ชุดมีปัญหา ไม่ใช่เซนเซอร์"));
    Serial.println(F("---- วนเช็คใหม่ใน 3 วิ ----"));
    Serial.println();
    delay(3000);
}
