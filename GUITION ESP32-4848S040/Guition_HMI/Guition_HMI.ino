// ============================================================================
//  Guition_HMI  --  หน้าจอควบคุมระบบ (GUITION ESP32-4848S040)
//
//  จอทำหน้าที่ 2 อย่าง:
//    1. รับ telemetry ($D) จาก Arduino Nano มาแสดงผล
//    2. ส่งคำสั่ง ($C/$S/$M) กลับไปให้ Nano ทำงานจริง
//
//  Nano คือตัวที่คุมของจริง -- จอไม่เคยสั่ง output ตรง ๆ และไม่เก็บสถานะเอง
//  ทุกอย่างที่เห็นบนจอคือสิ่งที่ Nano รายงานกลับมาเท่านั้น
//
//  --- Arduino IDE board settings (ต้องตรงเป๊ะ ไม่งั้นจอขาว/บูตวน) ---
//    Board            : ESP32S3 Dev Module
//    Flash Size       : 16MB (128Mb)
//    Flash Mode       : QIO 80MHz
//    PSRAM            : OPI PSRAM        <-- สำคัญที่สุด
//    USB CDC On Boot  : Disabled
//    Partition Scheme : Huge APP (3MB No OTA/1MB SPIFFS)
//
//  *** ถอดสายที่ต่อไป Arduino Nano ออกจาก header ก่อนอัปโหลดทุกครั้ง ***
//      (GPIO43/44 เป็นสายเดียวกับชิพ USB -- ชนกันแน่นอน)
//
//  ถ้าจอไม่ขึ้น: ดูไฟ backlight
//      กะพริบถี่ ๆ = display_init() ล้มเหลว (99% คือ PSRAM ไม่ได้ตั้ง OPI)
//      ดับสนิท     = ไปไม่ถึง ledcAttach เลย
// ============================================================================
#include <Arduino.h>
#include <lvgl.h>

#include "board_pins.h"
#include "display_driver.h"
#include "link_protocol.h"
#include "link_master.h"
#include "app_state.h"
#include "ui.h"
#include "rtclock.h"
#include "sdlog.h"

// ---------------------------------------------------------------------------
void setup()
{
    // ห้าม Serial.begin() เอง -- link_begin() จะเปิด UART0 บนขา 43/44 ให้
    // และห้าม Serial.print() ที่ไหนเลย มันจะไปปนกับเฟรมที่ Nano รออยู่

    if (!display_init()) {
        // panel หรือ LVGL ขึ้นไม่ได้ -- กะพริบ backlight บอกว่าเจ๊ง
        pinMode(LCD_BL, OUTPUT);
        for (;;) {
            digitalWrite(LCD_BL, HIGH); delay(120);
            digitalWrite(LCD_BL, LOW);  delay(120);
        }
    }

    clock_begin();  // โหลดเวลาที่ตั้งไว้ล่าสุดจาก NVS
    sdlog_begin();  // สร้าง task เขียน SD บนคอร์ 0 (ต้องหลัง display_init -- บัส SPI ว่างแล้ว)

    ui_begin();     // หน้า SETUP จะขอ config จาก Nano ($G) ให้เองตอนถูกสร้าง
                    // (คำสั่งแค่เข้าคิว -- ยิงออกจริงตอน link_task() รอบแรก)
    link_begin();
}

void loop()
{
    link_task();        // รับ $D / ส่งคำสั่ง + retry
    display_task();     // LVGL วาดจอ + อ่าน touch

    link_data_tick();   // เก็บสแนปช็อตเซนเซอร์ลง SD เป็นช่วง (จับเวลาเอง)
    clock_task();       // เซฟเวลาลง NVS เป็นระยะ (จับเวลาเอง)

    // อัปเดตตัวเลขบนจอ 10 Hz ก็พอ -- เร็วกว่านี้ตาไม่ทัน แต่เปลือง CPU
    static uint32_t lastUi = 0;
    if (millis() - lastUi >= 100) {
        lastUi = millis();
        ui_tick();
    }

    delay(2);
}
