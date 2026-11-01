// ============================================================================
//  Arduino Nano  --  ตัวควบคุมจริงของเครื่องหมักปุ๋ยควบคุมออกซิเจน
//
//  อุปกรณ์ที่คุม:  (*** ผังขาจริง = config.h เท่านั้น -- ที่นี่เป็นสรุปย่อ ***)
//    CP  Circulation Pump   = D10 (relay)
//    CB  Circulation Blower = D9  (สัญญาณ VSP 5V run/stop ตรงเข้า driver blower)
//    FB  Feed Blower        = A4  (relay)
//    FP  Feed Pump          = A5  (relay)
//    RV  Regulation Valve   = D4/D5 (2 relay H-bridge เปิด/ปิด, timed band) + feedback A2/A3
//    ไฟสถานะ เขียว=D11 แดง=D12
//
//  ไม่ได้ต่อเข้า Nano (ตามสเปก):
//    N2Gen     -- มีสวิตช์ ON/OFF แยกของมันเอง
//    CB speed  -- ปรับด้วย VR ภายนอก
//
//  เซนเซอร์ (RS485 3 บัสแยก บน SoftwareSerial D2/D3 -- ผังเต็มใน config.h):
//    O1 = O2 หลัก  Modbus id1  บัส DE1 (D8)   -- ใช้คุมวาล์ว
//    O2 = O2 รีเช็ค แบบพ่นเฟรม FF เอง บัส DE3 (D7) -- ดักฟัง ไม่ poll
//    PT = Pressure  Modbus id3  บัส DE2 (D6)
//  (id ของเซนเซอร์ O2 ล็อกตายที่ 1 ทั้งคู่ เปลี่ยนไม่ได้ -> ต้องแยกบัส)
//
//  ####################################################################
//  *** ก่อนอัปโหลดทุกครั้ง: ถอดสายจอออกจาก D0/D1 ก่อน ***
//  D0/D1 = ลิงก์จอ (HW UART) ใช้ขาเดียวกับชิพ USB -- จอเสียบค้าง = อัปไม่ผ่าน
//  ####################################################################
//
//  ไลบรารีที่ต้องมี:  SoftwareSerial (มากับ Arduino IDE อยู่แล้ว)
//                    -- ไม่ต้องลง AltSoftSerial / ModbusMaster อีกแล้ว
//  Board: Arduino Nano / Processor: ATmega328P
// ============================================================================
#include <avr/wdt.h>
#include "config.h"
#include "link_protocol.h"
#include "link_slave.h"
#include "actuators.h"
#include "settings.h"
#include "modbus_sensors.h"
#include "process.h"
#include "logbook.h"

// ---------------------------------------------------------------------------
//  โหมดค่าจำลอง -- 1 = จำลองเซนเซอร์ (เทส UI/logic โดยไม่มีของจริง), 0 = ของจริง
//  ของจริงยืนยันแล้ว: O2 หลัก reg6 x0.1 (id1) · O2 รีเช็คจากเฟรม FF · PT reg7-8 F32
// ---------------------------------------------------------------------------
#define USE_DUMMY_SENSORS   0

static uint8_t s_mode = LNK_MODE_AUTO;    // เครื่องนี้ทำงานอัตโนมัติเป็นหลัก

// ---------------------------------------------------------------------------
//  ปุ่มบนตู้
//    A0 = ปุ่มรีเซ็ต -- กดค้าง 5 วิ = reset alarm, 10 วิ = reset บอร์ด
//    A1 = ปุ่มสำรอง  -- ยังไม่ใช้ (อ่านค่าไว้เฉย ๆ ในตัวแปร)
// ---------------------------------------------------------------------------
static uint32_t s_a0Start   = 0;
static bool     s_a0Down    = false;
static bool     s_a0Alarmed = false;   // one-shot กันสั่ง reset alarm ซ้ำในหนึ่งครั้งกด
static bool     s_spareDown = false;   // A1 -- ตัวแปร placeholder เผื่ออนาคต

static void buttons_task()
{
    uint32_t now = millis();

    // --- A0 : ปุ่มรีเซ็ต (กดค้างตามเวลา) ---
    bool a0 = (digitalRead(PIN_BTN_RESET) == LOW);   // pull-up -> กด = LOW

    if (a0 && !s_a0Down) {                  // เพิ่งกด -> เริ่มจับเวลา
        s_a0Down    = true;
        s_a0Start   = now;
        s_a0Alarmed = false;
    } else if (!a0 && s_a0Down) {           // ปล่อย -> ล้าง
        s_a0Down = false;
    } else if (a0 && s_a0Down) {            // กดค้างอยู่
        uint32_t held = now - s_a0Start;

        if (held >= BTN_HOLD_BOARD_MS) {
            // กด 10 วิ -> รีเซ็ตบอร์ด (กรณีค้าง) ให้ watchdog เตะรีสตาร์ท
            act_all_off();                  // ดับ output ก่อนรีเซ็ต (ปลอดภัย)
            wdt_enable(WDTO_15MS);
            for (;;) { }                    // รอ watchdog รีเซ็ต
        }

        if (held >= BTN_HOLD_ALARM_MS && !s_a0Alarmed) {
            s_a0Alarmed = true;
            // reset alarm เฉพาะตอน "มี alarm ค้างจริง"
            if (g_tlm.errflags & LNK_F_O2_MISMATCH) {
                process_reset_alarm();
                logbook_event(LNK_LOG_ALARM, LNK_F_O2_MISMATCH, 0);   // ลง log ว่า alarm ลง
            }
        }
    }

    // --- A1 : ปุ่มสำรอง -- อ่านไว้เฉย ๆ ยังไม่ผูกงาน ---
    s_spareDown = (digitalRead(PIN_BTN_SPARE) == LOW);
    (void)s_spareDown;
}

// ---------------------------------------------------------------------------
//  callback จาก link_slave -- จอสั่งงานมา
// ---------------------------------------------------------------------------
uint8_t link_on_command(const char *cmd, long a1, long a2)
{
    if (strcmp(cmd, LNK_CMD_PING) == 0) return LNK_ERR_NONE;

    if (strcmp(cmd, LNK_CMD_ESTOP) == 0) {
        process_stop();
        act_estop();
        return LNK_ERR_NONE;
    }

    if (strcmp(cmd, LNK_CMD_START) == 0) {
        if (s_mode != LNK_MODE_AUTO) return LNK_ERR_LOCKED;   // โหมดมือ ไม่มี process ให้เริ่ม
        process_start();
        return LNK_ERR_NONE;
    }

    if (strcmp(cmd, LNK_CMD_STOP) == 0) {
        process_stop();
        return LNK_ERR_NONE;
    }

    if (strcmp(cmd, LNK_CMD_MODE) == 0) {
        if (a1 != LNK_MODE_MANUAL && a1 != LNK_MODE_AUTO) return LNK_ERR_BAD_ARG;
        // สลับโหมดต้องหยุดของเดิมให้หมดก่อน กันของค้าง
        process_stop();
        act_all_off();
        act_estop_clear();
        s_mode = (uint8_t)a1;
        return LNK_ERR_NONE;
    }

    if (strcmp(cmd, LNK_CMD_RELAY) == 0) {
        if (s_mode == LNK_MODE_AUTO)     return LNK_ERR_LOCKED;   // โหมดออโต้ ห้ามสั่งมือ
        if (a1 < 0 || a1 >= RELAY_COUNT) return LNK_ERR_BAD_ARG;
        if (a2 != 0 && a2 != 1)          return LNK_ERR_BAD_ARG;
        act_set_relay((uint8_t)a1, a2 != 0);
        return LNK_ERR_NONE;
    }

    if (strcmp(cmd, LNK_CMD_VALVE) == 0) {
        if (s_mode == LNK_MODE_AUTO) return LNK_ERR_LOCKED;
        if (a1 < 0 || a1 > 100)      return LNK_ERR_BAD_ARG;
        act_set_valve((uint8_t)a1);
        return LNK_ERR_NONE;
    }

    if (strcmp(cmd, LNK_CMD_SAVE) == 0) {
        settings_save();
        logbook_event(LNK_LOG_SAVED, 0, 0);
        return LNK_ERR_NONE;
    }

    return LNK_ERR_BAD_CMD;
}

uint8_t link_on_set_config(uint8_t key, float value)
{
    if (!settings_set(key, value)) return LNK_ERR_BAD_KEY;

    // เปลี่ยน baud ของ RS485 -> ต้องเปิดพอร์ตใหม่ทันที ไม่งั้นค่าใหม่ไม่มีผล
    if (key == CFG_MB_BAUD_IDX) mb_begin();

    return LNK_ERR_NONE;      // ยังไม่เขียน EEPROM -- รอจอสั่ง SAV
}

void link_on_get_config()
{
    // *** ห้ามยิง 58 เฟรมรวดเดียวที่นี่ *** -- จะบล็อก loop ~300ms + จอ RX ล้นเฟรมท้ายหาย
    // เริ่มการทยอยส่งแบบเว้นจังหวะแทน (link_task -> kv_pump ส่งทีละชุด)
    link_begin_config_dump();
}

uint8_t link_on_modbus_raw(uint8_t slave, uint8_t fc, uint16_t reg, uint16_t arg, long seq)
{
    if (slave < 1 || slave > 247) return LNK_ERR_BAD_ARG;

    // --- fc 6 = เขียน register เดียว (ตั้ง ID / เขียน config) -- arg = ค่าที่เขียน ---
    if (fc == 6) {
        if (!mb_raw_write(slave, reg, arg)) return LNK_ERR_MODBUS;
        uint16_t echo[1] = { arg };         // ตอบค่าที่เขียนกลับไปให้จอยืนยัน
        link_send_raw(seq, echo, 1);
        return LNK_ERR_NONE;
    }

    // --- fc 3/4 = อ่าน -- arg = จำนวน register ---
    if (fc != 3 && fc != 4)   return LNK_ERR_BAD_ARG;
    if (arg < 1 || arg > 8)   return LNK_ERR_BAD_ARG;

    uint16_t w[8];
    uint8_t  got = mb_raw_read(slave, fc, reg, (uint8_t)arg, w);
    if (got == 0) return LNK_ERR_MODBUS;

    link_send_raw(seq, w, got);
    return LNK_ERR_NONE;
}

// ---------------------------------------------------------------------------
//  ค่าจำลอง -- ให้ O2 ค่อย ๆ ไหลลงตอนวาล์วเปิด เพื่อทดสอบ RV zone ได้จริง
//
//  จำลองแบบหยาบ ๆ ว่า "วาล์วเปิดมาก = N2 เข้ามาก = O2 ลด"
//  และมีอากาศรั่วเข้าตลอดเวลา (ทำให้ O2 ไต่กลับขึ้นถ้าปิดวาล์ว)
//  พอแค่ให้เห็นว่าลูปควบคุมไล่โซน MAXOUT -> ROUGH -> MEDIUM -> FINE ได้จริง
// ---------------------------------------------------------------------------
#if USE_DUMMY_SENSORS
static float s_simO2 = 20.9f;

static void dummy_sensors()
{
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last < 500) return;
    float dt = (now - last) / 1000.0f;
    last = now;

    float valve = (float)act_get_valve();

    // N2 เข้ามาไล่ O2 ออก (ยิ่งเปิดวาล์วมาก ยิ่งลดเร็ว) + อากาศรั่วเข้าเล็กน้อย
    float purge = valve * 0.010f;      // ลด O2
    float leak  = 0.05f;               // เพิ่ม O2
    s_simO2 += (leak - purge) * dt;

    if (s_simO2 < 0.0f)  s_simO2 = 0.0f;
    if (s_simO2 > 21.0f) s_simO2 = 21.0f;

    g_tlm.o2a      = s_simO2;                        // O1 (หลัก)
    g_tlm.o2b      = s_simO2 + 0.15f;                // O2 (รีเช็ค) -- ต่างกันนิดหน่อย ปกติ
    g_tlm.pressure = 2.0f + valve * 0.01f;
    g_tlm.errflags = 0;
}
#endif

// ---------------------------------------------------------------------------
void setup()
{
    // หลัง watchdog reset ต้องปิด WDT ทันที ไม่งั้นมันจะเตะซ้ำจนบูตวน
    MCUSR = 0;
    wdt_disable();

    act_begin();                  // ต้องมาก่อนเสมอ -- ดับ output ทุกตัวก่อนทำอย่างอื่น
    settings_load();
    link_begin();
    mb_begin();
    process_begin();

    pinMode(PIN_BTN_RESET, INPUT_PULLUP);
    pinMode(PIN_BTN_SPARE, INPUT_PULLUP);

    g_tlm.mode     = s_mode;
    g_tlm.errflags = settings_was_corrupt() ? LNK_F_CFG_BAD : 0;

    // logbook ต้องเริ่ม "ก่อน" auto-start -- ไม่งั้น START ของ auto-start จะไม่ถูกจับ
    // (logbook prime สถานะเริ่มต้น = เครื่องหยุด แล้ว process_start จะทำให้ diff ได้ START)
    logbook_begin(s_mode);
    if (settings_was_corrupt())
        logbook_event(LNK_LOG_ALARM, LNK_F_CFG_BAD, 1);   // EEPROM เสีย -- ลง ALARMS

    // เปิดเครื่องแล้วเดินเลย (ถ้าตั้งไว้)
    // ไม่เริ่มถ้า EEPROM เสีย -- ค่าที่ใช้อยู่เป็น default ซึ่งอาจไม่ใช่ที่ต้องการ
    // ปล่อยให้คนมากดเองจะปลอดภัยกว่า
    if (g_cfg.autoStart && s_mode == LNK_MODE_AUTO && !settings_was_corrupt())
        process_start();

    // *** Watchdog 4 วิ (fail-safe) ***
    //   ถ้า loop ค้าง (SoftwareSerial/Modbus/อะไรก็ตาม) เกิน 4 วิ -> รีเซ็ตบอร์ดเอง
    //   บูตใหม่ -> วาล์ว RV วิ่งปิด (homing) + รีเลย์ดับหมด = เข้าสถานะปลอดภัย
    //   ไม่ค้างแช่ output ทิ้งไว้  (4 วิ เผื่อ Modbus timeout สูงสุด 2 วิ + เผื่อ burst)
    wdt_enable(WDTO_4S);
}

void loop()
{
    wdt_reset();     // เคาะ watchdog -- ถ้า loop หยุดเคาะเกิน 4 วิ = บอร์ดรีเซ็ตเอง

    // 1) ลิงก์จอ -- ต้องมาก่อน เพราะ HW UART buffer มีแค่ 64 byte
    link_task();

    // 2) ปุ่มบนตู้ -- A0 รีเซ็ต alarm/บอร์ด (กดค้าง), A1 สำรอง
    //    (START/STOP สั่งจากจอ -- ปุ่มตู้ไม่มีหน้าที่นั้นแล้ว)
    buttons_task();

    // 3) เซนเซอร์
    bool o1_ok, o2_ok;
#if USE_DUMMY_SENSORS
    dummy_sensors();
    o1_ok = o2_ok = true;
#else
    mb_task();
    g_tlm.o2a      = mb_value(CFG_SENSOR_O2A);
    g_tlm.o2b      = mb_value(CFG_SENSOR_O2B);
    g_tlm.pressure = mb_value(CFG_SENSOR_PRESS);
    g_tlm.errflags = mb_errflags();
    if (settings_was_corrupt()) g_tlm.errflags |= LNK_F_CFG_BAD;
    o1_ok = mb_ok(CFG_SENSOR_O2A);
    o2_ok = mb_ok(CFG_SENSOR_O2B);
#endif

    // 4) ลำดับการทำงานอัตโนมัติ (timer + วาล์ว RV + เทียบ O1/O2)
    //    ในโหมด MANUAL ไม่เรียก -- คนคุม output เองจากหน้าจอ
    if (s_mode == LNK_MODE_AUTO)
        process_task(g_tlm.o2a, g_tlm.o2b, o1_ok, o2_ok);

    // ขับวาล์ว RV เข้าหาเป้าหมาย (จับเวลา) -- ต้องทำทุกรอบ ทุกโหมด
    //   AUTO: process_task -> rv_control -> act_set_valve(เป้า)
    //   MANUAL: act_set_valve จากคำสั่ง $C VALVE
    //   หยุด/บูต: เป้า=0 -> วิ่งปิด/โฮมเอง
    act_valve_tick(millis());

    if (process_o2_mismatch()) g_tlm.errflags |= LNK_F_O2_MISMATCH;

    g_tlm.mode     = s_mode;
    g_tlm.runState = process_run_state();
    g_tlm.rvZone   = process_rv_zone();
    g_tlm.elapsed  = process_elapsed_sec();

    // พายลอตแลมป์: เขียว = เดินเครื่องจริง / แดง = หยุด หรือ alarm/E-STOP
    {
        bool running = (g_tlm.runState == LNK_RUN_RUNNING) && !act_is_estop();
        bool red     = !running || (g_tlm.errflags & LNK_F_O2_MISMATCH);
        act_set_pilot(running, red);
    }

    // 5) บันทึกเหตุการณ์ -- เทียบสถานะปัจจุบันกับครั้งก่อน แล้วส่ง $L ที่ค้างให้จอ
    logbook_note(act_relay_bits(), s_mode, g_tlm.runState,
                 g_tlm.rvZone, g_tlm.errflags, act_is_estop());
    logbook_task();

    // ไม่มี heartbeat LED แล้ว -- D13 ที่เคยกะพริบอยู่ติดกับรีเลย์ในผังเดิม
    // ปล่อยไว้รีเลย์จะคลิกวินาทีละ 2 ครั้งตลอดเวลา
    // ตัวบอกว่า Nano ยังไม่ค้างคือลิงก์เอง (เฟรม $D หยุด = จอขึ้น OFFLINE ให้เอง)
}
