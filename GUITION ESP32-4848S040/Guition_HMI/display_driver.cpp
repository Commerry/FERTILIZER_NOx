// ============================================================================
//  display_driver.cpp  --  ST7701S RGB panel + GT911 touch + LVGL 9
//                          GUITION ESP32-4848S040
// ============================================================================
#include "display_driver.h"
#include "board_pins.h"
#include "st7701_guition_init.h"

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

// ---------------------------------------------------------------------------
//  Arduino_GFX
// ---------------------------------------------------------------------------
// 3-wire software SPI ใช้เฉพาะตอนยิง init sequence เข้า ST7701 เท่านั้น
// (ไม่มีขา DC และไม่มีขา RST บนบอร์ดนี้)
static Arduino_DataBus *bus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED /* MISO */);

static Arduino_ESP32RGBPanel *panel = new Arduino_ESP32RGBPanel(
    LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
    LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
    LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
    LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
    LCD_HSYNC_POLARITY, LCD_HSYNC_FRONT_PORCH, LCD_HSYNC_PULSE_WIDTH, LCD_HSYNC_BACK_PORCH,
    LCD_VSYNC_POLARITY, LCD_VSYNC_FRONT_PORCH, LCD_VSYNC_PULSE_WIDTH, LCD_VSYNC_BACK_PORCH,
    LCD_PCLK_ACTIVE_NEG, LCD_PCLK_HZ);

static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, panel, 0 /* rotation */, true /* auto_flush */,
    bus, GFX_NOT_DEFINED /* RST -- ไม่ได้ต่อ */,
    st7701_guition_init_operations, sizeof(st7701_guition_init_operations));

// ---------------------------------------------------------------------------
//  GT911 touch -- เขียนเองผ่าน Wire
//
//  ทำไมไม่ใช้ TAMC_GT911: ไลบรารีนั้นบังคับ pinMode()/digitalWrite() บนขา
//  INT และ RST ตอน begin() แต่บอร์ดนี้ "ไม่ได้ต่อ" ขาทั้งสอง -- ส่ง -1 เข้าไป
//  จะกลายเป็น pinMode(255) ซึ่งเป็น GPIO ที่ไม่มีจริง
//  เพราะ INT/RST ไม่ได้ต่อ address จึงถูกล็อกที่ 0x5D เสมอ
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  GT911 register map (จากดาต้าชีต) -- *** ระวัง offset ให้ดี ***
//
//    0x814E  status      bit7 = มีข้อมูลใหม่ , bit0-3 = จำนวนนิ้ว
//    0x814F  track ID
//    0x8150  X low   <-- จุดที่ 1 เริ่มที่นี่
//    0x8151  X high
//    0x8152  Y low
//    0x8153  Y high
//    0x8154  size low
//    0x8155  size high
//
//  เคยพลาดตรงนี้มาแล้ว: อ่านเริ่มที่ 0x8150 แต่ดัน index เหมือนเริ่มที่ 0x814F
//  (นึกว่าไบต์แรกเป็น track ID) -> ทุก field เลื่อนไป 1 ไบต์
//  กลายเป็น x = X_high | (Y_low << 8) ซึ่งได้เลขมหาศาล แล้วถูก clamp
//  จนจุดสัมผัสไปกองอยู่มุมซ้ายล่างตลอด ไม่ว่าจะแตะตรงไหน -> กดปุ่มไม่ติดเลย
// ---------------------------------------------------------------------------
#define GT911_REG_STATUS   0x814E
#define GT911_REG_POINT1   0x8150   // ไบต์แรกคือ X low ไม่ใช่ track ID
#define GT911_REG_PRODUCT  0x8140   // 4 ไบต์ ASCII เช่น "911"

static bool     s_touch_alive = false;
static int16_t  s_last_x = 0, s_last_y = 0;

static bool gt911_read(uint16_t reg, uint8_t *out, size_t len)
{
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;   // repeated start

    size_t got = Wire.requestFrom((int)TOUCH_ADDR, (int)len);
    if (got != len) return false;

    for (size_t i = 0; i < len; i++) {
        int v = Wire.read();
        if (v < 0) return false;     // Wire.read() คืน int และเป็น -1 ได้
        out[i] = (uint8_t)v;         // ถ้าไม่ตรวจ -1 จะกลายเป็น 0xFF เงียบ ๆ
    }
    return true;
}

static bool gt911_write(uint16_t reg, uint8_t val)
{
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

static void gt911_init()
{
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(400000);

    // GT911 ต้องการเวลาบูตหลังจ่ายไฟ -- INT/RST ไม่ได้ต่อ จึงบังคับ reset ไม่ได้
    delay(100);

    uint8_t id[4] = {0};
    s_touch_alive = gt911_read(GT911_REG_PRODUCT, id, 4);
    // id ควรอ่านได้เป็น "911" -- ถ้าอ่านไม่ได้แปลว่าสาย I2C มีปัญหา
}

// ---------------------------------------------------------------------------
//  อ่านสถานะ touch
//
//  *** จุดที่เคยทำให้สัมผัสไม่เสถียร ***
//  GT911 มี "data ready" flag (bit7 ของ status)
//  flag = 0 แปลว่า "ยังไม่มีข้อมูลใหม่ตั้งแต่ครั้งที่แล้ว"
//  มัน *ไม่ได้* แปลว่า "ปล่อยนิ้วแล้ว"
//
//  โค้ดเดิมคืน false ตอน flag = 0 -> LVGL เห็นเป็น RELEASED
//  พอรอบถัดไปมีข้อมูล ก็เห็นเป็น PRESSED อีก
//  => กด-ปล่อย-กด-ปล่อย รัว ๆ ทั้งที่นิ้วแตะค้างอยู่
//     ปุ่มเลยกดติดบ้างไม่ติดบ้าง ลากสไลเดอร์ไม่ได้
//
//  ของจริง: "ปล่อยนิ้ว" = มีข้อมูลใหม่ และจำนวนจุด (bit0-3) เป็น 0 เท่านั้น
//  ถ้าไม่มีข้อมูลใหม่ / I2C พลาด -> ต้อง "คงสถานะเดิม" ไว้
// ---------------------------------------------------------------------------
#define GT911_POLL_MS   12       // ~80 Hz พอ -- อ่านถี่กว่านี้แค่ทำให้ I2C แน่นเปล่า ๆ

static bool     s_pressed = false;   // สถานะที่ยืนยันแล้ว (เป็น "ความจริง" ที่ส่งให้ LVGL)
static uint32_t s_readOk  = 0;       // นับไว้ไล่ปัญหา: อ่าน GT911 สำเร็จกี่ครั้ง
static uint32_t s_readErr = 0;       // I2C พลาดกี่ครั้ง

static bool gt911_poll(int16_t *x, int16_t *y)
{
    static uint32_t lastMs = 0;

    uint32_t now = millis();
    if (now - lastMs < GT911_POLL_MS) {          // ยังไม่ถึงรอบ -> ใช้ค่าเดิม
        *x = s_last_x; *y = s_last_y;
        return s_pressed;
    }
    lastMs = now;

    uint8_t status;
    if (!gt911_read(GT911_REG_STATUS, &status, 1)) {
        s_touch_alive = false;
        s_readErr++;
        *x = s_last_x; *y = s_last_y;
        return s_pressed;                        // I2C พลาดชั่วคราว -> คงสถานะเดิม
    }
    s_touch_alive = true;
    s_readOk++;

    if (!(status & 0x80)) {                      // ไม่มีข้อมูลใหม่ -> คงสถานะเดิม
        *x = s_last_x; *y = s_last_y;
        return s_pressed;
    }

    uint8_t n = status & 0x0F;                   // จำนวนนิ้วที่แตะอยู่

    if (n >= 1 && n <= 5) {
        uint8_t p[6];
        if (gt911_read(GT911_REG_POINT1, p, 6)) {
            // p[0]=X low  p[1]=X high  p[2]=Y low  p[3]=Y high  p[4..5]=size
            int16_t nx = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            int16_t ny = (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));

            if (nx < 0) nx = 0; else if (nx >= LCD_WIDTH)  nx = LCD_WIDTH  - 1;
            if (ny < 0) ny = 0; else if (ny >= LCD_HEIGHT) ny = LCD_HEIGHT - 1;

            s_last_x  = nx;
            s_last_y  = ny;
            s_pressed = true;
        }
    } else {
        s_pressed = false;                       // n == 0 = ปล่อยนิ้วจริง ๆ
    }

    gt911_write(GT911_REG_STATUS, 0);            // ต้องเคลียร์ ไม่งั้นจะไม่มีจุดใหม่

    *x = s_last_x;
    *y = s_last_y;
    return s_pressed;
}

// ---------------------------------------------------------------------------
//  LVGL glue
// ---------------------------------------------------------------------------
static lv_display_t *s_disp = nullptr;
static lv_color_t   *s_draw_buf = nullptr;

// LVGL 9 ต้องรู้เวลาเอง -- ป้อน millis() ให้
static uint32_t lv_millis_cb(void) { return millis(); }

static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
    lv_display_flush_ready(disp);
}

static void lv_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    int16_t x, y;
    bool pressed = gt911_poll(&x, &y);

    // ตอนปล่อยนิ้วก็ยังส่งพิกัดล่าสุดไป (gt911_poll คืนค่านั้นให้อยู่แล้ว)
    // ไม่งั้น LVGL จะนับว่าคลิกที่ (0,0)
    data->point.x = x;
    data->point.y = y;
    data->state   = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

// ---------------------------------------------------------------------------
//  Backlight
// ---------------------------------------------------------------------------
void display_set_backlight(uint8_t percent)
{
    if (percent > 100) percent = 100;
    ledcWrite(LCD_BL, (uint32_t)(percent * 255 / 100));
}

// ---------------------------------------------------------------------------
//  init
// ---------------------------------------------------------------------------
bool display_init()
{
    // 1) panel ต้องขึ้นก่อน -- ไฟ backlight ยังดับอยู่ จะได้ไม่เห็นจอขยะตอน init
    if (!gfx->begin()) return false;
    gfx->fillScreen(RGB565_BLACK);

    // 2) backlight (core 3.x: ledcAttach ผูก channel ให้เอง)
    ledcAttach(LCD_BL, LCD_BL_LEDC_FREQ, LCD_BL_LEDC_BITS);
    display_set_backlight(100);

    // 3) touch
    gt911_init();

    // 4) LVGL
    lv_init();
    lv_tick_set_cb(lv_millis_cb);

    // draw buffer 40 บรรทัด (480 x 40 x 2 ไบต์ = 37.5 KB) อยู่ใน internal RAM
    // ไม่ใช้ full framebuffer เพราะ framebuffer จริงอยู่ใน PSRAM แล้ว
    // (PARTIAL mode -- LVGL วาดทีละแถบแล้ว copy ลง panel)
    const uint32_t buf_px    = LCD_WIDTH * 40;
    const uint32_t buf_bytes = buf_px * 2;

    s_draw_buf = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_draw_buf) s_draw_buf = (lv_color_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_8BIT);
    if (!s_draw_buf) return false;

    s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    if (!s_disp) return false;
    lv_display_set_flush_cb(s_disp, lv_flush_cb);
    lv_display_set_buffers(s_disp, s_draw_buf, nullptr, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lv_touch_cb);

    return true;
}

void display_task()
{
    lv_timer_handler();
}

bool     touch_is_alive()   { return s_touch_alive; }
bool     touch_is_pressed() { return s_pressed; }
uint32_t touch_read_count() { return s_readOk; }
uint32_t touch_fail_count() { return s_readErr; }

void touch_last_point(int16_t *x, int16_t *y)
{
    if (x) *x = s_last_x;
    if (y) *y = s_last_y;
}
