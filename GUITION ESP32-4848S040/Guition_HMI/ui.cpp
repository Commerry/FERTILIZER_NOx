// ============================================================================
//  ui.cpp  --  header + nav bar + สลับหน้า + toast + แถบ alarm
// ============================================================================
#include "ui.h"
#include "ui_theme.h"
#include "app_state.h"
#include "link_master.h"
#include "link_protocol.h"
#include "display_driver.h"
#include "logo_factoryboxx.h"

#include <stdio.h>

static lv_obj_t *s_pages[PAGE_COUNT];
static lv_obj_t *s_navBtn[PAGE_COUNT];
static lv_obj_t *s_title;
static lv_obj_t *s_linkDot;
static lv_obj_t *s_linkLbl;
static uint8_t   s_page = PAGE_HOME;

// หน้า HOME ไม่มีชื่อ -- ปล่อยให้โลโก้เด่นไปเลย
static const char *PAGE_TITLE[PAGE_COUNT] = {
    "", "SENSOR DATA", "MANUAL CONTROL", "SETUP", "DIAGNOSTICS"
};
static const char *NAV_ICON[PAGE_COUNT] = {
    LV_SYMBOL_HOME, LV_SYMBOL_CHARGE, LV_SYMBOL_EDIT, LV_SYMBOL_SETTINGS, LV_SYMBOL_LIST
};
static const char *NAV_TEXT[PAGE_COUNT] = {
    "HOME", "DATA", "CTRL", "SETUP", "DIAG"
};

// ---------------------------------------------------------------------------
//  ตัวช่วยที่ใช้ร่วมกัน
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  ไล่เฉดสี -- ใช้ LV_GRAD_DIR_VER (แนวตั้ง) อย่างเดียวทั้งระบบ
//
//  แนวตั้งราคาเกือบเท่าถมสีทึบ: LVGL คิดสีทีละ "แถว" แล้วถมทั้งแถวรวดเดียว
//  (แนวนอนต้องคิดใหม่ทีละพิกเซล -- แพงกว่าโดยไม่ได้อะไรเพิ่ม)
//  สำคัญกับบอร์ดนี้ เพราะเคยเจอมาแล้วว่าถ้าวาดช้าจน lv_timer_handler ตามไม่ทัน
//  ตัวอ่าน touch จะไม่ได้รัน = จอกลายเป็นกดไม่ได้
// ---------------------------------------------------------------------------
void ui_grad(lv_obj_t *o, lv_color_t top, lv_color_t bot, lv_style_selector_t sel)
{
    lv_obj_set_style_bg_color(o, top, sel);
    lv_obj_set_style_bg_grad_color(o, bot, sel);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, sel);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, sel);
}

void ui_tint(lv_obj_t *o, lv_color_t base, lv_style_selector_t sel)
{
    ui_grad(o, lv_color_lighten(base, UI_GRAD_UP),
               lv_color_darken(base, UI_GRAD_DN), sel);
}

// ---------------------------------------------------------------------------
//  เลือกสีตัวอักษรบนพื้นสีหนึ่ง ๆ ให้ "อ่านออก" เสมอ
//
//  *** สำคัญมากบนธีมสว่าง ***  ปุ่มมีทั้งสีอ่อน (พาเนลมิ้น) และสีเข้ม-สด (เขียว/แดง)
//  ถ้าฟิกซ์ตัวอักษรขาวอย่างเดียว -> ตัวอักษรบนปุ่มสีอ่อนจะหายไปเลย
//  วัดความสว่างของพื้น: สว่าง -> ตัวอักษรเข้ม / เข้ม -> ตัวอักษรขาว
// ---------------------------------------------------------------------------
lv_color_t ui_text_on(lv_color_t base)
{
    return (lv_color_brightness(base) > 155) ? C_TEXT : lv_color_hex(0xFFFFFF);
}

void ui_btn_color(lv_obj_t *b, lv_color_t base)
{
    ui_tint(b, base);
    // ตอนกด: กลับหัวไล่เฉด (เข้มบน-สว่างล่าง) + หรี่ทั้งดวง = เห็นเป็นปุ่ม "จม" ลงไป
    ui_grad(b, lv_color_darken(base, 85), lv_color_darken(base, 20), LV_STATE_PRESSED);

    // อัปเดตสีตัวอักษรด้วย -- ปุ่มที่เปลี่ยนสีตอนรัน (SAVE/MODE/LOCK) จะได้อ่านออกเสมอ
    lv_obj_t *l = lv_obj_get_child(b, 0);
    if (l) lv_obj_set_style_text_color(l, ui_text_on(base), 0);
}

lv_obj_t *ui_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, w, h);
    lv_obj_set_pos(c, x, y);
    ui_grad(c, C_PANEL_TOP, C_PANEL_BOT);
    lv_obj_set_style_border_color(c, C_LINE, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_radius(c, UI_RADIUS, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h,
                    const char *text, lv_color_t color, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, UI_RADIUS, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    ui_btn_color(b, color);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, ui_text_on(color), 0);   // อ่านออกทั้งปุ่มสีอ่อน/เข้ม
    lv_obj_center(l);
    return b;
}

// ---------------------------------------------------------------------------
//  Toast
// ---------------------------------------------------------------------------
static lv_obj_t *s_toast      = nullptr;
static uint32_t  s_toastUntil = 0;

void ui_toast(const char *msg)
{
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        ui_grad(s_toast, C_PANEL_HI_TOP, C_PANEL_HI_BOT);
        lv_obj_set_style_border_color(s_toast, C_NEON, 0);
        lv_obj_set_style_border_width(s_toast, 1, 0);
        lv_obj_set_style_text_color(s_toast, C_TEXT, 0);
        lv_obj_set_style_text_font(s_toast, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_all(s_toast, 12, 0);
        lv_obj_set_style_radius(s_toast, 8, 0);
    }
    lv_label_set_text(s_toast, msg);
    lv_obj_align(s_toast, LV_ALIGN_CENTER, 0, 130);
    lv_obj_clear_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_toast);
    s_toastUntil = millis() + 1900;
}

static void toast_tick()
{
    if (s_toast && s_toastUntil && millis() > s_toastUntil) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toastUntil = 0;
    }
}

// ---------------------------------------------------------------------------
//  สลับหน้า
// ---------------------------------------------------------------------------
static void nav_cb(lv_event_t *e)
{
    ui_goto((uint8_t)(intptr_t)lv_event_get_user_data(e));
}

static void paint_nav()
{
    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        bool sel = (i == s_page);

        // ปุ่มที่เลือกอยู่ = ไล่เฉด neon นูนขึ้นมา / ที่ไม่ได้เลือก = โปร่งไปเลย
        if (sel) ui_tint(s_navBtn[i], C_NEON);
        else     lv_obj_set_style_bg_opa(s_navBtn[i], LV_OPA_TRANSP, 0);

        lv_obj_t *l = lv_obj_get_child(s_navBtn[i], 0);
        if (l) lv_obj_set_style_text_color(l, sel ? ui_text_on(C_NEON) : C_TEXT_DIM, 0);
    }
}

void ui_goto(uint8_t page)
{
    if (page >= PAGE_COUNT) return;

    for (uint8_t i = 0; i < PAGE_COUNT; i++)
        lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);

    lv_obj_clear_flag(s_pages[page], LV_OBJ_FLAG_HIDDEN);
    s_page = page;

    paint_nav();

    // fade เข้าเบา ๆ ให้รู้สึกว่าเปลี่ยนหน้า (ไม่ใช้ slide -- ทำให้เวียนหัวบนจอเล็ก)
    lv_obj_set_style_opa(s_pages[page], LV_OPA_0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_pages[page]);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_set_duration(&a, 160);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
    });
    lv_anim_start(&a);
}

uint8_t ui_current_page() { return s_page; }

// ---------------------------------------------------------------------------
//  header
// ---------------------------------------------------------------------------
static void build_header(lv_obj_t *scr)
{
    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_set_size(h, UI_W, UI_HEADER_H);
    lv_obj_set_pos(h, 0, 0);
    ui_grad(h, C_HEADER_TOP, C_HEADER_BOT);
    lv_obj_set_style_border_width(h, 0, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(h, C_NEON, 0);
    lv_obj_set_style_border_width(h, 2, 0);
    lv_obj_set_style_radius(h, 0, 0);
    lv_obj_set_style_pad_all(h, 0, 0);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);

    // ---- โลโก้ FACTORYBOXX ตัวจริง (180x50) ----
    // แปลงจาก logo01.png (โลโก้ขาวบนพื้นดำ) -> ย้อมฟ้า + พื้นโปร่งใส
    // เก็บ texture grunge และรูปทรงตัวอักษรไว้ครบ ไม่ได้วาดเลียนแบบ
    lv_obj_t *logo = lv_image_create(h);
    lv_image_set_src(logo, &logo_factoryboxx);
    lv_obj_align(logo, LV_ALIGN_LEFT_MID, 10, 0);

    // ---- ชื่อหน้า / ข้อความเตือน ----
    // ใช้ label เดียวกัน: ปกติแสดงชื่อหน้า ถ้ามี alarm จะกลายเป็นข้อความเตือนสีแดง
    // (ของเดิมทำ alarm เป็นแถบลอยเหนือ nav bar -> มันไปนั่งทับเนื้อหาทุกหน้าพอดี)
    s_title = lv_label_create(h);
    lv_label_set_text(s_title, PAGE_TITLE[PAGE_HOME]);
    lv_obj_set_style_text_color(s_title, C_TEXT_DIM, 0);
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_14, 0);
    lv_obj_align(s_title, LV_ALIGN_LEFT_MID, 216, 0);

    // จุดสถานะลิงก์ (เขียว/แดง) + ข้อความ
    s_linkDot = lv_obj_create(h);
    lv_obj_set_size(s_linkDot, 10, 10);
    lv_obj_align(s_linkDot, LV_ALIGN_RIGHT_MID, -104, 0);
    lv_obj_set_style_radius(s_linkDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_linkDot, 0, 0);
    ui_tint(s_linkDot, C_ALARM);

    s_linkLbl = lv_label_create(h);
    lv_label_set_text(s_linkLbl, "NO LINK");
    lv_obj_set_style_text_font(s_linkLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_linkLbl, C_TEXT_DIM, 0);
    lv_obj_align(s_linkLbl, LV_ALIGN_RIGHT_MID, -14, 0);
}

// ---------------------------------------------------------------------------
//  *** กฎเหล็กของหน้าจอ RGB ตัวนี้ ***
//
//  ห้ามเรียก lv_label_set_text / lv_obj_set_style_* / lv_obj_move_foreground
//  ซ้ำ ๆ ทุก tick ถ้าค่าไม่ได้เปลี่ยน
//
//  LVGL ไม่เช็คให้ว่าค่าเหมือนเดิมไหม -- มัน invalidate ทันที
//  และ move_foreground invalidate "ทั้งหน้าจอ" (480x480x2 = 460 KB ต่อเฟรม)
//
//  ถ้าทำทุก 100 ms -> lv_timer_handler วาดไม่ทัน -> timer ที่อ่าน touch
//  ไม่เคยได้รัน -> จอกดไม่ติดเลย  (เคยเจอมาแล้ว ตอนแรกนึกว่า driver พัง)
//
//  => ทุกฟังก์ชัน update ต้องจำค่าเก่าไว้ แล้วเขียนเฉพาะตอนที่มันเปลี่ยนจริง
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  ข้อความเตือน -- แสดงตรงตำแหน่งชื่อหน้าใน header
//
//  ของเดิมทำเป็นแถบลอยเหนือ nav bar (absolute y 389..418)
//  แต่พื้นที่ของ "หน้า" คือ y 58..423 -> มันไปนั่งทับเนื้อหาของ *ทุกหน้า* พอดี
//  (STAT card ในหน้า HOME, valve card ในหน้า CTRL, log ในหน้า DIAG,
//   การ์ดแถวล่างในหน้า SETUP)
//
//  ย้ายมาไว้ใน header แทน -> ไม่บังอะไรเลย และเห็นได้จากทุกหน้าเหมือนกัน
//  ข้อความต้องสั้น เพราะที่ใน header มีจำกัด (x 216..360)
// ---------------------------------------------------------------------------
static void update_alarm()
{
    uint8_t e = g_app.errflags;

    const char *msg = nullptr;
    if      (e & LNK_F_ESTOP)       msg = LV_SYMBOL_WARNING " E-STOP";
    else if (!g_app.linkUp)         msg = LV_SYMBOL_WARNING " NO LINK";
    else if (e & LNK_F_MB_BUS)      msg = LV_SYMBOL_WARNING " BUS SILENT";
    else if (e & LNK_F_O2_MISMATCH) msg = LV_SYMBOL_WARNING " O2 MISMATCH";
    else if (e & LNK_F_CFG_BAD)     msg = LV_SYMBOL_WARNING " CFG RESET";
    else if (e & LNK_F_O2A_FAIL)    msg = LV_SYMBOL_WARNING " O2#1 FAIL";
    else if (e & LNK_F_O2B_FAIL)    msg = LV_SYMBOL_WARNING " O2#2 FAIL";
    else if (e & LNK_F_PR_FAIL)     msg = LV_SYMBOL_WARNING " PT FAIL";

    static const char *s_lastMsg = (const char *)1;   // ค่าที่เป็นไปไม่ได้ -> บังคับวาดรอบแรก
    static uint8_t     s_lastPage = 0xFF;

    if (msg == s_lastMsg && s_page == s_lastPage) return;
    s_lastMsg  = msg;
    s_lastPage = s_page;

    if (msg) {
        lv_label_set_text(s_title, msg);
        lv_obj_set_style_text_color(s_title, C_ALARM, 0);
    } else {
        lv_label_set_text(s_title, PAGE_TITLE[s_page]);
        lv_obj_set_style_text_color(s_title, C_TEXT_DIM, 0);
    }
}

static void update_link()
{
    static bool     s_lastUp  = true;      // ตั้งตรงข้ามกับค่าเริ่มต้นจริง -> บังคับวาดรอบแรก
    static uint32_t s_seenSeq  = 0xFFFFFFFF;
    static uint32_t s_beatAt   = 0;
    static bool     s_beatOn   = true;

    bool up = g_app.linkUp;

    if (up != s_lastUp) {
        s_lastUp = up;
        ui_tint(s_linkDot, up ? C_OK : C_ALARM);
        // ไม่โชว์เลข seq แล้ว -- ผู้ใช้เข้าใจผิดว่ากำลังเชื่อมใหม่ซ้ำ ๆ
        // ความจริงลิงก์เชื่อมครั้งเดียวแล้วอยู่ยาว  แค่มีเฟรมวิ่งตลอด (5 ครั้ง/วิ)
        lv_label_set_text(s_linkLbl, up ? "LINK OK" : "NO LINK");
        lv_obj_set_style_text_color(s_linkLbl, up ? C_OK : C_ALARM, 0);
        lv_obj_set_style_opa(s_linkDot, LV_OPA_COVER, 0);
        s_beatOn = true;
    }

    // ชีพจร: จุดเต้นช้า ๆ (~0.6 Hz) "เฉพาะตอนมีเฟรมใหม่จริง"
    // -> เห็นจุดเต้น = ลิงก์มีชีวิต / จุดค้างนิ่ง = เฟรมหยุด (แม้ยังไม่ครบ timeout)
    // อ่านง่ายกว่าตัวเลขนับ และไม่มีทางเข้าใจผิดว่าเชื่อมใหม่
    if (up && millis() - s_beatAt >= 800) {
        s_beatAt = millis();
        bool fresh = (g_app.seq != s_seenSeq);   // มีเฟรมใหม่ตั้งแต่รอบก่อนไหม
        s_seenSeq = g_app.seq;
        if (fresh) {
            s_beatOn = !s_beatOn;
            lv_obj_set_style_opa(s_linkDot, s_beatOn ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
}

// ---------------------------------------------------------------------------
//  nav bar
// ---------------------------------------------------------------------------
static void build_nav(lv_obj_t *scr)
{
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, UI_W, UI_NAV_H);
    lv_obj_set_pos(bar, 0, UI_H - UI_NAV_H);
    ui_grad(bar, C_HEADER_TOP, C_HEADER_BOT);
    lv_obj_set_style_border_color(bar, C_LINE, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    const int bw = UI_W / PAGE_COUNT;    // 96 px ต่อปุ่ม -- นิ้วกดสบาย

    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *b = lv_btn_create(bar);
        lv_obj_set_size(b, bw - 4, UI_NAV_H - 8);
        lv_obj_set_pos(b, i * bw + 2, 4);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        // กดค้าง -> ไล่เฉดกลับหัว = ปุ่มจม (แม้จะยังไม่ใช่หน้าที่เลือกอยู่)
        ui_grad(b, C_PANEL_HI_BOT, C_PANEL_HI_TOP, LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text_fmt(l, "%s\n%s", NAV_ICON[i], NAV_TEXT[i]);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);

        s_navBtn[i] = b;
    }
}

// ---------------------------------------------------------------------------
//  ตัววัด touch -- วาดบน lv_layer_sys()
//
//  LVGL "ไม่เคย" ส่ง input ไป layer นี้ (มันมีไว้ทำ cursor/overlay ของระบบ)
//  จึงเอามาวัดได้โดยไม่ไปรบกวนการกดปุ่ม
//
//  ใช้ตอบคำถามเดียว: GT911 ยังอ่านค่าได้อยู่ไหม และ LVGL เห็นว่า "กด" ไหม
//    จุดเขียววิ่งตามนิ้ว + DOWN  = driver ปกติ  -> ปัญหาอยู่ที่ UI/hit-test
//    จุดไม่ขึ้น / T:DEAD          = driver พัง   -> ปัญหาอยู่ที่ I2C
// ---------------------------------------------------------------------------
// แก้เสร็จแล้ว (GT911 อ่าน register ผิด offset ไป 1 ไบต์) -> ปิดตัววัด
// ตั้งกลับเป็น 1 ได้ทุกเมื่อถ้าสงสัยว่า touch มีปัญหาอีก
#define UI_TOUCH_PROBE  0

#if UI_TOUCH_PROBE
static lv_obj_t *s_probeDot;
static lv_obj_t *s_probeTxt;

static void build_probe()
{
    lv_obj_t *sys = lv_layer_sys();

    s_probeDot = lv_obj_create(sys);
    lv_obj_set_size(s_probeDot, 26, 26);
    lv_obj_set_style_radius(s_probeDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_probeDot, C_OK, 0);
    lv_obj_set_style_bg_opa(s_probeDot, LV_OPA_70, 0);
    lv_obj_set_style_border_color(s_probeDot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(s_probeDot, 2, 0);
    lv_obj_add_flag(s_probeDot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_probeDot, LV_OBJ_FLAG_CLICKABLE);

    s_probeTxt = lv_label_create(sys);
    lv_obj_set_style_text_font(s_probeTxt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_probeTxt, lv_color_hex(0xFFEE58), 0);
    lv_obj_set_style_bg_color(s_probeTxt, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_probeTxt, LV_OPA_70, 0);
    lv_obj_set_style_pad_all(s_probeTxt, 3, 0);
    lv_obj_align(s_probeTxt, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text(s_probeTxt, "probe");
}

static void probe_tick()
{
    static bool     c_pressed = true;
    static int16_t  c_x = -1, c_y = -1;
    static uint32_t c_ok = 0xFFFFFFFF;

    int16_t x, y;
    touch_last_point(&x, &y);
    bool     pressed = touch_is_pressed();
    uint32_t ok      = touch_read_count();

    if (pressed != c_pressed || x != c_x || y != c_y) {
        c_pressed = pressed; c_x = x; c_y = y;
        if (pressed) {
            lv_obj_clear_flag(s_probeDot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_probeDot, x - 13, y - 13);
        } else {
            lv_obj_add_flag(s_probeDot, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // อัปเดตตัวเลขวินาทีละครั้งพอ ไม่ให้ invalidate ถี่
    static uint32_t lastTxt = 0;
    if (millis() - lastTxt < 500 && ok == c_ok) return;
    lastTxt = millis();
    c_ok = ok;

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);

    lv_label_set_text_fmt(s_probeTxt, "T:%s %s %d,%d  rd %lu err %lu  lv %uk",
                          touch_is_alive() ? "OK" : "DEAD",
                          pressed ? "DOWN" : "up ",
                          x, y,
                          (unsigned long)ok,
                          (unsigned long)touch_fail_count(),
                          (unsigned)(mon.free_size / 1024));
}
#endif // UI_TOUCH_PROBE

// ---------------------------------------------------------------------------
void ui_begin()
{
    lv_obj_t *scr = lv_screen_active();
    ui_grad(scr, C_BG_TOP, C_BG_BOT);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);
    build_nav(scr);

    // สร้าง container ของแต่ละหน้า -- ซ้อนกันอยู่ที่เดียว แล้วซ่อน/แสดงทีละอัน
    for (uint8_t i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *p = lv_obj_create(scr);
        lv_obj_set_size(p, UI_W, UI_BODY_H);
        lv_obj_set_pos(p, 0, UI_HEADER_H);
        ui_grad(p, C_BG_TOP, C_BG_BOT);
        lv_obj_set_style_border_width(p, 0, 0);
        lv_obj_set_style_radius(p, 0, 0);
        lv_obj_set_style_pad_all(p, 0, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
        s_pages[i] = p;
    }

    ui_home_build(s_pages[PAGE_HOME]);
    ui_data_build(s_pages[PAGE_DATA]);
    ui_control_build(s_pages[PAGE_CTRL]);
    ui_setup_build(s_pages[PAGE_SETUP]);
    ui_diag_build(s_pages[PAGE_DIAG]);


#if UI_TOUCH_PROBE
    build_probe();
#endif

    link_set_log_cb(ui_diag_log);

    ui_goto(PAGE_HOME);
}

// ---------------------------------------------------------------------------
//  Event log -- จับ "การเปลี่ยนแปลง" ของสถานะที่ Nano รายงานมา
//
//  ทำที่นี่ที่เดียว เพราะ ui_tick() ทำงานตลอดไม่ว่าจะเปิดหน้าไหนอยู่
//  (ถ้าไปดักในหน้า DIAG จะพลาด event ตอนที่ผู้ใช้อยู่หน้าอื่น)
// ---------------------------------------------------------------------------
void ui_event(uint8_t type, uint8_t data)
{
    ui_diag_event(type, data, millis());
}

static void detect_events()
{
    static bool     s_first = true;
    static bool     c_up    = false;
    static uint8_t  c_run   = LNK_RUN_IDLE;
    static uint8_t  c_mode  = LNK_MODE_AUTO;
    static uint8_t  c_zone  = LNK_RVZ_OFF;
    static uint8_t  c_err   = 0;

    // รอบแรกแค่จำค่าตั้งต้น -- ไม่งั้นจะได้ event ปลอมตอนบูต
    if (s_first) {
        s_first = false;
        c_up = g_app.linkUp;  c_run = g_app.runState;
        c_mode = g_app.mode;  c_zone = g_app.rvZone;  c_err = g_app.errflags;
        return;
    }

    if (g_app.linkUp != c_up) {
        c_up = g_app.linkUp;
        ui_event(c_up ? EV_LINK_UP : EV_LINK_DOWN, 0);
    }

    if (g_app.runState != c_run) {
        c_run = g_app.runState;
        ui_event(c_run == LNK_RUN_RUNNING ? EV_START : EV_STOP, 0);
    }

    if (g_app.mode != c_mode) {
        c_mode = g_app.mode;
        ui_event(c_mode == LNK_MODE_AUTO ? EV_MODE_AUTO : EV_MODE_MANUAL, 0);
    }

    if (g_app.rvZone != c_zone) {
        c_zone = g_app.rvZone;
        ui_event(EV_ZONE, c_zone);
    }

    // errflags: บันทึกทีละบิตที่ "เพิ่งขึ้น" และ "เพิ่งหาย"
    uint8_t now = g_app.errflags;
    uint8_t rose = (uint8_t)(now & ~c_err);
    uint8_t fell = (uint8_t)(~now & c_err);
    c_err = now;

    for (uint8_t b = 0; b < 8; b++) {
        uint8_t bit = (uint8_t)(1 << b);
        if (rose & bit) ui_event(bit == LNK_F_ESTOP ? EV_ESTOP : EV_ALARM_ON, bit);
        if (fell & bit) ui_event(EV_ALARM_OFF, bit);
    }
}

// ---------------------------------------------------------------------------
void ui_tick()
{
    update_link();

    // หมายเหตุ: "ไม่" ขอ config ($G) ซ้ำแบบ global ที่นี่
    //   เคยใส่ไว้เพื่อให้ได้ค่า PIN -> แต่ถ้า $K ตกบ่อย (SoftwareSerial) จะขอ $G
    //   ไม่หยุดทุก 1.5 วิ -> Nano ตอบ 58 เฟรมทุกรอบ -> ลิงก์ตัน ACK timeout คำสั่งหาย
    //   ตอนนี้ login มี "default PIN" ฝั่งจอแล้ว (1111/2222/3333/4444) ไม่ต้องพึ่ง Nano
    //   การ sync config จริงยังทำตอนเข้าหน้า SETUP (ui_setup_update) ตามเดิม

    // auto-logout -- ปลดล็อกแล้วเดินจากจอไป (หน้าไหนก็ตาม) ไม่แตะเกิน 3 นาที -> กลับ VIEW
    //   ใช้เวลา idle ของ LVGL (นับการแตะจอทั้งระบบ) -> ครอบทุกหน้า ไม่ใช่แค่ SETUP
    if (g_app.accessLevel > ACC_VIEW && lv_display_get_inactive_time(NULL) > 180000UL) {
        g_app.accessLevel = ACC_VIEW;
        ui_toast("AUTO-LOCKED");
    }

    update_alarm();
    toast_tick();
    detect_events();

#if UI_TOUCH_PROBE
    probe_tick();
#endif

    // อัปเดตเฉพาะหน้าที่เห็นอยู่ -- ประหยัด CPU และไม่ทำให้จอ RGB กระตุก
    switch (s_page) {
    case PAGE_HOME:  ui_home_update();    break;
    case PAGE_DATA:  ui_data_update();    break;
    case PAGE_CTRL:  ui_control_update(); break;
    case PAGE_SETUP: ui_setup_update();   break;
    case PAGE_DIAG:  ui_diag_update();    break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
//  คีย์บอร์ดตัวเลข -- ตัวกลาง ทุกหน้าเรียกใช้ได้
//
//  ของเดิมฝังอยู่ในหน้า SETUP หน้าเดียว -> หน้าอื่นอยากให้แก้ค่าก็ทำไม่ได้
//  ย้ายมาไว้ตรงนี้ให้เป็นของกลาง แล้วส่งผลลัพธ์กลับผ่าน callback
// ---------------------------------------------------------------------------
static lv_obj_t   *s_kbOverlay = nullptr;
static lv_obj_t   *s_kbTa      = nullptr;
static UiKeypadCb  s_kbCb      = nullptr;
static void       *s_kbUser    = nullptr;
static float       s_kbMin     = 0, s_kbMax = 0;
static uint8_t     s_kbDp      = 0;

static void kb_close()
{
    if (s_kbOverlay) {
        // ต้องเป็น del_async: ฟังก์ชันนี้ถูกเรียกจาก event callback ของ keyboard
        // ซึ่งเป็นลูกของ overlay -- ลบตรง ๆ ระหว่าง LVGL ไล่ event อยู่ = crash
        lv_obj_del_async(s_kbOverlay);
        s_kbOverlay = nullptr;
        s_kbTa      = nullptr;
    }
    s_kbCb   = nullptr;
    s_kbUser = nullptr;
}

static void kb_ready_cb(lv_event_t *e)
{
    (void)e;
    if (!s_kbTa) return;

    float v = (float)atof(lv_textarea_get_text(s_kbTa));

    if (v < s_kbMin || v > s_kbMax) {
        char msg[64];
        snprintf(msg, sizeof(msg), "RANGE %.*f - %.*f",
                 (int)s_kbDp, (double)s_kbMin, (int)s_kbDp, (double)s_kbMax);
        ui_toast(msg);
        return;                       // ไม่ปิด -- ให้แก้ต่อได้เลย
    }

    UiKeypadCb cb   = s_kbCb;
    void      *user = s_kbUser;
    kb_close();                       // ปิดก่อน แล้วค่อยเรียก cb
    if (cb) cb(v, user);              // (cb อาจเปิด keypad ตัวใหม่ได้)
}

static void kb_cancel_cb(lv_event_t *e) { (void)e; kb_close(); }

void ui_keypad_open(const char *title, const char *sub,
                    float cur, float vmin, float vmax, uint8_t dp,
                    bool password, UiKeypadCb cb, void *user)
{
    kb_close();

    s_kbCb   = cb;
    s_kbUser = user;
    s_kbMin  = vmin;
    s_kbMax  = vmax;
    s_kbDp   = dp;

    s_kbOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_kbOverlay, UI_W, UI_H);
    lv_obj_set_pos(s_kbOverlay, 0, 0);
    ui_grad(s_kbOverlay, C_BG_TOP, C_BG_BOT);
    lv_obj_set_style_border_width(s_kbOverlay, 0, 0);
    lv_obj_set_style_radius(s_kbOverlay, 0, 0);
    lv_obj_set_style_pad_all(s_kbOverlay, 0, 0);
    lv_obj_clear_flag(s_kbOverlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s_kbOverlay);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, C_NEON, 0);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 16);

    if (sub) {
        lv_obj_t *s = lv_label_create(s_kbOverlay);
        lv_label_set_text(s, sub);
        lv_obj_set_style_text_color(s, C_TEXT_DIM, 0);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
        lv_obj_align(s, LV_ALIGN_TOP_MID, 0, 46);
    }

    s_kbTa = lv_textarea_create(s_kbOverlay);
    lv_obj_set_size(s_kbTa, 320, 54);
    lv_obj_align(s_kbTa, LV_ALIGN_TOP_MID, 0, 74);
    lv_textarea_set_one_line(s_kbTa, true);
    lv_textarea_set_max_length(s_kbTa, 12);
    lv_obj_set_style_text_font(s_kbTa, &lv_font_montserrat_20, 0);
    ui_grad(s_kbTa, C_SUNK_TOP, C_SUNK_BOT);        // ช่องพิมพ์ = ร่อง -> ไล่เฉดกลับหัว
    lv_obj_set_style_border_color(s_kbTa, C_NEON, 0);
    lv_obj_set_style_text_color(s_kbTa, C_TEXT, 0);

    if (password) {
        lv_textarea_set_password_mode(s_kbTa, true);
        lv_textarea_set_text(s_kbTa, "");
    } else {
        char cbuf[24];
        snprintf(cbuf, sizeof(cbuf), "%.*f", (int)dp, (double)cur);
        lv_textarea_set_text(s_kbTa, cbuf);
    }

    lv_obj_t *kb = lv_keyboard_create(s_kbOverlay);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb, UI_W, 240);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);

    // ####################################################################
    //  *** ทาพื้นตรงไหน ต้องตั้งสีตัวอักษรตรงนั้นด้วยเสมอ ***
    //
    //  lv_conf.h ตั้ง LV_THEME_DEFAULT_DARK = 0  -> ธีมพื้นฐานของ LVGL เป็น
    //  "ธีมสว่าง" ซึ่งกำหนดสีตัวอักษรบนปุ่มเป็นสี "เข้ม" (ไว้วางบนพื้นขาว)
    //
    //  widget ที่เราไม่ได้ทาสีเอง จะได้สีจากธีมนั้น  พอเรามาทาแต่ "พื้น" ให้เข้ม
    //  ตามธีมของเรา แต่ลืมสีตัวอักษร -> เข้มบนเข้ม -> ตัวเลขหายทั้งแผง
    //  (เคยพลาดมาแล้วตอนใส่ไล่เฉด: หน้า ENTER PIN กลายเป็นแป้นเปล่า กดไม่ถูก)
    // ####################################################################
    ui_grad(kb, C_HEADER_TOP, C_HEADER_BOT);                    // พื้นแผง
    ui_grad(kb, C_PANEL_TOP, C_PANEL_BOT, LV_PART_ITEMS);       // ปุ่ม -- นูนขึ้นมา
    ui_grad(kb, C_PANEL_HI_BOT, C_PANEL_HI_TOP, LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED);   // กด -- จมลง

    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS);
    // กดแล้วพื้นเป็นมิ้นอ่อน -> ตัวอักษรต้องเข้ม (ธีมสว่าง) ไม่ใช่ขาว
    lv_obj_set_style_text_color(kb, C_TEXT, LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED);
    lv_obj_set_style_text_font(kb, &lv_font_montserrat_20, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, C_LINE, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);

    lv_keyboard_set_textarea(kb, s_kbTa);
    lv_obj_add_event_cb(kb, kb_ready_cb,  LV_EVENT_READY,  nullptr);
    lv_obj_add_event_cb(kb, kb_cancel_cb, LV_EVENT_CANCEL, nullptr);
}
