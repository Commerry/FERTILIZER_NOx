// ============================================================================
//  process.h  --  ลำดับการทำงานอัตโนมัติของเครื่อง
//
//  ทำ 3 อย่าง:
//    1. รอบเวลาของ CP / CB / FB / FP   (สเปกข้อ 2, 3, 4)
//    2. ควบคุมวาล์ว RV ตามค่า O2       (สเปกข้อ 6.1 / 6.2 / 6.3)
//    3. เทียบ O1 กับ O2 -> Alarm ถ้าค่าห่างกันเกินกำหนด
//
//  ทำงานเฉพาะตอน mode = AUTO และ runState = RUNNING เท่านั้น
//  ถ้าอยู่โหมด MANUAL ไฟล์นี้จะไม่แตะ output เลย (คนคุมเองจากหน้าจอ)
// ============================================================================
#ifndef PROCESS_H
#define PROCESS_H

#include <Arduino.h>
#include <stdint.h>

void process_begin();

// เรียกถี่ ๆ ใน loop() -- ต้องส่งค่า O2 ที่อ่านได้เข้ามา
//   o1     = เซนเซอร์หลัก (ใช้คุมวาล์ว)
//   o2     = เซนเซอร์รีเช็ค
//   o1_ok  = O1 อ่านค่าได้จริงไหม (ถ้าไม่ได้ จะไม่ขยับวาล์ว)
//   o2_ok  = O2 อ่านค่าได้จริงไหม (ถ้าไม่ได้ จะไม่เทียบ mismatch)
void process_task(float o1, float o2, bool o1_ok, bool o2_ok);

// --- สั่งงาน ----------------------------------------------------------------
void process_start();      // = กดปุ่ม START
void process_stop();       // = กดปุ่ม STOP -> ดับทุก output
bool process_is_running();

// --- สถานะ (เอาไปยัดลงเฟรม $D) ----------------------------------------------
uint8_t  process_run_state();    // LNK_RUN_*
uint8_t  process_rv_zone();      // LNK_RVZ_*
uint32_t process_elapsed_sec();  // เดินมากี่วินาทีแล้ว
bool     process_o2_mismatch();  // O1 กับ O2 ค่าห่างกันเกินกำหนดนานเกินไป

// เคลียร์ alarm mismatch ที่ค้างอยู่ (ปุ่ม A0 กดค้าง 5 วิ)
// ถ้าเงื่อนไขยังอยู่จริง มันจะจับเวลาแล้วเตือนใหม่หลัง o2DiffSec (= acknowledge)
void     process_reset_alarm();

// เหลืออีกกี่ ms อุปกรณ์ตัวนี้ถึงจะสลับ ติด<->ดับ  (0 = ไม่มีรอบ)
// ใช้ทำ countdown บนหน้าจอ ("CP จะดับอีก 45 วิ")
uint32_t process_next_change_ms(uint8_t dev);

#endif // PROCESS_H
