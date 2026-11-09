# การติดตั้ง & อัปโหลดโค้ด — FERTILIZER NOx (O2 Control)

เครื่องมี 2 บอร์ด:
- **Arduino Nano** — ตัวควบคุมจริง (โค้ดอยู่ที่โฟลเดอร์ราก)
- **จอ ESP32 GUITION 4848S040** — HMI (โค้ดอยู่ `GUITION ESP32-4848S040/Guition_HMI/`)

---

## 1. เตรียมเครื่อง (ทำครั้งเดียว)

**ก. ลงไดรเวอร์ CH340** (ให้ COM port ขึ้น)
- Windows 10/11 ส่วนใหญ่ลงให้เองตอนเสียบบอร์ด ถ้า COM ไม่ขึ้นใน Device Manager
  → โหลด `CH341SER.EXE` จาก https://www.wch-ic.com/downloads/CH341SER_EXE.html → ติดตั้ง → เสียบใหม่

**ข. ลง Git + โคลนโปรเจกต์** (เปิด PowerShell)
```powershell
winget install --id Git.Git -e --source winget
```
ปิด PowerShell แล้วเปิดใหม่ (ให้รู้จักคำสั่ง git) จากนั้น:
```powershell
cd $HOME\Desktop
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
- ก๊อป `lv_conf.h` ไปไว้ข้าง lvgl ที่ `%USERPROFILE%\Documents\Arduino\libraries` ให้
- ถ้าขั้นไหนล้ม สคริปต์จะหยุดพร้อมขึ้นตัวแดง (ไม่ขึ้น "เสร็จ!") → รันซ้ำได้

---

## 2. อัปโหลดโค้ด

> ⚠️ **สาย D0/D1 ของ Nano = สายเดียวกับจอ (UART0)** — แฟลชทีละบอร์ด ถอดอีกฝั่งก่อน

**หา COM port** (เสียบ USB บอร์ดก่อน):
```powershell
.\tools-bin\arduino-cli.exe board list
```

**แฟลชจอ ESP32** (ถอดสาย link Nano ก่อน; ถ้าค้าง `Connecting...` กด **BOOT** ค้าง):
```powershell
powershell -ExecutionPolicy Bypass -File flash.ps1 display COM5
```

**แฟลช Nano** (ถอดสายจอ D0/D1 ก่อน):
```powershell
powershell -ExecutionPolicy Bypass -File flash.ps1 nano COM5
```
(เปลี่ยน `COM5` เป็นพอร์ตจริง · ต้องพิมพ์ `powershell -ExecutionPolicy Bypass -File` นำหน้า
เพราะ Windows เครื่องใหม่ปิดการรันสคริปต์ไว้ — ถ้าพิมพ์ `.\flash.ps1` เฉย ๆ จะขึ้น "running scripts is disabled")

**อัปเดตโค้ดครั้งต่อไป** (ไม่ต้องรัน setup ซ้ำ):
```powershell
cd $HOME\Desktop\FERTILIZER_NOx
git pull
powershell -ExecutionPolicy Bypass -File flash.ps1 display COM5
```

---

## 2.5 ทางเลือก: ลงผ่าน macOS / Linux
ใช้สคริปต์ `.sh` แทน `.ps1` (ขั้นตอนเหมือนกัน):
```bash
# ลงไดรเวอร์ CH340 ก่อน: โหลด "CH34xVCPDriver" จาก wch.cn -> ติดตั้ง -> รีสตาร์ต
git clone https://github.com/Commerry/FERTILIZER_NOx.git
cd FERTILIZER_NOx
chmod +x setup.sh flash.sh
./setup.sh
ls /dev/cu.*                                   # หาพอร์ต (เช่น /dev/cu.wchusbserial10)
./flash.sh nano    /dev/cu.wchusbserial10      # ถอดสายจอ D0/D1 ก่อน
./flash.sh display /dev/cu.wchusbserial10      # ถอด link Nano ก่อน (ค้าง=กด BOOT)
```

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
