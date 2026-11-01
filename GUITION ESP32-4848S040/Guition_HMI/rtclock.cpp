// ============================================================================
//  rtclock.cpp
// ============================================================================
#include "rtclock.h"
#include <Preferences.h>

static Preferences s_nvs;
static uint32_t    s_baseEpoch = 0;     // epoch ณ ตอนที่ s_baseMs ถูกจับ
static uint32_t    s_baseMs    = 0;
static bool        s_isSet     = false;
static uint32_t    s_lastSave  = 0;

// ค่าเริ่มต้นก่อนตั้งเวลา = 2026-01-01  (ไม่ใช่ 1970 -> ชื่อไฟล์ไม่กลายเป็น 19700101)
#define DEFAULT_EPOCH   1767225600UL    // 2026-01-01 00:00:00
#define SAVE_EVERY_MS   600000UL        // เซฟลง NVS ทุก 10 นาที

// ---------------------------------------------------------------------------
//  แปลง วัน/เดือน/ปี <-> จำนวนวันนับจาก 1970  (อัลกอริทึม Howard Hinnant)
//  ทำเองเพราะ mktime/timegm บน ESP32 พึ่ง timezone ที่ไม่แน่นอน
// ---------------------------------------------------------------------------
static long days_from_civil(int y, int m, int d)
{
    y -= (m <= 2);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + (long)doe - 719468L;
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    z += 719468L;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp  = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*m <= 2));
}

// ---------------------------------------------------------------------------
uint32_t clock_make(int year, int mon, int day, int hh, int mm, int ss)
{
    long days = days_from_civil(year, mon, day);
    return (uint32_t)(days * 86400L + hh * 3600L + mm * 60L + ss);
}

void clock_break(uint32_t epoch, int *year, int *mon, int *day,
                 int *hh, int *mm, int *ss)
{
    long days = (long)(epoch / 86400UL);
    uint32_t rem = epoch % 86400UL;
    civil_from_days(days, year, mon, day);
    *hh = (int)(rem / 3600);
    *mm = (int)((rem % 3600) / 60);
    *ss = (int)(rem % 60);
}

// ---------------------------------------------------------------------------
void clock_begin()
{
    s_nvs.begin("rtclock", false);
    uint32_t saved = s_nvs.getUInt("epoch", DEFAULT_EPOCH);
    s_isSet        = s_nvs.getBool("set", false);

    // เดินต่อจากค่าที่เซฟไว้ล่าสุด (ดีกว่าเริ่มที่ 1970 แม้จะช้าไปช่วงที่ปิดไฟ)
    s_baseEpoch = saved;
    s_baseMs    = millis();
    s_lastSave  = millis();

#if RTCLOCK_USE_DS3231
    // (ถ้าต่อ DS3231: อ่านค่าจาก 0x68 มา clock_set() ตรงนี้ -> ทับค่า NVS)
    // เว้นไว้ให้เติมตอนมีโมดูลจริง -- ตอนนี้ยังไม่เรียกเพื่อไม่ให้ไปกวน I2C ทัช
#endif
}

uint32_t clock_now()
{
    return s_baseEpoch + (millis() - s_baseMs) / 1000UL;
}

bool clock_is_set() { return s_isSet; }

void clock_set(uint32_t epoch)
{
    s_baseEpoch = epoch;
    s_baseMs    = millis();
    s_isSet     = true;

    s_nvs.putUInt("epoch", epoch);
    s_nvs.putBool("set", true);
    s_lastSave = millis();
}

void clock_task()
{
    // เซฟค่าปัจจุบันลง NVS เป็นระยะ -> ไฟดับแล้วเปิดใหม่ได้ค่าใกล้เคียง
    if (millis() - s_lastSave < SAVE_EVERY_MS) return;
    s_lastSave = millis();
    s_nvs.putUInt("epoch", clock_now());
}

// ---------------------------------------------------------------------------
void clock_str_datetime(char *buf, uint32_t epoch)
{
    int y, mo, d, h, mi, s;
    clock_break(epoch, &y, &mo, &d, &h, &mi, &s);
    sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", y, mo, d, h, mi, s);
}

void clock_str_date(char *buf, uint32_t epoch)
{
    int y, mo, d, h, mi, s;
    clock_break(epoch, &y, &mo, &d, &h, &mi, &s);
    sprintf(buf, "%04d%02d%02d", y, mo, d);
}

void clock_str_clock(char *buf, uint32_t epoch)
{
    int y, mo, d, h, mi, s;
    clock_break(epoch, &y, &mo, &d, &h, &mi, &s);
    sprintf(buf, "%02d:%02d", h, mi);
}
