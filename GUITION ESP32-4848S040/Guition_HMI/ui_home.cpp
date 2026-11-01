// ============================================================================
//  ui_home.cpp  --  หน้าหลัก: ผัง P&ID ตามแบบจริง + สถานะอุปกรณ์สด
//
//  วาดด้วย LVGL ล้วน ไม่ใช้รูปภาพ -- เพราะท่อทุกเส้นต้องเปลี่ยนสีและมีจุดของไหล
//  วิ่งตามสถานะจริงที่ Nano รายงานกลับมา  ถ้าเป็นรูปนิ่งจะทำแบบนี้ไม่ได้
//
//   x:0        80       160      240      320      400      480
//   ┌────────────────────────────────────────────────────────────┐ y=0
//   │ ┌CROSS-CHECK┐   20.94   20.88                              │
//   │ │ ● MATCH   │      ◉       ◉                               │
//   │ │ DIFF 0.06 │  ┌───┴───────┴──────────┬──────────► VENT    │ 24
//   │ └───────────┘  │                      │                    │
//   │                │              ┌───────▼──┐    ╭────╮       │
//   │                │              │  FILTER  │───►│ CB │       │ 68
//   │      ╭────╮    │              └──────────┘    ╰──┬─╯       │
//   │      │ CP │────┤                                 │         │
//   │      ╰──▲─╯  ┌─┴──────────┐                      │         │
//   │         │    │  DIGESTER  │                      │         │
//   │         │    │┌──────────┐│   ┌────────┐         │  ╭────╮ │
//   │         │    ││   GAS    ││◄──│ MIXER  │◄────────┴──│ FB │◄── AIR
//   │         │    │├──────────┤│   └────┬───┘            ╰────╯ │ 164
//   │         │    ││          ││        │                       │
//   │         │    ││  LIQUID  ││      ╲ │ ╱  RV  65%            │
//   │         └────┤│ 1.02 bar ││      ╱ │ ╲  PID LOCK           │ 208
//   │              │└──────────┘│        │                       │
//   │              └──┬──────┬──┘   ┌────┴───┐                   │
//   │  ┌────╮ ╭────╮  │      │      │ N2 GEN │                   │
//   │  │FEED│►│ FP │──┘      ▼      └────────┘                   │
//   │  └────╯ ╰────╯     ┌───────┐                               │ 304
//   │                    │COMPOST│                               │
//   ├────────────────────────────────────────────────────────────┤ 330
//   │ ● RUNNING  00:12:34                                  AUTO  │
//   └────────────────────────────────────────────────────────────┘ 366
//
//  ~~~ กฎที่ห้ามลืม (เคยพลาดมาแล้วทั้งคู่) ~~~
//
//  1) ตัวหนังสือบนจอ "ต้องเป็นอังกฤษเท่านั้น"
//     ฟอนต์ Montserrat ไม่มี glyph ภาษาไทย -> ขึ้นเป็นสี่เหลี่ยมเอเลี่ยน
//     (คอมเมนต์ในโค้ดเป็นไทยได้ ไม่เกี่ยวกัน)
//
//  2) LVGL ไม่เช็คให้ว่าค่าที่สั่งซ้ำของเดิมไหม  สั่งซ้ำทุก tick
//     = invalidate ทุก tick = lv_timer_handler ตามไม่ทัน = ทัชตาย
//     -> ทุกค่าใน ui_home_update() ต้องผ่าน cache ก่อนเสมอ
// ============================================================================
#include "ui.h"
#include "ui_theme.h"
#include "app_state.h"
#include "link_protocol.h"

#include <math.h>

// ---------------------------------------------------------------------------
//  พิกัด -- พื้นที่ page = 480 x 366   ผัง 0..330 / แถบสถานะ 334..362
// ---------------------------------------------------------------------------
#define PIPE_W        3          // ความหนาท่อ
#define EQ_R         17          // รัศมีวงกลมอุปกรณ์
#define DOT_D         9          // ขนาดจุดของไหล

#define TANK_X      136
#define TANK_Y       92
#define TANK_W       96
#define TANK_H      176          // 92..268

#define VENT_Y       24          // ท่อระบายแก๊ส (แนวนอน)
#define VENT_X0     164          // ขึ้นจากหัวถังตรงนี้
#define VENT_X1     452

#define O1_X        196
#define O2_X        252

#define FILT_X      254
#define FILT_Y       50
#define FILT_W       58
#define FILT_H       36          // 50..86   -> กลาง (283, 68)
#define BRANCH_X    283          // vent -> filter

#define CB_CX       346
#define CB_CY        68

#define MIX_X       304
#define MIX_Y       150
#define MIX_W        84
#define MIX_H        28          // 150..178 -> กลาง (346, 164)
#define MIX_CY      164

#define FB_CX       426
#define FB_CY       164

#define RV_CX       346
#define RV_CY       208
#define RV_W         26
#define RV_H         24

#define N2_X        304
#define N2_Y        240
#define N2_W         84
#define N2_H         42          // 240..282

#define CP_CX        72
#define CP_CY       132
#define CP_TOP_Y    108          // ท่อ CP -> ปากถังบนซ้าย
#define CP_BOT_Y    244          // ปากถังล่างซ้าย -> CP

#define FEED_X        8
#define FEED_Y      282
#define FEED_W       46
#define FEED_H       44          // 282..326

#define FP_CX        86
#define FP_CY       304
#define FP_UP_X     152          // ท่อ FP ขึ้นเข้าก้นถัง

#define OUT_X       216          // ท่อปุ๋ยออก
#define COMP_X      180
#define COMP_Y      294
#define COMP_W       72
#define COMP_H       36          // 294..330

#define STAT_Y      334
#define STAT_H       28          // 334..362

// ---------------------------------------------------------------------------
//  เส้นทางของไหล 1 เส้น = ท่อหลายท่อน + จุดวิ่ง 2 จุด
//
//  จุดวิ่งอยู่บนท่อนที่ยาว/เห็นชัดที่สุดท่อนเดียว ไม่ต้องวิ่งครบทุกท่อน
//  (วิ่งครบทุกท่อน = จุดเยอะ = invalidate ถี่โดยไม่ได้อะไรเพิ่ม)
// ---------------------------------------------------------------------------
#define MAX_SEG   4

struct Path {
    lv_obj_t *seg[MAX_SEG];
    uint8_t   nSeg;
    lv_obj_t *dot[2];
    uint8_t   nDot;          // ท่อสั้นใช้จุดเดียวพอ -- 2 จุดจะเบียดกันดูรก
    int16_t   from, to;      // ช่วงที่จุดวิ่ง (พิกัดในแกนที่ขยับ)
    int16_t   fixed;         // แกนที่ไม่ขยับ
    bool      vert;
    bool      on;
    uint16_t  ms;            // รอบละกี่ ms
    lv_color_t med;          // สีของไหลในท่อนี้
};

// ---------------------------------------------------------------------------
//  ความเร็วของไหล -- ผูกกับของจริง ไม่ใช่อนิเมชันตกแต่ง
//
//  ของเหลว (ปั๊ม)      ไหลช้า
//  แก๊ส    (โบลเวอร์)   ไหลเร็วกว่า -- ความหนืดต่ำกว่ามาก
//  N2                  เร็วตามเปอร์เซ็นต์ที่วาล์ว RV เปิดจริง (เปิดมาก = ไหลแรง)
// ---------------------------------------------------------------------------
#define MS_LIQ      1700
#define MS_GAS       900

// แบ่ง N2 เป็นช่วง ไม่งั้น PID ขยับวาล์ว 1% ทีนึง อนิเมชันจะถูกรีสตาร์ตทุกครั้ง
static const uint16_t MS_N2[4] = { 0, 1800, 1200, 700 };
static uint8_t rv_bucket(uint8_t valve)
{
    if (valve == 0) return 0;
    if (valve < 34) return 1;
    if (valve < 67) return 2;
    return 3;
}

// ใบพัด: ปั๊มหมุนช้า / โบลเวอร์หมุนเร็ว  (เรียงตาม E_CP, E_CB, E_FB, E_FP)
static const uint16_t ROTOR_MS[4] = { 1700, 700, 700, 1700 };

enum {
    P_CP = 0,    // ของเหลวหมุนเวียน
    P_FP,        // ป้อนของเหลวเข้า
    P_VENT,      // แก๊สระบายออก
    P_CB,        // แก๊สหมุนเวียน
    P_FB,        // อากาศเข้า
    P_RV,        // N2 ฉีดเข้า
    P_MIX,       // mixer -> ถัง
    P_OUT,       // ปุ๋ยออก
    P_COUNT
};
static Path s_path[P_COUNT];

// อุปกรณ์ (วงกลม)
enum { E_CP = 0, E_CB, E_FB, E_FP, E_COUNT };
static lv_obj_t *s_eq[E_COUNT];
static lv_obj_t *s_eqLbl[E_COUNT];
static lv_obj_t *s_rotor[E_COUNT];      // ใบพัดหมุนในวงกลม (lv_arc)

static lv_obj_t *s_bub[3];              // ฟองแก๊สลอยขึ้นในของเหลว

// ตัวถัง + ค่าต่าง ๆ
static lv_obj_t *s_tank;
static lv_obj_t *s_gas;             // ช่องแก๊สในถัง -- สีบอกโซนควบคุม
static lv_obj_t *s_press;

static lv_obj_t *s_o1Mark, *s_o2Mark;
static lv_obj_t *s_o1Val,  *s_o2Val;

static lv_obj_t *s_rvLine;          // สัญลักษณ์วาล์ว (โบว์ไท)
static lv_obj_t *s_rvPct;
static lv_obj_t *s_rvZone;
static lv_obj_t *s_rvBar;           // แถบเติมแสดง % วาล์วที่ O2 สั่งเปิดจริง

static lv_obj_t *s_xcLamp, *s_xcTxt, *s_xcDiff;   // การ์ดเทียบ O1/O2

static lv_obj_t *s_runLamp, *s_runLbl, *s_modeLbl;

// ---------------------------------------------------------------------------
//  ชิ้นส่วนพื้นฐาน
// ---------------------------------------------------------------------------
static lv_obj_t *raw_box(lv_obj_t *p, int x, int y, int w, int h, int r)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// ท่อแนวนอน -- y คือแกนกลางท่อ
static lv_obj_t *pipe_h(lv_obj_t *p, int x0, int x1, int y)
{
    lv_obj_t *o = raw_box(p, x0, y - PIPE_W / 2, x1 - x0, PIPE_W, 1);
    ui_grad(o, C_LINE, C_LINE);
    return o;
}

// ท่อแนวตั้ง -- x คือแกนกลางท่อ
static lv_obj_t *pipe_v(lv_obj_t *p, int x, int y0, int y1)
{
    lv_obj_t *o = raw_box(p, x - PIPE_W / 2, y0, PIPE_W, y1 - y0, 1);
    ui_grad(o, C_LINE, C_LINE);
    return o;
}

static lv_obj_t *make_dot(lv_obj_t *p, lv_color_t c)
{
    lv_obj_t *d = raw_box(p, 0, 0, DOT_D, DOT_D, LV_RADIUS_CIRCLE);
    ui_grad(d, lv_color_lighten(c, 70), c);
    lv_obj_set_style_shadow_color(d, c, 0);
    lv_obj_set_style_shadow_width(d, 8, 0);
    lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    return d;
}

static lv_obj_t *make_text(lv_obj_t *p, int x, int y, const lv_font_t *f,
                           lv_color_t c, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

// ตัวหนังสือกลางแนวนอนรอบจุด cx (ใช้กับค่าเซนเซอร์เหนือท่อ)
static lv_obj_t *make_text_mid(lv_obj_t *p, int cx, int y, int w,
                               const lv_font_t *f, lv_color_t c, const char *t)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);      // ค่าเพี้ยนยาว ๆ ให้ตัดทิ้ง ไม่ดันทับกัน
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, t);
    lv_obj_set_pos(l, cx - w / 2, y);
    return l;
}

// กล่องอุปกรณ์ (FILTER / MIXER / N2 GEN / FEED / COMPOST)
static lv_obj_t *make_unit(lv_obj_t *p, int x, int y, int w, int h,
                           const char *name, lv_color_t border)
{
    lv_obj_t *b = raw_box(p, x, y, w, h, 8);
    ui_grad(b, C_PANEL_TOP, C_PANEL_BOT);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, border, 0);

    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, C_TEXT_DIM, 0);
    lv_obj_center(l);
    return b;
}

// วงกลมอุปกรณ์หมุน (CP / CB / FB / FP)
static void make_equip(lv_obj_t *p, uint8_t i, int cx, int cy, const char *name)
{
    lv_obj_t *c = raw_box(p, cx - EQ_R, cy - EQ_R, EQ_R * 2, EQ_R * 2, LV_RADIUS_CIRCLE);
    ui_grad(c, C_PANEL_TOP, C_PANEL_BOT);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, C_LINE, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);      // ยังไม่เดิน -> ไม่เรือง

    // ใบพัด -- ต้องสร้าง "ก่อน" ป้ายชื่อ เพื่อให้ชื่ออยู่ทับข้างบน อ่านออกตลอดแม้ใบพัดหมุนผ่าน
    lv_obj_t *a = lv_arc_create(c);
    lv_obj_set_size(a, EQ_R * 2 - 8, EQ_R * 2 - 8);      // 26x26 -- อยู่ในขอบวงกลมพอดี
    lv_obj_center(a);
    lv_arc_set_bg_angles(a, 0, 360);
    lv_arc_set_angles(a, 0, 100);                        // ใบพัด = เสี้ยว 100 องศา
    lv_arc_set_rotation(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);          // ไม่เอาปุ่มลาก
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);         // arc กดได้โดยปริยาย -> ต้องปิด ไม่งั้นแย่งทัช
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);   // ซ่อนรางวงกลมพื้นหลัง
    lv_obj_set_style_arc_width(a, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, C_OK, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    s_rotor[i] = a;

    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, name);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, C_TEXT_MUTE, 0);
    lv_obj_center(l);

    s_eq[i]    = c;
    s_eqLbl[i] = l;
}

static void rotor_set(uint8_t i, bool on);   // นิยามอยู่ล่าง (ต้องใช้ anim helper ก่อน)

// เดิน = ขอบเขียว + เรืองแสง + ใบพัดหมุน + ตัวหนังสือสว่าง  (เรียกเฉพาะตอนสถานะพลิก)
static void equip_set(uint8_t i, bool on)
{
    lv_obj_set_style_border_color(s_eq[i], on ? C_OK : C_LINE, 0);
    lv_obj_set_style_shadow_color(s_eq[i], C_OK, 0);
    lv_obj_set_style_shadow_width(s_eq[i], on ? 14 : 0, 0);
    lv_obj_set_style_shadow_spread(s_eq[i], on ? 1 : 0, 0);
    ui_grad(s_eq[i], on ? C_PANEL_HI_TOP : C_PANEL_TOP,
                     on ? C_PANEL_HI_BOT : C_PANEL_BOT);
    lv_obj_set_style_text_color(s_eqLbl[i], on ? C_TEXT : C_TEXT_MUTE, 0);
    rotor_set(i, on);
}

// ---------------------------------------------------------------------------
//  เส้นทางของไหล
// ---------------------------------------------------------------------------
static void anim_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }

static void path_begin(Path &p, lv_color_t med)
{
    p.nSeg = 0;
    p.nDot = 0;
    p.on   = false;
    p.med  = med;
}

static void path_seg(Path &p, lv_obj_t *o)
{
    if (p.nSeg < MAX_SEG) p.seg[p.nSeg++] = o;
}

// ตั้งช่วงที่จุดของไหลจะวิ่ง  from -> to คือทิศการไหลจริง
// *** งบ invalidate ของ LVGL มีแค่ 32 พื้นที่ต่อเฟรม (LV_INV_BUF_SIZE ฝังตายใน source) ***
// เกินเมื่อไหร่ มันเลิกคิดแล้ววาดใหม่ทั้งจอ 480x480 ทุกเฟรม -> lv_timer_handler ตามไม่ทัน
// -> ตัวอ่าน touch ไม่ได้รัน -> จอกดไม่ได้   (เคยพังด้วยเหตุนี้มาแล้ว)
//
// -> ท่อสั้นใช้จุดเดียวพอ  (ท่อ RV ยาว 44px ใส่ 2 จุดก็เบียดกันดูรกอยู่ดี)
static void path_dots(Path &p, lv_obj_t *par, int from, int to, int fixed,
                      bool vert, uint8_t n)
{
    p.from  = (int16_t)from;
    p.to    = (int16_t)to;
    p.fixed = (int16_t)fixed;
    p.vert  = vert;
    p.nDot  = n;
    for (uint8_t i = 0; i < n; i++) p.dot[i] = make_dot(par, p.med);
}

// *** เรียกเฉพาะตอนสถานะ/ความเร็วเปลี่ยน *** (set_style_* invalidate ทุกครั้งโดยไม่เช็คค่าเดิม)
static void path_set(Path &p, bool on, uint16_t ms)
{
    bool flip = (on != p.on);
    p.on = on;
    p.ms = ms;

    // ทาสีท่อใหม่เฉพาะตอน on/off พลิก -- แค่เปลี่ยนความเร็วไม่ต้องทาสีใหม่
    if (flip) {
        for (uint8_t i = 0; i < p.nSeg; i++) {
            ui_grad(p.seg[i], on ? p.med : C_LINE, on ? p.med : C_LINE);
            lv_obj_set_style_shadow_color(p.seg[i], p.med, 0);
            lv_obj_set_style_shadow_width(p.seg[i], on ? 6 : 0, 0);
        }
    }

    for (uint8_t i = 0; i < p.nDot; i++) {
        lv_anim_delete(p.dot[i], p.vert ? anim_y : anim_x);

        if (!on) {
            lv_obj_add_flag(p.dot[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        // จุดกึ่งกลางต้องตรงแกนท่อ -> ลบครึ่งขนาดจุด
        int cross = p.fixed - DOT_D / 2;
        if (p.vert) lv_obj_set_x(p.dot[i], cross);
        else        lv_obj_set_y(p.dot[i], cross);

        lv_obj_clear_flag(p.dot[i], LV_OBJ_FLAG_HIDDEN);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, p.dot[i]);
        lv_anim_set_exec_cb(&a, p.vert ? anim_y : anim_x);
        lv_anim_set_values(&a, p.from - DOT_D / 2, p.to - DOT_D / 2);
        lv_anim_set_duration(&a, ms);
        lv_anim_set_delay(&a, i * (ms / 2));     // เหลื่อมครึ่งรอบ -> เห็นเป็นสายของไหล
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

// ---------------------------------------------------------------------------
//  ใบพัดหมุนในตัวปั๊ม/โบลเวอร์ -- ตัวที่ทำให้ผังดู "มีชีวิต" ที่สุด
//  ใช้ lv_arc เสี้ยวเดียว แล้วหมุน rotation วนไปเรื่อย ๆ
// ---------------------------------------------------------------------------
static void rotor_cb(void *o, int32_t v) { lv_arc_set_rotation((lv_obj_t *)o, v); }

static void rotor_set(uint8_t i, bool on)
{
    lv_anim_delete(s_rotor[i], rotor_cb);

    if (!on) {
        lv_obj_add_flag(s_rotor[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_rotor[i], LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_rotor[i]);
    lv_anim_set_exec_cb(&a, rotor_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_duration(&a, ROTOR_MS[i]);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

// ---------------------------------------------------------------------------
//  ฟองแก๊สลอยขึ้นในของเหลว
//  มีเฉพาะตอนมีแก๊สถูกอัดเข้าถังจริง ๆ (CB หมุนเวียน / FB เป่าอากาศ / RV ฉีด N2)
//  ทั้ง 3 ทางเข้าถังผ่าน mixer ที่ก้นถังเหมือนกันหมด -> ฟองต้องลอยขึ้น
// ---------------------------------------------------------------------------
static void bubbles_set(bool on)
{
    for (uint8_t i = 0; i < 3; i++) {
        lv_anim_delete(s_bub[i], anim_y);

        if (!on) {
            lv_obj_add_flag(s_bub[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_bub[i], LV_OBJ_FLAG_HIDDEN);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_bub[i]);
        lv_anim_set_exec_cb(&a, anim_y);
        lv_anim_set_values(&a, 76, 2);           // ก้นของเหลว -> ผิวน้ำ
        lv_anim_set_duration(&a, 2100);
        lv_anim_set_delay(&a, i * 700);          // เหลื่อมกัน ไม่ลอยพร้อมกันเป็นแถว
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
}

// ---------------------------------------------------------------------------
//  หลอดไฟ (lv_led)
//
//  lv_led ผสมสีเอง: mix(สีหลอด, ดำ, brightness(style bg)) แล้ว mix ซ้ำตาม brightness
//  => style bg ต้องเป็น "ขาว" ไม่งั้นธีมเข้มจะทำให้หลอดมืดตื้อ ต่อให้สั่ง on แล้ว
// ---------------------------------------------------------------------------
static lv_obj_t *make_lamp(lv_obj_t *p, int x, int y, int d, uint8_t glow, lv_color_t c)
{
    lv_obj_t *l = lv_led_create(p);
    lv_obj_set_size(l, d, d);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_style_bg_color(l, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_color(l, lv_color_hex(0x6E6E6E), 0);
    lv_obj_set_style_bg_grad_dir(l, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_shadow_color(l, lv_color_white(), 0);
    lv_obj_set_style_shadow_spread(l, 1, 0);
    lv_obj_set_style_shadow_width(l, 0, 0);
    lv_obj_set_user_data(l, (void *)(uintptr_t)glow);
    lv_led_set_color(l, c);
    lv_led_off(l);
    return l;
}

static void lamp_set(lv_obj_t *l, bool on, lv_color_t c)
{
    uint8_t glow = (uint8_t)(uintptr_t)lv_obj_get_user_data(l);
    lv_led_set_color(l, c);
    lv_obj_set_style_shadow_width(l, on ? glow : 0, 0);
    if (on) lv_led_on(l);
    else    lv_led_off(l);
}

static void lamp_bright_cb(void *o, int32_t v)
{
    lv_led_set_brightness((lv_obj_t *)o, (uint8_t)v);
}

// หลอด RUN หายใจเบา ๆ ตอนเครื่องเดิน -- เห็นแล้วรู้ทันทีว่าจอไม่ได้ค้าง
static void run_pulse(bool on)
{
    lv_anim_delete(s_runLamp, lamp_bright_cb);
    if (!on) return;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_runLamp);
    lv_anim_set_exec_cb(&a, lamp_bright_cb);
    lv_anim_set_values(&a, 140, 255);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

// ---------------------------------------------------------------------------
void ui_home_build(lv_obj_t *par)
{
    // ================= การ์ดเทียบ O1 vs O2 (มุมบนซ้ายที่ว่างอยู่) =================
    lv_obj_t *xc = ui_card(par, 8, 6, 142, 66);
    make_text(xc, 12, 4, &lv_font_montserrat_14, C_TEXT_DIM, "CROSS-CHECK");

    s_xcLamp = make_lamp(xc, 12, 27, 12, 8, C_OK);
    s_xcTxt  = make_text(xc, 30, 24, &lv_font_montserrat_16, C_TEXT_MUTE, "--");
    s_xcDiff = make_text(xc, 12, 46, &lv_font_montserrat_14, C_TEXT_MUTE, "DIFF   --");

    // ================= ตัวถัง =================
    s_tank = raw_box(par, TANK_X, TANK_Y, TANK_W, TANK_H, 10);
    ui_grad(s_tank, C_PANEL_TOP, C_PANEL_BOT);
    lv_obj_set_style_border_width(s_tank, 2, 0);
    lv_obj_set_style_border_color(s_tank, C_LINE, 0);

    // ถังมีขอบ 2px -> พื้นที่ข้างในกว้าง TANK_W-4 = 92 ไม่ใช่ 96
    make_text_mid(s_tank, 46, 6, 88, &lv_font_montserrat_14, C_TEXT_DIM, "FERTILIZER\nNOx");

    // ช่องแก๊ส -- สีบอกโซนที่วาล์วกำลังคุมอยู่
    s_gas = raw_box(s_tank, 5, 26, TANK_W - 14, 54, 6);
    ui_grad(s_gas, C_ZONE_OFF, C_ZONE_OFF);
    lv_obj_set_style_bg_opa(s_gas, LV_OPA_30, 0);

    // เส้นผิวของเหลว
    lv_obj_t *surf = raw_box(s_tank, 5, 82, TANK_W - 14, 2, 0);
    ui_grad(surf, C_MED_LIQ, C_MED_LIQ);

    // ของเหลว -- ฟ้าน้ำอ่อน (ธีมสว่าง) ให้ตัวอักษรเข้มด้านในอ่านออก
    lv_obj_t *liq = raw_box(s_tank, 5, 84, TANK_W - 14, 84, 6);
    ui_grad(liq, lv_color_hex(0xCBE4F7), lv_color_hex(0x9FCDEE));

    // ฟองแก๊ส -- ต้องสร้างก่อนตัวหนังสือ ไม่งั้นฟองจะลอยทับค่าความดันจนอ่านไม่ออก
    for (uint8_t i = 0; i < 3; i++) {
        s_bub[i] = raw_box(liq, 14 + i * 26, 76, 5, 5, LV_RADIUS_CIRCLE);
        ui_grad(s_bub[i], lv_color_lighten(C_MED_GAS, 80), C_MED_GAS);
        lv_obj_set_style_bg_opa(s_bub[i], LV_OPA_70, 0);      // ui_grad ตั้ง COVER -> ต้องทับทีหลัง
        lv_obj_add_flag(s_bub[i], LV_OBJ_FLAG_HIDDEN);
    }

    make_text_mid(liq, 41, 26, 78, &lv_font_montserrat_14, C_TEXT_DIM, "PRESSURE");
    s_press = make_text_mid(liq, 41, 44, 78, &lv_font_montserrat_16, C_TEXT, "--");

    // ================= ท่อระบายแก๊ส + เซนเซอร์ O1 / O2 =================
    path_begin(s_path[P_VENT], C_MED_GAS);
    path_seg(s_path[P_VENT], pipe_v(par, VENT_X0, VENT_Y, TANK_Y));
    path_seg(s_path[P_VENT], pipe_h(par, VENT_X0, VENT_X1, VENT_Y));
    path_dots(s_path[P_VENT], par, VENT_X0 + 40, VENT_X1 - 10, VENT_Y, false, 2);  // 238 px

    make_text(par, VENT_X1 + 2, VENT_Y - 9,
              &lv_font_montserrat_14, C_MED_GAS, LV_SYMBOL_RIGHT);
    // ป้ายต้องอยู่ "ใต้" ท่อ -- ท่ออยู่ y=24 ถ้าวางเหนือท่อจะได้ y ติดลบ = หลุดจอ
    make_text(par, 420, VENT_Y + 8, &lv_font_montserrat_14, C_TEXT_MUTE, "VENT");

    // ตัวเซนเซอร์คร่อมท่อ + ค่าที่อ่านได้ลอยอยู่เหนือมัน
    s_o1Mark = raw_box(par, O1_X - 7, VENT_Y - 6, 14, 12, 3);
    ui_grad(s_o1Mark, C_PANEL_HI_TOP, C_PANEL_HI_BOT);
    lv_obj_set_style_border_width(s_o1Mark, 2, 0);
    lv_obj_set_style_border_color(s_o1Mark, C_O2A, 0);

    s_o2Mark = raw_box(par, O2_X - 7, VENT_Y - 6, 14, 12, 3);
    ui_grad(s_o2Mark, C_PANEL_HI_TOP, C_PANEL_HI_BOT);
    lv_obj_set_style_border_width(s_o2Mark, 2, 0);
    lv_obj_set_style_border_color(s_o2Mark, C_O2B, 0);

    s_o1Val = make_text_mid(par, O1_X, 1, 52, &lv_font_montserrat_14, C_O2A, "--");
    s_o2Val = make_text_mid(par, O2_X, 1, 52, &lv_font_montserrat_14, C_O2B, "--");

    // ================= แก๊สหมุนเวียน: ถัง -> CB -> MIXER  (ตัด FILTER ออก) ========
    make_equip(par, E_CB, CB_CX, CB_CY, UI_NAME_CB);

    path_begin(s_path[P_CB], C_MED_GAS);
    path_seg(s_path[P_CB], pipe_v(par, BRANCH_X, VENT_Y, CB_CY));            // vent ลงถึงระดับ CB
    path_seg(s_path[P_CB], pipe_h(par, BRANCH_X, CB_CX - EQ_R, CB_CY));      // -> เข้า CB
    path_seg(s_path[P_CB], pipe_v(par, CB_CX, CB_CY + EQ_R, MIX_Y));
    path_dots(s_path[P_CB], par, CB_CY + EQ_R, MIX_Y, CB_CX, true, 2);   // 65 px

    // ================= อากาศเข้า: AIR -> FB -> MIXER =================
    make_equip(par, E_FB, FB_CX, FB_CY, UI_NAME_FB);

    path_begin(s_path[P_FB], C_MED_GAS);
    path_seg(s_path[P_FB], pipe_h(par, FB_CX + EQ_R, 474, FB_CY));
    path_seg(s_path[P_FB], pipe_h(par, MIX_X + MIX_W, FB_CX - EQ_R, FB_CY));
    path_dots(s_path[P_FB], par, FB_CX - EQ_R, MIX_X + MIX_W, FB_CY, false, 1);  // 21 px -- จุดเดียวพอ

    // 446 ไม่ใช่ 440 -- FB กินถึง x=443 วางที่ 440 จะไปทับหัววงกลม
    make_text(par, 446, FB_CY - 26, &lv_font_montserrat_14, C_TEXT_MUTE, "AIR");

    // ================= N2: N2GEN -> RV -> MIXER =================
    make_unit(par, N2_X, N2_Y, N2_W, N2_H, "N2 GEN", C_MED_N2);

    path_begin(s_path[P_RV], C_MED_N2);
    path_seg(s_path[P_RV], pipe_v(par, RV_CX, RV_CY + RV_H / 2, N2_Y));
    path_seg(s_path[P_RV], pipe_v(par, RV_CX, MIX_Y + MIX_H, RV_CY - RV_H / 2));
    path_dots(s_path[P_RV], par, N2_Y, RV_CY - RV_H / 2, RV_CX, true, 1);   // 44 px -- จุดเดียวพอ

    // สัญลักษณ์วาล์ว -- สามเหลี่ยม 2 อันประกบปลาย (โบว์ไท) วาดด้วย lv_line
    // จุดต้องไม่ติดลบ ไม่งั้นเส้นจะโดนตัดที่ขอบ object
    static const lv_point_precise_t RV_PTS[] = {
        { 1, 1 }, { 1, RV_H + 1 }, { RV_W / 2 + 1, RV_H / 2 + 1 },
        { RV_W + 1, RV_H + 1 }, { RV_W + 1, 1 }, { RV_W / 2 + 1, RV_H / 2 + 1 }, { 1, 1 }
    };
    s_rvLine = lv_line_create(par);
    lv_line_set_points(s_rvLine, RV_PTS, sizeof(RV_PTS) / sizeof(RV_PTS[0]));
    lv_obj_set_size(s_rvLine, RV_W + 3, RV_H + 3);
    lv_obj_set_pos(s_rvLine, RV_CX - RV_W / 2 - 1, RV_CY - RV_H / 2 - 1);
    lv_obj_set_style_line_width(s_rvLine, 2, 0);
    lv_obj_set_style_line_color(s_rvLine, C_ZONE_OFF, 0);
    lv_obj_set_style_line_rounded(s_rvLine, true, 0);

    make_text(par, RV_CX - EQ_R - 30, RV_CY - 9, &lv_font_montserrat_14, C_TEXT, UI_NAME_RV);
    s_rvPct  = make_text(par, RV_CX + 24, RV_CY - 20, &lv_font_montserrat_16, C_TEXT, "0 %");
    s_rvZone = make_text(par, RV_CX + 24, RV_CY + 2,  &lv_font_montserrat_14, C_ZONE_OFF, "OFF");

    // แถบเติม %: O2 สั่งเปิดวาล์วมากแค่ไหน -> เห็นเป็นภาพ (เติมลื่นตอนค่าเปลี่ยน)
    s_rvBar = lv_bar_create(par);
    lv_obj_set_size(s_rvBar, 72, 7);
    lv_obj_set_pos(s_rvBar, RV_CX + 24, RV_CY + 22);
    lv_bar_set_range(s_rvBar, 0, 100);
    lv_bar_set_value(s_rvBar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_rvBar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_rvBar, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_rvBar, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_radius(s_rvBar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_rvBar, C_ZONE_OFF, LV_PART_INDICATOR);
    // ป้ายบอกที่มา: วาล์วนี้ถูกคุมด้วยค่า O2
    make_text(par, RV_CX + 24, RV_CY + 33, &lv_font_montserrat_14, C_TEXT_DIM, "O2 CTRL");

    // ================= MIXER -> ถัง =================
    make_unit(par, MIX_X, MIX_Y, MIX_W, MIX_H, "MIXER", C_MED_GAS);

    path_begin(s_path[P_MIX], C_MED_GAS);
    path_seg(s_path[P_MIX], pipe_h(par, TANK_X + TANK_W, MIX_X, MIX_CY));
    path_dots(s_path[P_MIX], par, MIX_X, TANK_X + TANK_W, MIX_CY, false, 2);  // 72 px

    // ================= CP: ของเหลวหมุนเวียน (ก้นถัง -> CP -> หัวถัง) =================
    make_equip(par, E_CP, CP_CX, CP_CY, UI_NAME_CP);

    path_begin(s_path[P_CP], C_MED_LIQ);
    path_seg(s_path[P_CP], pipe_h(par, CP_CX, TANK_X, CP_BOT_Y));               // ถังล่าง -> ซ้าย
    path_seg(s_path[P_CP], pipe_v(par, CP_CX, CP_CY + EQ_R, CP_BOT_Y));         // ขึ้นเข้า CP
    path_seg(s_path[P_CP], pipe_v(par, CP_CX, CP_TOP_Y, CP_CY - EQ_R));         // ออกจาก CP
    path_seg(s_path[P_CP], pipe_h(par, CP_CX, TANK_X, CP_TOP_Y));               // -> ปากถังบน
    path_dots(s_path[P_CP], par, CP_CX, TANK_X, CP_TOP_Y, false, 2);  // 64 px

    // ================= FP: ป้อนของเหลวเข้า =================
    make_unit(par, FEED_X, FEED_Y, FEED_W, FEED_H, "FEED", C_MED_LIQ);
    make_equip(par, E_FP, FP_CX, FP_CY, UI_NAME_FP);

    path_begin(s_path[P_FP], C_MED_LIQ);
    path_seg(s_path[P_FP], pipe_h(par, FEED_X + FEED_W, FP_CX - EQ_R, FP_CY));
    path_seg(s_path[P_FP], pipe_h(par, FP_CX + EQ_R, FP_UP_X, FP_CY));
    path_seg(s_path[P_FP], pipe_v(par, FP_UP_X, TANK_Y + TANK_H, FP_CY));
    path_dots(s_path[P_FP], par, FP_CX + EQ_R, FP_UP_X, FP_CY, false, 1);  // 49 px -- จุดเดียวพอ

    // ================= ปุ๋ยออกทางก้นถัง =================
    make_unit(par, COMP_X, COMP_Y, COMP_W, COMP_H, "FERTILIZER", C_MED_LIQ);

    path_begin(s_path[P_OUT], C_MED_LIQ);
    path_seg(s_path[P_OUT], pipe_v(par, OUT_X, TANK_Y + TANK_H, COMP_Y));
    path_dots(s_path[P_OUT], par, TANK_Y + TANK_H, COMP_Y, OUT_X, true, 1);  // 26 px -- จุดเดียวพอ

    // ================= แถบสถานะล่าง =================
    lv_obj_t *st = ui_card(par, 4, STAT_Y, 472, STAT_H);

    s_runLamp = make_lamp(st, 12, 6, 14, 12, C_OFF);
    s_runLbl  = make_text(st, 34, 3, &lv_font_montserrat_16, C_TEXT_MUTE, "OFFLINE");

    s_modeLbl = lv_label_create(st);
    lv_label_set_text(s_modeLbl, "--");
    lv_obj_set_style_text_font(s_modeLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_modeLbl, C_TEXT_MUTE, 0);
    lv_obj_align(s_modeLbl, LV_ALIGN_RIGHT_MID, -12, 0);
}

// ---------------------------------------------------------------------------
//  อัปเดต -- ทุกค่าผ่าน cache ก่อน เขียนเฉพาะตอนเปลี่ยนจริง
// ---------------------------------------------------------------------------
void ui_home_update()
{
    static bool     c_up    = true;          // true ไว้ -> รอบแรก linkChanged = true
    static uint8_t  c_relay = 0xFF;
    static uint8_t  c_zone  = 0xFF;
    static uint8_t  c_valve = 0xFF;
    static uint8_t  c_run   = 0xFF;
    static uint8_t  c_mode  = 0xFF;
    static uint32_t c_elap  = 0xFFFFFFFF;
    static float    c_o1 = -999.0f, c_o2 = -999.0f, c_pr = -999.0f;
    static float    c_diff  = -999.0f;
    static uint8_t  c_xc    = 0xFF;          // 0 = ตรงกัน, 1 = ไม่ตรง, 2 = อ่านไม่ได้
    static uint8_t  c_o1s   = 0xFF;          // สถานะตัวเซนเซอร์ 0=ok 1=พัง 2=ไม่มีลิงก์
    static uint8_t  c_o2s   = 0xFF;
    static uint8_t  c_gasOpa = 0xFF;         // ขั้นความทึบของแก๊สในถัง (ตามระดับ O2)
    static bool     c_bub   = false;

    bool    up    = g_app.linkUp;
    uint8_t e     = g_app.errflags;
    uint8_t relay = up ? g_app.relayBits : 0;
    uint8_t zone  = up ? g_app.rvZone    : LNK_RVZ_OFF;
    uint8_t valve = up ? g_app.valve     : 0;
    uint8_t run   = up ? g_app.runState  : LNK_RUN_IDLE;
    uint8_t mode  = g_app.mode;
    bool    linkChanged = (up != c_up);

    bool o1ok = up && !(e & LNK_F_O2A_FAIL);
    bool o2ok = up && !(e & LNK_F_O2B_FAIL);
    bool prok = up && !(e & LNK_F_PR_FAIL);

    bool cp = (relay >> CFG_DEV_CP) & 1;
    bool cb = (relay >> CFG_DEV_CB) & 1;
    bool fb = (relay >> CFG_DEV_FB) & 1;
    bool fp = (relay >> CFG_DEV_FP) & 1;
    bool rv = (valve > 0);

    // --- โซน -> ชื่อ + สี ---
    const char *zn;
    lv_color_t  zc;
    switch (zone) {
    case LNK_RVZ_MAXOUT: zn = "MAX OUT";  zc = C_ZONE_MAXOUT; break;
    case LNK_RVZ_ROUGH:  zn = "ROUGH";    zc = C_ZONE_ROUGH;  break;
    case LNK_RVZ_MEDIUM: zn = "MEDIUM";   zc = C_ZONE_MEDIUM; break;
    case LNK_RVZ_FINE:   zn = "FINE";     zc = C_ZONE_FINE;   break;
    default:             zn = "OFF";      zc = C_ZONE_OFF;    break;
    }

    // ===== วงกลมอุปกรณ์ -- แตะเฉพาะตอน relay เปลี่ยนจริง =====
    if (relay != c_relay) {
        c_relay = relay;
        equip_set(E_CP, cp);
        equip_set(E_CB, cb);
        equip_set(E_FB, fb);
        equip_set(E_FP, fp);
    }

    // ===== เส้นทางของไหล =====
    // path_set() แพง (ทาสีท่อทุกท่อน + สตาร์ท/หยุดอนิเมชัน) จึงมี guard เช็ค .on/.ms
    // ให้เอง -> เรียกเช็คทุก tick ได้ ไม่ต้องมีเงื่อนไขซ้อนข้างนอกอีก
    {
        bool gasIn = cb || fb || rv;         // mixer มีของไหลเข้าถังไหม
        bool vent  = cb || rv;               // อัดแก๊ส/N2 เข้า -> แก๊สส่วนเกินระบายออก

        // N2 ไหลเร็วตามเปอร์เซ็นต์ที่วาล์วเปิดจริง (เปิดมาก = แรงดันตกคร่อมมาก = ไหลแรง)
        uint16_t n2ms = MS_N2[rv ? rv_bucket(valve) : 1];

        if (s_path[P_CP].on   != cp)    path_set(s_path[P_CP],   cp,    MS_LIQ);
        if (s_path[P_FP].on   != fp)    path_set(s_path[P_FP],   fp,    MS_LIQ);
        if (s_path[P_OUT].on  != fp)    path_set(s_path[P_OUT],  fp,    MS_LIQ);  // ป้อนเข้า = ปุ๋ยล้นออก
        if (s_path[P_CB].on   != cb)    path_set(s_path[P_CB],   cb,    MS_GAS);
        if (s_path[P_FB].on   != fb)    path_set(s_path[P_FB],   fb,    MS_GAS);
        if (s_path[P_MIX].on  != gasIn) path_set(s_path[P_MIX],  gasIn, MS_GAS);
        if (s_path[P_VENT].on != vent)  path_set(s_path[P_VENT], vent,  MS_GAS);

        // RV: รีสตาร์ตทั้งตอน on/off พลิก และตอนความเร็วข้ามช่วง
        if (s_path[P_RV].on != rv || (rv && s_path[P_RV].ms != n2ms))
            path_set(s_path[P_RV], rv, n2ms);

        // ฟองแก๊ส -- ขึ้นเฉพาะตอนมีแก๊สถูกอัดเข้าถังจริง ๆ ไม่ใช่ขึ้นตลอดเวลา
        if (c_bub != gasIn) { c_bub = gasIn; bubbles_set(gasIn); }
    }

    // ===== โซนควบคุม -- แก๊สในถัง + วาล์ว =====
    bool zoneChanged = (zone != c_zone);
    if (zoneChanged) {
        c_zone = zone;
        lv_obj_set_style_border_color(s_tank, (zone == LNK_RVZ_OFF) ? C_LINE : zc, 0);
        lv_obj_set_style_line_color(s_rvLine, zc, 0);
        lv_label_set_text(s_rvZone, zn);
        lv_obj_set_style_text_color(s_rvZone, zc, 0);
        lv_obj_set_style_bg_color(s_rvBar, zc, LV_PART_INDICATOR);
    }

    // แก๊สในถัง:  สี = โซนที่กำลังคุมอยู่   ความทึบ = ระดับ O2 ที่ยังเหลือจริง
    //   O2 สูง = ยังมีออกซิเจนเยอะ -> ทึบ
    //   O2 ต่ำ = ไล่ออกไปได้เยอะแล้ว -> จางลง  (เห็นกระบวนการทำงานด้วยตา)
    // แบ่งเป็น 5 ขั้น ไม่งั้นเซนเซอร์ขยับทศนิยมทีนึงก็ invalidate ทีนึง
    uint8_t go = 0;
    if (o1ok && g_app.o2a > 0.0f) {
        int st = (int)(g_app.o2a / 5.0f);
        go = (uint8_t)(st > 4 ? 4 : st);
    }
    if (zoneChanged || go != c_gasOpa) {
        static const lv_opa_t GAS_OPA[5] = { LV_OPA_20, LV_OPA_30, LV_OPA_40, LV_OPA_50, LV_OPA_60 };
        c_gasOpa = go;
        ui_grad(s_gas, zc, zc);
        lv_obj_set_style_bg_opa(s_gas, GAS_OPA[go], 0);   // ui_grad ตั้ง COVER -> ต้องทับทีหลัง
    }

    // ===== เปอร์เซ็นต์วาล์ว =====
    if (valve != c_valve || linkChanged) {
        c_valve = valve;
        lv_label_set_text_fmt(s_rvPct, "%u %%", (unsigned)valve);
        lv_bar_set_value(s_rvBar, valve, LV_ANIM_ON);   // เติมลื่น -> เห็น O2 ดันวาล์วขึ้น/ลง
    }

    // ===== ค่าเซนเซอร์บนท่อ =====
    // *** ห้ามใช้ %f ใน lv_label_set_text_fmt! ***
    // lv_conf.h ตั้ง LV_USE_FLOAT=0 -> vsnprintf ภายใน LVGL ไม่รู้จัก %f
    // -> ไม่กิน vararg -> ตัวถัดไป (%s) ไปหยิบ bits ของ float มาเป็น pointer
    // -> เคยแครชจริง (Guru Meditation LoadProhibited ตอน pressure=0.000 = NULL เป๊ะ)
    // ทางแก้: ฟอร์แมตด้วย snprintf ของ ESP32 (รองรับ %f เต็ม) แล้วค่อยส่ง string
    char fbuf[48];

    float o1 = o1ok ? g_app.o2a : -999.0f;
    if (fabsf(o1 - c_o1) > 0.004f) {
        c_o1 = o1;
        if (o1ok) { snprintf(fbuf, sizeof(fbuf), "%.2f", (double)g_app.o2a);
                    lv_label_set_text(s_o1Val, fbuf); }
        else      lv_label_set_text(s_o1Val, "--");
    }

    float o2 = o2ok ? g_app.o2b : -999.0f;
    if (fabsf(o2 - c_o2) > 0.004f) {
        c_o2 = o2;
        if (o2ok) { snprintf(fbuf, sizeof(fbuf), "%.2f", (double)g_app.o2b);
                    lv_label_set_text(s_o2Val, fbuf); }
        else      lv_label_set_text(s_o2Val, "--");
    }

    // สีขอบตัวเซนเซอร์บนท่อ: ฟ้า/ม่วง = อ่านได้ / แดง = อ่านไม่ได้ / เทา = ไม่มีลิงก์
    // (ลิงก์หลุดไม่ใช่ความผิดของเซนเซอร์ -- ห้ามโชว์แดงหลอกให้ไปรื้อของ)
    uint8_t o1s = !up ? 2 : (o1ok ? 0 : 1);
    if (o1s != c_o1s) {
        c_o1s = o1s;
        lv_obj_set_style_border_color(s_o1Mark,
            o1s == 0 ? C_O2A : (o1s == 1 ? C_ALARM : C_TEXT_MUTE), 0);
    }
    uint8_t o2s = !up ? 2 : (o2ok ? 0 : 1);
    if (o2s != c_o2s) {
        c_o2s = o2s;
        lv_obj_set_style_border_color(s_o2Mark,
            o2s == 0 ? C_O2B : (o2s == 1 ? C_ALARM : C_TEXT_MUTE), 0);
    }

    float pr = prok ? g_app.pressure : -999.0f;
    if (fabsf(pr - c_pr) > 0.004f) {
        c_pr = pr;
        if (prok) { snprintf(fbuf, sizeof(fbuf), "%.2f %s", (double)g_app.pressure, UI_UNIT_PRESSURE);
                    lv_label_set_text(s_press, fbuf); }
        else      lv_label_set_text(s_press, "--");
    }

    // ===== การ์ดเทียบ O1 vs O2 =====
    uint8_t xc = (!o1ok || !o2ok) ? 2 : ((e & LNK_F_O2_MISMATCH) ? 1 : 0);
    if (xc != c_xc || linkChanged) {
        c_xc = xc;

        const char *t;  lv_color_t c;
        switch (xc) {
        case 0:  t = "MATCH";    c = C_OK;    break;
        case 1:  t = "MISMATCH"; c = C_ALARM; break;
        default: t = "NO DATA";  c = C_OFF;   break;
        }
        lamp_set(s_xcLamp, up && xc != 2, c);
        lv_label_set_text(s_xcTxt, t);
        lv_obj_set_style_text_color(s_xcTxt, xc == 2 ? C_TEXT_MUTE : c, 0);
    }

    // *** ค่าต่างต้องเทียบกับ "ค่าต่างครั้งก่อน" ไม่ใช่เทียบ O1 กับ O2 ***
    //
    // เขียนเป็น fabsf(o1 - o2) > 0.004 เฉย ๆ = เทียบเซนเซอร์สองตัวเข้าหากัน
    // ซึ่งของจริงไม่มีทางเท่ากันเป๊ะ -> เงื่อนไขจริงทุก tick -> วาดการ์ดใหม่ทุก 100 ms
    // -> invalidate ถี่ -> lv_timer_handler ตามไม่ทัน -> ตัวอ่าน touch ไม่ได้รัน -> จอกดไม่ได้
    // (เคยพังด้วยเหตุนี้มาแล้ว -- ทุก "ค่า" ที่โชว์บนจอต้องมี cache ของตัวเอง)
    float diff = (xc == 2) ? -999.0f : fabsf(o1 - o2);
    if (fabsf(diff - c_diff) > 0.004f) {
        c_diff = diff;
        if (xc == 2) lv_label_set_text(s_xcDiff, "DIFF   --");
        else         { snprintf(fbuf, sizeof(fbuf), "DIFF   %.2f %s", (double)diff, UI_UNIT_O2);
                       lv_label_set_text(s_xcDiff, fbuf); }
    }

    // ===== สถานะเดินเครื่อง =====
    if (run != c_run || g_app.elapsed != c_elap || linkChanged) {
        bool wasRunning = (c_run == LNK_RUN_RUNNING);
        c_run  = run;
        c_elap = g_app.elapsed;

        if (!up) {
            lv_label_set_text(s_runLbl, "OFFLINE");
            lv_obj_set_style_text_color(s_runLbl, C_TEXT_MUTE, 0);
        } else if (run == LNK_RUN_RUNNING) {
            uint32_t t = g_app.elapsed;
            lv_label_set_text_fmt(s_runLbl, "RUNNING   %02lu:%02lu:%02lu",
                                  (unsigned long)(t / 3600),
                                  (unsigned long)((t / 60) % 60),
                                  (unsigned long)(t % 60));
            lv_obj_set_style_text_color(s_runLbl, C_OK, 0);
        } else {
            lv_label_set_text(s_runLbl, "IDLE");
            lv_obj_set_style_text_color(s_runLbl, C_TEXT_DIM, 0);
        }

        // แตะหลอด/อนิเมชันเฉพาะตอนสถานะพลิก ไม่งั้นจังหวะหายใจจะถูกรีสตาร์ตทุกวินาที
        bool running = up && (run == LNK_RUN_RUNNING);
        if (running != wasRunning || linkChanged) {
            lamp_set(s_runLamp, running, running ? C_OK : C_OFF);
            run_pulse(running);
        }
    }

    // ===== โหมด =====
    if (mode != c_mode || linkChanged) {
        c_mode = mode;
        bool autoMode = (mode == LNK_MODE_AUTO);
        lv_label_set_text(s_modeLbl, !up ? "--" : (autoMode ? "AUTO" : "MANUAL"));
        lv_obj_set_style_text_color(s_modeLbl,
                                    !up ? C_TEXT_MUTE : (autoMode ? C_NEON : C_WARN), 0);
    }

    c_up = up;
}
