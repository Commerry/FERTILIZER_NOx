// ============================================================================
//  actuators.h  --  รีเลย์ 4 ตัว + วาล์ว RV (บอลวาล์ว on/off, 2 รีเลย์ จับเวลา)
//
//  ทุกการสั่ง output ของระบบต้องผ่านไฟล์นี้ที่เดียว
//  จะได้มีจุดเดียวที่บังคับ safety (E-STOP) และจุดเดียวที่รู้สถานะจริง
// ============================================================================
#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>
#include "config.h"

void    act_begin();                 // ตั้งขาเป็น OUTPUT + ดับทุกตัว (เรียกใน setup)

void    act_set_relay(uint8_t idx, bool on);   // idx 0..3
bool    act_get_relay(uint8_t idx);
uint8_t act_relay_bits();            // bit0 = relay1 ... bit3 = relay4 (ใช้ส่งขึ้นจอ)

void    act_set_valve(uint8_t percent);        // 0..100 -- ตั้ง "เป้าหมาย" ตำแหน่งวาล์ว
uint8_t act_get_valve();                       // ตำแหน่งจริง (ประเมิน) 0..100
void    act_valve_tick(uint32_t now);          // เรียกทุก loop -- ขับวาล์วเข้าหาเป้า (จับเวลา)

// E-STOP: ดับทุก output ทันที และ "ล็อก" ไม่ให้สั่งอะไรได้อีกจนกว่าจะ clear
void    act_estop();
void    act_estop_clear();
bool    act_is_estop();

// ดับทุก output แต่ไม่ล็อก (ใช้ตอนสลับโหมด / ตอนลิงก์หลุด)
void    act_all_off();

// พายลอตแลมป์ (ไฟสถานะ) -- เขียว = เดินเครื่อง, แดง = หยุด/alarm/estop
// ขับจาก loop() ตามสถานะจริง  (ไม่นับใน relaybits ของ $D)
void    act_set_pilot(bool green, bool red);

#endif // ACTUATORS_H
