// ============================================================================
//  logbook.h  --  Nano เป็น "เจ้าของความจริง" ของ event log
//
//  Nano จับเหตุการณ์ ณ วินาทีที่เกิด (ระดับ loop) แล้วส่ง $L ให้ ESP32 ไปเขียน SD
//  ESP32 ตอบ $P บอกว่ารับถึง seq ไหนแล้ว -> Nano ทิ้ง record นั้น
//  record ที่ยังไม่ถูก ack จะถูกส่งซ้ำ -> ลิงก์กระตุกแล้วเหตุการณ์ไม่หาย
//
//  ทำไมให้ Nano เป็นคนจับ ไม่ใช่ ESP32 เดาจาก telemetry:
//    telemetry มา 5 Hz -- เหตุการณ์ที่เกิดแล้วหายใน 200 ms (รีเลย์กระพริบ)
//    ESP32 จะไม่มีวันเห็น  ส่วน Nano เห็นทุกอย่างเพราะมันเป็นคนสั่งเอง
// ============================================================================
#ifndef LOGBOOK_H
#define LOGBOOK_H

#include <Arduino.h>
#include <stdint.h>

// เรียกใน setup() -- ก่อน auto-start  (เพื่อให้ START ของ auto-start ถูกจับ)
void logbook_begin(uint8_t mode0);

// เรียกทุก loop พร้อม "สถานะปัจจุบัน" -- เทียบกับครั้งก่อน แล้วคิว record ให้เอง
void logbook_note(uint8_t relay, uint8_t mode, uint8_t run,
                  uint8_t zone, uint8_t errflags, bool estop);

// คิว record ตรง ๆ (สำหรับเหตุการณ์ที่ไม่มี state ให้ diff เช่น SAVED)
void logbook_event(uint8_t type, uint8_t a, uint8_t b);

// เรียกทุก loop -- ส่ง record ที่ยังไม่ถูก ack (ทั้งครั้งแรกและส่งซ้ำ)
void logbook_task();

// ESP32 ตอบ $P มา -> ทิ้ง record ที่ถึง seq นี้แล้ว
void logbook_ack(uint8_t seq);

uint8_t logbook_pending();     // เหลือ record ค้างกี่ตัว (ไว้โชว์ debug)

#endif // LOGBOOK_H
