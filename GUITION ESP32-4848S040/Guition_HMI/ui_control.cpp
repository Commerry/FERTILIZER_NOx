// ============================================================================
//  ui_control.cpp  --  หน้าสั่งงาน
//
//  รวม 3 อย่างไว้ที่เดียว:
//    1. START / STOP กระบวนการอัตโนมัติ + สลับโหมด + E-STOP
//    2. สั่งอุปกรณ์ทีละตัวด้วยมือ (โหมด MANUAL)
//    3. *** แตะชื่ออุปกรณ์ -> เปิดหน้าตั้งเวลาของตัวนั้นทันที ***
//       (ไม่ต้องเดินไป SETUP > TIMERS แล้วไล่หาในรายการยาว ๆ)
//
//  หลักสำคัญ: ปุ่มบนจอ "ไม่ใช่" ตัวเก็บสถานะ
//  กดปุ่ม -> ส่ง $C ไป Nano -> Nano ทำจริง -> Nano ส่ง $D กลับมาบอกสถานะจริง
//  -> UI ค่อยอัปเดตตาม $D   (จอจะได้ไม่ "โกหก" ว่ารีเลย์ติดทั้งที่คำสั่งตกหล่น)
// ============================================================================
#include "ui.h"
#include "ui_theme.h"
#include "app_state.h"
#include "link_master.h"
#include "link_protocol.h"

#include <stdio.h>

static lv_obj_t *s_btnStart;
static lv_obj_t *s_btnStop;
static lv_obj_t *s_btnMode;
static lv_obj_t *s_modeLbl;
static lv_obj_t *s_hint;

static lv_obj_t *s_sw[4];
static lv_obj_t *s_devBtn[4];       // ปุ่มชื่ออุปกรณ์ -> เปิดหน้าตั้งเวลา
static lv_obj_t *s_slider;
static lv_obj_t *s_sliderLbl;

static bool s_syncing  = false;     // true = กำลังเซ็ตค่าจากโค้ด ไม่ใช่คนกด
static bool s_needSync = false;     // ผู้ใช้ขยับสวิตช์แต่คำสั่งถูกปฏิเสธ -> ต้องดึงกลับ

// --- หน้าตั้งเวลาของอุปกรณ์ (overlay) ---
static lv_obj_t *s_devPanel = nullptr;
static lv_obj_t *s_devRowVal[3];
static uint8_t   s_devIdx = 0;

static const char *DEV_FULL[4]  = { UI_FULL_CP, UI_FULL_CB, UI_FULL_FB, UI_FULL_FP };
static const char *DEV_SHORT[4] = { UI_NAME_CP, UI_NAME_CB, UI_NAME_FB, UI_NAME_FP };

// ลำดับต้องตรงกับ CFG_DEV_* : CP=0 CB=1 FB=2 FP=3
static const uint8_t DEV_CFG[4] = { CFG_DEV_CP, CFG_DEV_CB, CFG_DEV_FB, CFG_DEV_FP };

static const char *ROW_NAME[3] = { "START DELAY", "ON TIME", "OFF TIME" };
static const uint8_t ROW_FLD[3] = { CFG_FLD_DELAY, CFG_FLD_ON, CFG_FLD_OFF };

// ---------------------------------------------------------------------------
//  *** ห้ามใช้ LV_STATE_DISABLED กับปุ่ม ***
//  LVGL จะไม่ส่ง event ให้ object ที่ disabled เลย -> กดแล้วเงียบสนิท
//  ผู้ใช้ไม่มีทางรู้ว่าทำไมมันไม่ทำงาน (นึกว่าจอเสีย)
//  แทนที่จะ disable: ให้กดได้เสมอ แล้วเช็คเงื่อนไขใน callback + เด้ง toast บอกเหตุผล
//  ตัวปุ่มแค่ "หรี่สี" ให้รู้ว่าตอนนี้ยังกดไม่ได้ผล
// ---------------------------------------------------------------------------
static bool can_command()
{
    if (!g_app.linkUp) {
        ui_toast("NO LINK TO NANO - CHECK WIRING");
        return false;
    }
    if (g_app.accessLevel < ACC_OPERATOR) {
        ui_toast("NEED OPERATOR PIN - GO TO SETUP");
        return false;
    }
    return true;
}

static void start_cb(lv_event_t *e)
{
    (void)e;
    if (!can_command()) return;
    if (g_app.mode == LNK_MODE_MANUAL) { ui_toast("SWITCH TO AUTO MODE FIRST"); return; }
    link_cmd_start();
}

static void stop_cb(lv_event_t *e)
{
    (void)e;
    if (!can_command()) return;
    link_cmd_stop();
}

static void mode_cb(lv_event_t *e)
{
    (void)e;
    if (!can_command()) return;
    // สลับโหมด -- Nano จะดับ output ทุกตัวให้เองตอนเปลี่ยนโหมด
    link_cmd_mode(g_app.mode == LNK_MODE_AUTO ? LNK_MODE_MANUAL : LNK_MODE_AUTO);
}

// E-STOP กดได้เสมอ ไม่ต้องมีสิทธิ์ -- ปุ่มหยุดฉุกเฉินต้องไม่มีอะไรมาขวาง
static void estop_cb(lv_event_t *e)
{
    (void)e;
    if (!g_app.linkUp) { ui_toast("NO LINK TO NANO"); return; }
    link_cmd_estop();
    ui_toast("EMERGENCY STOP SENT");
}

static void relay_cb(lv_event_t *e)
{
    if (s_syncing) return;

    if (!can_command())              { s_needSync = true; return; }
    if (g_app.mode == LNK_MODE_AUTO) {
        ui_toast("AUTO MODE - SWITCH TO MANUAL FIRST");
        s_needSync = true;                        // ดึงสวิตช์กลับไปตามสถานะจริง
        return;
    }

    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    link_cmd_relay((uint8_t)idx, lv_obj_has_state(sw, LV_STATE_CHECKED));
    // ไม่แตะ UI ตรงนี้ -- รอ $D จาก Nano มายืนยัน
}

static void valve_cb(lv_event_t *e)
{
    if (s_syncing) return;

    if (!can_command())              { s_needSync = true; return; }
    if (g_app.mode == LNK_MODE_AUTO) {
        ui_toast("AUTO MODE - SWITCH TO MANUAL FIRST");
        s_needSync = true;
        return;
    }

    lv_obj_t *sl = (lv_obj_t *)lv_event_get_target(e);
    int v = (int)lv_slider_get_value(sl);
    lv_label_set_text_fmt(s_sliderLbl, "%d%%", v);
    link_cmd_valve((uint8_t)v);
}

// ===========================================================================
//  หน้าตั้งเวลาของอุปกรณ์ 1 ตัว  (เปิดจากการแตะชื่ออุปกรณ์)
//
//  เวลาเก็บใน Nano เป็น ms แต่แสดง/กรอกเป็นวินาที 3 ทศนิยม
//  -> ตั้งได้ทั้งพัลส์สั้น (0.200 s) และรอบยาว (3600.000 s) ในช่องเดียวกัน
// ===========================================================================
static void dev_panel_refresh()
{
    if (!s_devPanel) return;

    for (uint8_t r = 0; r < 3; r++) {
        uint8_t key = CFG_TKEY(DEV_CFG[s_devIdx], ROW_FLD[r]);
        char buf[24];
        if (ui_cfg_have(key)) snprintf(buf, sizeof(buf), "%.3f s", (double)(ui_cfg_get(key) / 1000.0f));
        else                  snprintf(buf, sizeof(buf), "--");
        lv_label_set_text(s_devRowVal[r], buf);
    }
}

static void dev_panel_close()
{
    if (s_devPanel) {
        lv_obj_del_async(s_devPanel);   // ถูกเรียกจาก event callback -> ต้อง async
        s_devPanel = nullptr;
    }
}

static void dev_close_cb(lv_event_t *e) { (void)e; dev_panel_close(); }

static void dev_save_cb(lv_event_t *e)
{
    (void)e;
    if (g_app.accessLevel < ACC_SUPERVISOR) { ui_toast("NEED SUPERVISOR PIN"); return; }
    ui_cfg_save();
}

// user = index ของแถว (0=delay 1=on 2=off) ผสมกับอุปกรณ์ที่เปิดอยู่
static void dev_value_entered(float seconds, void *user)
{
    uint8_t r   = (uint8_t)(intptr_t)user;
    uint8_t key = CFG_TKEY(DEV_CFG[s_devIdx], ROW_FLD[r]);

    ui_cfg_set(key, seconds * 1000.0f);     // วินาที -> ms
    dev_panel_refresh();
}

static void dev_row_cb(lv_event_t *e)
{
    uint8_t r = (uint8_t)(intptr_t)lv_event_get_user_data(e);

    if (g_app.accessLevel < ACC_SUPERVISOR) { ui_toast("NEED SUPERVISOR PIN"); return; }
    if (!g_app.linkUp)                      { ui_toast("NO LINK TO NANO");     return; }

    uint8_t key = CFG_TKEY(DEV_CFG[s_devIdx], ROW_FLD[r]);

    char title[48];
    snprintf(title, sizeof(title), "%s - %s", DEV_SHORT[s_devIdx], ROW_NAME[r]);

    ui_keypad_open(title, "range  0.000  to  9999.999  seconds",
                   ui_cfg_get(key) / 1000.0f, 0.0f, 9999.999f, 3,
                   false, dev_value_entered, (void *)(intptr_t)r);
}

static void dev_panel_open(uint8_t idx)
{
    dev_panel_close();
    s_devIdx = idx;

    // ฉากหลังทึบ กันกดทะลุไปโดนของข้างหลัง
    s_devPanel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_devPanel, UI_W, UI_H);
    lv_obj_set_pos(s_devPanel, 0, 0);
    ui_grad(s_devPanel, C_BG_TOP, C_BG_BOT);
    lv_obj_set_style_border_width(s_devPanel, 0, 0);
    lv_obj_set_style_radius(s_devPanel, 0, 0);
    lv_obj_set_style_pad_all(s_devPanel, 0, 0);
    lv_obj_clear_flag(s_devPanel, LV_OBJ_FLAG_SCROLLABLE);

    // --- หัวข้อ ---
    lv_obj_t *t = lv_label_create(s_devPanel);
    lv_label_set_text(t, DEV_FULL[idx]);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(t, C_NEON, 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 20);

    ui_button(s_devPanel, 392, 14, 72, 40, LV_SYMBOL_CLOSE, C_PANEL_HI, dev_close_cb, nullptr);

    // --- สถานะจริงตอนนี้ ---
    lv_obj_t *st = ui_card(s_devPanel, 16, 66, 448, 54);

    lv_obj_t *sn = lv_label_create(st);
    lv_label_set_text(sn, "STATUS");
    lv_obj_set_style_text_font(sn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sn, C_TEXT_DIM, 0);
    lv_obj_align(sn, LV_ALIGN_LEFT_MID, 14, 0);

    bool on = g_app.linkUp && g_app.relay(idx);
    lv_obj_t *sv = lv_label_create(st);
    lv_label_set_text(sv, on ? LV_SYMBOL_OK "  RUNNING" : "OFF");
    lv_obj_set_style_text_font(sv, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sv, on ? C_OK : C_TEXT_DIM, 0);
    lv_obj_align(sv, LV_ALIGN_RIGHT_MID, -14, 0);

    // --- 3 ช่องเวลา ---
    for (uint8_t r = 0; r < 3; r++) {
        lv_obj_t *row = lv_btn_create(s_devPanel);
        lv_obj_set_size(row, 448, 52);
        lv_obj_set_pos(row, 16, 132 + r * 60);
        lv_obj_set_style_radius(row, UI_RADIUS, 0);
        ui_grad(row, C_PANEL_TOP, C_PANEL_BOT);
        ui_grad(row, C_PANEL_HI_BOT, C_PANEL_HI_TOP, LV_STATE_PRESSED);   // กดแล้วจม
        lv_obj_set_style_border_color(row, C_LINE, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_add_event_cb(row, dev_row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)r);

        lv_obj_t *nm = lv_label_create(row);
        lv_label_set_text(nm, ROW_NAME[r]);
        lv_obj_set_style_text_font(nm, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(nm, C_TEXT, 0);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 14, 0);

        s_devRowVal[r] = lv_label_create(row);
        lv_obj_set_style_text_font(s_devRowVal[r], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s_devRowVal[r], C_NEON, 0);
        lv_obj_align(s_devRowVal[r], LV_ALIGN_RIGHT_MID, -14, 0);
    }

    // --- คำอธิบายกติกา ---
    lv_obj_t *h = lv_label_create(s_devPanel);
    lv_label_set_text(h, "ON = 0  ->  device never runs\n"
                         "OFF = 0  ->  runs continuously after the delay");
    lv_obj_set_style_text_font(h, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(h, C_TEXT_MUTE, 0);
    lv_obj_set_pos(h, 18, 318);

    ui_button(s_devPanel, 16, 372, 448, 48,
              LV_SYMBOL_SAVE "  SAVE TO EEPROM", C_OK, dev_save_cb, nullptr);

    dev_panel_refresh();
}

static void dev_btn_cb(lv_event_t *e)
{
    dev_panel_open((uint8_t)(intptr_t)lv_event_get_user_data(e));
}

// ===========================================================================
//  พิกัดต้องจบก่อน UI_BODY_H (366) ไม่งั้นไปทับ nav bar
//    y   4.. 62  START / STOP
//    y  68..110  MODE / E-STOP
//    y 116..133  คำอธิบาย
//    y 138..294  อุปกรณ์ 4 ตัว (ชื่อ = ปุ่มเปิดหน้าตั้งเวลา, สวิตช์ = สั่งมือ)
//    y 300..342  วาล์ว RV
// ===========================================================================
void ui_control_build(lv_obj_t *par)
{
    s_btnStart = ui_button(par,   8,  4, 228, 58, LV_SYMBOL_PLAY "  START", C_OK,    start_cb, nullptr);
    s_btnStop  = ui_button(par, 244,  4, 228, 58, LV_SYMBOL_STOP "  STOP",  C_ALARM, stop_cb,  nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(s_btnStart, 0), &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(s_btnStop,  0), &lv_font_montserrat_20, 0);

    s_btnMode = ui_button(par, 8, 68, 228, 42, "MODE: AUTO", C_NEON, mode_cb, nullptr);
    s_modeLbl = lv_obj_get_child(s_btnMode, 0);

    lv_obj_t *stop2 = ui_button(par, 244, 68, 228, 42,
                                LV_SYMBOL_POWER "  E-STOP", C_ALARM, estop_cb, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(stop2, 0), &lv_font_montserrat_14, 0);

    s_hint = lv_label_create(par);
    lv_label_set_text(s_hint, "");
    lv_obj_set_style_text_font(s_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_hint, C_WARN, 0);
    lv_obj_set_pos(s_hint, 12, 116);

    // --- อุปกรณ์ 4 ตัว ---
    for (int i = 0; i < 4; i++) {
        int y = 138 + i * 40;

        lv_obj_t *card = ui_card(par, 8, y, 464, 36);

        // ปุ่มชื่อ -- แตะแล้วเข้าหน้าตั้งเวลาของตัวนั้นทันที
        lv_obj_t *b = lv_btn_create(card);
        lv_obj_set_size(b, 250, 34);
        lv_obj_set_pos(b, 1, 1);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        ui_grad(b, C_PANEL_HI_BOT, C_PANEL_HI_TOP, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_add_event_cb(b, dev_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_devBtn[i] = b;

        lv_obj_t *s = lv_label_create(b);
        lv_label_set_text(s, DEV_SHORT[i]);
        lv_obj_set_style_text_font(s, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(s, C_NEON, 0);
        lv_obj_align(s, LV_ALIGN_LEFT_MID, 12, 0);

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, DEV_FULL[i]);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, C_TEXT_DIM, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 52, 0);

        // ไอคอนบอกว่ากดเข้าไปตั้งค่าได้
        lv_obj_t *ar = lv_label_create(b);
        lv_label_set_text(ar, LV_SYMBOL_SETTINGS);
        lv_obj_set_style_text_font(ar, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ar, C_TEXT_MUTE, 0);
        lv_obj_align(ar, LV_ALIGN_RIGHT_MID, -8, 0);

        // สวิตช์สั่งมือ
        lv_obj_t *sw = lv_switch_create(card);
        lv_obj_set_size(sw, 56, 28);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -10, 0);
        ui_grad(sw, C_SUNK_TOP, C_SUNK_BOT, LV_PART_MAIN);            // ราง = ร่องจม
        ui_tint(sw, C_OK, LV_PART_INDICATOR | (lv_style_selector_t)LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, relay_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        s_sw[i] = sw;
    }

    // --- วาล์ว RV ---
    lv_obj_t *vc = ui_card(par, 8, 300, 464, 42);

    lv_obj_t *vt = lv_label_create(vc);
    lv_label_set_text(vt, UI_NAME_RV);
    lv_obj_set_style_text_font(vt, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(vt, C_NEON, 0);
    lv_obj_align(vt, LV_ALIGN_LEFT_MID, 12, 0);

    s_slider = lv_slider_create(vc);
    lv_obj_set_size(s_slider, 320, 12);
    lv_obj_align(s_slider, LV_ALIGN_LEFT_MID, 52, 0);
    lv_slider_set_range(s_slider, 0, 100);
    ui_grad(s_slider, C_SUNK_TOP, C_SUNK_BOT, LV_PART_MAIN);          // ราง = ร่องจม
    ui_tint(s_slider, C_NEON, LV_PART_INDICATOR);
    ui_tint(s_slider, C_NEON, LV_PART_KNOB);
    // ส่งคำสั่งตอนปล่อยนิ้ว ไม่ใช่ทุกพิกเซลที่ลาก -- ไม่งั้นจะยิง $C ถล่ม Nano
    lv_obj_add_event_cb(s_slider, valve_cb, LV_EVENT_RELEASED, nullptr);

    s_sliderLbl = lv_label_create(vc);
    lv_label_set_text(s_sliderLbl, "0%");
    lv_obj_set_style_text_font(s_sliderLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_sliderLbl, C_TEXT, 0);
    lv_obj_align(s_sliderLbl, LV_ALIGN_RIGHT_MID, -10, 0);
}

// ---------------------------------------------------------------------------
//  อัปเดตเฉพาะตอนค่าเปลี่ยนจริง -- ดูเหตุผลใน ui.cpp (invalidate ถล่ม -> touch ตาย)
// ---------------------------------------------------------------------------
void ui_control_update()
{
    static bool    c_up     = true;
    static bool    c_manual = true;
    static bool    c_canOp  = true;
    static uint8_t c_relay  = 0xFF;
    static uint8_t c_valve  = 0xFF;

    bool    up     = g_app.linkUp;
    bool    manual = (g_app.mode == LNK_MODE_MANUAL);
    bool    canOp  = up && (g_app.accessLevel >= ACC_OPERATOR);
    uint8_t relay  = up ? g_app.relayBits : 0;
    uint8_t valve  = up ? g_app.valve : 0;

    // คำสั่งหมดเวลา (ACK ไม่กลับ = คำสั่งน่าจะหาย) -> บังคับ resync สวิตช์/สไลเดอร์
    // กลับไปตามสถานะจริงจาก Nano ($D) ไม่งั้นปุ่มจะค้างโชว์ค่าที่สั่งไปแต่ไม่ได้ทำจริง
    static uint32_t c_ackTo = 0;
    if (g_app.ackTimeouts != c_ackTo) { c_ackTo = g_app.ackTimeouts; s_needSync = true; }

    bool modeChanged = (manual != c_manual) || (canOp != c_canOp) || (up != c_up);

    s_syncing = true;

    if (modeChanged) {
        c_manual = manual;
        c_canOp  = canOp;

        lv_label_set_text(s_modeLbl, manual ? "MODE: MANUAL" : "MODE: AUTO");
        ui_btn_color(s_btnMode, manual ? C_WARN : C_NEON);

        lv_opa_t runOpa = (canOp && !manual) ? LV_OPA_COVER : LV_OPA_40;
        lv_obj_set_style_opa(s_btnStart, runOpa, 0);
        lv_obj_set_style_opa(s_btnStop,  runOpa, 0);
        lv_obj_set_style_opa(s_btnMode,  canOp ? LV_OPA_COVER : LV_OPA_40, 0);

        lv_opa_t manOpa = (canOp && manual) ? LV_OPA_COVER : LV_OPA_40;
        for (int i = 0; i < 4; i++) lv_obj_set_style_opa(s_sw[i], manOpa, 0);
        lv_obj_set_style_opa(s_slider, manOpa, 0);

        if (!up)         lv_label_set_text(s_hint, LV_SYMBOL_WARNING "  NO LINK TO NANO");
        else if (!canOp) lv_label_set_text(s_hint, LV_SYMBOL_WARNING "  ENTER PIN IN SETUP FIRST");
        else if (manual) lv_label_set_text(s_hint, "MANUAL - drive each device by hand");
        else             lv_label_set_text(s_hint, "AUTO - tap a device name to set its timing");
    }

    // สถานะรีเลย์จริงจาก Nano
    if (relay != c_relay || up != c_up || s_needSync) {
        c_relay = relay;
        for (int i = 0; i < 4; i++) {
            if ((relay >> i) & 0x01) lv_obj_add_state(s_sw[i], LV_STATE_CHECKED);
            else                     lv_obj_clear_state(s_sw[i], LV_STATE_CHECKED);
        }
    }

    // วาล์ว (อย่าแย่งสไลเดอร์ตอนผู้ใช้กำลังลากอยู่)
    if ((valve != c_valve || up != c_up || s_needSync) &&
        !lv_obj_has_state(s_slider, LV_STATE_PRESSED)) {
        c_valve = valve;
        lv_slider_set_value(s_slider, valve, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_sliderLbl, "%u%%", valve);
    }

    s_needSync = false;
    c_up = up;
    s_syncing = false;
}
