// ============================================================================
//  ui.h  --  โครงหน้าจอทั้งหมด
//
//  โครงสร้าง: 1 screen + หลาย "page" ซ้อนกัน (ซ่อน/แสดงทีละอัน)
//    header (58)  -- โลโก้ + ชื่อหน้า/ข้อความเตือน + สถานะลิงก์
//    page   (366) -- เนื้อหาแต่ละหน้า
//    nav    (56)  -- ปุ่มสลับหน้า
// ============================================================================
// *** ชื่อ guard ห้ามเป็น UI_H เด็ดขาด ***
// ui_theme.h นิยาม UI_H = 480 (ความสูงจอ) ไว้แล้ว
// ถ้าใช้ UI_H เป็น guard: ไฟล์ไหน include ui_theme.h ก่อน ui.h -> UI_H ถูกนิยามแล้ว
// -> #ifndef UI_H เป็นเท็จ -> เนื้อ ui.h ถูกข้ามทั้งไฟล์เงียบ ๆ -> พังแบบหาสาเหตุไม่เจอ
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <lvgl.h>
#include <stdint.h>

enum UiPage {
    PAGE_HOME = 0,      // ผังเดินท่อ (SCADA mimic)
    PAGE_DATA,          // เกจค่าเซนเซอร์
    PAGE_CTRL,          // สั่งงานด้วยมือ + ตั้งเวลารายอุปกรณ์
    PAGE_SETUP,         // ตั้งค่า (เมนู -> หน้าย่อย)
    PAGE_DIAG,          // ไล่ปัญหา + Modbus scanner + event log
    PAGE_COUNT
};

void    ui_begin();
void    ui_tick();                 // เรียกทุก ~100 ms
void    ui_goto(uint8_t page);
uint8_t ui_current_page();

void ui_toast(const char *msg);

// ---------------------------------------------------------------------------
//  คีย์บอร์ดตัวเลข -- ตัวกลาง ใช้ได้จากทุกหน้า
//
//  cb ถูกเรียกเมื่อกด OK "และค่าอยู่ในช่วง" เท่านั้น
//  ถ้าค่านอกช่วง keypad จะไม่ปิด แต่เด้ง toast บอกช่วงที่รับได้
// ---------------------------------------------------------------------------
typedef void (*UiKeypadCb)(float value, void *user);

void ui_keypad_open(const char *title,   // หัวข้อ (ชื่อค่าที่กำลังแก้)
                    const char *sub,     // บรรทัดอธิบาย (ช่วงค่า) -- NULL ได้
                    float cur,           // ค่าปัจจุบัน (เติมให้ในช่องเลย)
                    float vmin, float vmax,
                    uint8_t dp,          // ทศนิยมกี่ตำแหน่ง
                    bool password,       // true = ซ่อนตัวเลข (ใส่ PIN)
                    UiKeypadCb cb, void *user);

// ---------------------------------------------------------------------------
//  ค่า config ที่ดึงมาจาก Nano ($K) -- ui_setup เป็นเจ้าของ cache
//  หน้าอื่น (เช่น CTRL) เรียกใช้ผ่าน 3 ฟังก์ชันนี้
// ---------------------------------------------------------------------------
float ui_cfg_get(uint8_t key);            // ค่าที่ Nano บอกมา (หน่วยดิบ เช่น ms)
bool  ui_cfg_have(uint8_t key);           // ได้ค่าจาก Nano แล้วหรือยัง
void  ui_cfg_set(uint8_t key, float v);   // ส่ง $S ให้ Nano + mark ว่ายังไม่ SAVE
void  ui_cfg_save();                      // ส่ง $C SAV -> Nano เขียน EEPROM

// ---------------------------------------------------------------------------
//  Event log -- บันทึกว่าเกิดอะไรขึ้นเมื่อไหร่ (ดูย้อนหลังได้ในหน้า DIAG)
// ---------------------------------------------------------------------------
enum EvType {
    EV_LINK_UP = 0,
    EV_LINK_DOWN,
    EV_START,
    EV_STOP,
    EV_ESTOP,
    EV_MODE_AUTO,
    EV_MODE_MANUAL,
    EV_ALARM_ON,      // data = errflag bit ที่เพิ่งขึ้น
    EV_ALARM_OFF,
    EV_ZONE,          // data = LNK_RVZ_*
    EV_SAVED,
};

void ui_event(uint8_t type, uint8_t data);

// --- แต่ละหน้า -------------------------------------------------------------
void ui_home_build(lv_obj_t *p);      void ui_home_update();
void ui_data_build(lv_obj_t *p);      void ui_data_update();
void ui_control_build(lv_obj_t *p);   void ui_control_update();
void ui_setup_build(lv_obj_t *p);     void ui_setup_update();
void ui_diag_build(lv_obj_t *p);      void ui_diag_update();

void ui_diag_log(const char *line, bool isTx);
void ui_diag_event(uint8_t type, uint8_t data, uint32_t ms);   // event log เก็บที่หน้า DIAG

// ---------------------------------------------------------------------------
//  ไล่เฉดสี  (ดูเหตุผล + พาเลตต์ที่ ui_theme.h)
//
//  *** ห้ามสั่ง lv_obj_set_style_bg_color() เดี่ยว ๆ กับการ์ด/ปุ่ม/แถบ อีกต่อไป ***
//
//  เพราะพอทุกกรอบเป็นไล่เฉดแล้ว bg_color = "สีบนสุด" เท่านั้น
//  สั่งเดี่ยว ๆ จะเปลี่ยนแค่สีบน ปล่อยสีล่างค้างเป็นของเก่า
//  เช่นปุ่ม SAVE เปลี่ยนเขียว -> ส้ม จะได้ไล่เฉด "ส้ม -> เขียวเข้ม" มั่วทันที
//
//  3 ตัวนี้ตั้งครบทั้ง 2 สต็อปให้เสมอ
// ---------------------------------------------------------------------------
void ui_grad(lv_obj_t *o, lv_color_t top, lv_color_t bot, lv_style_selector_t sel = 0);
void ui_tint(lv_obj_t *o, lv_color_t base, lv_style_selector_t sel = 0);  // ไล่เฉดจากสีเดียว
void ui_btn_color(lv_obj_t *b, lv_color_t base);   // ปุ่ม: ตั้งทั้งสีปกติ + สีตอนกด + สีตัวอักษร
lv_color_t ui_text_on(lv_color_t base);            // สีตัวอักษรที่อ่านออกบนพื้นสีนี้

// --- ตัวช่วยที่ใช้ร่วมกันหลายหน้า -------------------------------------------
lv_obj_t *ui_card(lv_obj_t *parent, int x, int y, int w, int h);

lv_obj_t *ui_button(lv_obj_t *parent, int x, int y, int w, int h,
                    const char *text, lv_color_t color, lv_event_cb_t cb, void *user);

#endif // UI_MAIN_H
