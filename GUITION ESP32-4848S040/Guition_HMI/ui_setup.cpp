// ============================================================================
//  ui_setup.cpp  --  หน้าตั้งค่า: เมนู -> หน้าย่อยแยกกลุ่ม
//
//  โครง 2 ชั้น (ไม่ยัดทุกอย่างไว้หน้าเดียวให้ตาลาย):
//
//    ชั้น 1  MENU   [TIMERS] [VALVE]      กดการ์ดไหน เข้าหน้านั้น
//                   [PID]    [ALARM]      การ์ดที่สิทธิ์ไม่ถึง = หรี่ กดแล้วบอกว่าต้องใช้ PIN อะไร
//                   [SENSOR] [SYSTEM]
//
//    ชั้น 2  LIST   < BACK    TIMERS    [SAVE]
//                   CP - ON TIME ......... 900.000 s   <- แตะ = เด้ง keypad
//
//  หลักการ:
//    - ค่า "ของจริง" อยู่ใน EEPROM ของ Nano ไม่ใช่ที่จอ
//      เปิดหน้านี้ -> ส่ง $G ขอค่าปัจจุบันมาแสดง (ไม่เดาเอง)
//    - แก้ค่า -> ส่ง $S ทันที (Nano ตรวจช่วงให้อีกชั้น) แต่ยังไม่ลง EEPROM
//    - กด SAVE -> ส่ง $C SAV -> Nano เขียน EEPROM
//      (จงใจแยก 2 จังหวะ เพื่อให้ลองค่าดูก่อนได้ ถ้าไม่ชอบก็แค่ปิดไฟ)
//
//  ไฟล์นี้เป็นเจ้าของ "cache ค่า config" ที่ดึงมาจาก Nano
//  หน้าอื่น (เช่น CTRL) เรียกใช้ผ่าน ui_cfg_get / ui_cfg_set / ui_cfg_save
// ============================================================================
#include "ui.h"
#include "ui_theme.h"
#include "app_state.h"
#include "link_master.h"
#include "link_protocol.h"
#include "rtclock.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
//  ตารางพารามิเตอร์
//
//  mul = ตัวคูณแปลงหน่วย:  ค่าที่แสดง = ค่าที่เก็บ x mul
//    เวลาเก็บเป็น ms แต่แสดงเป็นวินาที -> mul = 0.001
//    ที่เหลือหน่วยเดียวกันอยู่แล้ว     -> mul = 1
//  min/max อยู่ใน "หน่วยที่แสดง" (คนอ่านเข้าใจง่ายกว่า)
// ---------------------------------------------------------------------------
struct Item {
    uint8_t     key;
    const char *name;
    const char *unit;
    float       min, max;
    uint8_t     dp;
    uint8_t     level;
    float       mul;
};

// เวลา: เก็บ ms / แสดงเป็นวินาที 3 ทศนิยม -> ตั้งได้ทั้ง 0.200 s และ 3600.000 s
#define T_SEC   "s", 0.0f, 9999.999f, 3, ACC_SUPERVISOR, 0.001f

#define TIMER_ROWS(D, LBL)                                                    \
    { CFG_TKEY(D, CFG_FLD_DELAY), LBL " - START DELAY", T_SEC },              \
    { CFG_TKEY(D, CFG_FLD_ON),    LBL " - ON TIME",     T_SEC },              \
    { CFG_TKEY(D, CFG_FLD_OFF),   LBL " - OFF TIME",    T_SEC }

static const Item ITEMS_TIMER[] = {
    TIMER_ROWS(CFG_DEV_CP, UI_NAME_CP),
    TIMER_ROWS(CFG_DEV_CB, UI_NAME_CB),
    TIMER_ROWS(CFG_DEV_FB, UI_NAME_FB),
    TIMER_ROWS(CFG_DEV_FP, UI_NAME_FP),
    { CFG_AUTO_START, "AUTO START ON POWER-UP", "", 0, 1, 0, ACC_SUPERVISOR, 1.0f },
};

// ควบคุมวาล์ว RV แบบ "band" -- O2 อยู่ช่วงไหน เปิดวาล์ว % คงที่ของช่วงนั้น
//   ขอบ O2 3 จุด แบ่งเป็น 4 ช่วง:  >MAXOUT / ROUGH / MEDIUM / FINE
//   แต่ละช่วงตั้ง "% เปิดวาล์ว" ได้เอง  (ไม่มี step/PID แล้ว)
static const Item ITEMS_RV[] = {
    // --- ขอบช่วง O2 (%) ---
    { CFG_RV1_MAX,        "O2 ABOVE -> MAXOUT",  "%", 0,  25, 1, ACC_COMMISSIONER, 1.0f }, // > นี้ = เปิดสุด
    { CFG_RV1_MIN,        "O2 ABOVE -> ROUGH",   "%", 0,  25, 1, ACC_COMMISSIONER, 1.0f }, // นี้..MAXOUT = ROUGH
    { CFG_RV2_MIN,        "O2 ABOVE -> MEDIUM",  "%", 0,  25, 1, ACC_COMMISSIONER, 1.0f }, // นี้..ROUGH = MEDIUM, ต่ำกว่า = FINE
    // --- % เปิดวาล์วของแต่ละช่วง ---
    { CFG_RV_MAX_OUT,     "MAXOUT - VALVE %",    "%", 0, 100, 0, ACC_COMMISSIONER, 1.0f },
    { CFG_RV_COARSE_STEP, "ROUGH  - VALVE %",    "%", 0, 100, 0, ACC_COMMISSIONER, 1.0f },
    { CFG_RV_MID_STEP,    "MEDIUM - VALVE %",    "%", 0, 100, 0, ACC_COMMISSIONER, 1.0f },
    { CFG_RV_MIN_OUT,     "FINE   - VALVE %",    "%", 0, 100, 0, ACC_COMMISSIONER, 1.0f },
};

static const Item ITEMS_ALARM[] = {
    { CFG_O2_DIFF_MAX, "O1 vs O2 - MAX DIFF",  "%", 0,   25, 2, ACC_COMMISSIONER, 1.0f },
    { CFG_O2_DIFF_SEC, "O1 vs O2 - HOLD TIME", "s", 1, 9999, 0, ACC_COMMISSIONER, 1.0f },
};

#define SENSOR_ROWS(S, LBL)                                                                  \
    { CFG_KEY(S, CFG_FLD_SLAVE_ID), LBL " - SLAVE ID",  "", 1,   247,  0, ACC_EXPERT, 1.0f }, \
    { CFG_KEY(S, CFG_FLD_FC),       LBL " - FUNCTION",  "", 3,     4,  0, ACC_EXPERT, 1.0f }, \
    { CFG_KEY(S, CFG_FLD_REG),      LBL " - REGISTER",  "", 0, 65535,  0, ACC_EXPERT, 1.0f }, \
    { CFG_KEY(S, CFG_FLD_NREGS),    LBL " - WORDS",     "", 1,     2,  0, ACC_EXPERT, 1.0f }, \
    { CFG_KEY(S, CFG_FLD_DTYPE),    LBL " - DATA TYPE", "", 0,     5,  0, ACC_EXPERT, 1.0f }, \
    { CFG_KEY(S, CFG_FLD_SCALE),    LBL " - SCALE",     "", -999, 999, 4, ACC_EXPERT, 1.0f }, \
    { CFG_KEY(S, CFG_FLD_OFFSET),   LBL " - OFFSET",    "", -999, 999, 4, ACC_EXPERT, 1.0f }

static const Item ITEMS_SENSOR[] = {
    SENSOR_ROWS(CFG_SENSOR_O2A,   "O2 #1"),
    SENSOR_ROWS(CFG_SENSOR_O2B,   "O2 #2"),
    SENSOR_ROWS(CFG_SENSOR_PRESS, "PRESS"),
};

static const Item ITEMS_SYSTEM[] = {
    { CFG_MB_BAUD_IDX,    "RS485 BAUD",         "",   1200, 115200, 0, ACC_EXPERT, 1.0f },
    { CFG_MB_POLL_MS,     "SENSOR POLL",        "ms",   50,  10000, 0, ACC_EXPERT, 1.0f },
    { CFG_MB_TIMEOUT_MS,  "MODBUS TIMEOUT",     "ms",   20,   2000, 0, ACC_EXPERT, 1.0f },
    { CFG_PIN_OPERATOR,   "PIN - OPERATOR",     "",      0,   9999, 0, ACC_EXPERT, 1.0f },
    { CFG_PIN_SUPERVISOR, "PIN - SUPERVISOR",   "",      0,   9999, 0, ACC_EXPERT, 1.0f },
    { CFG_PIN_COMMISSION, "PIN - COMMISSIONER", "",      0,   9999, 0, ACC_EXPERT, 1.0f },
    { CFG_PIN_EXPERT,     "PIN - EXPERT",       "",      0,   9999, 0, ACC_EXPERT, 1.0f },
};

#define N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))

struct Group {
    const char *icon;
    const char *title;
    const char *desc;
    const Item *items;
    uint8_t     count;
    uint8_t     level;
    lv_color_t (*color)();
};

static lv_color_t col_neon()  { return C_NEON;  }
static lv_color_t col_ok()    { return C_OK;    }
static lv_color_t col_warn()  { return C_WARN;  }
static lv_color_t col_press() { return C_PRESS; }
static lv_color_t col_o2b()   { return C_O2B;   }
static lv_color_t col_dim()   { return C_TEXT_DIM; }

// *** ข้อความบนจอต้องเป็นอังกฤษเท่านั้น -- ฟอนต์ Montserrat ไม่มี glyph ไทย ***
static const Group GROUPS[] = {
    { LV_SYMBOL_REFRESH,  "TIMERS",  "CP CB FB FP\nrun cycle + autostart", ITEMS_TIMER,  N(ITEMS_TIMER),  ACC_SUPERVISOR,   col_neon  },
    { LV_SYMBOL_CHARGE,   "VALVE",   "RV O2 bands\nvalve open %",          ITEMS_RV,     N(ITEMS_RV),     ACC_COMMISSIONER, col_ok    },
    { LV_SYMBOL_BELL,     "ALARM",   "O1 vs O2 check\nwarning limits",     ITEMS_ALARM,  N(ITEMS_ALARM),  ACC_COMMISSIONER, col_press },
    { LV_SYMBOL_GPS,      "SENSORS", "slave ID / register\nof 3 sensors",  ITEMS_SENSOR, N(ITEMS_SENSOR), ACC_EXPERT,       col_o2b   },
    { LV_SYMBOL_POWER,    "SYSTEM",  "RS485 bus\nand PIN codes",           ITEMS_SYSTEM, N(ITEMS_SYSTEM), ACC_EXPERT,       col_dim   },
};
#define GROUP_COUNT N(GROUPS)

// ---------------------------------------------------------------------------
//  cache ค่าจาก Nano (เฟรม $K) -- ไฟล์นี้เป็นเจ้าของ หน้าอื่นใช้ผ่าน ui_cfg_*
// ---------------------------------------------------------------------------
static float s_val[CFG_KEY_COUNT];
static bool  s_have[CFG_KEY_COUNT];
static bool  s_dirty    = false;
static bool  s_needDraw = false;

static void on_kv(uint8_t key, float value)
{
    if (key >= CFG_KEY_COUNT) return;
    s_val[key]  = value;
    s_have[key] = true;
    s_needDraw  = true;
}

float ui_cfg_get(uint8_t key)  { return (key < CFG_KEY_COUNT) ? s_val[key]  : 0.0f; }
bool  ui_cfg_have(uint8_t key) { return (key < CFG_KEY_COUNT) ? s_have[key] : false; }

void ui_cfg_set(uint8_t key, float v)
{
    if (key >= CFG_KEY_COUNT) return;
    s_val[key]  = v;
    s_have[key] = true;
    s_dirty     = true;
    s_needDraw  = true;
    link_cmd_set_config(key, v);      // ส่งให้ Nano ทันที (ยังไม่ลง EEPROM)
}

void ui_cfg_save()
{
    if (!s_dirty) { ui_toast("NOTHING TO SAVE"); return; }
    link_cmd_save();
    s_dirty = false;
    ui_event(EV_SAVED, 0);
    ui_toast("SAVED TO EEPROM");
}

// ---------------------------------------------------------------------------
static lv_obj_t *s_menu;
static lv_obj_t *s_listView;
static lv_obj_t *s_listTitle;
static lv_obj_t *s_list;
static lv_obj_t *s_lockBtn;
static lv_obj_t *s_lockLbl;
static lv_obj_t *s_saveBtn;
static lv_obj_t *s_menuCard[GROUP_COUNT];

static uint8_t   s_group   = 0;
static bool      s_inList  = false;
static uint32_t  s_touchMs = 0;

#define MAX_ROWS 24
static lv_obj_t *s_rowVal[MAX_ROWS];
static uint8_t   s_rowCount = 0;

// ---------------------------------------------------------------------------
static const char *level_name(uint8_t lv)
{
    switch (lv) {
    case ACC_EXPERT:       return "EXPERT";
    case ACC_COMMISSIONER: return "COMMISSIONER";
    case ACC_SUPERVISOR:   return "SUPERVISOR";
    case ACC_OPERATOR:     return "OPERATOR";
    default:               return "LOCKED";
    }
}

static void fmt_value(char *buf, size_t n, const Item *it)
{
    if (!s_have[it->key]) { snprintf(buf, n, "--"); return; }
    float shown = s_val[it->key] * it->mul;
    snprintf(buf, n, "%.*f%s%s", (int)it->dp, (double)shown,
             it->unit[0] ? " " : "", it->unit);
}

// ---------------------------------------------------------------------------
//  แก้ค่า 1 ช่อง -- ใช้ keypad กลางจาก ui.cpp
// ---------------------------------------------------------------------------
static void on_value_entered(float shown, void *user)
{
    const Item *it = (const Item *)user;
    ui_cfg_set(it->key, shown / it->mul);        // แปลงกลับเป็นหน่วยที่เก็บ (เช่น ms)
}

static void row_cb(lv_event_t *e)
{
    const Item *it = (const Item *)lv_event_get_user_data(e);
    s_touchMs = millis();

    if (g_app.accessLevel < it->level) {
        char msg[48];
        snprintf(msg, sizeof(msg), "NEED %s PIN", level_name(it->level));
        ui_toast(msg);
        return;
    }

    char sub[72];
    snprintf(sub, sizeof(sub), "range  %.*f  to  %.*f  %s",
             (int)it->dp, (double)it->min, (int)it->dp, (double)it->max, it->unit);

    ui_keypad_open(it->name, sub, s_val[it->key] * it->mul,
                   it->min, it->max, it->dp, false, on_value_entered, (void *)it);
}

// ---------------------------------------------------------------------------
//  PIN
// ---------------------------------------------------------------------------
static void on_pin_entered(float v, void *user)
{
    (void)user;
    long pin = (long)v;

    // ค่า PIN ที่ใช้เทียบ:
    //   - ได้ค่าจาก Nano แล้ว (s_have) -> ใช้ค่าจาก Nano (ตั้งจากจอได้)
    //   - ยังไม่ได้ (ไม่ต่อ Nano / config ยังไม่ sync) -> ใช้ค่า default ฝั่งจอ
    //   *** ต้องยอมให้ default เสมอตอนยังไม่ sync -- ไม่งั้นถ้า config ไม่มา
    //       (เช่น Nano เก่า/บัสเงียบ) operator จะ login ไม่ได้เลย = ล็อกตาย ***
    long vExpert = s_have[CFG_PIN_EXPERT]     ? (long)s_val[CFG_PIN_EXPERT]     : 4444;
    long vComm   = s_have[CFG_PIN_COMMISSION] ? (long)s_val[CFG_PIN_COMMISSION] : 3333;
    long vSup    = s_have[CFG_PIN_SUPERVISOR] ? (long)s_val[CFG_PIN_SUPERVISOR] : 2222;
    long vOper   = s_have[CFG_PIN_OPERATOR]   ? (long)s_val[CFG_PIN_OPERATOR]   : 1111;

    // ไล่จากสิทธิ์สูงสุดลงมา -- ถ้า PIN ซ้ำกัน จะได้สิทธิ์ที่สูงกว่า
    uint8_t lvl = 0;
    if      (pin == vExpert) lvl = ACC_EXPERT;
    else if (pin == vComm)   lvl = ACC_COMMISSIONER;
    else if (pin == vSup)    lvl = ACC_SUPERVISOR;
    else if (pin == vOper)   lvl = ACC_OPERATOR;

    if (lvl == 0) {
        ui_toast("WRONG PIN");
        return;
    }

    g_app.accessLevel = lvl;
    s_touchMs = millis();

    char msg[40];
    snprintf(msg, sizeof(msg), "UNLOCKED: %s", level_name(lvl));
    ui_toast(msg);
}

static void lock_cb(lv_event_t *e)
{
    (void)e;
    s_touchMs = millis();

    if (g_app.accessLevel > ACC_VIEW) {        // กดซ้ำ = ล็อกกลับ
        g_app.accessLevel = ACC_VIEW;
        ui_toast("LOCKED");
        return;
    }
    ui_keypad_open("ENTER PIN", nullptr, 0, 0, 9999, 0, true, on_pin_entered, nullptr);
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    s_touchMs = millis();

    if (g_app.accessLevel < ACC_SUPERVISOR) { ui_toast("NEED SUPERVISOR PIN"); return; }
    ui_cfg_save();
}

// ---------------------------------------------------------------------------
//  ชั้น 2 -- รายการพารามิเตอร์
// ---------------------------------------------------------------------------
static void build_list()
{
    lv_obj_clean(s_list);
    s_rowCount = 0;

    const Group &g = GROUPS[s_group];
    lv_label_set_text(s_listTitle, g.title);

    for (uint8_t i = 0; i < g.count && s_rowCount < MAX_ROWS; i++) {
        const Item *it = &g.items[i];
        bool allowed = (g_app.accessLevel >= it->level);

        lv_obj_t *row = lv_btn_create(s_list);
        lv_obj_set_size(row, UI_W - 2 * UI_PAD - 14, 40);
        lv_obj_set_style_radius(row, 8, 0);
        ui_grad(row, C_PANEL_TOP, C_PANEL_BOT);
        ui_grad(row, C_PANEL_HI_BOT, C_PANEL_HI_TOP, LV_STATE_PRESSED);   // กดแล้วจม
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_CLICKED, (void *)it);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, it->name);
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(nm, allowed ? C_TEXT : C_TEXT_MUTE, 0);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 12, 0);

        char buf[32];
        fmt_value(buf, sizeof(buf), it);

        lv_obj_t *vl = lv_label_create(row);
        lv_label_set_text(vl, buf);
        lv_obj_set_style_text_font(vl, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(vl, allowed ? C_NEON : C_TEXT_MUTE, 0);
        lv_obj_align(vl, LV_ALIGN_RIGHT_MID, -12, 0);

        s_rowVal[s_rowCount++] = vl;
    }
    s_needDraw = false;
}

// วาดตัวเลขใหม่ -- เรียกเฉพาะตอนค่าเปลี่ยนจริง ห้ามเรียกทุก tick
static void refresh_rows()
{
    const Group &g = GROUPS[s_group];
    for (uint8_t i = 0; i < s_rowCount && i < g.count; i++) {
        char buf[32];
        fmt_value(buf, sizeof(buf), &g.items[i]);
        lv_label_set_text(s_rowVal[i], buf);
    }
    s_needDraw = false;
}

// ---------------------------------------------------------------------------
static void show_menu()
{
    s_inList = false;
    lv_obj_clear_flag(s_menu,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_listView, LV_OBJ_FLAG_HIDDEN);
}

static void back_cb(lv_event_t *e) { (void)e; show_menu(); }

static void group_cb(lv_event_t *e)
{
    uint8_t g = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    s_touchMs = millis();

    if (g_app.accessLevel < GROUPS[g].level) {
        char msg[48];
        snprintf(msg, sizeof(msg), "NEED %s PIN", level_name(GROUPS[g].level));
        ui_toast(msg);
        return;
    }

    s_group  = g;
    s_inList = true;
    build_list();

    lv_obj_add_flag(s_menu,       LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_listView, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// ===========================================================================
//  overlay ตั้งวัน/เวลา  (นาฬิกาไม่มี RTC -> ผู้ใช้ตั้งเอง เก็บลง NVS)
//
//  ใช้ปุ่มขึ้น/ลงต่อช่อง ไม่ใช้ keypad -- ตั้งเวลาแบบเลื่อนทีละหน่วยเป็นธรรมชาติกว่า
//  แต่ละช่อง wrap รอบ (นาที 59 -> 0)  ยกเว้นปีที่ clamp 2020..2099
// ===========================================================================
static lv_obj_t *s_clkOv = nullptr;
static lv_obj_t *s_clkVal[5];
static int       s_clk[5];      // 0=ปี 1=เดือน 2=วัน 3=ชม 4=นาที
static const char *CLK_HDR[5] = { "YEAR", "MONTH", "DAY", "HOUR", "MIN" };
static const int   CLK_LO[5]  = { 2020, 1, 1, 0, 0 };
static const int   CLK_HI[5]  = { 2099, 12, 31, 23, 59 };

static void clk_refresh()
{
    lv_label_set_text_fmt(s_clkVal[0], "%04d", s_clk[0]);
    for (uint8_t i = 1; i < 5; i++)
        lv_label_set_text_fmt(s_clkVal[i], "%02d", s_clk[i]);
}

static void clk_close()
{
    if (s_clkOv) { lv_obj_del(s_clkOv); s_clkOv = nullptr; }
}

// user_data = field*2 + (ขึ้น?1:0)
static void clk_step_cb(lv_event_t *e)
{
    intptr_t code = (intptr_t)lv_event_get_user_data(e);
    int f  = (int)(code >> 1);
    int dv = (code & 1) ? +1 : -1;
    int v  = s_clk[f] + dv;

    if (f == 0) {                              // ปี = clamp
        if (v < CLK_LO[0]) v = CLK_LO[0];
        if (v > CLK_HI[0]) v = CLK_HI[0];
    } else {                                   // ที่เหลือ = wrap รอบ
        int lo = CLK_LO[f], hi = CLK_HI[f];
        if (v < lo) v = hi;
        if (v > hi) v = lo;
    }
    s_clk[f] = v;
    clk_refresh();
}

static void clk_set_cb(lv_event_t *e)
{
    (void)e;
    clock_set(clock_make(s_clk[0], s_clk[1], s_clk[2], s_clk[3], s_clk[4], 0));
    clk_close();
    ui_toast("CLOCK SET");
}

static void clk_cancel_cb(lv_event_t *e) { (void)e; clk_close(); }

static void clock_overlay_open()
{
    int ss;
    clock_break(clock_now(), &s_clk[0], &s_clk[1], &s_clk[2], &s_clk[3], &s_clk[4], &ss);

    s_clkOv = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_clkOv, UI_W, UI_H);
    lv_obj_set_pos(s_clkOv, 0, 0);
    ui_grad(s_clkOv, C_BG_TOP, C_BG_BOT);
    lv_obj_set_style_border_width(s_clkOv, 0, 0);
    lv_obj_set_style_radius(s_clkOv, 0, 0);
    lv_obj_set_style_pad_all(s_clkOv, 0, 0);
    lv_obj_clear_flag(s_clkOv, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_clkOv);
    lv_label_set_text(t, "SET DATE / TIME");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, C_NEON, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 26);

    // 5 ช่อง เรียงแนวนอน -- แต่ละช่องกว้าง 96 (480/5)
    for (uint8_t i = 0; i < 5; i++) {
        int cx = i * 96 + 48;                  // กลางช่อง

        lv_obj_t *hd = lv_label_create(s_clkOv);
        lv_label_set_text(hd, CLK_HDR[i]);
        lv_obj_set_style_text_font(hd, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(hd, C_TEXT_DIM, 0);
        lv_obj_align(hd, LV_ALIGN_TOP_MID, cx - UI_W / 2, 78);

        lv_obj_t *up = ui_button(s_clkOv, i * 96 + 8, 108, 80, 56,
                                 LV_SYMBOL_UP, C_PANEL_HI, clk_step_cb,
                                 (void *)(intptr_t)(i * 2 + 1));
        (void)up;

        s_clkVal[i] = lv_label_create(s_clkOv);
        lv_obj_set_style_text_font(s_clkVal[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(s_clkVal[i], C_TEXT, 0);
        lv_obj_align(s_clkVal[i], LV_ALIGN_TOP_MID, cx - UI_W / 2, 180);

        ui_button(s_clkOv, i * 96 + 8, 214, 80, 56,
                  LV_SYMBOL_DOWN, C_PANEL_HI, clk_step_cb,
                  (void *)(intptr_t)(i * 2 + 0));
    }
    clk_refresh();

    ui_button(s_clkOv, 40,  300, 180, 56, LV_SYMBOL_CLOSE " CANCEL", C_PANEL_HI, clk_cancel_cb, nullptr);
    ui_button(s_clkOv, 260, 300, 180, 56, LV_SYMBOL_OK " SET",       C_OK,       clk_set_cb,    nullptr);
}

void ui_setup_build(lv_obj_t *par)
{
    for (uint16_t i = 0; i < CFG_KEY_COUNT; i++) { s_val[i] = 0; s_have[i] = false; }

    link_set_kv_cb(on_kv);
    link_cmd_get_config();          // ขอค่าปัจจุบันจาก Nano

    // ================= ชั้น 1: เมนู =================
    s_menu = lv_obj_create(par);
    lv_obj_set_size(s_menu, UI_W, UI_BODY_H);
    lv_obj_set_pos(s_menu, 0, 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_menu, 0, 0);
    lv_obj_set_style_pad_all(s_menu, 0, 0);
    lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_SCROLLABLE);

    s_lockBtn = ui_button(s_menu, 8, 6, 232, 42, LV_SYMBOL_CLOSE "  LOCKED", C_PANEL_HI, lock_cb, nullptr);
    s_lockLbl = lv_obj_get_child(s_lockBtn, 0);

    // ปุ่มตั้งนาฬิกา -- ใครก็ตั้งได้ ไม่ต้องปลดล็อก (เวลาต้องตรงเพื่อให้ log มีค่า)
    ui_button(s_menu, 248, 6, 60, 42, "TIME", C_PANEL_HI,
              [](lv_event_t *e){ (void)e; clock_overlay_open(); }, nullptr);
    lv_obj_set_style_border_color(s_lockBtn, C_NEON, 0);
    lv_obj_set_style_border_width(s_lockBtn, 1, 0);

    s_saveBtn = ui_button(s_menu, 316, 6, 156, 42, LV_SYMBOL_SAVE "  SAVE", C_OK, save_cb, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(s_saveBtn, 0), &lv_font_montserrat_14, 0);

    // การ์ดกลุ่ม 2 คอลัมน์ x 3 แถว  (y 56..354 -- ต้องจบก่อน UI_BODY_H = 366)
    for (uint8_t i = 0; i < GROUP_COUNT; i++) {
        int col = i % 2, row = i / 2;
        int x = 8 + col * 236;
        int y = 56 + row * 100;

        lv_obj_t *c = lv_btn_create(s_menu);
        lv_obj_set_size(c, 228, 94);
        lv_obj_set_pos(c, x, y);
        lv_obj_set_style_radius(c, UI_RADIUS, 0);
        ui_grad(c, C_PANEL_TOP, C_PANEL_BOT);
        ui_grad(c, C_PANEL_HI_BOT, C_PANEL_HI_TOP, LV_STATE_PRESSED);   // กดแล้วจม
        lv_obj_set_style_border_color(c, C_LINE, 0);
        lv_obj_set_style_border_width(c, 1, 0);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_add_event_cb(c, group_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *ic = lv_label_create(c);
        lv_label_set_text(ic, GROUPS[i].icon);
        lv_obj_set_style_text_font(ic, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(ic, GROUPS[i].color(), 0);
        lv_obj_set_pos(ic, 12, 10);

        lv_obj_t *ti = lv_label_create(c);
        lv_label_set_text(ti, GROUPS[i].title);
        lv_obj_set_style_text_font(ti, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(ti, C_TEXT, 0);
        lv_obj_set_pos(ti, 44, 10);

        lv_obj_t *de = lv_label_create(c);
        lv_label_set_text(de, GROUPS[i].desc);
        lv_obj_set_style_text_font(de, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(de, C_TEXT_DIM, 0);
        lv_obj_set_pos(de, 12, 42);

        s_menuCard[i] = c;
    }

    // ================= ชั้น 2: รายการ =================
    s_listView = lv_obj_create(par);
    lv_obj_set_size(s_listView, UI_W, UI_BODY_H);
    lv_obj_set_pos(s_listView, 0, 0);
    lv_obj_set_style_bg_opa(s_listView, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_listView, 0, 0);
    lv_obj_set_style_pad_all(s_listView, 0, 0);
    lv_obj_clear_flag(s_listView, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_listView, LV_OBJ_FLAG_HIDDEN);

    ui_button(s_listView, 8, 6, 110, 40, LV_SYMBOL_LEFT "  BACK", C_PANEL_HI, back_cb, nullptr);

    s_listTitle = lv_label_create(s_listView);
    lv_label_set_text(s_listTitle, "");
    lv_obj_set_style_text_font(s_listTitle, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_listTitle, C_NEON, 0);
    lv_obj_set_pos(s_listTitle, 130, 14);

    ui_button(s_listView, 316, 6, 156, 40, LV_SYMBOL_SAVE "  SAVE", C_OK, save_cb, nullptr);

    s_list = lv_obj_create(s_listView);
    lv_obj_set_size(s_list, UI_W - 2 * UI_PAD, UI_BODY_H - 56);
    lv_obj_set_pos(s_list, UI_PAD, 52);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 0, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, 5, 0);

    show_menu();
}

// ---------------------------------------------------------------------------
void ui_setup_update()
{
    static uint8_t s_lastLevel = 255;
    static bool    s_lastDirty = false;

    // *** ขอ config ($G) ซ้ำจนครบ -- มี cap + เว้นห่าง กัน flood ***
    //   เช็คจาก "จำนวน key ที่ได้ครบทั้งหมด" ไม่ใช่ PIN_EXPERT ตัวเดียว (กัน key ท้ายหลุด
    //   แล้ววนขอไม่จบ).  ปกติ RX buffer(2048)+Nano ทยอยส่ง = $G เดียวได้ครบ -> cap
    //   เป็นแค่ safety net.  เว้น 2.5 วิ + ไม่เกิน 8 ครั้ง -> ไม่ตันลิงก์เหมือนของเดิม
    static uint32_t s_lastCfgReq  = 0;
    static uint8_t  s_cfgReqTries = 0;
    if (!g_app.linkUp) {
        s_cfgReqTries = 0;                       // ลิงก์หลุด -> รีเซ็ต cap เผื่อ reconnect
    } else {
        uint16_t have = 0;
        for (uint16_t i = 0; i < CFG_KEY_COUNT; i++) if (s_have[i]) have++;
        if (have < CFG_KEY_COUNT && s_cfgReqTries < 8 &&
            (millis() - s_lastCfgReq) > 2500) {
            s_lastCfgReq = millis();
            s_cfgReqTries++;
            link_cmd_get_config();
        }
    }

    // (auto-logout ย้ายไปอยู่ ui_tick() แล้ว -- ทำงานทุกหน้า ไม่ใช่แค่ SETUP)

    if (g_app.accessLevel != s_lastLevel) {
        s_lastLevel = g_app.accessLevel;

        if (g_app.accessLevel > ACC_VIEW) {
            char b[40];
            snprintf(b, sizeof(b), LV_SYMBOL_OK "  %s", level_name(g_app.accessLevel));
            lv_label_set_text(s_lockLbl, b);
            ui_btn_color(s_lockBtn, C_OK);
        } else {
            lv_label_set_text(s_lockLbl, LV_SYMBOL_CLOSE "  LOCKED - TAP PIN");
            ui_btn_color(s_lockBtn, C_PANEL_HI);
        }

        for (uint8_t i = 0; i < GROUP_COUNT; i++) {
            bool ok = (g_app.accessLevel >= GROUPS[i].level);
            lv_obj_set_style_border_color(s_menuCard[i], ok ? GROUPS[i].color() : C_LINE, 0);
            lv_obj_set_style_opa(s_menuCard[i], ok ? LV_OPA_COVER : LV_OPA_50, 0);
        }

        if (s_inList) build_list();
    }

    if (s_dirty != s_lastDirty) {
        s_lastDirty = s_dirty;
        ui_btn_color(s_saveBtn, s_dirty ? C_WARN : C_OK);
    }

    if (s_inList && s_needDraw) refresh_rows();
}
