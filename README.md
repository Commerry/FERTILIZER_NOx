# เครื่องหมักปุ๋ยควบคุมออกซิเจน — Arduino Nano + GUITION ESP32-4848S040

ถังหมักปุ๋ยที่ต้องกด O2 ในถังลงให้ต่ำ (เป้า ~1.5%) โดยฉีด N2 ผ่านวาล์วหรี่ **RV**
พร้อมกับหมุนเวียนของเหลว/แก๊สและป้อนวัตถุดิบเป็นรอบ ๆ ตามเวลา

| บอร์ด | หน้าที่ | โฟลเดอร์ |
|---|---|---|
| **Arduino Nano** (ATmega328P) | ตัวควบคุมจริง — อ่านเซนเซอร์, ขับรีเลย์ 4 + วาล์ว PWM, รันลำดับการทำงาน | `Arduinonano_Display_O2_pressure/` |
| **GUITION ESP32-4848S040** | HMI — แสดงผล, ตั้งค่า, START/STOP, ระบบ PIN | `GUITION ESP32-4848S040/Guition_HMI/` |

**Nano คือสมองของเครื่อง** — ถ้าจอดับ/สายหลุด Nano ยังเดินกระบวนการต่อได้เอง
จอเป็นแค่หน้าต่างมองเข้าไปและปุ่มสั่งงาน ไม่เคยคุม output ตรง ๆ

---

## อุปกรณ์ (ตาม P&ID)

| ตัวย่อ | ชื่อเต็ม | คุมด้วย |
|---|---|---|
| **CP** | Circulation Pump — ปั๊มหมุนเวียนของเหลว | Relay 1 (D4) — รอบเวลา |
| **CB** | Circulation Blower — โบลเวอร์หมุนเวียนแก๊ส | Relay 2 (D5) — รอบเวลา |
| **FB** | Feed Blower — โบลเวอร์ป้อน | Relay 3 (D6) — รอบเวลา |
| **FP** | Feed Pump — ปั๊มป้อนของเหลว | Relay 4 (D7) — รอบเวลา |
| **RV** | Regulation Valve — วาล์วหรี่ N2 | MOSFET PWM (D3) — **คุมด้วยค่า O2** |
| **O1** | เซนเซอร์ออกซิเจนหลัก | RS485 — **ตัวที่ป้อนเข้าลูปควบคุม** |
| **O2** | เซนเซอร์ออกซิเจนตัวรีเช็ค | RS485 — ห่างจาก O1 เกินกำหนด → **Alarm** |
| **PT** | Pressure | RS485 |

**ไม่ได้ต่อเข้า Nano** (ตามสเปก):
- **N2Gen** — *"there is a switch ON/OFF separately as option"* → มีสวิตช์แยกของมันเอง
- **CB speed** — *"adjust speed via HMI or simple VR"* → ใช้ **VR ภายนอก** (MOSFET ตัวเดียวที่มีถูกจองให้ RV แล้ว)

---

## ⚠️ 3 ข้อที่ทำให้ของพังถ้าข้าม

### 1. Nano TX (5V) → ESP32 RX (3.3V) ต้องมี resistor divider

GPIO44 ของ ESP32 รับได้แค่ 3.3V ต่อ 5V ตรง ๆ = ขาพัง

```
Nano D1 (TX, 5V) ──[ 1kΩ ]──┬──> ESP32 GPIO44 (RX)
                            │
                          [ 2kΩ ]
                            │
                           GND
```
ทิศทางกลับกัน (ESP32 GPIO43 TX 3.3V → Nano D0 RX) **ต่อตรงได้**
**GND ของทั้งสองบอร์ดต้องต่อถึงกันเสมอ**

### 2. GPIO43/44 ของ ESP32 = สายเดียวกับชิพ USB (CH340)

- **อัปโหลดลง ESP32 → ถอดสาย Nano ออกจาก header ก่อน**
- **ห้ามใช้ `Serial.print()` debug** — ดู log ที่ **หน้า DIAG บนจอ** แทน

### 3. D0/D1 ของ Nano = สายเดียวกับ USB ของ Nano

**อัปโหลดลง Nano → ถอดสายที่ต่อไป ESP32 ออกก่อน**

---

## Pin map — Arduino Nano

| ขา | ต่อกับ | หมายเหตุ |
|---|---|---|
| D0 (RX0) | ESP32 GPIO43 (TX) | ต่อตรงได้ |
| D1 (TX0) | ESP32 GPIO44 (RX) | **ผ่าน divider 1k/2k** |
| D2 | MAX485 `DE` + `RE` | HIGH = ส่ง |
| **D3** | **RV** — MOSFET gate driver | PWM, Timer2 |
| **D4** | **CP** — Circulation Pump | Relay |
| **D5** | **CB** — Circulation Blower | Relay |
| **D6** | **FB** — Feed Blower | Relay |
| **D7** | **FP** — Feed Pump | Relay |
| D8 | MAX485 `RO` | **AltSoftSerial RX — ขาตายตัว** |
| D9 | MAX485 `DI` | **AltSoftSerial TX — ขาตายตัว** |
| D13 | LED heartbeat | |
| **A0** | **ปุ่ม START** บนตู้ | momentary → GND (INPUT_PULLUP) |
| **A1** | **ปุ่ม STOP** บนตู้ | momentary → GND (INPUT_PULLUP) |

### ทำไม pin map ต้องเป็นแบบนี้ (อย่าย้ายมั่ว)

ATmega328P มี Hardware UART **แค่ตัวเดียว** แต่งานนี้ต้องใช้ 2 เส้นพร้อมกัน

- **HW UART (D0/D1) → ลิงก์จอ**
- **AltSoftSerial (D8/D9) → RS485**

ใช้ `AltSoftSerial` ไม่ใช่ `SoftwareSerial` เพราะ `SoftwareSerial` **ปิด global interrupt ตลอดช่วงส่ง/รับ 1 byte** (~1 ms ที่ 9600) → HW UART พลาด byte → ลิงก์จอพัง
`AltSoftSerial` ใช้ Timer1 Input Capture จึงไม่บล็อก interrupt

**ผลตามมา:** Timer1 ถูกยึด → `analogWrite()` บน **D9/D10 ใช้ไม่ได้**
→ RV PWM จึงต้องอยู่ที่ **D3 (Timer2/OC2B)** ซึ่งไม่กระทบ `millis()` (Timer0)

---

## ลำดับการทำงาน (Nano — `process.cpp`)

```
IDLE  --[กด START (ปุ่ม A0 บนตู้ หรือปุ่มบนจอ)]-->  RUNNING
RUNNING --[STOP / E-STOP]-->  IDLE (ดับทุก output)
```

### 1) รอบเวลา — CP / CB / FB / FP

แต่ละตัวอิสระต่อกัน ตั้งจากหน้าจอได้ทั้ง 3 ค่า:
```
รอ <START DELAY> วิ → ติด <ON> วิ → ดับ <OFF> วิ → ติด <ON> วิ → ...
```
`ON = 0` → ไม่ทำงานเลย · `OFF = 0` → ติดค้างตลอด

คำนวณจากเวลาที่ผ่านไปตั้งแต่กด START ด้วย modulo ตรง ๆ ไม่เก็บ state ของรอบไว้
⇒ ไม่มีทางหลุดเฟส และไม่พังตอน `millis()` วนกลับ

### 2) วาล์ว RV — ไล่ตามโซน O2 (ยึดค่าจาก **O1**)

| ค่า O2 (จาก O1) | โซน | ทำอะไร |
|---|---|---|
| สูงกว่า RV1_Max (20%) | **RV MAX** | เปิดวาล์วสุด — ไล่อากาศออกให้เร็ว |
| 5–20% | **RV ROUGH** (6.1) | เปิดเพิ่มทีละ `coarseStep` (5%) — open-loop |
| 3–5% | **RV MEDIUM** (6.2) | เปิดเพิ่มทีละ `midStep` (2%) — open-loop |
| ต่ำกว่า 3% | **RV PID** (6.3) | PID ไล่เข้าหา `O2_TARGET` (1.5%) |

ทิศทาง: **O2 สูง = ต้องเติม N2 มากขึ้น = เปิดวาล์วมากขึ้น**

**`STEP INTERVAL` (default 10 วิ) — พารามิเตอร์ที่สเปกไม่มีแต่จำเป็น:**
open-loop ต้องเว้นจังหวะระหว่างก้าว เพราะระบบมี dead time (แก๊สต้องผสม + เซนเซอร์ตอบช้า)
ถ้าก้าวทุกรอบ loop วาล์วจะพุ่งไป 100% ใน 2 วินาทีแล้วแกว่งหนัก

PID มี **anti-windup** และล้าง integral ทุกครั้งที่เพิ่งเข้าโซน FINE (กันวาล์วกระชาก)

### 3) เทียบ O1 กับ O2 → Alarm

`|O1 − O2| > O2_DIFF_MAX` ติดต่อกันนานเกิน `O2_DIFF_HOLD` → จอขึ้นแถบแดง
**"O2 SENSORS DISAGREE - CHECK CALIBRATION"** — ไม่หยุดเครื่องเอง แค่เรียกช่างมาสอบเทียบ

---

## หน้าจอ (4 แท็บ)

| แท็บ | มีอะไร |
|---|---|
| **MONITOR** | ค่า O2×2 + Pressure · สถานะ RUN + เวลาที่เดินมา · **ชิปบอกโซน RV** (MAX/ROUGH/MEDIUM/PID) · แถบวาล์ว % · CP→CB→FB→FP ไฮไลต์ตัวที่ทำงาน · **ปุ่ม START / STOP** |
| **MANUAL** | สั่งรีเลย์/วาล์วเองทีละตัว (ต้องสลับเป็นโหมด MANUAL ก่อน) + E-STOP |
| **SETUP** | ตั้งค่าทุกพารามิเตอร์ผ่าน **คีย์บอร์ดตัวเลข** + **ระบบ PIN** + ปุ่ม SAVE TO EEPROM |
| **DIAG** | สถิติลิงก์ · log เฟรมดิบ · **Modbus Scanner** |

### ระบบ PIN (Access level)

| ระดับ | PIN เริ่มต้น | ทำอะไรได้ |
|---|---|---|
| — | ไม่ต้องใส่ | ดู MONITOR / DIAG |
| **Operator** | `1111` | + กด START / STOP, สั่ง MANUAL |
| **Supervisor** | `2222` | + แก้ **TIMERS** |
| **Commissioner** | `3333` | + แก้ **VALVE** (โซน RV, step) และ **ALARM** |
| **Expert** | `4444` | + แก้ **PID**, **SENSORS**, **SYSTEM**, และ PIN เอง |

แถวที่สิทธิ์ไม่ถึงจะเป็น**สีเทา กดไม่ได้** · ไม่แตะจอ 3 นาที → **ล็อกอัตโนมัติ**
เปลี่ยน PIN ได้ที่แท็บ SETUP → SYSTEM (ต้องมีสิทธิ์ Expert)

### พารามิเตอร์ที่ตั้งได้ (แท็บ SETUP)

- **TIMERS** — CP/CB/FB/FP × (START DELAY / ON TIME / OFF TIME) = 12 ค่า
- **VALVE** — RV1/RV2/RV3 ช่วง O2, coarseStep, midStep, STEP INTERVAL, VALVE MIN/MAX OUTPUT
- **PID** — O2 TARGET, Kp, Ki, Kd
- **ALARM** — O1 vs O2 MAX DIFF, HOLD TIME
- **SENSORS** — slave ID / function / register / data type / scale / offset ของทั้ง 3 ตัว
- **SYSTEM** — RS485 baud, poll, timeout, และ PIN ทั้ง 4 ระดับ

**ค่าจะยังไม่ถาวรจนกว่าจะกด `SAVE TO EEPROM`** (จงใจ — จะได้ลองค่าดูก่อนได้ ถ้าไม่ชอบก็ปิดไฟ)

---

## วิธีทดสอบ (ทำตามลำดับ)

### ทดสอบที่ 1 — จอขึ้นไหม (ยังไม่ต้องต่อ Nano)

อัปโหลด `Guition_HMI.ino` (ตั้ง board settings ตามตารางล่าง — **PSRAM = OPI สำคัญที่สุด**)

**ต้องเห็น:** 4 แท็บ · header เขียว · มุมขวาบนขึ้น `✕ NO LINK` สีแดง (ถูกต้อง เพราะยังไม่ต่อ Nano) · แตะสลับแท็บได้

| อาการ | สาเหตุ |
|---|---|
| จอขาว / บูตวน | PSRAM ไม่ได้ตั้งเป็น **OPI** |
| backlight กะพริบถี่ ๆ จอดำ | `display_init()` ล้มเหลว |
| สีเพี้ยน (แดงเป็นน้ำเงิน / ภาพติดแดง) | แก้ที่ `st7701_guition_init.h` — สลับ 2 ไบต์ที่มาร์ก `PATCHED` กลับทีละตัว |
| ภาพกระพริบ / เลื่อนแนวนอน | ลด `LCD_PCLK_HZ` ใน `board_pins.h`: 12M → 8M → 6M |

### ทดสอบที่ 2 — Nano (ยังไม่ต้องต่อจอ)

ถอดสาย ESP32 ออกจาก D0/D1 → อัปโหลด → เปิด Serial Monitor **57600**

**ต้องเห็น** เฟรมไหลออกมา 5 ครั้ง/วินาที และ LED กะพริบ:
```
$D,1,20.90,21.05,2.000,0,0,1,0,0,0,0*4A
     ^O1   ^O2   ^PT  ^relay ^valve ^mode ^err ^run ^zone ^elapsed
```

### ทดสอบที่ 3 — ลิงก์ + ลำดับการทำงาน (เป้าหมายหลัก)

ต่อสายครบ (**อย่าลืม divider**) → จ่ายไฟทั้งสองบอร์ด

1. มุมขวาบนต้องเป็น `✓ LINK #123` สีเขียว เลขวิ่งขึ้นเรื่อย ๆ
2. ไปแท็บ **SETUP** → กดปุ่ม `LOCKED` → ใส่ PIN **`2222`** (Supervisor)
3. แท็บย่อย **TIMERS** → ตั้ง `CIRCULATION PUMP - ON TIME` = **5** วิ, `OFF TIME` = **5** วิ
4. กลับหน้า **MONITOR** → กด **START**

**ต้องเห็น:**
- สถานะเปลี่ยนเป็น `RUNNING 00:00:12` และเวลาเดินขึ้น
- **ได้ยินเสียงรีเลย์ CP คลิกทุก 5 วินาที** และกล่อง CP บนจอสว่าง-ดับตาม
- ชิปโซนเปลี่ยนจาก `RV MAX` (แดง) → `RV ROUGH` (ส้ม) → `RV MEDIUM` (เหลือง) → `RV PID` (เขียว)
  พร้อมกับแถบวาล์วไต่ขึ้นทีละ 5% ทุก 10 วินาที
  *(ค่าจำลองใน Nano จำลองว่า "วาล์วเปิด → O2 ลด" จึงเห็นลูปควบคุมทำงานได้จริงโดยยังไม่ต้องมีเซนเซอร์)*
- **วัดแรงดันที่ขา D3** ของ Nano → ต้องเปลี่ยนตามแถบวาล์วบนจอ
- แท็บ **DIAG**: `crc-err` ต้องเป็น **0**

5. กด **STOP** → ทุกอย่างต้องดับหมดทันที
6. กดปุ่ม START จริงบนตู้ (A0) → ต้องเริ่มเหมือนกดบนจอ

### ทดสอบที่ 4 — EEPROM

ตั้งค่าบนจอ → กด **SAVE TO EEPROM** → ปิดไฟ → เปิดใหม่ → ค่าต้องยังอยู่

---

## หา register ของเซนเซอร์ (Modbus Scanner)

ยังไม่มีดาต้าชีต → โค้ด**ไม่ hardcode register** เลย ให้ไล่หาจากหน้าจอแทน

1. ต่อเซนเซอร์เข้า RS485 (บัสเดียว MAX485 ตัวเดียว)
2. แท็บ **DIAG** → **MODBUS SCANNER**
3. เลือก `SLAVE` (ไล่ 1, 2, 3…) และ `FUNCTION` (03 / 04)
4. กด `+1` `+10` `+100` เลื่อน `REG` แล้วกด **READ**
5. ดูผล — จอ decode ให้ดูหลายแบบพร้อมกัน:
   ```
   RAW: 0082 0000
   U16 130   x0.1 13.0   x0.01 1.30
   F32 ABCD 0.000
   F32 CDAB 20.938      <-- อันนี้แหละ! O2 ในอากาศปกติ ≈ 20.9
   ```
6. เจอแล้ว → ไปแท็บ **SETUP → SENSORS** (PIN Expert `4444`) ใส่ `slave / function / register / data type / scale`
7. กด **SAVE TO EEPROM** → แก้ `USE_DUMMY_SENSORS` เป็น `0` ใน `.ino` → อัปโหลดใหม่

**ถ้า O2 สองตัว slave ID ซ้ำกันจากโรงงาน** ต้องเอามาต่อทีละตัวแล้วตั้ง ID ใหม่ก่อน ไม่งั้นจะตอบชนกันบนบัส

---

## ⚠️ กฎเหล็กเวลาแก้โค้ด

`link_protocol.h` **มี 2 สำเนา** (Nano / ESP32) และต้องเหมือนกันเป๊ะ ๆ
ถ้าไม่ตรงกัน CRC จะไม่ตรง → **ลิงก์เงียบสนิทโดยไม่มี error ให้เห็น** ซึ่งหาสาเหตุยากมาก

👉 **แก้ไฟล์นี้เมื่อไหร่ ให้ดับเบิลคลิก [`sync_protocol.bat`](sync_protocol.bat) ทุกครั้ง**

---

## Environment

**Arduino Nano** — Board: `Arduino Nano` / Processor: `ATmega328P`
ไลบรารี: `AltSoftSerial`, `ModbusMaster 2.0.1`
ขนาด: **18.9 KB flash (61%) · 1253 B RAM (61%)**

**ESP32-4848S040** — ไลบรารี: `GFX Library for Arduino 1.6.6`, `lvgl 9.5.0`
ขนาด: **757 KB flash (24%) · 126 KB RAM (38%)**

| Arduino IDE setting | ค่า |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| **PSRAM** | **OPI PSRAM** ← ผิดแล้วจอขาว |
| **USB CDC On Boot** | **Disabled** |
| Partition Scheme | Huge APP (3MB No OTA) |

`lv_conf.h` อยู่ที่ `Documents/Arduino/libraries/lv_conf.h` (เปิด montserrat 14/16/20/28/32/48 + LV_MEM 96KB)

---

## สถานะ

| Step | | สถานะ |
|---|---|---|
| 1-5 | ลิงก์ + จอ + Modbus Scanner | ✅ เสร็จ |
| 6 | หน้า SETUP + keypad + PIN + EEPROM | ✅ เสร็จ |
| 7 | ลำดับการทำงาน (timer + RV zone + PID + alarm) | ✅ เสร็จ |
| — | **ทดสอบบนฮาร์ดแวร์จริง** | ⬜ **รอคุณ** |
| — | จูน PID หน้างาน | ⏸ ค่าเริ่มต้น Kp=2 Ki=0.1 Kd=0 (จูนผ่านจอได้เลย) |
