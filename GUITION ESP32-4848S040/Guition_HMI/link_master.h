// ============================================================================
//  link_master.h  --  ฝั่งจอของลิงก์ไป Arduino Nano
//
//  ESP32 เป็น "master" ของลิงก์:
//    - รับ $D (telemetry) ที่ Nano ส่งมาเองทุก 200 ms  -> อัปเดต g_app
//    - ส่ง $C / $S / $G / $M แล้วรอ $A (ACK)           -> ส่งซ้ำถ้าไม่ตอบ
//
//  ทำไมต้อง retry: Nano มี HW UART buffer แค่ 64 byte และบางจังหวะมันติด
//  poll Modbus อยู่ -> คำสั่งอาจตกได้ การส่งซ้ำจนได้ ACK คือสิ่งที่ทำให้
//  ลิงก์นี้เชื่อถือได้จริงบนฮาร์ดแวร์ที่มีข้อจำกัด
// ============================================================================
#ifndef LINK_MASTER_H
#define LINK_MASTER_H

#include <Arduino.h>
#include <stdint.h>

void link_begin();
void link_task();       // เรียกถี่ ๆ ใน loop()

// เก็บสแนปช็อตเซนเซอร์ลง DATA log เป็นช่วง -- เรียกใน loop (จับเวลาเอง)
void link_data_tick();

// เก็บ DATA ทุกกี่วินาที (8640 แถว/วัน ที่ 10 วิ -- ~0.5 MB/วัน สบาย ๆ)
#define LOG_DATA_INTERVAL_S   10

// --- คำสั่ง (ไม่ block -- เข้าคิวแล้วคืนทันที) --------------------------------
void link_cmd_relay(uint8_t idx, bool on);
void link_cmd_valve(uint8_t percent);
void link_cmd_mode(uint8_t mode);
void link_cmd_estop();
void link_cmd_start();       // = กดปุ่ม START บนตู้
void link_cmd_stop();        // = กดปุ่ม STOP บนตู้
void link_cmd_save();
void link_cmd_get_config();
void link_cmd_modbus_raw(uint8_t slave, uint8_t fc, uint16_t reg, uint8_t n);
void link_cmd_modbus_write(uint8_t slave, uint16_t reg, uint16_t value);   // FC06 -- ตั้ง ID
void link_cmd_set_config(uint8_t key, float value);

// --- callback ให้ UI เอาไปโชว์ ------------------------------------------------
// ทุกเฟรมดิบที่รับเข้ามา (ไว้โชว์ในหน้า Diagnostics)
typedef void (*LinkLogCb)(const char *line, bool isTx);
void link_set_log_cb(LinkLogCb cb);

// ผลของ $M (Modbus scanner)  -- words = nullptr แปลว่าอ่านไม่สำเร็จ
typedef void (*LinkRawCb)(const uint16_t *words, uint8_t n);
void link_set_raw_cb(LinkRawCb cb);

// ผลของ $G  (ไล่มาทีละ key)
typedef void (*LinkKvCb)(uint8_t key, float value);
void link_set_kv_cb(LinkKvCb cb);

#endif // LINK_MASTER_H
