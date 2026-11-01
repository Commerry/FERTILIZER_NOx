# ============================================================================
#  setup.ps1  --  ติดตั้งเครื่องมือ + cores + libraries ทั้งหมดอัตโนมัติ
#
#  วิธีใช้ (เปิด PowerShell ในโฟลเดอร์นี้):
#     powershell -ExecutionPolicy Bypass -File setup.ps1
#
#  สคริปต์นี้จะ:
#    1. โหลด arduino-cli (ถ้ายังไม่มี) ไปไว้ใน .\tools-bin\
#    2. ติดตั้ง core:  arduino:avr (Nano)  +  esp32:esp32@3.3.11 (จอ)
#    3. ติดตั้ง library:  lvgl@9.5.0  +  GFX Library for Arduino@1.6.6
#    4. ก๊อป lv_conf.h (ตั้งค่าเฉพาะโปรเจกต์) ไปไว้ในโฟลเดอร์ libraries ของ Arduino
#
#  *** ยังต้องลงไดรเวอร์ CH340 เองก่อน (ให้ COM port ขึ้น) ***
#     ดาวน์โหลด CH341SER.EXE จาก wch.cn
# ============================================================================
$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
$BIN  = Join-Path $ROOT "tools-bin"
$CLI  = Join-Path $BIN "arduino-cli.exe"
$ESP32_URL = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

function Say($m){ Write-Host ""; Write-Host ">> $m" -ForegroundColor Cyan }

# --- 1) arduino-cli ---
if (-not (Test-Path $CLI)) {
    Say "โหลด arduino-cli..."
    New-Item -ItemType Directory -Force -Path $BIN | Out-Null
    $zip = Join-Path $BIN "acli.zip"
    Invoke-WebRequest -Uri "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $BIN -Force
    Remove-Item $zip
} else { Say "arduino-cli มีแล้ว" }
& $CLI version

# --- 2) cores ---
Say "ตั้งค่า board manager (เพิ่ม URL ของ esp32)..."
& $CLI config init --overwrite | Out-Null
& $CLI config add board_manager.additional_urls $ESP32_URL
& $CLI core update-index

Say "ติดตั้ง core: arduino:avr (สำหรับ Nano)..."
& $CLI core install arduino:avr

Say "ติดตั้ง core: esp32:esp32@3.3.11 (สำหรับจอ) -- โหลดใหญ่ ~2GB ใจเย็น ๆ..."
& $CLI core install esp32:esp32@3.3.11

# --- 3) libraries ---
Say "ติดตั้ง library: lvgl@9.5.0 ..."
& $CLI lib install "lvgl@9.5.0"
Say "ติดตั้ง library: GFX Library for Arduino@1.6.6 ..."
& $CLI lib install "GFX Library for Arduino@1.6.6"

# --- 4) lv_conf.h (ตั้งค่าเฉพาะโปรเจกต์) ---
Say "ก๊อป lv_conf.h ไปไว้ในโฟลเดอร์ libraries..."
$libDir = Join-Path $env:USERPROFILE "Documents\Arduino\libraries"
New-Item -ItemType Directory -Force -Path $libDir | Out-Null
Copy-Item -Force (Join-Path $ROOT "lv_conf.h") (Join-Path $libDir "lv_conf.h")

Say "เสร็จ! ติดตั้งครบแล้ว"
Write-Host ""
Write-Host "ขั้นต่อไป -- แฟลชบอร์ด (ดู COM port จาก Device Manager):" -ForegroundColor Green
Write-Host "   .\flash.ps1 nano COM15        # แฟลช Arduino Nano (ถอดสายจอ D0/D1 ก่อน)"
Write-Host "   .\flash.ps1 display COM15      # แฟลชจอ ESP32 (ถอดสาย link Nano ก่อน)"
Write-Host ""
Write-Host "ยังไม่ได้ลงไดรเวอร์ CH340? -> โหลด CH341SER.EXE จาก wch.cn ก่อน" -ForegroundColor Yellow
