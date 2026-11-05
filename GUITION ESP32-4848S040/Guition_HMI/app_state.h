// ============================================================================
//  app_state.h  --  สถานะกลางของแอป
//
//  link_master เขียนที่นี่ / UI อ่านจากที่นี่
//  มีจุดเดียวที่เก็บ "ความจริง" เพื่อไม่ให้ UI กับลิงก์เห็นค่าคนละอย่าง
// ============================================================================
#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdint.h>

struct AppState {
    // --- ค่าจากเซนเซอร์ (มาจากเฟรม $D) ---
    float    o2a      = 0.0f;    // O1 -- เซนเซอร์หลัก (ตัวที่ใช้คุมวาล์ว)
    float    o2b      = 0.0f;    // O2 -- เซนเซอร์รีเช็ค
    float    pressure = 0.0f;

    // --- สถานะ actuator ที่ Nano รายงานกลับมา (ของจริง ไม่ใช่ที่เรากดสั่ง) ---
    uint8_t  relayBits = 0;      // bit0=CP bit1=CB bit2=FB bit3=FP
    uint8_t  valve     = 0;      // RV 0-100 %
    uint8_t  mode      = 0;      // LNK_MODE_MANUAL / AUTO
    uint8_t  errflags  = 0;      // LNK_F_*

    // --- สถานะกระบวนการ ---
    uint8_t  runState = 0;       // LNK_RUN_IDLE / LNK_RUN_RUNNING
    uint8_t  rvZone   = 0;       // LNK_RVZ_* -- ตอนนี้คุมวาล์วด้วยโหมดไหน
    uint32_t elapsed  = 0;       // เดินมากี่วินาทีแล้ว

    // --- สิทธิ์ที่ปลดล็อกอยู่ (ฝั่งจอล้วน ๆ ไม่เกี่ยวกับ Nano) ---
    uint8_t  accessLevel = 1;    // ACC_VIEW เป็นค่าเริ่มต้น

    // --- สุขภาพของลิงก์ ---
    bool     linkUp      = false;
    uint32_t lastFrameMs = 0;
    uint32_t rxFrames    = 0;
    uint32_t crcErrors   = 0;
    uint32_t txFrames    = 0;
    uint32_t ackTimeouts = 0;
    uint32_t seq         = 0;    // seq ล่าสุดที่ Nano ส่งมา

    bool relay(uint8_t i) const { return (relayBits >> i) & 0x01; }
};

extern AppState g_app;

#endif // APP_STATE_H
