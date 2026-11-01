// ============================================================================
//  RelayTest  --  ทดสอบรีเลย์ D10-D13 + วาล์ว D9  พร้อมตัวสลับขั้ว (จับ low-trigger)
//
//  โหมดอัตโนมัติ (เริ่มเอง): ไล่ติดทีละตัว 1 วิ -> ดับ -> ตัวถัดไป วนไป
//    R1(D10 CP) -> R2(D11 CB) -> R3(D12 FB) -> R4(D13 FP) -> วนใหม่
//
//  คีย์ใน Serial Monitor @9600:
//    1..4 = สลับรีเลย์ตัวนั้น (ติด/ดับ)     a = กลับโหมดอัตโนมัติ
//    v    = วาล์ว D9 สลับ 0% <-> 100%       0 = ดับทุกตัว
//    p    = สลับขั้วสัญญาณ (active HIGH <-> active LOW)
//
//  วิธีอ่านผล:
//    เริ่มต้น = active HIGH (ดับ = LOW ตาม config ปัจจุบัน)
//    ถ้าเปิดมา "รีเลย์ติดหมด/ลั่น" แล้วกด p ทีเดียวหาย = โมดูลเป็น LOW-trigger
//    -> บอกผล ผมแก้ RELAY_ACTIVE_LOW ใน config ให้ถาวร
// ============================================================================
#define N_RELAY 6
const uint8_t PINS[N_RELAY] = { 10, 11, 12, 13, A0, A1 };
const char *NAMES[N_RELAY]  = { "R1 D10", "R2 D11", "R3 D12", "R4 D13", "R5 A0", "R6 A1" };
// *** A6/A7 ขับรีเลย์ไม่ได้ (analog-input อย่างเดียว) -- R5/R6 อยู่ A0/A1 ***
//     ปุ่มย้ายไป A7 (อ่านผ่าน analogRead + ต้องมี pullup 10k ภายนอก)

bool activeLow = false;              // false = active HIGH (ตาม config ปัจจุบัน)
bool st[N_RELAY] = { 0, 0, 0, 0, 0, 0 };
bool valveOn = false;
bool autoMode = true;
uint8_t autoIdx = 0;
uint8_t autoPhase = 0;               // 0 = ติดอยู่, 1 = ดับคั่น
uint32_t lastStep = 0;

static void apply(uint8_t i)
{
    digitalWrite(PINS[i], st[i] ? (activeLow ? LOW : HIGH)
                                : (activeLow ? HIGH : LOW));
}

static void applyAll() { for (uint8_t i = 0; i < N_RELAY; i++) apply(i); }

static void show()
{
    Serial.print(F("ขั้ว: active-"));
    Serial.print(activeLow ? F("LOW ") : F("HIGH"));
    Serial.print(F(" | "));
    for (uint8_t i = 0; i < N_RELAY; i++) {
        Serial.print(i + 1); Serial.print(st[i] ? F(":ON  ") : F(":off "));
    }
    Serial.print(F("| วาล์ว D9: ")); Serial.println(valveOn ? F("100%") : F("0%"));
}

void setup()
{
    for (uint8_t i = 0; i < N_RELAY; i++) { digitalWrite(PINS[i], activeLow ? HIGH : LOW); pinMode(PINS[i], OUTPUT); }
    pinMode(9, OUTPUT); analogWrite(9, 0);
    Serial.begin(9600);
    delay(300);
    Serial.println();
    Serial.println(F("=== RELAY TEST 6 ช่อง : ไล่ติดทีละตัว 1 วิ ==="));
    Serial.println(F("R1=D10 R2=D11 R3=D12 R4=D13 R5=A0 R6=A1"));
    Serial.println(F("1-6=สลับตัวนั้น  v=วาล์ว  a=อัตโนมัติ  0=ดับหมด  p=สลับขั้ว"));
    show();
    lastStep = millis();
}

void loop()
{
    if (Serial.available()) {
        char c = (char)Serial.read();
        if (c >= '1' && c <= '6') {
            autoMode = false;
            uint8_t i = (uint8_t)(c - '1');
            st[i] = !st[i]; apply(i);
            Serial.print(NAMES[i]); Serial.println(st[i] ? F(" -> ON") : F(" -> OFF"));
            show();
        } else if (c == 'v' || c == 'V') {
            valveOn = !valveOn;
            analogWrite(9, valveOn ? 255 : 0);
            show();
        } else if (c == '0') {
            autoMode = false;
            for (uint8_t i = 0; i < N_RELAY; i++) { st[i] = false; }
            applyAll();
            Serial.println(F(">> ดับหมด"));
            show();
        } else if (c == 'a' || c == 'A') {
            autoMode = true; autoIdx = 0; autoPhase = 0; lastStep = millis();
            Serial.println(F(">> โหมดอัตโนมัติ"));
        } else if (c == 'p' || c == 'P') {
            activeLow = !activeLow;
            applyAll();
            analogWrite(9, valveOn ? 255 : 0);
            Serial.print(F(">> สลับขั้วเป็น active-"));
            Serial.println(activeLow ? F("LOW") : F("HIGH"));
            show();
        }
    }

    if (autoMode && millis() - lastStep >= 1000) {
        lastStep = millis();
        if (autoPhase == 0) {                        // เพิ่งติดครบ 1 วิ -> ดับ แล้วไปตัวถัดไป
            st[autoIdx] = false; apply(autoIdx);
            autoIdx = (uint8_t)((autoIdx + 1) % N_RELAY);
            autoPhase = 1;
        } else {                                     // คั่นดับ 1 วิ -> ติดตัวถัดไป
            st[autoIdx] = true; apply(autoIdx);
            Serial.print(F("ติด: ")); Serial.println(NAMES[autoIdx]);
            autoPhase = 0;
        }
    }
}
