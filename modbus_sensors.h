// ============================================================================
//  modbus_sensors.h  --  RS485 / Modbus RTU (MAX485 สองตัว = 2 บัสแยก)
//
//  Modbus RTU อยู่บน SoftwareSerial (D2/D3) -- เขียน RTU เอง ไม่ใช้ ModbusMaster
//    บัส 1 (DE=D6) : O2#1 id1 + O2#2 id2
//    บัส 2 (DE=D7) : pressure id3 (แยกเพราะมันตรึงบัสค้าง -- ดู config.h ข้อ 2)
//  โค้ดเลือกบัสให้เองจาก slave id  ผู้เรียกไม่ต้องรู้เรื่องบัส
// ============================================================================
#ifndef MODBUS_SENSORS_H
#define MODBUS_SENSORS_H

#include <Arduino.h>
#include <stdint.h>
#include "settings.h"

void mb_begin();

// เรียกถี่ ๆ ใน loop() -- poll เซนเซอร์ทีละตัวแบบวนไป ไม่ block นาน
void mb_task();

// ค่าล่าสุดที่อ่านได้ (ถ้าเซนเซอร์ตัวนั้น fail อยู่ ค่าจะค้างที่ค่าเก่า)
float mb_value(uint8_t sensorIdx);      // 0=O2A 1=O2B 2=PRESSURE
bool  mb_ok(uint8_t sensorIdx);         // false = อ่านไม่ได้
uint8_t mb_errflags();                  // รวมเป็น LNK_F_* ให้ยัดลงเฟรม $D ได้เลย

// --- Modbus Scanner (ใช้จากหน้า Diagnostics บนจอ) --------------------------
//  อ่าน register ดิบ ๆ ตามที่จอสั่ง -- นี่คือเครื่องมือที่เราจะใช้
//  "หา register map จริง" ของเซนเซอร์ โดยไม่ต้องคอมไพล์ใหม่
//
//  คืนจำนวน word ที่อ่านได้ (0 = ไม่ตอบ / exception)
uint8_t mb_raw_read(uint8_t slave, uint8_t fc, uint16_t reg, uint8_t n, uint16_t *out);

// เขียน register เดียว (FC06) -- ใช้ตั้ง Slave ID  คืน true ถ้า echo ตรง
bool    mb_raw_write(uint8_t slave, uint16_t reg, uint16_t val);

// ถอดค่าดิบเป็นเลขจริงตาม dtype/scale/offset -- แชร์กันใช้ทั้ง poll และ scanner
float mb_decode(const uint16_t *w, uint8_t nRegs, uint8_t dtype, float scale, float offset);

#endif // MODBUS_SENSORS_H
