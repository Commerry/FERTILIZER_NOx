// ============================================================================
//  board_pins.h  --  GUITION ESP32-4848S040  (ESP32-S3-WROOM-1 N16R8)
//                    480x480 IPS / ST7701S RGB565 / GT911 capacitive touch
//
//  ค่าทั้งหมดในไฟล์นี้ verify มาจากแหล่งที่ใช้งานจริงกับบอร์ดรุ่นนี้ ไม่ได้เดา:
//    - rzeldent/platformio-espressif32-sunton -> esp32-4848S040CIY3.json
//    - ESPHome device page: guition-esp32-s3-4848s040
//    - moononournation/Arduino_GFX issue #684
//    - paulhamsh/LVGL_GUITION (Arduino IDE reference ที่ใช้ได้จริง)
//
//  *** อย่าย้ายขาพวกนี้ *** ทุกขาเดินเป็น trace บน PCB แล้ว
// ============================================================================
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

// ---------------------------------------------------------------------------
//  ST7701S -- 3-wire software SPI (ใช้เฉพาะตอน init panel เท่านั้น)
//  หมายเหตุ: SCK/MOSI ใช้สายร่วมกับ SD card -> ต้อง init panel ให้เสร็จก่อน
//            แล้วค่อยเปิด SD (ถ้าจะใช้)
// ---------------------------------------------------------------------------
#define LCD_CS              39
#define LCD_SCK             48
#define LCD_MOSI            47
// LCD_RST ไม่ได้ต่อบนบอร์ดนี้ -> ใช้ GFX_NOT_DEFINED (อาศัย power-on reset)

// ---------------------------------------------------------------------------
//  RGB parallel interface (16-bit)
// ---------------------------------------------------------------------------
#define LCD_DE              18
#define LCD_VSYNC           17
#define LCD_HSYNC           16
#define LCD_PCLK            21

// R4 = GPIO0 = ปุ่ม BOOT ด้วย  -> กด BOOT ค้างแล้วสีแดงจะเพี้ยน เป็นเรื่องปกติ
#define LCD_R0              11
#define LCD_R1              12
#define LCD_R2              13
#define LCD_R3              14
#define LCD_R4               0

// G1 = GPIO20 = USB D+  -> native USB ใช้ไม่ได้ (ถูก panel ยึดไป)
#define LCD_G0               8
#define LCD_G1              20
#define LCD_G2               3
#define LCD_G3              46
#define LCD_G4               9
#define LCD_G5              10

#define LCD_B0               4
#define LCD_B1               5
#define LCD_B2               6
#define LCD_B3               7
#define LCD_B4              15

// ---------------------------------------------------------------------------
//  RGB timing -- ตัวเลขชุดนี้มาจาก esp32-smartdisplay board file
//  ถ้าจอกระพริบ/ภาพเลื่อนแนวนอน: ลด PCLK ลงเป็น 8 หรือ 6 MHz
// ---------------------------------------------------------------------------
#define LCD_WIDTH           480
#define LCD_HEIGHT          480

#define LCD_PCLK_HZ         12000000

#define LCD_HSYNC_POLARITY  1
#define LCD_HSYNC_FRONT_PORCH   10
#define LCD_HSYNC_PULSE_WIDTH    8
#define LCD_HSYNC_BACK_PORCH    50

#define LCD_VSYNC_POLARITY  1
#define LCD_VSYNC_FRONT_PORCH   10
#define LCD_VSYNC_PULSE_WIDTH    8
#define LCD_VSYNC_BACK_PORCH    20

#define LCD_PCLK_ACTIVE_NEG  0

// ---------------------------------------------------------------------------
//  Backlight -- ต่อตรงเข้า GPIO (บอร์ดนี้ไม่มี IO expander) active HIGH
// ---------------------------------------------------------------------------
#define LCD_BL              38
#define LCD_BL_LEDC_CH       7
#define LCD_BL_LEDC_FREQ  5000
#define LCD_BL_LEDC_BITS     8

// ---------------------------------------------------------------------------
//  GT911 capacitive touch (I2C)
//  INT และ RST ไม่ได้ต่อบนบอร์ดนี้ -> I2C address ถูกล็อกที่ 0x5D
//  (จะบังคับเป็น 0x14 ไม่ได้ เพราะต้องขยับ INT ตอน reset ซึ่งทำไม่ได้)
//  GPIO45 = SCL เป็น strapping pin -> ตอน boot จะมี warning แต่ไม่มีผลตอนรัน
// ---------------------------------------------------------------------------
#define TOUCH_SDA           19
#define TOUCH_SCL           45
#define TOUCH_INT           -1     // ไม่ได้ต่อ
#define TOUCH_RST           -1     // ไม่ได้ต่อ
#define TOUCH_ADDR        0x5D
#define TOUCH_MAX_X        480
#define TOUCH_MAX_Y        480

// ---------------------------------------------------------------------------
//  UART0 -> header "5V / TXD / RXD / GND"  = ลิงก์ไป Arduino Nano
//
//  *** สำคัญ: GPIO43/44 เป็นสายเดียวกับที่ชิพ CH340 (USB) ใช้ ***
//      - ตอนอัปโหลดโปรแกรม ต้องถอดสาย Nano ออกจาก header ก่อน
//      - ห้ามใช้ Serial.print เพื่อ debug ระหว่างที่ลิงก์ทำงาน
//        (มันจะไปปนกับเฟรม $... ที่ Nano รออยู่)
//        -> ให้ดู log ที่หน้า Diagnostics บนจอแทน
//
//  GPIO44 (RX) รับ 3.3V เท่านั้น -> Nano TX (5V) ต้องผ่าน divider 1k/2k
// ---------------------------------------------------------------------------
#define NANO_UART_NUM        0
#define NANO_TX             43
#define NANO_RX             44

// ---------------------------------------------------------------------------
//  microSD (TF slot) -- SPI
//
//  verify แล้ว: CS=42, MISO=41 เป็นของ SD เอง  ส่วน SCK=48/MOSI=47 ใช้ "สายร่วม"
//  กับ ST7701 (LCD_SCK/LCD_MOSI)  แต่ ST7701 ใช้ SPI แค่ตอน init panel ตอนบูต
//  เสร็จแล้วจอวิ่งบน RGB parallel -> บัส SPI ว่าง SD ใช้ต่อได้เต็มที่
//  (ที่มา: rzeldent sunton board file + Tasmota #20527 + ESPHome device page)
//
//  *** ต้อง display_init() ให้เสร็จก่อน แล้วค่อย sdlog_begin() ***
// ---------------------------------------------------------------------------
#define SD_CS               42
#define SD_MISO             41
#define SD_SCK              48     // = LCD_SCK  (ใช้ร่วม -- panel init เสร็จแล้วว่าง)
#define SD_MOSI             47     // = LCD_MOSI
#define SD_SPI_HOST         FSPI   // S3: FSPI = SPI2 bus
#define SD_FREQ_HZ      20000000

// ---------------------------------------------------------------------------
//  GPIO ที่เหลืออยู่บนบอร์ดนี้ (เผื่ออนาคต) -- แทบไม่เหลือแล้ว
//    GPIO 40 / 2 / 1      : รีเลย์บนบอร์ด (รุ่น Y_3) หรือ I2S amp -- เลือกได้อย่างเดียว
//    GPIO 35 / 36 / 37    : ใช้ไม่ได้ -- OPI PSRAM ยึดไว้
//    GPIO 19 / 20         : ใช้ไม่ได้ -- native USB ถูก touch/panel ยึดไป
// ---------------------------------------------------------------------------

#endif // BOARD_PINS_H
