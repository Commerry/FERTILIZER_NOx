// ============================================================================
//  process.cpp
// ============================================================================
#include "process.h"
#include "config.h"
#include "settings.h"
#include "actuators.h"
#include "link_protocol.h"

static uint8_t  s_runState = LNK_RUN_IDLE;
static uint8_t  s_rvZone   = LNK_RVZ_OFF;
static uint32_t s_runStart = 0;      // millis() ตอนกด START

// --- วาล์ว RV (คุมแบบ band -- % คงที่ต่อช่วง O2) ---
static float    s_valve      = 0.0f;   // % เป้าหมายล่าสุดที่ส่งให้วาล์ว (ไว้ส่งขึ้นจอ/debug)

// --- Alarm O1 vs O2 ---
static uint32_t s_mismatchSince = 0;   // 0 = ค่ายังตรงกันอยู่
static bool     s_mismatch      = false;

// ---------------------------------------------------------------------------
void process_begin()
{
    s_runState = LNK_RUN_IDLE;
    s_rvZone   = LNK_RVZ_OFF;
    s_valve    = 0.0f;
    s_mismatch = false;
    s_mismatchSince = 0;
}

void process_start()
{
    if (s_runState == LNK_RUN_RUNNING) return;      // กดซ้ำ = ไม่รีเซ็ตเวลา

    s_runState   = LNK_RUN_RUNNING;
    s_runStart   = millis();

    s_valve      = 0.0f;
    s_rvZone     = LNK_RVZ_OFF;

    act_estop_clear();
}

void process_stop()
{
    s_runState = LNK_RUN_IDLE;
    s_rvZone   = LNK_RVZ_OFF;
    s_valve    = 0.0f;
    act_all_off();
}

bool     process_is_running()  { return s_runState == LNK_RUN_RUNNING; }
uint8_t  process_run_state()   { return s_runState; }
uint8_t  process_rv_zone()     { return s_rvZone; }
bool     process_o2_mismatch() { return s_mismatch; }

void process_reset_alarm()
{
    s_mismatch      = false;
    s_mismatchSince = 0;
}

uint32_t process_elapsed_sec()
{
    if (s_runState != LNK_RUN_RUNNING) return 0;
    return (millis() - s_runStart) / 1000UL;
}

// ---------------------------------------------------------------------------
//  รอบเวลาของอุปกรณ์ 1 ตัว  (ทุกอย่างเป็น ms)
//
//  คำนวณจาก "เวลาที่ผ่านไปตั้งแต่กด START" ตรง ๆ ด้วย modulo
//  ไม่เก็บ state ของรอบไว้เลย -> ไม่มีทางหลุดเฟส และไม่พังตอน millis() วน
// ---------------------------------------------------------------------------
static bool cycle_state(const CycleCfg &c, uint32_t elapsedMs)
{
    if (c.onMs == 0) return false;                     // ไม่ได้ตั้ง -> ไม่ทำงาน
    if (elapsedMs < c.delayMs) return false;           // ยังไม่ถึงคิวเริ่ม

    if (c.offMs == 0) return true;                     // ตั้ง off = 0 -> ติดค้างตลอด

    uint32_t period = c.onMs + c.offMs;
    uint32_t pos    = (elapsedMs - c.delayMs) % period;
    return pos < c.onMs;
}

// เหลืออีกกี่ ms อุปกรณ์ตัวนี้ถึงจะสลับสถานะ (ไว้ทำ countdown บนหน้าจอ)
// คืน 0 ถ้าไม่มีรอบ (ไม่ได้ตั้ง หรือ ติดค้างตลอด)
uint32_t process_next_change_ms(uint8_t dev)
{
    if (dev >= CFG_DEV_COUNT)          return 0;
    if (s_runState != LNK_RUN_RUNNING) return 0;

    const CycleCfg &c = g_cfg.cyc[dev];
    if (c.onMs == 0 || c.offMs == 0)   return 0;

    uint32_t elapsed = millis() - s_runStart;
    if (elapsed < c.delayMs) return c.delayMs - elapsed;      // ยังรอคิวเริ่ม

    uint32_t period = c.onMs + c.offMs;
    uint32_t pos    = (elapsed - c.delayMs) % period;
    return (pos < c.onMs) ? (c.onMs - pos) : (period - pos);
}

// ---------------------------------------------------------------------------
//  ควบคุมวาล์ว RV ตามค่า O2 (สเปกข้อ 6)
//
//  ทิศทาง: O2 สูง = ในถังยังมีออกซิเจนเยอะ = ต้องเติม N2 มากขึ้น = เปิดวาล์วมากขึ้น
//
//  โซน Rough/Medium เป็น open-loop -- "ก้าว" ทีละ step ทุก ๆ stepSec วินาที
//  ที่ต้องเว้นจังหวะเพราะระบบมี dead time (แก๊สต้องผสม + เซนเซอร์ตอบช้า)
//  ถ้าก้าวทุกรอบ loop วาล์วจะพุ่งไป 100% ในไม่กี่วินาทีแล้วแกว่งหนัก
// ---------------------------------------------------------------------------
static void rv_control(float o2, bool o2_ok, uint32_t now)
{
    (void)now;

    // อ่านค่าไม่ได้ -> ค้างวาล์วไว้ที่เดิม ไม่เดา
    // (ถ้าปิดวาล์วทันที O2 จะพุ่งขึ้น / ถ้าเปิดสุดก็เปลือง N2 -- ค้างไว้ปลอดภัยสุด)
    if (!o2_ok) return;

    const Settings &g = g_cfg;

    // === ควบคุมแบบ "ช่วง (band)" -- O2 อยู่ช่วงไหน เปิดวาล์ว % คงที่ของช่วงนั้น ===
    //   ไม่ก้าวทีละ step, ไม่ PID -- วาล์วไป "ตำแหน่งคงที่" ของแต่ละช่วงแล้วหยุด
    //   (วาล์วขยับเฉพาะตอน O2 ข้ามขอบช่วง = ไม่สั่งถี่ ตามที่ผู้ใช้ต้องการ)
    //
    //   % เปิดต่อช่วง (ตั้งได้จากจอ):
    //     MAXOUT (O2 > rv1Max)        -> rvMaxOut      (เปิดสุด ไล่ออกเร็ว)
    //     ROUGH  (rv1Min..rv1Max)     -> coarseStep    (ช่อง "ก้าวหยาบ" = % ช่วง Rough)
    //     MEDIUM (rv2Min..rv1Min)     -> midStep       (ช่อง "ก้าวละเอียด" = % ช่วง Medium)
    //     FINE   (O2 < rv2Min)        -> rvMinOut      (เปิดน้อย คงระดับ)
    // --- เลือกโซนแบบมี hysteresis (กันวาล์วกระพือตอน O2 แกว่งอยู่บนขอบ) ---
    //   ต้องข้ามขอบ "เกิน RV_HYST" ถึงจะเปลี่ยนโซน -- ไม่งั้น O2 noise ±เล็กน้อยบนขอบ
    //   จะสลับโซนกลับไปกลับมา (เช่น ROUGH 75% <-> MEDIUM 40% = ขยับ 35% ทุกที) มอเตอร์สึก
    const float RV_HYST = 0.3f;    // %O2
    //   ระดับโซนเรียงตาม O2 ต่ำ->สูง : 0=FINE 1=MEDIUM 2=ROUGH 3=MAXOUT
    //   ขอบบนของแต่ละระดับ: edge[0]=rv2Min, edge[1]=rv1Min, edge[2]=rv1Max
    const float edge[3] = { g.rv2Min, g.rv1Min, g.rv1Max };

    uint8_t lvl = (s_rvZone == LNK_RVZ_MAXOUT) ? 3 :
                  (s_rvZone == LNK_RVZ_ROUGH)  ? 2 :
                  (s_rvZone == LNK_RVZ_MEDIUM) ? 1 : 0;     // OFF/FINE -> 0

    // ขึ้นโซน: O2 ต้องสูงเกินขอบถัดไป + hyst
    while (lvl < 3 && o2 > edge[lvl]     + RV_HYST) lvl++;
    // ลงโซน: O2 ต้องต่ำกว่าขอบปัจจุบัน - hyst
    while (lvl > 0 && o2 < edge[lvl - 1] - RV_HYST) lvl--;

    uint8_t zone, pct;
    switch (lvl) {
    case 3:  zone = LNK_RVZ_MAXOUT; pct = g.rvMaxOut;           break;
    case 2:  zone = LNK_RVZ_ROUGH;  pct = (uint8_t)g.coarseStep; break;
    case 1:  zone = LNK_RVZ_MEDIUM; pct = (uint8_t)g.midStep;    break;
    default: zone = LNK_RVZ_FINE;   pct = g.rvMinOut;           break;
    }

    s_rvZone = zone;

    if (pct > 100) pct = 100;
    s_valve = (float)pct;
    act_set_valve(pct);
}

// ---------------------------------------------------------------------------
//  เทียบ O1 กับ O2 -- ถ้าห่างกันเกินกำหนดนานเกินไป ให้ขึ้น Alarm
//
//  ไม่หยุดเครื่องเอง -- แค่เตือนให้ช่างมาสอบเทียบ (ตามที่ผู้ใช้ระบุ)
//  ต้องห่างกัน "ต่อเนื่อง" ถึงจะเตือน ไม่งั้นสัญญาณรบกวนแวบเดียวก็เตือนแล้ว
// ---------------------------------------------------------------------------
static void cross_check(float o1, float o2, bool o1_ok, bool o2_ok, uint32_t now)
{
    if (!o1_ok || !o2_ok) {         // ตัวใดตัวหนึ่งอ่านไม่ได้ -> เทียบไม่ได้
        s_mismatchSince = 0;
        s_mismatch      = false;
        return;
    }

    float diff = fabs(o1 - o2);

    if (diff <= g_cfg.o2DiffMax) {
        s_mismatchSince = 0;
        s_mismatch      = false;
        return;
    }

    if (s_mismatchSince == 0) s_mismatchSince = now;    // เริ่มจับเวลา

    if ((now - s_mismatchSince) >= (uint32_t)g_cfg.o2DiffSec * 1000UL)
        s_mismatch = true;
}

// ---------------------------------------------------------------------------
void process_task(float o1, float o2, bool o1_ok, bool o2_ok)
{
    uint32_t now = millis();

    // เทียบเซนเซอร์ตลอดเวลา แม้เครื่องจะหยุดอยู่ -- ช่างจะได้เห็นปัญหาก่อนเดินเครื่อง
    cross_check(o1, o2, o1_ok, o2_ok, now);

    if (s_runState != LNK_RUN_RUNNING) return;
    if (act_is_estop()) { process_stop(); return; }

    uint32_t elapsedMs = now - s_runStart;

    // 1) รอบเวลาของอุปกรณ์ทั้ง 4 ตัว (หน่วย ms)
    for (uint8_t d = 0; d < CFG_DEV_COUNT; d++)
        act_set_relay(d, cycle_state(g_cfg.cyc[d], elapsedMs));

    // 2) วาล์ว RV -- O1 เป็นหลัก แต่ถ้า O1 อ่านไม่ได้ให้ถอยไปใช้ O2 แทน
    //    (ดีกว่าปล่อยวาล์วค้าง -- เครื่องยังคุม O2 ในถังต่อได้ด้วยเซนเซอร์ที่เหลือ
    //     จอเห็นธง O2A_FAIL อยู่แล้ว คนคุมรู้ว่ากำลังวิ่งด้วยตัวสำรอง)
    if (o1_ok)      rv_control(o1, true, now);
    else if (o2_ok) rv_control(o2, true, now);
    else            rv_control(0.0f, false, now);   // ตายคู่ -> ค้างวาล์วไว้ (ปลอดภัยสุด)
}
