// ============================================================================
//  logbook.cpp
// ============================================================================
#include "logbook.h"
#include "link_slave.h"          // link_send_log()
#include "link_protocol.h"

// ---------------------------------------------------------------------------
//  ring buffer ของ record ที่ยังไม่ถูก ack
//
//  12 ตัวก็เหลือเฟือ -- เหตุการณ์เกิดไม่กี่ครั้งต่อนาที และ ESP32 ack ภายใน ~100 ms
//  ถ้า ESP32 หายไปนานจนล้น 12 ตัว -> ทับตัวเก่าสุด + นับ dropped
//  (ยอมเสีย record เก่าดีกว่าปล่อยให้ RAM บวม -- Nano มี RAM แค่ 2 KB)
// ---------------------------------------------------------------------------
#define LB_MAX          12
#define LB_RESEND_MS   250      // ยังไม่ ack -> ส่งซ้ำทุก 250 ms
#define LB_SETTLE_MS  4000      // ช่วงแรกหลังบูต ไม่ log alarm (เซนเซอร์ยังไม่ตอบ = ขยะ)

struct LbRec {
    uint8_t  seq;
    uint32_t ms;
    uint8_t  type;
    uint8_t  a, b;
};

static LbRec    s_rec[LB_MAX];
static uint8_t  s_head  = 0;      // ตัวเก่าสุดที่ยังไม่ถูก ack
static uint8_t  s_count = 0;
static uint8_t  s_nextSeq = 1;    // 0 สงวนไว้ (บาง ack ใช้ 0 = ยังไม่รับอะไร)
static uint32_t s_lastSendMs = 0;
static uint16_t s_dropped = 0;

// สถานะที่ diff -- ค่าที่ ESP32 ได้รับไปแล้ว
static uint8_t  c_relay, c_mode, c_run, c_zone, c_err;
static bool     c_estop;
static uint32_t s_bootMs;

// ---------------------------------------------------------------------------
static void enqueue(uint8_t type, uint8_t a, uint8_t b)
{
    LbRec &r = s_rec[(s_head + s_count) % LB_MAX];
    r.seq  = s_nextSeq++;
    if (s_nextSeq == 0) s_nextSeq = 1;      // ข้าม 0
    r.ms   = millis();
    r.type = type;
    r.a    = a;
    r.b    = b;

    if (s_count < LB_MAX) {
        s_count++;
    } else {
        // ล้น -> ตัวใหม่ทับตัวเก่าสุด (ขยับ head)
        s_head = (uint8_t)((s_head + 1) % LB_MAX);
        s_dropped++;
    }
    s_lastSendMs = 0;          // มีของใหม่ -> ส่งทันทีในรอบถัดไป
}

// ---------------------------------------------------------------------------
void logbook_begin(uint8_t mode0)
{
    s_head = s_count = 0;
    s_nextSeq = 1;
    s_bootMs  = millis();

    // prime สถานะเริ่มต้น = เครื่องหยุด output ดับหมด  (ตรงกับความจริงตอนบูต)
    // -> ถ้า auto-start ทำงาน run จะกลายเป็น RUNNING ใน loop แรก -> diff ได้ START
    c_relay = 0;
    c_mode  = mode0;
    c_run   = LNK_RUN_IDLE;
    c_zone  = LNK_RVZ_OFF;
    c_err   = 0;
    c_estop = false;

    enqueue(LNK_LOG_BOOT, 0, 0);
}

// ---------------------------------------------------------------------------
void logbook_event(uint8_t type, uint8_t a, uint8_t b)
{
    enqueue(type, a, b);
}

// ---------------------------------------------------------------------------
void logbook_note(uint8_t relay, uint8_t mode, uint8_t run,
                  uint8_t zone, uint8_t errflags, bool estop)
{
    // --- E-STOP ก่อน -- ถ้าเพิ่งกด จะข้าม STOP ที่ตามมา (ESTOP ครอบคลุมแล้ว) ---
    bool estopRising = (estop && !c_estop);
    if (estop != c_estop) {
        if (estopRising) enqueue(LNK_LOG_ESTOP, 0, 0);
        c_estop = estop;
    }

    // --- เดิน/หยุด ---
    if (run != c_run) {
        if (run == LNK_RUN_RUNNING)     enqueue(LNK_LOG_START, 0, 0);
        else if (!estopRising)          enqueue(LNK_LOG_STOP, 0, 0);   // ไม่ใช่เพราะ estop
        c_run = run;
    }

    // --- โหมด ---
    if (mode != c_mode) {
        enqueue(LNK_LOG_MODE, mode, 0);
        c_mode = mode;
    }

    // --- โซนวาล์ว ---
    if (zone != c_zone) {
        enqueue(LNK_LOG_ZONE, zone, 0);
        c_zone = zone;
    }

    // --- รีเลย์ (ทีละบิตที่เปลี่ยน) ---
    if (relay != c_relay) {
        uint8_t diff = relay ^ c_relay;
        for (uint8_t i = 0; i < 4; i++)
            if (diff & (1 << i))
                enqueue(LNK_LOG_RELAY, i, (relay >> i) & 1);
        c_relay = relay;
    }

    // --- alarm (errflags) -- ข้ามช่วง settle หลังบูต ---
    // ตอนบูตเซนเซอร์ยังไม่ถูกตั้งค่า -> ตอบไม่ได้ -> errflags เต็มไปด้วย fail bit
    // ถ้า log ช่วงนั้นจะได้ ALARM ขยะเพียบ  รอ 4 วิให้ระบบตั้งตัวก่อน
    if (millis() - s_bootMs >= LB_SETTLE_MS) {
        if (errflags != c_err) {
            uint8_t diff = errflags ^ c_err;
            for (uint8_t i = 0; i < 8; i++)
                if (diff & (1 << i))
                    enqueue(LNK_LOG_ALARM, (uint8_t)(1 << i), (errflags >> i) & 1);
        }
    }
    c_err = errflags;      // track เสมอ -- จบ settle จะได้ diff กับค่าล่าสุด
}

// ---------------------------------------------------------------------------
void logbook_task()
{
    if (s_count == 0) return;

    uint32_t now = millis();
    if (now - s_lastSendMs < LB_RESEND_MS) return;
    s_lastSendMs = now;

    // ส่งตัวเก่าสุดที่ยังไม่ถูก ack (ส่งซ้ำเรื่อย ๆ จนกว่าจะ ack)
    const LbRec &r = s_rec[s_head];
    link_send_log(r.seq, r.ms, r.type, r.a, r.b);
}

// ---------------------------------------------------------------------------
void logbook_ack(uint8_t seq)
{
    // ทิ้งทุก record ที่ seq "อยู่ก่อนหรือเท่ากับ" ack (เผื่อ ack ข้ามหลายตัว)
    //
    // *** ต้องเทียบแบบ wrap-safe ***  seq เป็น uint8 วนรอบ
    // ถ้าเขียน hseq == seq เฉย ๆ แล้วเจอ ack เก่าซ้ำ (seq ไม่อยู่ใน buffer แล้ว)
    // loop จะลบ record ที่ยังไม่ถูก ack ทิ้งเกลี้ยง -> เหตุการณ์หาย
    // (int8_t)(seq - hseq) >= 0  =  hseq อยู่ก่อนหรือเท่ากับ seq ในกรอบ 128 ตัว
    bool dropped = false;
    while (s_count > 0) {
        uint8_t hseq = s_rec[s_head].seq;
        if ((int8_t)(seq - hseq) < 0) break;      // head ใหม่กว่า ack -> หยุด
        s_head = (uint8_t)((s_head + 1) % LB_MAX);
        s_count--;
        dropped = true;
    }
    if (dropped) s_lastSendMs = 0;                // ให้ส่งตัวถัดไปทันที
}

uint8_t logbook_pending() { return s_count; }
