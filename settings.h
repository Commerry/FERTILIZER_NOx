// ============================================================================
//  settings.h  --  config ที่เก็บใน EEPROM
//
//  หลักการ: "ห้าม hardcode อะไรที่หน้างานอาจต้องปรับ"
//    - register ของเซนเซอร์  -> ยังไม่มีดาต้าชีต ต้องหาด้วย Modbus Scanner
//    - เวลาเดินเครื่องของ CP/CB/FB/FP
//    - โซนควบคุมวาล์ว RV + ก้าวการปรับ + ค่า PID
//  ทั้งหมดตั้งจากหน้าจอได้ แล้วเขียนลง EEPROM
// ============================================================================
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <stdint.h>
#include "link_protocol.h"      // CFG_* keys, LNK_DT_*

// --- เซนเซอร์ Modbus 1 ตัว --------------------------------------------------
struct SensorCfg {
    uint8_t  slaveId;    // 1..247
    uint8_t  fc;         // 3 = holding register, 4 = input register
    uint16_t reg;        // start address
    uint8_t  nRegs;      // 1 หรือ 2
    uint8_t  dtype;      // LNK_DT_*
    float    scale;      // ค่าจริง = raw * scale + offset
    float    offset;
};

// --- รอบเวลาของอุปกรณ์ 1 ตัว (CP / CB / FB / FP) ----------------------------
//   รอ delay -> ติด on -> ดับ off -> ติด on -> ...
//
//   *** หน่วยเป็นมิลลิวินาที ***
//   เก็บเป็น ms เพื่อให้ตั้งได้ทั้งพัลส์สั้น (200 ms) และรอบยาว (3600 วินาที)
//   ในช่องเดียวกัน  (หน้าจอแสดงเป็นวินาทีทศนิยม 3 ตำแหน่งให้อ่านง่าย)
//
//   on  == 0  -> ไม่ทำงานเลย
//   off == 0  -> ติดค้างตลอด (หลังพ้น delay)
struct CycleCfg {
    uint32_t delayMs;
    uint32_t onMs;
    uint32_t offMs;
};

struct Settings {
    uint16_t  magic;                          // EEPROM_MAGIC

    SensorCfg sensor[CFG_SENSOR_COUNT];       // [0]=O1(หลัก) [1]=O2(รีเช็ค) [2]=PRESSURE
    uint32_t  mbBaud;
    uint16_t  pollMs;
    uint16_t  timeoutMs;

    CycleCfg  cyc[CFG_DEV_COUNT];             // [0]=CP [1]=CB [2]=FB [3]=FP

    // --- ควบคุมวาล์ว RV ---
    float     rv1Min, rv1Max;                 // โซน Rough  (%O2)
    float     rv2Min, rv2Max;                 // โซน Medium
    float     rv3Min, rv3Max;                 // โซน Fine
    float     coarseStep;                     // ก้าววาล์วโซน Rough  (%)
    float     midStep;                        // ก้าววาล์วโซน Medium (%)
    uint16_t  stepSec;                        // ก้าวทุกกี่วินาที
    float     o2Target;                       // เป้าหมาย PID (%O2)
    float     kp, ki, kd;
    uint8_t   rvMinOut, rvMaxOut;             // จำกัดช่วงเปิดวาล์ว (%)

    // --- Alarm: O1 กับ O2 ไม่ตรงกัน ---
    float     o2DiffMax;                      // ห่างกันเกินนี้ (%)
    uint16_t  o2DiffSec;                      // นานเกินนี้ -> Alarm

    // --- PIN 4 หลักของแต่ละระดับสิทธิ์ ---
    uint16_t  pinOperator;
    uint16_t  pinSupervisor;
    uint16_t  pinCommission;
    uint16_t  pinExpert;

    uint8_t   autoStart;                      // 1 = เปิดเครื่องแล้วเดินเลย ไม่ต้องกด START

    uint8_t   crc;                            // CRC-8 ของทุกไบต์ก่อนหน้า
};

extern Settings g_cfg;

void settings_load();        // อ่าน EEPROM -- CRC ไม่ผ่าน/ค่าเพี้ยน -> ใช้ default
void settings_save();        // เขียน EEPROM (คำนวณ CRC ให้เอง)
void settings_defaults();    // รีเซ็ตเป็นค่าเริ่มต้น (ยังไม่เขียน EEPROM)
bool settings_was_corrupt(); // true = ตอนบูตอ่านไม่ผ่าน -> ตั้ง LNK_F_CFG_BAD

// map ระหว่าง config key (จากจอ) <-> field จริงใน struct
bool  settings_set(uint8_t key, float value);   // false = key ผิด หรือค่านอกช่วง
float settings_get(uint8_t key);

#endif // SETTINGS_H
