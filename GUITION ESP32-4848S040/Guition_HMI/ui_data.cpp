// ============================================================================
//  ui_data.cpp  --  หน้าเกจค่าเซนเซอร์
//
//  O2 #1 (ตัวหลักที่ใช้คุมวาล์ว) เป็นเกจใหญ่ -- อ่านจากไกลได้
//  O2 #2 กับ Pressure เป็นเกจรอง
//  มีแถบเทียบ O1 กับ O2 ให้เห็นว่าต่างกันเท่าไร (ถ้าต่างมาก = ต้องสอบเทียบ)
// ============================================================================
#include "ui.h"
#include "ui_theme.h"
#include "app_state.h"
#include "link_protocol.h"

#include <math.h>

static lv_obj_t *s_arcMain;
static lv_obj_t *s_valMain;
static lv_obj_t *s_zoneMain;

static lv_obj_t *s_arcB;
static lv_obj_t *s_valB;

static lv_obj_t *s_arcP;
static lv_obj_t *s_valP;

static lv_obj_t *s_diffLbl;
static lv_obj_t *s_diffBar;
static lv_obj_t *s_rvLbl;
static lv_obj_t *s_rvBar;

// ---------------------------------------------------------------------------
static lv_obj_t *make_arc(lv_obj_t *par, int x, int y, int size, lv_color_t c)
{
    lv_obj_t *a = lv_arc_create(par);
    lv_obj_set_size(a, size, size);
    lv_obj_set_pos(a, x, y);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, 0, 1000);
    lv_arc_set_value(a, 0);

    lv_obj_remove_style(a, NULL, LV_PART_KNOB);          // ไม่ให้มีปุ่มลาก
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);         // เกจ ไม่ใช่ตัวควบคุม

    lv_obj_set_style_arc_color(a, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(a, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

// ---------------------------------------------------------------------------
void ui_data_build(lv_obj_t *par)
{
    // ================= เกจใหญ่: O2 #1 (ตัวคุมวาล์ว) =================
    lv_obj_t *card = ui_card(par, 8, 6, 464, 172);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, "O2 SENSOR 1   (CONTROL INPUT)");
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, C_O2A, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 14, 10);

    s_arcMain = make_arc(card, 16, 30, 130, C_O2A);

    // ค่า (font 28 -> สูง ~34) ต้องจบก่อนบรรทัดหน่วย ไม่งั้นตัวหนังสือทับกัน
    s_valMain = lv_label_create(card);
    lv_label_set_text(s_valMain, "--");
    lv_obj_set_style_text_font(s_valMain, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_valMain, C_TEXT, 0);
    lv_obj_set_pos(s_valMain, 44, 78);          // 78..112

    lv_obj_t *u = lv_label_create(card);
    lv_label_set_text(u, UI_UNIT_O2);
    lv_obj_set_style_text_font(u, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(u, C_TEXT_DIM, 0);
    lv_obj_set_pos(u, 74, 115);                 // 115..132

    // โซนควบคุม + วาล์ว (ฝั่งขวาของการ์ด)
    s_zoneMain = lv_label_create(card);
    lv_label_set_text(s_zoneMain, "RV OFF");
    lv_obj_set_style_text_font(s_zoneMain, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_zoneMain, C_ZONE_OFF, 0);
    lv_obj_set_pos(s_zoneMain, 170, 46);

    s_rvLbl = lv_label_create(card);
    lv_label_set_text(s_rvLbl, "VALVE  0%");
    lv_obj_set_style_text_font(s_rvLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_rvLbl, C_TEXT, 0);
    lv_obj_set_pos(s_rvLbl, 170, 78);

    s_rvBar = lv_bar_create(card);
    lv_obj_set_size(s_rvBar, 270, 10);
    lv_obj_set_pos(s_rvBar, 170, 104);
    lv_bar_set_range(s_rvBar, 0, 100);
    ui_grad(s_rvBar, C_SUNK_TOP, C_SUNK_BOT, LV_PART_MAIN);       // ราง = ร่องจม
    ui_tint(s_rvBar, C_NEON, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_rvBar, 5, LV_PART_MAIN);
    lv_obj_set_style_radius(s_rvBar, 5, LV_PART_INDICATOR);

    // ================= เกจรอง: O2 #2 =================
    lv_obj_t *c2 = ui_card(par, 8, 186, 228, 120);

    lv_obj_t *t2 = lv_label_create(c2);
    lv_label_set_text(t2, "O2 SENSOR 2   (CHECK)");
    lv_obj_set_style_text_font(t2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t2, C_O2B, 0);
    lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 12, 8);

    s_arcB = make_arc(c2, 12, 30, 78, C_O2B);

    s_valB = lv_label_create(c2);
    lv_label_set_text(s_valB, "--");
    lv_obj_set_style_text_font(s_valB, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_valB, C_TEXT, 0);
    lv_obj_set_pos(s_valB, 104, 52);

    // ================= เกจรอง: Pressure =================
    lv_obj_t *c3 = ui_card(par, 244, 186, 228, 120);

    lv_obj_t *t3 = lv_label_create(c3);
    lv_label_set_text(t3, "PRESSURE");
    lv_obj_set_style_text_font(t3, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t3, C_PRESS, 0);
    lv_obj_align(t3, LV_ALIGN_TOP_LEFT, 12, 8);

    s_arcP = make_arc(c3, 12, 30, 78, C_PRESS);

    s_valP = lv_label_create(c3);
    lv_label_set_text(s_valP, "--");
    lv_obj_set_style_text_font(s_valP, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_valP, C_TEXT, 0);
    lv_obj_set_pos(s_valP, 104, 52);

    // ================= เทียบ O1 กับ O2 =================
    // ถ้า 2 ตัวนี้ค่าห่างกันมาก แปลว่าตัวใดตัวหนึ่งเพี้ยน -> ลูปควบคุมเชื่อถือไม่ได้
    // y 310..362  (ต้องจบก่อน UI_BODY_H = 366 ไม่งั้นไปทับ nav bar)
    lv_obj_t *c4 = ui_card(par, 8, 310, 464, 52);

    s_diffLbl = lv_label_create(c4);
    lv_label_set_text(s_diffLbl, "SENSOR AGREEMENT   --");
    lv_obj_set_style_text_font(s_diffLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_diffLbl, C_TEXT_DIM, 0);
    lv_obj_align(s_diffLbl, LV_ALIGN_TOP_LEFT, 14, 7);

    s_diffBar = lv_bar_create(c4);
    lv_obj_set_size(s_diffBar, 436, 8);
    lv_obj_align(s_diffBar, LV_ALIGN_BOTTOM_LEFT, 14, -9);
    lv_bar_set_range(s_diffBar, 0, 100);
    ui_grad(s_diffBar, C_SUNK_TOP, C_SUNK_BOT, LV_PART_MAIN);     // ราง = ร่องจม
    ui_tint(s_diffBar, C_OK, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_diffBar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(s_diffBar, 4, LV_PART_INDICATOR);
}

// ---------------------------------------------------------------------------
static void set_gauge(lv_obj_t *arc, lv_obj_t *lbl, float v, float vmax, bool ok, uint8_t dp)
{
    // %f ใน set_text_fmt ใช้ไม่ได้ (LV_USE_FLOAT=0) -- snprintf ก่อนเสมอ
    if (ok) { char b[24]; snprintf(b, sizeof(b), "%.*f", (int)dp, (double)v);
              lv_label_set_text(lbl, b); }
    else    lv_label_set_text(lbl, "--");

    float t = v / vmax;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    lv_arc_set_value(arc, (int32_t)(t * 1000));
}

// อัปเดตเฉพาะตอนค่าเปลี่ยนจริง -- ดูเหตุผลใน ui.cpp (invalidate ถล่ม -> touch ตาย)
void ui_data_update()
{
    static bool    c_up   = true;
    static uint8_t c_zone = 0xFF;
    static uint8_t c_val  = 0xFF;
    static float   c_o2a  = -999.0f, c_o2b = -999.0f, c_pt = -999.0f, c_diff = -999.0f;

    bool    up = g_app.linkUp;
    uint8_t e  = g_app.errflags;

    bool o1ok = up && !(e & LNK_F_O2A_FAIL);
    bool o2ok = up && !(e & LNK_F_O2B_FAIL);
    bool ptok = up && !(e & LNK_F_PR_FAIL);

    float o2a = o1ok ? g_app.o2a      : -1.0f;
    float o2b = o2ok ? g_app.o2b      : -1.0f;
    float pt  = ptok ? g_app.pressure : -1.0f;

    if (fabsf(o2a - c_o2a) > 0.004f) { c_o2a = o2a; set_gauge(s_arcMain, s_valMain, g_app.o2a,      UI_O2_MAX,    o1ok, 2); }
    if (fabsf(o2b - c_o2b) > 0.004f) { c_o2b = o2b; set_gauge(s_arcB,    s_valB,    g_app.o2b,      UI_O2_MAX,    o2ok, 2); }
    if (fabsf(pt  - c_pt)  > 0.004f) { c_pt  = pt;  set_gauge(s_arcP,    s_valP,    g_app.pressure, UI_PRESS_MAX, ptok, 2); }

    // --- โซน ---
    uint8_t zone = up ? g_app.rvZone : LNK_RVZ_OFF;
    lv_color_t zc;
    if (zone != c_zone) {
        c_zone = zone;
        const char *zn;
        switch (zone) {
        case LNK_RVZ_MAXOUT: zn = "RV MAX OUT";  zc = C_ZONE_MAXOUT; break;
        case LNK_RVZ_ROUGH:  zn = "RV ROUGH";    zc = C_ZONE_ROUGH;  break;
        case LNK_RVZ_MEDIUM: zn = "RV MEDIUM";   zc = C_ZONE_MEDIUM; break;
        case LNK_RVZ_FINE:   zn = "RV FINE";     zc = C_ZONE_FINE;   break;
        default:             zn = "RV OFF";      zc = C_ZONE_OFF;    break;
        }
        lv_label_set_text(s_zoneMain, zn);
        lv_obj_set_style_text_color(s_zoneMain, zc, 0);
        ui_tint(s_rvBar, zc, LV_PART_INDICATOR);
    }

    // --- วาล์ว ---
    uint8_t v = up ? g_app.valve : 0;
    if (v != c_val || up != c_up) {
        c_val = v;
        lv_label_set_text_fmt(s_rvLbl, "VALVE  %u%%", v);
        lv_bar_set_value(s_rvBar, v, LV_ANIM_ON);
    }

    // --- เทียบ O1 vs O2 ---
    float diff = (o1ok && o2ok) ? fabsf(g_app.o2a - g_app.o2b) : -1.0f;
    if (fabsf(diff - c_diff) > 0.004f || up != c_up) {
        c_diff = diff;

        if (diff >= 0.0f) {
            bool bad = (e & LNK_F_O2_MISMATCH) != 0;
            // ห้าม %f ใน set_text_fmt (LV_USE_FLOAT=0 -> vararg เพี้ยน -> แครช) -- snprintf ก่อน
            char dbuf[72];
            snprintf(dbuf, sizeof(dbuf), "SENSOR AGREEMENT   diff %.2f %s%s",
                     (double)diff, UI_UNIT_O2, bad ? "   -- CHECK CALIBRATION" : "");
            lv_label_set_text(s_diffLbl, dbuf);
            lv_obj_set_style_text_color(s_diffLbl, bad ? C_ALARM : C_TEXT_DIM, 0);

            // bar เต็ม = ตรงกันเป๊ะ, สั้นลง = ห่างกันมากขึ้น (อ้างอิงที่ 5%)
            int32_t agree = (int32_t)(100.0f - (diff / 5.0f) * 100.0f);
            if (agree < 0) agree = 0;
            lv_bar_set_value(s_diffBar, agree, LV_ANIM_ON);
            ui_tint(s_diffBar, bad ? C_ALARM : C_OK, LV_PART_INDICATOR);
        } else {
            lv_label_set_text(s_diffLbl, "SENSOR AGREEMENT   --");
            lv_obj_set_style_text_color(s_diffLbl, C_TEXT_MUTE, 0);
            lv_bar_set_value(s_diffBar, 0, LV_ANIM_OFF);
        }
    }

    c_up = up;
}
