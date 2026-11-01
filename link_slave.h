// ============================================================================
//  link_slave.h  --  ฝั่ง Nano ของลิงก์ไปจอ
//
//  หน้าที่:
//    - ส่ง telemetry ($D) ขึ้นจอทุก 200 ms โดยอัตโนมัติ
//    - รับคำสั่งจากจอ ($C / $S / $G / $M) แล้วตอบ ACK ($A)
//
//  ออกแบบให้ "ไม่ block": link_task() ต้องถูกเรียกถี่ ๆ ใน loop()
//  และต้องไม่มี delay() อยู่ข้างใน เพราะ Modbus ต้องได้เวลารันด้วย
// ============================================================================
#ifndef LINK_SLAVE_H
#define LINK_SLAVE_H

#include <Arduino.h>
#include <stdint.h>
#include "config.h"

// ลิงก์จออยู่บน Hardware UART (D0/D1) -- RX มี ISR + buffer 64B ของจริง
// (SoftwareSerial ย้ายไปเป็นของ RS485 ใน modbus_sensors.cpp แล้ว)
#define LINK_SERIAL   Serial

// ค่าที่จะถูกยัดลงเฟรม $D -- ให้ main sketch อัปเดต struct นี้
struct Telemetry {
    float    o2a      = 0.0f;   // O1 -- เซนเซอร์หลัก (ใช้คุมวาล์ว)
    float    o2b      = 0.0f;   // O2 -- เซนเซอร์รีเช็ค
    float    pressure = 0.0f;
    uint8_t  mode     = 0;      // LNK_MODE_MANUAL / LNK_MODE_AUTO
    uint8_t  errflags = 0;      // LNK_F_*
    uint8_t  runState = 0;      // LNK_RUN_IDLE / LNK_RUN_RUNNING
    uint8_t  rvZone   = 0;      // LNK_RVZ_*
    uint32_t elapsed  = 0;      // เดินมากี่วินาทีแล้ว
};

extern Telemetry g_tlm;

// callback ที่ main sketch ต้อง implement -- ถูกเรียกเมื่อจอสั่งงานมา
// คืน error code (LNK_ERR_NONE = สำเร็จ) แล้ว link_slave จะตอบ ACK ให้เอง
uint8_t link_on_command(const char *cmd, long a1, long a2);
uint8_t link_on_set_config(uint8_t key, float value);
void    link_on_get_config();                                   // ให้ตอบด้วย link_send_kv()
// fc 3/4 = อ่าน (arg = จำนวน register) · fc 6 = เขียน (arg = ค่าที่จะเขียน)
uint8_t link_on_modbus_raw(uint8_t slave, uint8_t fc, uint16_t reg, uint16_t arg, long seq);

void link_begin();
void link_task();      // เรียกถี่ ๆ ใน loop()

// ใช้ตอบเฟรม $G -- เรียกได้หลายครั้ง (ทีละ key)
void link_send_kv(uint8_t key, float value);

// เริ่มทยอยส่ง config ทั้งชุด ($K) แบบเว้นจังหวะ -- เรียกจาก link_on_get_config()
void link_begin_config_dump();

// ใช้ตอบเฟรม $M -- ส่ง register ดิบกลับขึ้นจอ
void link_send_raw(long seq, const uint16_t *words, uint8_t n);

// ส่ง log record 1 รายการ ($L) -- logbook เรียกตอน trickle-send
void link_send_log(uint8_t seq, uint32_t ms, uint8_t type, uint8_t a, uint8_t b);

// --- สถานะลิงก์ (ให้ main sketch เอาไปทำ errflag / LED) ---------------------
bool     link_is_up();          // เคยได้รับเฟรมจากจอภายใน LINK_LOSS_MS ล่าสุดไหม
uint32_t link_rx_ok();
uint32_t link_rx_crc_err();

#endif // LINK_SLAVE_H
