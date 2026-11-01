# การติดตั้ง & อัปโหลดโค้ด — FERTILIZER NOx (O2 Control)

เครื่องมี 2 บอร์ด:
- **Arduino Nano** — ตัวควบคุมจริง (โค้ดอยู่ที่โฟลเดอร์ราก)
- **จอ ESP32 GUITION 4848S040** — HMI (โค้ดอยู่ `GUITION ESP32-4848S040/Guition_HMI/`)

---

## 1. เตรียมเครื่อง (ทำครั้งเดียว)

**ก. ลงไดรเวอร์ CH340** (ให้ COM port ขึ้น)
- โหลด `CH341SER.EXE` จาก https://www.wch.cn → ติดตั้ง → เสียบบอร์ด → เช็ค COMx ใน Device Manager

**ข. โคลนโปรเจกต์**
```powershell
git clone https://github.com/Commerry/FERTILIZER_NOx.git
cd FERTILIZER_NOx
```

**ค. รัน setup (ติดตั้ง arduino-cli + cores + libraries อัตโนมัติ)**
```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```
สคริปต์จะลงให้เอง:
- `arduino-cli` (โหลดไป `tools-bin/`)
- core: `arduino:avr` (Nano) + `esp32:esp32@3.3.11` (จอ — โหลด ~2GB)
- library: `lvgl@9.5.0` + `GFX Library for Arduino@1.6.6`
- ก๊อป `lv_conf.h` ไปโฟลเดอร์ libraries ให้

---

## 2. อัปโหลดโค้ด

> ⚠️ **สาย D0/D1 ของ Nano = สายเดียวกับจอ (UART0)** — แฟลชทีละบอร์ด ถอดอีกฝั่งก่อน

**แฟลช Nano** (ถอดสายจอ D0/D1 ก่อน):
```powershell
.\flash.ps1 nano COM15
```

**แฟลชจอ ESP32** (ถอดสาย link Nano ก่อน; ถ้าค้าง `Connecting...` กด **BOOT** ค้าง):
```powershell
.\flash.ps1 display COM15
```
(เปลี่ยน `COM15` เป็นพอร์ตจริง)

---

## 3. รหัส PIN (ดู FERTILIZER_NOx_User_Access_Guide.pdf)
| ระดับ | PIN |
|---|---|
| OPERATOR | 1111 |
| SUPERVISOR | 2222 |
| COMMISSIONER | 3333 |
| EXPERT | 4444 |

---

## ผังขา Nano (ย่อ — ดูเต็มใน config.h)
| ขา | อุปกรณ์ |
|----|---------|
| D10 | CP ปั๊มหมุนเวียน (relay) |
| D9 | CB โบลเวอร์หมุนเวียน (สัญญาณ VSP 5V) |
| A4 | FB โบลเวอร์ป้อน (relay) |
| A5 | FP ปั๊มป้อน (relay) |
| D4/D5 | RV วาล์ว เปิด/ปิด (H-bridge) |
| A2/A3 | RV feedback เทา/ดำ |
| D11/D12 | ไฟเขียว/แดง |
| D0/D1 | ลิงก์จอ | D2/D3/D6/D7/D8 RS485 |

---

## เครื่องมืออื่น (ถ้าแค่อยากแฟลชไม่คอมไพล์)
แฟลชไฟล์ .bin ตรง ๆ ด้วย `esptool` (จอ) / `avrdude` (Nano) ก็ได้ — ดู README.md
