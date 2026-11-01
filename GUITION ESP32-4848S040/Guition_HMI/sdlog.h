// ============================================================================
//  sdlog.h  --  เขียน log ลง microSD  (บน task แยกคอร์ 0)
//
//  ดีไซน์เพื่อ "ประสิทธิภาพสูงสุด + ไม่กระทบทัช":
//    - main loop (คอร์ 1, LVGL/ทัช) แค่ push บรรทัดลง FreeRTOS queue -> ไม่บล็อก
//    - task เขียน SD อยู่คอร์ 0 คนละคอร์ -> ต่อให้การ์ดหน่วง 100ms ทัชก็ไม่สะดุด
//    - queue เต็ม (ไม่มีการ์ด/การ์ดช้า) -> ทิ้งบรรทัด + นับ dropped (ไม่ค้าง)
//
//  ไม่มีการ์ด = ทำงานปกติ  logging เป็นแค่ side-channel ตรวจการ์ดเป็นระยะ
//  เสียบเข้า -> mount เอง / ถอดออก -> ปิดไฟล์แล้วรอ mount ใหม่
//
//  4 ไฟล์แยกกัน (CSV เปิด Excel ได้):
//    /EVENTS/YYYYMMDD.csv   การทำงานทั้งหมด
//    /ALARMS/YYYYMMDD.csv   เฉพาะ alarm/error (ซับเซ็ตของ events)
//    /DATA/YYYYMMDD.csv     สแนปช็อตเซนเซอร์เป็นช่วง
//    /SYSTEM.csv            บูต / mount / ปัญหาการ์ด
// ============================================================================
#ifndef SDLOG_H
#define SDLOG_H

#include <Arduino.h>
#include <stdint.h>

enum {
    LOGF_EVENT = 0,
    LOGF_ALARM,
    LOGF_DATA,
    LOGF_SYSTEM,
    LOGF_COUNT
};

#define SDLOG_LINE_MAX   100     // ความยาวบรรทัด CSV สูงสุด (รวม null)

struct SdStat {
    bool     mounted;
    uint32_t sizeMB;
    uint32_t freeMB;
    uint32_t lines;          // เขียนสำเร็จกี่บรรทัดตั้งแต่บูต
    uint32_t dropped;        // ทิ้งกี่บรรทัด (queue เต็ม)
    uint8_t  queueUse;       // ตอนนี้คิวค้างกี่ตัว
    uint8_t  queueMax;       // ขนาดคิว
};

void sdlog_begin();                                  // สร้าง task + queue (เรียกใน setup หลัง display_init)
bool sdlog_push(uint8_t kind, uint32_t epoch, const char *line);   // false = ถูกทิ้ง (คิวเต็ม)
void sdlog_stat(SdStat *out);                        // อ่านสถิติไปโชว์หน้า DIAG
bool sdlog_present();                                 // การ์ด mount อยู่ไหม (ไว้ทำไอคอน)

#endif // SDLOG_H
