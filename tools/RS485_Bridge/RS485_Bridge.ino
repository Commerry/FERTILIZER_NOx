// ============================================================================
//  RS485_Bridge  --  ทำให้ Arduino Nano กลายเป็น "USB-to-RS485 adapter"
//
//  ใช้กับ Modbus Poll / QModMaster / โปรแกรม Modbus บนคอมได้เลย
//    คอม --USB(COM15)--> Nano --RS485(D8/D9)--> เซนเซอร์
//    เซนเซอร์ --RS485--> Nano --USB--> โปรแกรมบนคอม   (ส่งต่อดิบ ๆ ไม่แตะต้อง)
//
//  *** สเก็ตช์นี้ไม่ได้อ่านค่าเอง -- มันเป็นแค่ "สะพาน" ***
//      ให้ Modbus Poll เป็นคนคุยกับเซนเซอร์โดยตรง Nano แค่ส่งผ่าน
//
//  การต่อสาย (เหมือนสเก็ตช์ทดสอบเดิม -- ไม่ต้องย้ายสายเลย):
//        MAX485  RO -> D8      DI -> D9      DE+RE -> D6
//        A/B เข้าเซนเซอร์       ไฟ 5V/GND ปกติ
//
//  ---- ตั้งค่าใน Modbus Poll ----
//    Connection > Connect...
//        Connection      = Serial Port
//        Serial Settings = USB-SERIAL CH340 (COM15)
//        9600 Baud · 8 Data bits · None Parity · 1 Stop Bit
//        Mode = RTU      Response Timeout = 1000 ms
//    Setup > Read/Write Definition...
//        Slave ID = 1     Function = 03 Read Holding Registers
//        Address  = 0     Quantity = 25
//        (เอาเครื่องหมายถูก "PLC Addresses (Base 1)" ออก -> ให้เป็น address ดิบ)
//
//    -> แถว 6  = ค่า O2  (209 = 20.9 %)
//    -> แถว 14 = อุณหภูมิ
//    -> แถว 24 = ID เซนเซอร์
//
//  ---- ตั้ง ID (เช่นตัวที่ 2 ให้เป็น ID=2) ----
//    หา ID ปัจจุบันก่อน (ลองเปลี่ยน Slave ID 1..8 ในหน้าอ่านจนมีค่าขึ้น)
//    Functions > 06: Write Single Register
//        Address = 24   Value = 2   -> Send
//    จากนั้นเปลี่ยน Slave ID ในหน้าอ่านเป็น 2 เพื่อยืนยัน
// ============================================================================
#include <SoftwareSerial.h>

#define PIN_DE   6
#define GAP_US   2000          // ช่องว่างระหว่าง byte ที่ถือว่า "จบเฟรม" (~2 ตัวอักษร @9600)

SoftwareSerial mod(8, 9);      // RX=D8 (<-RO) , TX=D9 (->DI)

void setup()
{
    pinMode(PIN_DE, OUTPUT);
    digitalWrite(PIN_DE, LOW);         // เริ่มที่โหมด "รับ" (ฟังบัส)
    Serial.begin(9600);                // <- คุยกับคอม  (USB / COM15)
    mod.begin(9600);                   // <- คุยกับเซนเซอร์ (RS485)
}

void loop()
{
    // ---- คอม -> เซนเซอร์ : เก็บทั้งเฟรมคำขอก่อน แล้วค่อยยิงออก RS485 ----
    if (Serial.available()) {
        uint8_t  buf[64];
        uint8_t  n    = 0;
        uint32_t last = micros();
        // อ่านจนกว่าจะเงียบเกิน GAP_US = จบเฟรมคำขอ (ตอนนี้ interrupt เปิด micros() แม่น)
        while ((uint32_t)(micros() - last) < GAP_US) {
            if (Serial.available() && n < sizeof(buf)) {
                buf[n++] = (uint8_t)Serial.read();
                last = micros();
            }
        }

        while (mod.available()) mod.read();   // เคลียร์ขยะเก่าในบัฟเฟอร์ RX ก่อนส่ง
        digitalWrite(PIN_DE, HIGH);           // ขับบัส (ส่ง)
        delayMicroseconds(50);                // ให้ไดรเวอร์ MAX485 ตื่น
        for (uint8_t i = 0; i < n; i++)
            mod.write(buf[i]);                // write() บล็อกจนบิตสุดท้ายออกจริง
        delayMicroseconds(100);               // เผื่อ stop bit ตัวสุดท้ายลงหลัก
        digitalWrite(PIN_DE, LOW);            // ปล่อยบัส -> กลับไปฟังคำตอบ
    }

    // ---- เซนเซอร์ -> คอม : ส่งต่อทุก byte ที่ได้กลับไปให้คอมแบบเรียลไทม์ ----
    while (mod.available())
        Serial.write((uint8_t)mod.read());
}
