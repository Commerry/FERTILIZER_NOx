// ============================================================================
//  sdlog.cpp
// ============================================================================
#include "sdlog.h"
#include "board_pins.h"
#include "rtclock.h"

#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---------------------------------------------------------------------------
//  ค่าปรับได้
// ---------------------------------------------------------------------------
#define QUEUE_LEN        48      // บรรทัดค้างในคิวได้กี่ตัว
#define FLUSH_MS       2000      // flush ลงการ์ดทุกกี่ ms (ไฟดับเสียแค่ช่วงนี้)
#define MOUNT_RETRY_MS 2000      // ไม่มีการ์ด -> ลอง mount ใหม่ทุกกี่ ms
#define FREE_REFRESH_MS 60000    // อัปเดตพื้นที่ว่างทุกกี่ ms (คำนวณช้า ไม่ทำถี่)
#define TASK_STACK      8192     // ไบต์ -- SD/FAT กิน stack เยอะ
#define TASK_CORE          0     // คอร์ 0 (LVGL/ทัชอยู่คอร์ 1)

// *** header ต้องตรงกับรูปแบบบรรทัดที่ producer ประกอบใน link_master.cpp ***
static const char *HEADER[LOGF_COUNT] = {
    "datetime,uptime_s,event,detail",
    "datetime,uptime_s,alarm,state",
    "datetime,uptime_s,o2_main,o2_check,pressure,valve,relays,mode,run,zone",
    "datetime,message",
};
static const char *DIR[LOGF_COUNT] = { "/EVENTS", "/ALARMS", "/DATA", "" };

// ---------------------------------------------------------------------------
struct LogItem {
    uint8_t  kind;
    uint32_t epoch;
    char     line[SDLOG_LINE_MAX];
};

static QueueHandle_t s_q       = nullptr;
static SPIClass      s_spi(SD_SPI_HOST);

static volatile bool     s_mounted = false;
static volatile uint32_t s_lines   = 0;
static volatile uint32_t s_sizeMB  = 0;
static volatile uint32_t s_freeMB  = 0;

// s_dropped แยก 2 ตัวตามคอร์ที่เขียน -- กัน data race
//   s_dropTask เขียนจาก sd_task (คอร์ 0) เท่านั้น
//   s_dropPush เขียนจาก sdlog_push (คอร์ 1) เท่านั้น
// ถ้าใช้ตัวเดียวกัน 2 คอร์บวกพร้อมกัน -> update หาย  (stat เพี้ยน)
static volatile uint32_t s_dropTask = 0;
static volatile uint32_t s_dropPush = 0;

// ไฟล์ที่เปิดค้างไว้ทีละไฟล์ต่อชนิด + วันที่ของไฟล์นั้น (เพื่อรู้ว่าข้ามวันหรือยัง)
static File s_file[LOGF_COUNT];
static char s_fileDate[LOGF_COUNT][9];   // "YYYYMMDD"
static bool s_dirty = false;

// ---------------------------------------------------------------------------
//  ปิดไฟล์ทั้งหมด (ตอน unmount หรือถอดการ์ด)
// ---------------------------------------------------------------------------
static void close_all()
{
    for (uint8_t k = 0; k < LOGF_COUNT; k++) {
        if (s_file[k]) s_file[k].close();
        s_fileDate[k][0] = '\0';
    }
    s_dirty = false;
}

// ---------------------------------------------------------------------------
//  หาไฟล์ที่ถูกต้องสำหรับ (ชนิด, วันที่) -- เปิดใหม่ถ้าข้ามวัน / ยังไม่เปิด
//  ไฟล์ใหม่: สร้างโฟลเดอร์ + เขียน header ก่อน
// ---------------------------------------------------------------------------
static File *file_for(uint8_t kind, uint32_t epoch)
{
    char date[9];
    char path[40];

    if (kind == LOGF_SYSTEM) {
        strcpy(date, "-");                   // ไฟล์เดียว ไม่แยกวัน
        strcpy(path, "/SYSTEM.csv");
    } else {
        clock_str_date(date, epoch);         // "YYYYMMDD"
        snprintf(path, sizeof(path), "%s/%s.csv", DIR[kind], date);
    }

    // ไฟล์เดิมยังใช้ได้ (วันเดียวกัน) -> คืนเลย
    if (s_file[kind] && strcmp(s_fileDate[kind], date) == 0)
        return &s_file[kind];

    // ข้ามวัน / ยังไม่เปิด -> ปิดตัวเก่า เปิดตัวใหม่
    if (s_file[kind]) s_file[kind].close();

    if (kind != LOGF_SYSTEM && !SD.exists(DIR[kind]))
        SD.mkdir(DIR[kind]);

    bool isNew = !SD.exists(path);

    s_file[kind] = SD.open(path, FILE_APPEND);
    if (!s_file[kind]) return nullptr;       // เปิดไม่ได้ (การ์ดมีปัญหา)

    if (isNew) {                             // ไฟล์เพิ่งสร้าง -> ใส่หัวตาราง
        s_file[kind].println(HEADER[kind]);
        s_file[kind].flush();
    }
    strncpy(s_fileDate[kind], date, sizeof(s_fileDate[kind]));
    s_fileDate[kind][8] = '\0';
    return &s_file[kind];
}

// ---------------------------------------------------------------------------
static void write_system(const char *msg)
{
    if (!s_mounted) return;
    File *f = file_for(LOGF_SYSTEM, clock_now());
    if (!f) return;
    char dt[24];
    clock_str_datetime(dt, clock_now());
    f->printf("%s,%s\n", dt, msg);
    f->flush();
}

// ---------------------------------------------------------------------------
//  พยายาม mount การ์ด
// ---------------------------------------------------------------------------
static bool try_mount()
{
    SD.end();                                // เผื่อรอบก่อนค้างครึ่ง ๆ
    if (!SD.begin(SD_CS, s_spi, SD_FREQ_HZ)) return false;
    if (SD.cardType() == CARD_NONE) { SD.end(); return false; }

    uint64_t sz = SD.cardSize();
    s_sizeMB = (uint32_t)(sz / (1024ULL * 1024ULL));
    s_freeMB = 0;
    return true;
}

static void refresh_free()
{
    if (!s_mounted) return;
    uint64_t total = SD.totalBytes();
    uint64_t used  = SD.usedBytes();
    s_freeMB = (uint32_t)((total - used) / (1024ULL * 1024ULL));
}

// ---------------------------------------------------------------------------
//  task หลัก -- อยู่คอร์ 0
// ---------------------------------------------------------------------------
static void sd_task(void *arg)
{
    (void)arg;
    s_spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    uint32_t lastMount = 0;
    uint32_t lastFlush = 0;
    uint32_t lastFree  = 0;

    for (;;) {
        LogItem it;
        bool got = (xQueueReceive(s_q, &it, pdMS_TO_TICKS(300)) == pdTRUE);
        uint32_t now = millis();

        // ----- ยังไม่ mount -> ลองเป็นระยะ -----
        if (!s_mounted) {
            if (now - lastMount >= MOUNT_RETRY_MS) {
                lastMount = now;
                if (try_mount()) {
                    s_mounted = true;
                    write_system("SD mounted");
                    refresh_free();
                    lastFree = now;
                }
            }
            // การ์ดยังไม่มา -> บรรทัดที่ได้จากคิวก็ได้แต่ทิ้ง (นับ dropped)
            if (got) s_dropTask = s_dropTask + 1;
            continue;
        }

        // ----- mount อยู่ -> เขียน -----
        if (got) {
            File *f = file_for(it.kind, it.epoch);
            if (!f) {                        // เปิด/เขียนไม่ได้ = การ์ดหลุด
                s_mounted = false;
                close_all();
                SD.end();
                continue;
            }
            size_t n = f->println(it.line);
            if (n == 0) {                    // เขียนล้มเหลว = การ์ดหลุดกลางคัน
                s_mounted = false;
                close_all();
                SD.end();
                continue;
            }
            s_lines = s_lines + 1;
            s_dirty = true;

            // เขียนบรรทัดพวก ALARM ให้ flush ทันที -- ของสำคัญ ห้ามหายตอนไฟดับ
            if (it.kind == LOGF_ALARM) { f->flush(); s_dirty = false; }
        }

        // ----- flush ตามรอบ -----
        if (s_dirty && now - lastFlush >= FLUSH_MS) {
            lastFlush = now;
            for (uint8_t k = 0; k < LOGF_COUNT; k++)
                if (s_file[k]) s_file[k].flush();
            s_dirty = false;
        }

        // ----- เช็คการ์ดหลุด + พื้นที่ว่าง ตอนคิวว่าง -----
        if (!got) {
            if (SD.cardType() == CARD_NONE) {    // ถอดการ์ดออก
                s_mounted = false;
                close_all();
                SD.end();
                continue;
            }
            if (now - lastFree >= FREE_REFRESH_MS) {
                lastFree = now;
                refresh_free();
            }
        }
    }
}

// ---------------------------------------------------------------------------
void sdlog_begin()
{
    s_q = xQueueCreate(QUEUE_LEN, sizeof(LogItem));
    if (!s_q) return;
    for (uint8_t k = 0; k < LOGF_COUNT; k++) s_fileDate[k][0] = '\0';

    xTaskCreatePinnedToCore(sd_task, "sdlog", TASK_STACK, nullptr, 2, nullptr, TASK_CORE);
}

// ---------------------------------------------------------------------------
bool sdlog_push(uint8_t kind, uint32_t epoch, const char *line)
{
    if (!s_q || kind >= LOGF_COUNT) return false;

    LogItem it;
    it.kind  = kind;
    it.epoch = epoch;
    strncpy(it.line, line, SDLOG_LINE_MAX - 1);
    it.line[SDLOG_LINE_MAX - 1] = '\0';

    // *** ห้ามบล็อก *** -- อยู่บนคอร์ 1 (LVGL/ทัช) timeout 0
    if (xQueueSend(s_q, &it, 0) != pdTRUE) {
        s_dropPush = s_dropPush + 1;
        return false;
    }
    return true;
}

void sdlog_stat(SdStat *out)
{
    out->mounted  = s_mounted;
    out->sizeMB   = s_sizeMB;
    out->freeMB   = s_freeMB;
    out->lines    = s_lines;
    out->dropped  = s_dropTask + s_dropPush;
    out->queueUse = s_q ? (uint8_t)uxQueueMessagesWaiting(s_q) : 0;
    out->queueMax = QUEUE_LEN;
}

bool sdlog_present() { return s_mounted; }
