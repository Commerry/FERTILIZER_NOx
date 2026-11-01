// ============================================================================
//  settings.cpp
// ============================================================================
#include "settings.h"
#include "config.h"
#include <EEPROM.h>

Settings g_cfg;
static bool s_corrupt = false;

// ---------------------------------------------------------------------------
void settings_defaults()
{
    g_cfg.magic = EEPROM_MAGIC;

    // --- เซนเซอร์ (จุดตั้งต้นตามดาต้าชีต -- ยังต้องยืนยันด้วย Scanner ก่อนเชื่อ) ---
    //
    //  ############ O2 sensor (O2A, O2B) -- ยืนยันจากการสแกนจริง ############
    //    *** ค่า O2 อยู่ที่ reg 6 (U16, x0.1) *** -- ไม่ใช่ reg 0!
    //        (สแกนแล้วในอากาศได้ reg6 = 209 -> 20.9%)
    //    reg 14 = อุณหภูมิ (~2500 = 25.00 C ถ้า x0.01)
    //    reg 24 = Slave Address (อ่านได้ = ID ปัจจุบัน) -> ใช้ตั้ง ID ที่ reg นี้
    //    โค้ดตัวอย่างอ่าน reg 0 count 2 แต่ reg 0/1 = 0 จริง ๆ ค่าอยู่ reg 6
    //
    //  ############ Gas pressure sensor -- จากดาต้าชีต (รูปที่แนบ) ############
    //    reg 0 = Slave Address (R/W)   <- ตั้ง ID ด้วย setid <cur> 0 <new>
    //    reg 1 = Baud (3 = 9600)
    //    reg 2 = Unit (0=MPa 1=kPa 3=Bar ...)   reg 3 = Decimal places
    //    reg 4 = Measured Value (S16)  ค่าจริง = raw / 10^(decimal places)
    //    reg 7-8 = Floating-point Output (F32 big-endian ABCD) = ค่าจริงตรง ๆ
    //    -> เลือกอ่าน "float ที่ reg 7" เป็นค่าเริ่มต้น เพราะได้ค่าจริงเลย
    //       ไม่ต้องเดา decimal places  (ถ้าอ่านเป็นเลขมั่ว -> ลองสลับเป็น F32_CDAB
    //        หรือใช้ integer ที่ reg 4 แล้วตั้ง scale = 1/10^decimal แทน)
    //
    //  *** Slave ID 1/2/3 เป็นค่าที่ "ตั้งใจจะกำหนด" -- ต้องไปตั้งจริงที่เซนเซอร์ก่อน
    //      ด้วย tools/RS485_SetID ทีละตัว ไม่งั้นชนกันบนบัสเดียว ***

    // O2 #1 (หลัก)   -- ค่า O2 อยู่ reg 6, U16, x0.1  (ยืนยันจากการสแกน)
    g_cfg.sensor[CFG_SENSOR_O2A] = { 1, 3, 6, 1, LNK_DT_U16, 0.1f, 0.0f };
    // O2 #2 (รีเช็ค)
    g_cfg.sensor[CFG_SENSOR_O2B] = { 2, 3, 6, 1, LNK_DT_U16, 0.1f, 0.0f };
    // Gas pressure -- อ่าน float ที่ reg 7 (ได้ค่าจริงตรง ๆ ไม่ต้องเดา decimal)
    g_cfg.sensor[CFG_SENSOR_PRESS] = { 3, 3, 7, 2, LNK_DT_F32_ABCD, 1.0f, 0.0f };

    g_cfg.mbBaud    = RS485_BAUD_DEFAULT;
    g_cfg.pollMs    = RS485_POLL_MS;
    g_cfg.timeoutMs = RS485_TIMEOUT_MS;

    // --- รอบเวลา (ค่าตัวอย่างจากสเปก) -- หน่วย ms ---
    g_cfg.cyc[CFG_DEV_CP] = {  0,  900000UL, 3600000UL };  // CP: ON 900 / OFF 3600 วิ
    g_cfg.cyc[CFG_DEV_CB] = {  0,        0,        0   };  // CB: ยังไม่ตั้ง -> ไม่ทำงาน
    g_cfg.cyc[CFG_DEV_FB] = {  0,        0,        0   };  // FB: ยังไม่ตั้ง -> ไม่ทำงาน
    g_cfg.cyc[CFG_DEV_FP] = {  0, 1800000UL, 3600000UL };  // FP: ON 1800 / OFF 3600 วิ

    g_cfg.autoStart = 0;        // ค่าปลอดภัย: ต้องกด START เอง

    // --- ควบคุมวาล์ว RV แบบ band (% คงที่ต่อช่วง O2) ---
    //   ขอบช่วง O2 (ตามสเปก):
    g_cfg.rv1Min = 5.0f;   g_cfg.rv1Max = 20.0f;   // Rough  : O2 5-20 %
    g_cfg.rv2Min = 3.0f;   g_cfg.rv2Max =  5.0f;   // Medium : O2 3-5 %
    g_cfg.rv3Min = 0.0f;   g_cfg.rv3Max =  3.0f;   // Fine   : O2 < 3 %

    // *** % เปิดวาล์วของแต่ละช่วง (ปรับจูนหน้างานผ่านจอได้) ***
    //   O2 สูง = เปิดมาก ไล่ออกเร็ว / O2 ต่ำเข้าเป้า = เปิดน้อย คงระดับ
    g_cfg.rvMaxOut   = 100;     // MAXOUT (O2 > 20%)  -> เปิดสุด
    g_cfg.coarseStep = 75.0f;   // ROUGH  (O2 5-20%)  -> 75%
    g_cfg.midStep    = 40.0f;   // MEDIUM (O2 3-5%)   -> 40%
    g_cfg.rvMinOut   = 15;      // FINE   (O2 < 3%)   -> 15%

    // ค่าเดิมของโหมด step/PID -- ไม่ใช้แล้วใน band control (เก็บ default กัน validate ตก)
    g_cfg.stepSec  = 10;
    g_cfg.o2Target = 1.5f;
    g_cfg.kp = 2.0f;  g_cfg.ki = 0.1f;  g_cfg.kd = 0.0f;

    // --- Alarm O1 vs O2 ---
    g_cfg.o2DiffMax = 2.0f;
    g_cfg.o2DiffSec = 30;

    // --- PIN ---
    g_cfg.pinOperator   = 1111;
    g_cfg.pinSupervisor = 2222;
    g_cfg.pinCommission = 3333;
    g_cfg.pinExpert     = 4444;

    g_cfg.crc = 0;
}

// ---------------------------------------------------------------------------
static uint8_t calc_crc()
{
    const uint8_t *p = (const uint8_t *)&g_cfg;
    return link_crc8((const char *)p, sizeof(Settings) - 1);   // ทุกไบต์ ยกเว้น crc เอง
}

void settings_load()
{
    EEPROM.get(EEPROM_ADDR_BASE, g_cfg);

    bool ok = (g_cfg.magic == EEPROM_MAGIC) && (g_cfg.crc == calc_crc());

    // ต่อให้ CRC ผ่าน ก็ยังต้องกันค่าที่ทำให้ระบบพัง
    // (เช่น pollMs = 0 -> loop รัวจนลิงก์ตาย, stepSec = 0 -> วาล์วพุ่ง 100% ทันที)
    if (ok) {
        if (g_cfg.mbBaud < 1200 || g_cfg.mbBaud > 115200)   ok = false;
        if (g_cfg.pollMs < 50   || g_cfg.pollMs > 10000)    ok = false;
        if (g_cfg.timeoutMs < 20 || g_cfg.timeoutMs > 2000) ok = false;
        if (g_cfg.stepSec < 1   || g_cfg.stepSec > 3600)    ok = false;
        if (g_cfg.rvMaxOut > 100 || g_cfg.rvMinOut > g_cfg.rvMaxOut) ok = false;

        for (uint8_t i = 0; i < CFG_SENSOR_COUNT && ok; i++) {
            if (g_cfg.sensor[i].slaveId < 1 || g_cfg.sensor[i].slaveId > 247) ok = false;
            if (g_cfg.sensor[i].fc != 3 && g_cfg.sensor[i].fc != 4)           ok = false;
            if (g_cfg.sensor[i].nRegs < 1 || g_cfg.sensor[i].nRegs > 2)       ok = false;
            if (g_cfg.sensor[i].dtype >= LNK_DT_COUNT)                        ok = false;
        }
    }

    // *** auto-heal: EEPROM ใช้ไม่ได้ (เพิ่งอัปเฟิร์มแวร์ที่แก้ struct / ครั้งแรก /
    //     เพี้ยนจริง) -> โหลด default แล้วเขียนลง EEPROM ให้เลย ***
    //   -> บูตหน้าโหลดผ่านปกติ ไม่ต้องชูธง CFG ให้กวนใจ
    //   ปรัชญา: จำแค่ "ค่าล่าสุดที่เซฟ"  ถ้ายังไม่เคยเซฟ/ของหาย = ใช้ default เงียบ ๆ
    if (!ok) {
        settings_defaults();
        settings_save();          // เขียน default + set magic/crc (เคลียร์ s_corrupt ในตัว)
    }
    s_corrupt = false;            // ไม่เตือน CFG อีก -- โหลด/ซ่อมเรียบร้อยแล้ว
}

void settings_save()
{
    g_cfg.magic = EEPROM_MAGIC;
    g_cfg.crc   = calc_crc();
    EEPROM.put(EEPROM_ADDR_BASE, g_cfg);    // put เขียนเฉพาะไบต์ที่เปลี่ยน
    s_corrupt = false;
}

bool settings_was_corrupt() { return s_corrupt; }

// ---------------------------------------------------------------------------
//  map: config key <-> field
// ---------------------------------------------------------------------------
bool settings_set(uint8_t key, float v)
{
    // --- 0..20 : เซนเซอร์ 3 ตัว x 7 field ---
    if (key < CFG_SENSOR_COUNT * CFG_SENSOR_STRIDE) {
        uint8_t    s = key / CFG_SENSOR_STRIDE;
        uint8_t    f = key % CFG_SENSOR_STRIDE;
        SensorCfg &c = g_cfg.sensor[s];

        switch (f) {
        case CFG_FLD_SLAVE_ID:
            if (v < 1 || v > 247) return false;
            c.slaveId = (uint8_t)v;                                  return true;
        case CFG_FLD_FC:
            if ((int)v != 3 && (int)v != 4) return false;
            c.fc = (uint8_t)v;                                       return true;
        case CFG_FLD_REG:
            if (v < 0 || v > 65535) return false;
            c.reg = (uint16_t)v;                                     return true;
        case CFG_FLD_NREGS:
            if ((int)v != 1 && (int)v != 2) return false;
            c.nRegs = (uint8_t)v;                                    return true;
        case CFG_FLD_DTYPE:
            if (v < 0 || v >= LNK_DT_COUNT) return false;
            c.dtype = (uint8_t)v;                                    return true;
        case CFG_FLD_SCALE:   c.scale  = v;                          return true;
        case CFG_FLD_OFFSET:  c.offset = v;                          return true;
        default: return false;
        }
    }

    // --- 24..35 : รอบเวลาของ CP/CB/FB/FP  (หน่วย ms) ---
    if (key >= CFG_TIMER_BASE && key < CFG_RV_BASE) {
        uint8_t   d = (uint8_t)((key - CFG_TIMER_BASE) / CFG_DEV_STRIDE);
        uint8_t   f = (uint8_t)((key - CFG_TIMER_BASE) % CFG_DEV_STRIDE);
        if (v < 0 || v > (float)CFG_TIME_MAX_MS) return false;
        CycleCfg &c = g_cfg.cyc[d];
        switch (f) {
        case CFG_FLD_DELAY: c.delayMs = (uint32_t)v; return true;
        case CFG_FLD_ON:    c.onMs    = (uint32_t)v; return true;
        case CFG_FLD_OFF:   c.offMs   = (uint32_t)v; return true;
        default: return false;
        }
    }

    // --- 36..52 : ควบคุมวาล์ว RV ---
    switch (key) {
    case CFG_RV1_MIN: if (v < 0 || v > 25) return false; g_cfg.rv1Min = v; return true;
    case CFG_RV1_MAX: if (v < 0 || v > 25) return false; g_cfg.rv1Max = v; return true;
    case CFG_RV2_MIN: if (v < 0 || v > 25) return false; g_cfg.rv2Min = v; return true;
    case CFG_RV2_MAX: if (v < 0 || v > 25) return false; g_cfg.rv2Max = v; return true;
    case CFG_RV3_MIN: if (v < 0 || v > 25) return false; g_cfg.rv3Min = v; return true;
    case CFG_RV3_MAX: if (v < 0 || v > 25) return false; g_cfg.rv3Max = v; return true;

    case CFG_RV_COARSE_STEP: if (v < 0 || v > 100) return false; g_cfg.coarseStep = v; return true;
    case CFG_RV_MID_STEP:    if (v < 0 || v > 100) return false; g_cfg.midStep    = v; return true;

    case CFG_RV_STEP_SEC:
        // 0 ไม่ได้ -- วาล์วจะก้าวทุก loop แล้วพุ่งไป 100% ในไม่กี่วินาที
        if (v < 1 || v > 3600) return false;
        g_cfg.stepSec = (uint16_t)v;                                     return true;

    case CFG_O2_TARGET: if (v < 0 || v > 25)  return false; g_cfg.o2Target = v; return true;
    case CFG_PID_KP:    if (v < 0 || v > 999) return false; g_cfg.kp = v;       return true;
    case CFG_PID_KI:    if (v < 0 || v > 999) return false; g_cfg.ki = v;       return true;
    case CFG_PID_KD:    if (v < 0 || v > 999) return false; g_cfg.kd = v;       return true;

    case CFG_RV_MIN_OUT:
        if (v < 0 || v > 100 || v > g_cfg.rvMaxOut) return false;
        g_cfg.rvMinOut = (uint8_t)v;                                     return true;
    case CFG_RV_MAX_OUT:
        if (v < 0 || v > 100 || v < g_cfg.rvMinOut) return false;
        g_cfg.rvMaxOut = (uint8_t)v;                                     return true;

    case CFG_O2_DIFF_MAX: if (v < 0 || v > 25)   return false; g_cfg.o2DiffMax = v;             return true;
    case CFG_O2_DIFF_SEC: if (v < 1 || v > 9999) return false; g_cfg.o2DiffSec = (uint16_t)v;   return true;

    // --- 53..56 : PIN ---
    case CFG_PIN_OPERATOR:   if (v < 0 || v > 9999) return false; g_cfg.pinOperator   = (uint16_t)v; return true;
    case CFG_PIN_SUPERVISOR: if (v < 0 || v > 9999) return false; g_cfg.pinSupervisor = (uint16_t)v; return true;
    case CFG_PIN_COMMISSION: if (v < 0 || v > 9999) return false; g_cfg.pinCommission = (uint16_t)v; return true;
    case CFG_PIN_EXPERT:     if (v < 0 || v > 9999) return false; g_cfg.pinExpert     = (uint16_t)v; return true;

    // --- 57 : เปิดเครื่องแล้วเดินเลย ---
    case CFG_AUTO_START:
        if ((int)v != 0 && (int)v != 1) return false;
        g_cfg.autoStart = (uint8_t)v;    return true;

    // --- 21..23 : ระบบ ---
    case CFG_MB_BAUD_IDX:
        if (v < 1200 || v > 115200) return false;
        g_cfg.mbBaud = (uint32_t)v;      return true;
    case CFG_MB_POLL_MS:
        if (v < 50 || v > 10000) return false;
        g_cfg.pollMs = (uint16_t)v;      return true;
    case CFG_MB_TIMEOUT_MS:
        if (v < 20 || v > 2000) return false;
        g_cfg.timeoutMs = (uint16_t)v;   return true;

    default:
        return false;
    }
}

float settings_get(uint8_t key)
{
    if (key < CFG_SENSOR_COUNT * CFG_SENSOR_STRIDE) {
        uint8_t          s = key / CFG_SENSOR_STRIDE;
        uint8_t          f = key % CFG_SENSOR_STRIDE;
        const SensorCfg &c = g_cfg.sensor[s];

        switch (f) {
        case CFG_FLD_SLAVE_ID: return (float)c.slaveId;
        case CFG_FLD_FC:       return (float)c.fc;
        case CFG_FLD_REG:      return (float)c.reg;
        case CFG_FLD_NREGS:    return (float)c.nRegs;
        case CFG_FLD_DTYPE:    return (float)c.dtype;
        case CFG_FLD_SCALE:    return c.scale;
        case CFG_FLD_OFFSET:   return c.offset;
        default: return 0.0f;
        }
    }

    if (key >= CFG_TIMER_BASE && key < CFG_RV_BASE) {
        uint8_t         d = (uint8_t)((key - CFG_TIMER_BASE) / CFG_DEV_STRIDE);
        uint8_t         f = (uint8_t)((key - CFG_TIMER_BASE) % CFG_DEV_STRIDE);
        const CycleCfg &c = g_cfg.cyc[d];
        switch (f) {
        case CFG_FLD_DELAY: return (float)c.delayMs;
        case CFG_FLD_ON:    return (float)c.onMs;
        case CFG_FLD_OFF:   return (float)c.offMs;
        default: return 0.0f;
        }
    }

    switch (key) {
    case CFG_RV1_MIN:        return g_cfg.rv1Min;
    case CFG_RV1_MAX:        return g_cfg.rv1Max;
    case CFG_RV2_MIN:        return g_cfg.rv2Min;
    case CFG_RV2_MAX:        return g_cfg.rv2Max;
    case CFG_RV3_MIN:        return g_cfg.rv3Min;
    case CFG_RV3_MAX:        return g_cfg.rv3Max;
    case CFG_RV_COARSE_STEP: return g_cfg.coarseStep;
    case CFG_RV_MID_STEP:    return g_cfg.midStep;
    case CFG_RV_STEP_SEC:    return (float)g_cfg.stepSec;
    case CFG_O2_TARGET:      return g_cfg.o2Target;
    case CFG_PID_KP:         return g_cfg.kp;
    case CFG_PID_KI:         return g_cfg.ki;
    case CFG_PID_KD:         return g_cfg.kd;
    case CFG_RV_MIN_OUT:     return (float)g_cfg.rvMinOut;
    case CFG_RV_MAX_OUT:     return (float)g_cfg.rvMaxOut;
    case CFG_O2_DIFF_MAX:    return g_cfg.o2DiffMax;
    case CFG_O2_DIFF_SEC:    return (float)g_cfg.o2DiffSec;

    case CFG_PIN_OPERATOR:   return (float)g_cfg.pinOperator;
    case CFG_PIN_SUPERVISOR: return (float)g_cfg.pinSupervisor;
    case CFG_PIN_COMMISSION: return (float)g_cfg.pinCommission;
    case CFG_PIN_EXPERT:     return (float)g_cfg.pinExpert;
    case CFG_AUTO_START:     return (float)g_cfg.autoStart;

    case CFG_MB_BAUD_IDX:    return (float)g_cfg.mbBaud;
    case CFG_MB_POLL_MS:     return (float)g_cfg.pollMs;
    case CFG_MB_TIMEOUT_MS:  return (float)g_cfg.timeoutMs;
    default:                 return 0.0f;
    }
}
