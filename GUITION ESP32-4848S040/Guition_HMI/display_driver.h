// ============================================================================
//  display_driver.h  --  จอ + touch + LVGL สำหรับ GUITION ESP32-4848S040
//
//  ห่อ Arduino_GFX (ST7701S RGB panel) + GT911 (I2C touch) + LVGL 9
//  ให้เหลือแค่ 3 ฟังก์ชันให้ส่วนอื่นเรียกใช้
// ============================================================================
#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <lvgl.h>
#include <stdint.h>

// เรียกครั้งเดียวใน setup() -- คืน false ถ้า panel หรือ LVGL ขึ้นไม่สำเร็จ
bool display_init();

// เรียกถี่ ๆ ใน loop() -- ขับ LVGL (วาดจอ + อ่าน touch)
void display_task();

// ความสว่างไฟ backlight 0-100 %
void display_set_backlight(uint8_t percent);

// --- ใช้ไล่ปัญหา touch -------------------------------------------------------
bool     touch_is_alive();                       // GT911 ตอบ I2C มาไหม
bool     touch_is_pressed();                     // ตอนนี้นิ้วแตะอยู่ไหม (ค่าที่ส่งให้ LVGL)
void     touch_last_point(int16_t *x, int16_t *y);
uint32_t touch_read_count();                     // อ่าน GT911 สำเร็จมากี่ครั้ง
uint32_t touch_fail_count();                     // I2C พลาดมากี่ครั้ง

#endif // DISPLAY_DRIVER_H
