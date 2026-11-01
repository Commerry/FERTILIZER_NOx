// ============================================================================
//  link_protocol.h  --  Arduino Nano  <-->  GUITION ESP32-4848S040  UART link
//
//  *** SHARED FILE -- ต้องเหมือนกันเป๊ะ ๆ ทั้ง 2 ฝั่ง ***
//  ถ้าแก้ไฟล์นี้ที่ฝั่งไหน ต้องคัดลอกไปทับอีกฝั่งเสมอ
//    Nano  : Arduinonano_Display_O2_pressure/link_protocol.h
//    ESP32 : GUITION ESP32-4848S040/Guition_HMI/link_protocol.h
//
//  เขียนแบบ header-only, ไม่มี dynamic allocation, ไม่มี String
//  จึงคอมไพล์ได้ทั้งบน AVR (2 KB RAM) และ ESP32-S3
//
//  รูปแบบเฟรม:   $<TYPE>,<field>,<field>,...*<CRC8 2 hex>\n
//
//  ทำไมต้องมี framing + CRC:
//    ตอน ESP32 บูต ROM bootloader จะพ่น log ออกทาง GPIO43 (TX) ด้วย
//    ขยะพวกนั้นจะตกเฟรม/ตก CRC แล้วถูกทิ้งเงียบ ๆ ไม่ทำให้ลิงก์พัง
// ============================================================================
#ifndef LINK_PROTOCOL_H
#define LINK_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ---------------------------------------------------------------------------
// พารามิเตอร์ของลิงก์
// ---------------------------------------------------------------------------
// Nano HW UART (D0/D1) <-> ESP32 UART0 (GPIO43/44)
// (RS485/Modbus อยู่ SoftwareSerial D2/D3 -- จอเป็นปลั๊กถอดได้ตอนอัปโหลด)
//
// *** เปลี่ยนค่านี้ต้องเปลี่ยนทั้ง 2 ฝั่งพร้อมกันเสมอ ***
// 38400 บน HW UART สบาย ๆ  ระหว่าง Nano คุย Modbus (SoftwareSerial ปิด
// interrupt ~1ms/byte) เฟรมจากจออาจตกหล่นบ้าง -> CRC+ACK+retry เอาอยู่
#define LINK_BAUD            38400
#define LINK_MAX_FRAME       128     // ความยาวเฟรมสูงสุดรวม $ ... *CRC\n
#define LINK_MAX_FIELDS      14      // จำนวน field สูงสุดหลัง TYPE

#define LINK_TELEMETRY_MS    200     // Nano ส่ง $D ทุก 200 ms (5 Hz)
#define LINK_ACK_TIMEOUT_MS  200     // ESP32 รอ ACK นานสุด
#define LINK_ACK_RETRIES     3       // ส่งซ้ำได้กี่ครั้ง
#define LINK_STALE_MS        1500    // ไม่ได้ $D เกินนี้ = ถือว่าลิงก์หลุด

// ---------------------------------------------------------------------------
// TYPE ของเฟรม (ตัวอักษรตัวแรกหลัง '$')
// ---------------------------------------------------------------------------
#define LNK_T_DATA    'D'   // Nano -> ESP32 : telemetry (ส่งเองอัตโนมัติ)
#define LNK_T_CMD     'C'   // ESP32 -> Nano : สั่งงาน actuator
#define LNK_T_SET     'S'   // ESP32 -> Nano : ตั้งค่า config (เขียน EEPROM)
#define LNK_T_GET     'G'   // ESP32 -> Nano : ขอ config ทั้งชุด
#define LNK_T_MB      'M'   // ESP32 -> Nano : Modbus raw read (ตัวช่วยหา register)
#define LNK_T_ACK     'A'   // Nano -> ESP32 : ตอบรับ
#define LNK_T_KV      'K'   // Nano -> ESP32 : ตอบ $G ทีละคู่ key/value
#define LNK_T_RAW     'R'   // Nano -> ESP32 : ตอบ $M เป็น raw register
#define LNK_T_LOG     'L'   // Nano -> ESP32 : บันทึกเหตุการณ์ 1 รายการ (ไปเขียน SD)
#define LNK_T_LOGACK  'P'   // ESP32 -> Nano : รับ log ถึง seq นี้แล้ว (Nano ทิ้งได้)

// ---------------------------------------------------------------------------
// เฟรม $L  ->  $L,<seq>,<ms>,<type>,<a>,<b>
//
//   Nano เป็น "เจ้าของความจริง" ว่าเกิดอะไรขึ้นเมื่อไหร่ (จับ ณ วินาทีที่เกิด
//   ในระดับ loop ไม่ใช่ระดับ telemetry 5Hz ที่ตกหล่นเหตุการณ์สั้น ๆ)
//   ส่ง record ที่ยังไม่ถูก ack ซ้ำเรื่อย ๆ -> ลิงก์กระตุกแล้วเหตุการณ์ไม่หาย
//
//   ESP32 = "มือเขียนดิสก์": รับ -> ประทับเวลาจริง -> เขียน SD -> ตอบ $P,<seq>
//   ack ทันทีที่ "รับ" ไม่ใช่ตอนเขียนเสร็จ (ถ้าไม่มีการ์ด ก็ยัง ack เพื่อให้
//   Nano ทิ้ง record ได้ ไม่ส่งซ้ำไม่จบ -- แค่ไม่ได้เก็บลงดิสก์)
//
//   seq เป็น uint8 วนรอบได้ -- เหตุการณ์เกิดไม่กี่ครั้งต่อนาที ack ทันเสมอ
// ---------------------------------------------------------------------------
#define LNK_LOG_BOOT    0   // Nano เพิ่งบูต (ตัวคั่น session + สั่ง ESP32 รีเซ็ต seq)
#define LNK_LOG_START   1   // เริ่มเดินเครื่อง
#define LNK_LOG_STOP    2   // หยุดเดินเครื่อง (ปกติ)
#define LNK_LOG_ESTOP   3   // E-STOP (หยุดฉุกเฉิน -> ลง ALARMS ด้วย)
#define LNK_LOG_MODE    4   // เปลี่ยนโหมด          a = LNK_MODE_*
#define LNK_LOG_ZONE    5   // เปลี่ยนโซนวาล์ว RV    a = LNK_RVZ_*
#define LNK_LOG_RELAY   6   // รีเลย์เปลี่ยน         a = ดัชนี CFG_DEV_*, b = 0/1
#define LNK_LOG_ALARM   7   // alarm ขึ้น/ลง        a = errflag bit (LNK_F_*), b = 1 ขึ้น/0 ลง
#define LNK_LOG_SAVED   8   // เขียน config ลง EEPROM

// ---------------------------------------------------------------------------
// รหัสคำสั่งของเฟรม $C  ->  $C,<seq>,<CMD>,<a1>,<a2>
// ---------------------------------------------------------------------------
#define LNK_CMD_RELAY   "RLY"   // a1 = index 0..3, a2 = 0/1   (ใช้ได้เฉพาะโหมด MANUAL)
#define LNK_CMD_VALVE   "VLV"   // a1 = 0..100 (%)             (ใช้ได้เฉพาะโหมด MANUAL)
#define LNK_CMD_MODE    "MOD"   // a1 = 0 = MANUAL, 1 = AUTO
#define LNK_CMD_ESTOP   "STP"   // ดับทุก output ทันที (ไม่มี arg)
#define LNK_CMD_PING    "PNG"   // ทดสอบลิงก์ (ไม่มี arg)
#define LNK_CMD_SAVE    "SAV"   // สั่ง commit config ลง EEPROM
#define LNK_CMD_START   "RUN"   // เริ่มกระบวนการ (เท่ากับกดปุ่ม START บนตู้)
#define LNK_CMD_STOP    "HLT"   // หยุดกระบวนการ -> ดับทุก output

// ---------------------------------------------------------------------------
// รหัส error ของเฟรม $A,<seq>,ERR,<code>
// ---------------------------------------------------------------------------
#define LNK_ERR_NONE        0
#define LNK_ERR_BAD_CMD     1   // ไม่รู้จักคำสั่ง
#define LNK_ERR_BAD_ARG     2   // arg นอกช่วง
#define LNK_ERR_BAD_KEY     3   // config key ไม่มีอยู่จริง
#define LNK_ERR_MODBUS      4   // Modbus ไม่ตอบ / exception
#define LNK_ERR_BUSY        5   // กำลังทำงานอื่นอยู่ ลองใหม่
#define LNK_ERR_LOCKED      6   // อยู่ในโหมด AUTO สั่งมือไม่ได้

// ---------------------------------------------------------------------------
// errflags ในเฟรม $D (bitmask) -- ใช้ทำไอคอนเตือนบนจอ
// ---------------------------------------------------------------------------
#define LNK_F_O2A_FAIL      0x01   // O1 (เซนเซอร์หลัก) อ่านไม่ได้
#define LNK_F_O2B_FAIL      0x02   // O2 (เซนเซอร์รีเช็ค) อ่านไม่ได้
#define LNK_F_PR_FAIL       0x04   // Pressure อ่านไม่ได้
#define LNK_F_MB_BUS        0x08   // RS485 bus เงียบสนิท (ไม่มีใครตอบเลย)
#define LNK_F_ESTOP         0x10   // อยู่ในสถานะ emergency stop
#define LNK_F_CFG_BAD       0x20   // EEPROM เสีย -> ใช้ค่า default
#define LNK_F_O2_MISMATCH   0x40   // O1 กับ O2 ค่าห่างกันเกินกำหนด -> เรียกช่างมาสอบเทียบ

// ---------------------------------------------------------------------------
// โหมดการทำงาน (field <mode> ในเฟรม $D)
// ---------------------------------------------------------------------------
#define LNK_MODE_MANUAL  0      // คนสั่งรีเลย์/วาล์วเองจากหน้าจอ
#define LNK_MODE_AUTO    1      // process ควบคุมเอง (timer + RV zone + PID)

// ---------------------------------------------------------------------------
// สถานะกระบวนการ (field <runState> ในเฟรม $D)
// ---------------------------------------------------------------------------
#define LNK_RUN_IDLE     0      // หยุดอยู่ -- output ดับหมด
#define LNK_RUN_RUNNING  1      // กด START แล้ว กำลังเดินตามลำดับ

// ---------------------------------------------------------------------------
// โซนควบคุมวาล์ว RV (field <rvZone> ในเฟรม $D)
//
// ไล่ตามค่า O2 ที่อ่านจาก O1 (เซนเซอร์หลัก):
//   O2 สูง = ต้องเติม N2 มากขึ้น = เปิดวาล์วมากขึ้น
// ---------------------------------------------------------------------------
#define LNK_RVZ_OFF      0      // ไม่ได้คุม (เครื่องหยุด หรือโหมด MANUAL)
#define LNK_RVZ_MAXOUT   1      // O2 สูงกว่าช่วง RV1 -> เปิดสุด ไล่อากาศออกเร็ว ๆ
#define LNK_RVZ_ROUGH    2      // 6.1 Rough control  -> ก้าวใหญ่ (coarseStep) open-loop
#define LNK_RVZ_MEDIUM   3      // 6.2 Medium control -> ก้าวเล็ก (midStep)   open-loop
#define LNK_RVZ_FINE     4      // 6.3 Fine tune      -> PID เข้าหา O2_Target

// ---------------------------------------------------------------------------
// Config keys  ->  ใช้ใน $S,<seq>,<key>,<value>  และ  $K,<key>,<value>
//
// เซนเซอร์ 3 ตัวใช้ block ละ 7 key เรียงกัน  =>  key = sensor*7 + field
// จงใจใช้ numeric key แทน string เพื่อประหยัด flash ฝั่ง Nano
// ชื่อที่คนอ่านรู้เรื่องอยู่ฝั่ง ESP32 (ui_settings.cpp) ซึ่ง flash เหลือเฟือ
// ---------------------------------------------------------------------------
#define CFG_SENSOR_STRIDE   7
#define CFG_SENSOR_O2A      0
#define CFG_SENSOR_O2B      1
#define CFG_SENSOR_PRESS    2
#define CFG_SENSOR_COUNT    3

// field offset ภายใน block ของเซนเซอร์แต่ละตัว
#define CFG_FLD_SLAVE_ID    0   // 1..247
#define CFG_FLD_FC          1   // 3 = holding reg, 4 = input reg
#define CFG_FLD_REG         2   // start address 0..65535
#define CFG_FLD_NREGS       3   // 1 หรือ 2
#define CFG_FLD_DTYPE       4   // ดู LNK_DT_* ด้านล่าง
#define CFG_FLD_SCALE       5   // ค่าจริง = raw * scale + offset
#define CFG_FLD_OFFSET      6

// key ระดับระบบ (ต่อท้าย block ของเซนเซอร์)
#define CFG_MB_BAUD_IDX     (CFG_SENSOR_COUNT * CFG_SENSOR_STRIDE)      // 21
#define CFG_MB_POLL_MS      (CFG_MB_BAUD_IDX + 1)                       // 22
#define CFG_MB_TIMEOUT_MS   (CFG_MB_BAUD_IDX + 2)                       // 23

// helper: หา key ของเซนเซอร์ตัวที่ s field f
#define CFG_KEY(s, f)   ((uint8_t)((s) * CFG_SENSOR_STRIDE + (f)))

// ---------------------------------------------------------------------------
//  Cyclic timer ของอุปกรณ์ 4 ตัว -- key 24..35 (block ละ 3)
//
//  อุปกรณ์แต่ละตัวทำงานเป็นรอบ นับจากตอนกด START:
//     รอ <delay>  ->  ติด <on>  ->  ดับ <off>  ->  ติด <on>  -> ...
//
//  *** หน่วยเป็น "มิลลิวินาที" ***
//  เก็บเป็น ms เพื่อให้ตั้งได้ทั้งพัลส์สั้น (เช่น 200 ms) และรอบยาว (3600 วินาที)
//  ในช่องเดียวกัน -- หน้าจอจะแสดงเป็นวินาทีทศนิยม 3 ตำแหน่งให้อ่านง่าย
//  ช่วงที่รับ: 0 .. 9,999,999 ms  (= 0 .. 9999.999 วินาที)
//
//  N2Gen ไม่อยู่ในนี้ -- สเปกระบุว่ามีสวิตช์ ON/OFF แยกของมันเอง
//  CB speed ก็ไม่อยู่ในนี้ -- ปรับด้วย VR ภายนอก
// ---------------------------------------------------------------------------
#define CFG_DEV_STRIDE      3
#define CFG_DEV_CP          0   // Circulation Pump   -> Relay 1 (D10)
#define CFG_DEV_CB          1   // Circulation Blower -> Relay 2 (D11)
#define CFG_DEV_FB          2   // Feed Blower        -> Relay 3 (D12)
#define CFG_DEV_FP          3   // Feed Pump          -> Relay 4 (D13)
#define CFG_DEV_COUNT       4

#define CFG_FLD_DELAY       0   // หน่วงกี่ ms หลังกด START ถึงจะเริ่มรอบแรก
#define CFG_FLD_ON          1   // ติดกี่ ms   (0 = ไม่ทำงานเลย)
#define CFG_FLD_OFF         2   // ดับกี่ ms   (0 = ติดค้างตลอดหลังพ้น delay)

#define CFG_TIME_MAX_MS     9999999UL   // เพดานของทุกช่องเวลา

#define CFG_TIMER_BASE      (CFG_MB_TIMEOUT_MS + 1)                     // 24
#define CFG_TKEY(d, f)      ((uint8_t)(CFG_TIMER_BASE + (d) * CFG_DEV_STRIDE + (f)))

// ---------------------------------------------------------------------------
//  ควบคุมวาล์ว RV -- key 36..52
//  ทุกค่าตั้งได้จากหน้าจอ (ผู้ใช้ระบุว่าจะปรับจูนหน้างานอีก)
// ---------------------------------------------------------------------------
#define CFG_RV_BASE         (CFG_TIMER_BASE + CFG_DEV_COUNT * CFG_DEV_STRIDE)  // 36

#define CFG_RV1_MIN         (CFG_RV_BASE + 0)    // 36  ขอบล่างโซน Rough   (%O2)  def 5
#define CFG_RV1_MAX         (CFG_RV_BASE + 1)    // 37  ขอบบนโซน Rough           def 20
#define CFG_RV2_MIN         (CFG_RV_BASE + 2)    // 38  ขอบล่างโซน Medium         def 3
#define CFG_RV2_MAX         (CFG_RV_BASE + 3)    // 39  ขอบบนโซน Medium           def 5
#define CFG_RV3_MIN         (CFG_RV_BASE + 4)    // 40  ขอบล่างโซน Fine           def 0
#define CFG_RV3_MAX         (CFG_RV_BASE + 5)    // 41  ขอบบนโซน Fine             def 3

#define CFG_RV_COARSE_STEP  (CFG_RV_BASE + 6)    // 42  ก้าววาล์วโซน Rough  (%)   def 5
#define CFG_RV_MID_STEP     (CFG_RV_BASE + 7)    // 43  ก้าววาล์วโซน Medium (%)   def 2
#define CFG_RV_STEP_SEC     (CFG_RV_BASE + 8)    // 44  ก้าวทุกกี่วินาที          def 10
                                                 //     (ระบบมี dead time -- ก้าวรัวจะแกว่ง)
#define CFG_O2_TARGET       (CFG_RV_BASE + 9)    // 45  เป้าหมาย O2 ของ PID (%)   def 1.5
#define CFG_PID_KP          (CFG_RV_BASE + 10)   // 46
#define CFG_PID_KI          (CFG_RV_BASE + 11)   // 47
#define CFG_PID_KD          (CFG_RV_BASE + 12)   // 48
#define CFG_RV_MIN_OUT      (CFG_RV_BASE + 13)   // 49  วาล์วเปิดต่ำสุด (%)  def 0
#define CFG_RV_MAX_OUT      (CFG_RV_BASE + 14)   // 50  วาล์วเปิดสูงสุด (%)  def 100

#define CFG_O2_DIFF_MAX     (CFG_RV_BASE + 15)   // 51  O1 vs O2 ห่างกันเกินนี้ (%) def 2
#define CFG_O2_DIFF_SEC     (CFG_RV_BASE + 16)   // 52  ห่างนานเกินนี้ -> Alarm    def 30

// ---------------------------------------------------------------------------
//  PIN ของ access level -- key 53..56
//  (ตัวเลข 4 หลัก เก็บเป็น uint16)
// ---------------------------------------------------------------------------
#define CFG_PIN_BASE        (CFG_O2_DIFF_SEC + 1)   // 53
#define CFG_PIN_OPERATOR    (CFG_PIN_BASE + 0)      // 53
#define CFG_PIN_SUPERVISOR  (CFG_PIN_BASE + 1)      // 54
#define CFG_PIN_COMMISSION  (CFG_PIN_BASE + 2)      // 55
#define CFG_PIN_EXPERT      (CFG_PIN_BASE + 3)      // 56

// เปิดเครื่องแล้วเริ่มเดินกระบวนการทันที โดยไม่ต้องกด START
// (0 = ต้องกด START เอง, 1 = เริ่มเองเลย)
#define CFG_AUTO_START      (CFG_PIN_BASE + 4)      // 57

// (ขารีเลย์ฟิกซ์ตายตัว D10-D13 -- ไม่มี config key แล้ว แก้ที่ config.h)
#define CFG_KEY_COUNT       (CFG_PIN_BASE + 5)      // 58

// ระดับสิทธิ์ (ตามตาราง Access level ในสเปก -- จงใจข้าม 5 ตามต้นฉบับ)
#define ACC_VIEW         1   // ไม่ต้องใส่ PIN -- ดูอย่างเดียว
#define ACC_OPERATOR     2   // + กด START / STOP
#define ACC_SUPERVISOR   3   // + แก้ TIMERS
#define ACC_COMMISSIONER 4   // + แก้ RV zones / steps
#define ACC_EXPERT       6   // + แก้ PID, Sensor config, PIN

// ชนิดข้อมูลที่ decode จาก Modbus register
#define LNK_DT_U16          0
#define LNK_DT_S16          1
#define LNK_DT_U32_ABCD     2   // word สูงมาก่อน
#define LNK_DT_U32_CDAB     3   // word ต่ำมาก่อน (word-swapped)
#define LNK_DT_F32_ABCD     4   // IEEE754 big-endian word order
#define LNK_DT_F32_CDAB     5   // IEEE754 word-swapped (พบบ่อยที่สุดในจีน)
#define LNK_DT_COUNT        6

// ---------------------------------------------------------------------------
// CRC-8 (poly 0x07, init 0x00) คิดจากทุกตัวอักษรระหว่าง '$' กับ '*' (ไม่รวม 2 ตัวนั้น)
// ---------------------------------------------------------------------------
static inline uint8_t link_crc8(const char *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (uint8_t b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

// ---------------------------------------------------------------------------
// เฟรมที่ parse แล้ว -- ตัวชี้ทั้งหมดชี้เข้าไปใน buffer ของ LinkReader
// (in-place, ไม่ copy)  จึงใช้ได้เฉพาะจนกว่าจะ feed ตัวอักษรถัดไป
// ---------------------------------------------------------------------------
struct LinkFrame {
    char        type;                     // 'D' 'C' 'A' ...
    uint8_t     count;                    // จำนวน field
    const char *field[LINK_MAX_FIELDS];   // field[0] มักเป็น seq

    long  asLong(uint8_t i, long dflt = 0) const {
        return (i < count && field[i][0]) ? atol(field[i]) : dflt;
    }
    float asFloat(uint8_t i, float dflt = 0.0f) const {
        return (i < count && field[i][0]) ? (float)atof(field[i]) : dflt;
    }
    const char *asStr(uint8_t i) const {
        return (i < count) ? field[i] : "";
    }
    bool is(uint8_t i, const char *s) const {
        return (i < count) && strcmp(field[i], s) == 0;
    }
};

// ---------------------------------------------------------------------------
// LinkReader -- ป้อนทีละ byte, คืน true เมื่อได้เฟรมที่ CRC ผ่าน
//
//   LinkReader rx;
//   LinkFrame  f;
//   while (Serial.available())
//       if (rx.feed(Serial.read(), f)) handle(f);
// ---------------------------------------------------------------------------
class LinkReader {
public:
    // ตัวนับสำหรับหน้า Diagnostics
    uint32_t rx_ok      = 0;   // เฟรมที่ CRC ผ่าน
    uint32_t rx_crc_err = 0;   // CRC ไม่ตรง  (สายรบกวน / ต่อผิด)
    uint32_t rx_dropped = 0;   // เฟรมยาวเกิน buffer หรือรูปแบบเพี้ยน

    void reset() { _len = 0; _sync = false; }

    bool feed(int c, LinkFrame &out)
    {
        if (c < 0) return false;
        char ch = (char)c;

        if (ch == '$') {                 // เริ่มเฟรมใหม่เสมอ ทิ้งของเก่าทิ้งไป
            _len  = 0;
            _sync = true;
            return false;
        }
        if (!_sync) return false;        // ยังไม่เจอ '$' -> เป็นขยะ (เช่น boot log)

        if (ch == '\r') return false;

        if (ch == '\n') {
            _sync = false;
            return _finish(out);
        }

        if (_len >= (LINK_MAX_FRAME - 1)) {   // ยาวเกิน -> ทิ้งทั้งเฟรม
            _sync = false;
            rx_dropped++;
            return false;
        }
        _buf[_len++] = ch;
        return false;
    }

private:
    char     _buf[LINK_MAX_FRAME];
    uint16_t _len  = 0;
    bool     _sync = false;

    bool _finish(LinkFrame &out)
    {
        // ต้องมีอย่างน้อย  T*HH  = 4 ตัว
        if (_len < 4) { rx_dropped++; return false; }

        // หา '*' ตัวสุดท้าย ตามด้วย hex 2 ตัวพอดี
        int star = -1;
        for (int i = (int)_len - 3; i >= 0; i--) {
            if (_buf[i] == '*') { star = i; break; }
        }
        if (star < 0 || (uint16_t)(star + 3) != _len) { rx_dropped++; return false; }

        uint8_t want = link_crc8(_buf, (size_t)star);
        uint8_t got  = _hex2(_buf[star + 1], _buf[star + 2]);
        if (want != got) { rx_crc_err++; return false; }

        _buf[star] = '\0';               // ตัด *HH ออก เหลือแต่ payload

        out.type  = _buf[0];
        out.count = 0;
        if (_buf[1] == '\0') { rx_ok++; return true; }      // เฟรมไม่มี field เลย
        if (_buf[1] != ',')  { rx_dropped++; return false; }

        // แยก field ด้วย ',' แบบ in-place
        char *p = &_buf[2];
        out.field[out.count++] = p;
        while (*p) {
            if (*p == ',') {
                *p = '\0';
                if (out.count >= LINK_MAX_FIELDS) { rx_dropped++; return false; }
                out.field[out.count++] = p + 1;
            }
            p++;
        }
        rx_ok++;
        return true;
    }

    static uint8_t _hex1(char c)
    {
        if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
        if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
        if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
        return 0xFF;
    }
    static uint8_t _hex2(char hi, char lo)
    {
        uint8_t h = _hex1(hi), l = _hex1(lo);
        if (h == 0xFF || l == 0xFF) return 0xFF;   // จะไม่ตรง CRC เอง
        return (uint8_t)((h << 4) | l);
    }
};

// ---------------------------------------------------------------------------
// LinkWriter -- ประกอบเฟรมลง buffer ภายใน แล้วค่อยยิงออก Serial ทีเดียว
//
//   LinkWriter w;
//   w.begin(LNK_T_CMD); w.addLong(seq); w.addStr(LNK_CMD_RELAY);
//   w.addLong(idx); w.addLong(on);
//   Serial.write(w.finish(), w.length());
// ---------------------------------------------------------------------------
class LinkWriter {
public:
    void begin(char type)
    {
        _len = 0;
        _buf[_len++] = '$';
        _buf[_len++] = type;
        _ok = true;
    }

    void addStr(const char *s)
    {
        if (!_room(strlen(s) + 1)) return;
        _buf[_len++] = ',';
        while (*s) _buf[_len++] = *s++;
    }

    void addLong(long v)
    {
        char t[12];
        snprintf(t, sizeof(t), "%ld", v);
        addStr(t);
    }

    void addFloat(float v, uint8_t dp = 2)
    {
        char t[16];
#if defined(ARDUINO_ARCH_AVR)
        // AVR ตัดฟีเจอร์ %f ออกจาก snprintf โดย default -> ต้องใช้ dtostrf
        dtostrf(v, 0, dp, t);
#else
        snprintf(t, sizeof(t), "%.*f", (int)dp, (double)v);
#endif
        addStr(t);
    }

    void addHex16(uint16_t v)
    {
        char t[8];
        snprintf(t, sizeof(t), "%04X", v);
        addStr(t);
    }

    // ปิดเฟรม: ต่อท้าย *CRC\n  แล้วคืน pointer (คืน nullptr ถ้า buffer ล้น)
    const char *finish()
    {
        if (!_ok || !_room(4)) return nullptr;
        uint8_t crc = link_crc8(&_buf[1], (size_t)(_len - 1));   // ข้าม '$'
        _buf[_len++] = '*';
        // ห้ามตั้งชื่อ HEX -- ชนกับ macro `#define HEX 16` ของ Arduino Print.h
        static const char LNK_HEXDIG[] = "0123456789ABCDEF";
        _buf[_len++] = LNK_HEXDIG[(crc >> 4) & 0x0F];
        _buf[_len++] = LNK_HEXDIG[crc & 0x0F];
        _buf[_len++] = '\n';
        _buf[_len]   = '\0';
        return _buf;
    }

    uint16_t    length() const { return _len; }
    const char *c_str() const  { return _buf; }

private:
    char     _buf[LINK_MAX_FRAME];
    uint16_t _len = 0;
    bool     _ok  = true;

    bool _room(size_t n)
    {
        if (_len + n + 1 >= LINK_MAX_FRAME) { _ok = false; return false; }
        return true;
    }
};

#endif // LINK_PROTOCOL_H
