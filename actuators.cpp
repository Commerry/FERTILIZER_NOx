// ============================================================================
//  actuators.cpp
// ============================================================================
#include "actuators.h"

// ขารีเลย์ฟิกซ์ตายตัว (ไม่แก้จากจอ) -- ลำดับตรงกับ CFG_DEV_* และ bit ใน relaybits
static const uint8_t RELAY_PINS[RELAY_COUNT] = {
    PIN_RELAY_CP,   // bit0 -- Circulation Pump   (D10)
    PIN_RELAY_CB,   // bit1 -- Circulation Blower (D9, สัญญาณ VSP)
    PIN_RELAY_FB,   // bit2 -- Feed Blower        (A4)
    PIN_RELAY_FP    // bit3 -- Feed Pump          (A5)
};

static bool    s_relay[RELAY_COUNT] = { false, false, false, false };
static bool    s_estop = false;

// --- วาล์ว RV (บอลวาล์ว on/off, คุมตำแหน่งด้วยการจับเวลา) ---
static uint8_t  s_valveTarget = 0;      // เป้าหมาย 0-100 %
static float    s_valvePos    = 0.0f;   // ตำแหน่งประเมิน 0-100 %
static bool     s_homing      = true;   // บูตมา = บังคับโฮม (วิ่งปิด) ก่อนเสมอ (fail-safe)
static int8_t   s_valveDir    = 0;      // -1 ปิด, 0 หยุด, +1 เปิด
static uint32_t s_valveLastMs = 0;
static uint32_t s_homeStart   = 0;

// รีเลย์โมดูล opto ส่วนใหญ่เป็น active-LOW -> แปลงระดับไฟที่จุดเดียวตรงนี้
static inline uint8_t relay_level(bool on)
{
#if RELAY_ACTIVE_LOW
    return on ? LOW : HIGH;
#else
    return on ? HIGH : LOW;
#endif
}

// ขับวาล์ว: +1 เปิด / -1 ปิด / 0 หยุด
//   หยุด = 2 รีเลย์ OFF -> ปลายมอเตอร์ 2 เส้นต่อระดับเดียวกัน = ไม่มีแรงดันคร่อม
//   *** ไม่มีเส้นทางให้ OPEN+CLOSE ขับพร้อมกัน (กันช็อต/ฝืนทิศ) ***
static void valve_drive(int8_t dir)
{
    if (dir > 0) {                      // เปิด
        digitalWrite(PIN_VALVE_CLOSE, relay_level(false));
        digitalWrite(PIN_VALVE_OPEN,  relay_level(true));
    } else if (dir < 0) {               // ปิด
        digitalWrite(PIN_VALVE_OPEN,  relay_level(false));
        digitalWrite(PIN_VALVE_CLOSE, relay_level(true));
    } else {                            // หยุด
        digitalWrite(PIN_VALVE_OPEN,  relay_level(false));
        digitalWrite(PIN_VALVE_CLOSE, relay_level(false));
    }
    s_valveDir = dir;
}

// ลิมิตสวิตช์ปลายทาง (หน้าสัมผัสแห้ง -> COM=GND) ต่อ INPUT_PULLUP: LOW = ถึงปลายแล้ว
static inline bool valve_fb_open()   { return digitalRead(PIN_VALVE_FB_OPEN)   == LOW; }
static inline bool valve_fb_closed() { return digitalRead(PIN_VALVE_FB_CLOSED) == LOW; }

void act_begin()
{
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        // เขียนระดับ "ดับ" ก่อน แล้วค่อยตั้งเป็น OUTPUT แล้วเขียนซ้ำ
        // -> ขาถูกบังคับเป็นระดับดับตั้งแต่จังหวะแรก ไม่มีคลิกตอนบูต
        digitalWrite(RELAY_PINS[i], relay_level(false));   // = LOW
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], relay_level(false));   // = LOW
        s_relay[i] = false;                                // สถานะทางตรรกะ = ดับ
    }

    // วาล์ว RV = 2 รีเลย์ (เปิด/ปิด) -- เริ่มที่ "หยุด" ทั้งคู่ (เขียนดับก่อนตั้ง OUTPUT)
    digitalWrite(PIN_VALVE_OPEN,  relay_level(false));
    pinMode(PIN_VALVE_OPEN,  OUTPUT);
    digitalWrite(PIN_VALVE_OPEN,  relay_level(false));
    digitalWrite(PIN_VALVE_CLOSE, relay_level(false));
    pinMode(PIN_VALVE_CLOSE, OUTPUT);
    digitalWrite(PIN_VALVE_CLOSE, relay_level(false));
    pinMode(PIN_VALVE_FB_OPEN,   INPUT_PULLUP);
    pinMode(PIN_VALVE_FB_CLOSED, INPUT_PULLUP);
    s_valveTarget = 0;
    s_valvePos    = 0.0f;
    s_homing      = true;     // บูต/รีเซ็ต -> บังคับวิ่งปิดก่อนเสมอ (RV ต้องปิดตอนเปิดเครื่อง)
    s_valveDir    = 0;
    s_homeStart   = 0;
    s_valveLastMs = millis();

    // พายลอตแลมป์ -- เริ่มดับทั้งคู่ (ตั้ง level ก่อน OUTPUT กันกระพริบตอนบูต)
    digitalWrite(PIN_LAMP_GREEN, relay_level(false));
    pinMode(PIN_LAMP_GREEN, OUTPUT);
    digitalWrite(PIN_LAMP_GREEN, relay_level(false));
    digitalWrite(PIN_LAMP_RED, relay_level(false));
    pinMode(PIN_LAMP_RED, OUTPUT);
    digitalWrite(PIN_LAMP_RED, relay_level(false));

    s_estop = false;
}

void act_set_pilot(bool green, bool red)
{
    digitalWrite(PIN_LAMP_GREEN, relay_level(green));
    digitalWrite(PIN_LAMP_RED,   relay_level(red));
}

void act_set_relay(uint8_t idx, bool on)
{
    if (idx >= RELAY_COUNT) return;
    if (s_estop && on) return;              // ตอน E-STOP สั่งติดไม่ได้ แต่สั่งดับได้เสมอ

    s_relay[idx] = on;
    digitalWrite(RELAY_PINS[idx], relay_level(on));
}

bool act_get_relay(uint8_t idx)
{
    return (idx < RELAY_COUNT) ? s_relay[idx] : false;
}

uint8_t act_relay_bits()
{
    uint8_t b = 0;
    for (uint8_t i = 0; i < RELAY_COUNT; i++)
        if (s_relay[i]) b |= (uint8_t)(1 << i);
    return b;
}

// ตั้ง "เป้าหมาย" % วาล์ว -- ตัวขับจริง (จับเวลา) อยู่ที่ act_valve_tick()
void act_set_valve(uint8_t percent)
{
    if (percent > VALVE_MAX_PCT) percent = VALVE_MAX_PCT;
    s_valveTarget = percent;
}

// เรียกทุกรอบ loop -- ขยับมอเตอร์เข้าหาเป้า + คาลิเบรตตำแหน่งจากลิมิตปลายทาง
void act_valve_tick(uint32_t now)
{
    uint32_t dt = now - s_valveLastMs;
    s_valveLastMs = now;
    if (dt > 1000) dt = 1000;               // กันกระโดดตอน loop ค้าง / millis วน

    // === โฮมตอนบูต: "บังคับ" วิ่งปิดจนชนลิมิตดำ หรือครบ 125% stroke ===
    //   fail-safe: เปิดเครื่องมา RV ต้องปิดเสมอ -- ช่วงนี้ *ไม่เชื่อ* fb_open
    //   (กันกรณี feedback ยังไม่ต่อ/เพี้ยนแล้วข้ามการปิด -> วาล์วค้างเปิด)
    if (s_homing) {
        if (s_homeStart == 0) s_homeStart = now;
        if (valve_fb_closed() || (now - s_homeStart) > (VALVE_FULL_MS + VALVE_FULL_MS / 4)) {
            s_homing    = false;
            s_valvePos  = 0.0f;
            s_valveTarget = 0;
            valve_drive(0);
        } else {
            valve_drive(-1);                // วิ่งปิด
        }
        return;
    }

    // 1) อัปเดตตำแหน่งประเมินตามทิศที่กำลังขับ (เปิด=+ ปิด=-)
    if (s_valveDir > 0)      s_valvePos += (float)dt * 100.0f / (float)VALVE_FULL_MS;
    else if (s_valveDir < 0) s_valvePos -= (float)dt * 100.0f / (float)VALVE_FULL_MS;
    if (s_valvePos < 0.0f)   s_valvePos = 0.0f;
    if (s_valvePos > 100.0f) s_valvePos = 100.0f;

    // 2) ลิมิตปลายทางชนะค่าประเมินเสมอ (ความจริง) -> กัน drift สะสม
    if (valve_fb_closed()) s_valvePos = 0.0f;
    if (valve_fb_open())   s_valvePos = 100.0f;

    // 3) E-STOP -> บังคับปิด (fail-safe: หยุดฉีด N2) ; ปกติ -> ตามเป้า
    uint8_t target = s_estop ? 0 : s_valveTarget;

    // 4) คุมเข้าหาเป้าด้วยเขตตาย (deadband) -- อยู่ในเขต = หยุดค้าง
    float err = (float)target - s_valvePos;
    if      (err >  (float)VALVE_DEADBAND_PCT && !valve_fb_open())   valve_drive(+1);
    else if (err < -(float)VALVE_DEADBAND_PCT && !valve_fb_closed()) valve_drive(-1);
    else                                                            valve_drive(0);
}

uint8_t act_get_valve() { return (uint8_t)(s_valvePos + 0.5f); }

void act_all_off()
{
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        s_relay[i] = false;
        digitalWrite(RELAY_PINS[i], relay_level(false));
    }
    // วาล์ว: สั่งปิด (เป้า 0) + หยุดมอเตอร์ทันที -- act_valve_tick จะพาปิดต่อจนสุด
    s_valveTarget = 0;
    valve_drive(0);
}

void act_estop()
{
    act_all_off();
    s_estop = true;      // ตั้ง flag หลังดับ เพื่อให้ act_all_off ทำงานได้
}

void act_estop_clear() { s_estop = false; }
bool act_is_estop()    { return s_estop; }
