// ============================================================================
//  ui_diag.cpp  --  หน้าไล่ปัญหา
//
//    - สถานะลิงก์ (rx / crc-err / tx / ack-timeout) + สถานะ SD
//    - log ล่าง สลับได้: FRAMES (เฟรมดิบบนสาย) / EVENTS (START/STOP/ALARM/zone)
//
//  *** ตัด MODBUS SCANNER + SET ID ออกแล้ว ***
//     ตั้งเซนเซอร์เสร็จตั้งแต่ตอน commissioning -> ไม่ต้องสแกนซ้ำหน้างาน
//     (ประหยัด flash ESP32 + หน้าจอสะอาด)  ถ้าจะสแกน/ตั้ง ID อีก ใช้สเก็ตช์
//     ใน tools/ กับ Nano โดยตรง
// ============================================================================
#include "ui.h"
#include "sdlog.h"
#include "ui_theme.h"
#include "app_state.h"
#include "link_master.h"
#include "link_protocol.h"

#include <stdio.h>
#include <string.h>

static lv_obj_t *s_stats;
static lv_obj_t *s_sdStat;
static lv_obj_t *s_log;

// ---------------------------------------------------------------------------
//  กล่อง log ล่าง แสดงได้ 2 อย่าง สลับด้วยปุ่ม:
//    FRAMES  -- เฟรมดิบที่วิ่งบนสาย (แทน Serial monitor ที่ใช้ไม่ได้)
//    EVENTS  -- เหตุการณ์สำคัญย้อนหลัง พร้อมเวลา
// ---------------------------------------------------------------------------
#define LOG_LINES    9
#define LOG_WIDTH   52

static char    s_logBuf[LOG_LINES][LOG_WIDTH];
static uint8_t s_logHead  = 0;
static uint8_t s_logCount = 0;
static bool    s_logDirty = false;

// --- event log: เก็บมากกว่าที่แสดง เพื่อเลื่อนดูย้อนหลังได้ ---
#define EV_MAX  32

struct EvRec {
    uint32_t ms;
    uint8_t  type;
    uint8_t  data;
};

static EvRec   s_ev[EV_MAX];
static uint8_t s_evHead  = 0;
static uint8_t s_evCount = 0;
static bool    s_evDirty = false;

static bool      s_showEvents = false;     // false = FRAMES, true = EVENTS
static lv_obj_t *s_logTitle;
static lv_obj_t *s_logToggle;

// ---------------------------------------------------------------------------
void ui_diag_log(const char *line, bool isTx)
{
    if (!line || !line[0]) return;
    snprintf(s_logBuf[s_logHead], LOG_WIDTH, "%s %s", isTx ? ">" : "<", line);
    s_logHead = (uint8_t)((s_logHead + 1) % LOG_LINES);
    if (s_logCount < LOG_LINES) s_logCount++;
    s_logDirty = true;
}

void ui_diag_event(uint8_t type, uint8_t data, uint32_t ms)
{
    EvRec &e = s_ev[s_evHead];
    e.ms   = ms;
    e.type = type;
    e.data = data;

    s_evHead = (uint8_t)((s_evHead + 1) % EV_MAX);
    if (s_evCount < EV_MAX) s_evCount++;
    s_evDirty = true;
}

// --- แปลง event เป็นข้อความ ---
static const char *ev_text(const EvRec &e, char *tmp, size_t n)
{
    switch (e.type) {
    case EV_LINK_UP:     return "LINK UP";
    case EV_LINK_DOWN:   return "LINK LOST";
    case EV_START:       return "PROCESS START";
    case EV_STOP:        return "PROCESS STOP";
    case EV_ESTOP:       return "EMERGENCY STOP";
    case EV_MODE_AUTO:   return "MODE -> AUTO";
    case EV_MODE_MANUAL: return "MODE -> MANUAL";
    case EV_SAVED:       return "CONFIG SAVED";

    case EV_ZONE:
        switch (e.data) {
        case LNK_RVZ_MAXOUT: return "RV ZONE -> MAX OUT";
        case LNK_RVZ_ROUGH:  return "RV ZONE -> ROUGH";
        case LNK_RVZ_MEDIUM: return "RV ZONE -> MEDIUM";
        case LNK_RVZ_FINE:   return "RV ZONE -> FINE";
        default:             return "RV ZONE -> OFF";
        }

    case EV_ALARM_ON:
    case EV_ALARM_OFF: {
        const char *what;
        switch (e.data) {
        case LNK_F_O2A_FAIL:    what = "O2#1 FAIL";    break;
        case LNK_F_O2B_FAIL:    what = "O2#2 FAIL";    break;
        case LNK_F_PR_FAIL:     what = "PT FAIL";      break;
        case LNK_F_MB_BUS:      what = "RS485 SILENT"; break;
        case LNK_F_ESTOP:       what = "E-STOP";       break;
        case LNK_F_CFG_BAD:     what = "CONFIG BAD";   break;
        case LNK_F_O2_MISMATCH: what = "O2 MISMATCH";  break;
        default:                what = "ALARM";        break;
        }
        snprintf(tmp, n, "%s %s", (e.type == EV_ALARM_ON) ? "!" : "ok", what);
        return tmp;
    }

    default: return "?";
    }
}

// ---------------------------------------------------------------------------
static void redraw_log()
{
    static char blob[LOG_LINES * LOG_WIDTH + 8];

    if (s_showEvents) {
        if (!s_evDirty) return;
        s_evDirty = false;
        blob[0] = '\0';

        uint8_t show = (s_evCount < LOG_LINES) ? s_evCount : LOG_LINES;
        for (uint8_t i = 0; i < show; i++) {
            uint8_t idx = (uint8_t)((s_evHead + EV_MAX - 1 - i) % EV_MAX);
            const EvRec &e = s_ev[idx];

            char tmp[32];
            uint32_t s = e.ms / 1000;
            char line[LOG_WIDTH];
            snprintf(line, sizeof(line), "%02lu:%02lu:%02lu  %s\n",
                     (unsigned long)(s / 3600),
                     (unsigned long)((s / 60) % 60),
                     (unsigned long)(s % 60),
                     ev_text(e, tmp, sizeof(tmp)));
            strncat(blob, line, sizeof(blob) - strlen(blob) - 1);
        }
        if (!show) strncat(blob, "no events yet", sizeof(blob) - 1);
        lv_label_set_text(s_log, blob);
        return;
    }

    if (!s_logDirty) return;
    s_logDirty = false;
    blob[0] = '\0';

    uint8_t start = (uint8_t)((s_logHead + LOG_LINES - s_logCount) % LOG_LINES);
    for (uint8_t i = 0; i < s_logCount; i++) {
        strncat(blob, s_logBuf[(start + i) % LOG_LINES], LOG_WIDTH);
        strncat(blob, "\n", 2);
    }
    lv_label_set_text(s_log, blob);
}

static void toggle_cb(lv_event_t *e)
{
    (void)e;
    s_showEvents = !s_showEvents;

    lv_label_set_text(s_logTitle, s_showEvents ? "EVENT LOG" : "FRAME LOG");
    lv_label_set_text(lv_obj_get_child(s_logToggle, 0),
                      s_showEvents ? "FRAMES" : "EVENTS");

    s_logDirty = true;
    s_evDirty  = true;
    redraw_log();
}

// ---------------------------------------------------------------------------
void ui_diag_build(lv_obj_t *par)
{
    // --- สถิติลิงก์ ---
    s_stats = lv_label_create(par);
    lv_obj_set_style_text_font(s_stats, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_stats, C_TEXT_DIM, 0);
    lv_obj_set_pos(s_stats, 12, 8);
    lv_label_set_text(s_stats, "...");

    // สถานะ SD -- ชิดขวาบรรทัดเดียวกับ stats ลิงก์
    s_sdStat = lv_label_create(par);
    lv_obj_set_style_text_font(s_sdStat, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_sdStat, C_TEXT_MUTE, 0);
    lv_obj_align(s_sdStat, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_label_set_text(s_sdStat, "SD --");

    // ================= log  (เต็มพื้นที่ที่ scanner เคยกินไป) =================
    s_logTitle = lv_label_create(par);
    lv_label_set_text(s_logTitle, "FRAME LOG");
    lv_obj_set_style_text_font(s_logTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_logTitle, C_NEON, 0);
    lv_obj_set_pos(s_logTitle, 12, 40);

    s_logToggle = ui_button(par, 360, 36, 112, 26, "EVENTS", C_PANEL_HI, toggle_cb, nullptr);
    lv_obj_set_style_text_font(lv_obj_get_child(s_logToggle, 0), &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_color(s_logToggle, C_NEON, 0);
    lv_obj_set_style_border_width(s_logToggle, 1, 0);

    lv_obj_t *lg = ui_card(par, 8, 66, 464, 296);      // สูงเต็มจอ (9 บรรทัด)
    ui_grad(lg, C_LOG_TOP, C_LOG_BOT);
    lv_obj_set_style_pad_all(lg, 8, 0);

    s_log = lv_label_create(lg);
    lv_obj_set_style_text_font(s_log, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_log, C_TEXT, 0);
    lv_label_set_text(s_log, "waiting for frames...");
}

// ---------------------------------------------------------------------------
// อัปเดตเฉพาะตอนตัวเลขเปลี่ยนจริง -- ดูเหตุผลใน ui.cpp (invalidate ถล่ม -> touch ตาย)
void ui_diag_update()
{
    static uint32_t c_rx = 0xFFFFFFFF, c_crc = 0, c_tx = 0, c_ack = 0;

    if (g_app.rxFrames != c_rx || g_app.crcErrors != c_crc ||
        g_app.txFrames != c_tx || g_app.ackTimeouts != c_ack) {

        bool crcWasZero = (c_crc == 0);
        c_rx = g_app.rxFrames; c_crc = g_app.crcErrors;
        c_tx = g_app.txFrames; c_ack = g_app.ackTimeouts;

        lv_label_set_text_fmt(s_stats,
            "rx %lu    crc-err %lu    tx %lu    ack-timeout %lu",
            (unsigned long)c_rx, (unsigned long)c_crc,
            (unsigned long)c_tx, (unsigned long)c_ack);

        if (crcWasZero != (c_crc == 0))
            lv_obj_set_style_text_color(s_stats, c_crc ? C_ALARM : C_TEXT_DIM, 0);
    }

    // ----- สถานะ SD (เขียนเฉพาะตอนเปลี่ยน) -----
    {
        static uint32_t c_lines = 0xFFFFFFFF, c_drop = 0xFFFFFFFF;
        static int8_t   c_mount = -1;
        static uint32_t c_free  = 0xFFFFFFFF;
        SdStat sd;
        sdlog_stat(&sd);
        if ((int8_t)sd.mounted != c_mount || sd.lines != c_lines ||
            sd.dropped != c_drop || sd.freeMB != c_free) {
            c_mount = sd.mounted; c_lines = sd.lines;
            c_drop = sd.dropped; c_free = sd.freeMB;

            if (!sd.mounted) {
                lv_label_set_text(s_sdStat, LV_SYMBOL_SD_CARD "  NO CARD");
                lv_obj_set_style_text_color(s_sdStat, C_TEXT_MUTE, 0);
            } else if (sd.dropped) {
                lv_label_set_text_fmt(s_sdStat,
                    LV_SYMBOL_SD_CARD "  %luMB  %lu ln  %lu DROP",
                    (unsigned long)sd.freeMB, (unsigned long)sd.lines,
                    (unsigned long)sd.dropped);
                lv_obj_set_style_text_color(s_sdStat, C_WARN, 0);
            } else {
                lv_label_set_text_fmt(s_sdStat,
                    LV_SYMBOL_SD_CARD "  %luMB free  %lu ln",
                    (unsigned long)sd.freeMB, (unsigned long)sd.lines);
                lv_obj_set_style_text_color(s_sdStat, C_OK, 0);
            }
        }
    }

    redraw_log();
}
