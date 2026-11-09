# ============================================================================
#  setup.ps1  --  ติดตั้งเครื่องมือ + cores + libraries ทั้งหมดอัตโนมัติ (Windows)
#
#  วิธีใช้ (เปิด PowerShell ในโฟลเดอร์นี้):
#     powershell -ExecutionPolicy Bypass -File setup.ps1
#
#  สคริปต์นี้จะ:
#    1. โหลด arduino-cli (ถ้ายังไม่มี) ไปไว้ใน .\tools-bin\
#    2. ติดตั้ง core:  arduino:avr (Nano)  +  esp32:esp32@3.3.11 (จอ)
#    3. ติดตั้ง library:  lvgl@9.5.0  +  GFX Library for Arduino@1.6.6
#    4. ก๊อป lv_conf.h (ตั้งค่าเฉพาะโปรเจกต์) ไปไว้ข้าง ๆ library lvgl
#
#  *** ไฟล์นี้ต้องเซฟเป็น UTF-8 "มี BOM" ***
#     PowerShell 5.1 (ที่มากับ Windows) อ่านไฟล์ไม่มี BOM เป็น ANSI
#     -> ตัวอักษรไทยกลายเป็นเครื่องหมายคำพูด -> parse error รันไม่ได้เลย
# ============================================================================
$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"     # progress bar ของ PS 5.1 ทำให้โหลดช้ามาก
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
$BIN  = Join-Path $ROOT "tools-bin"
$CLI  = Join-Path $BIN "arduino-cli.exe"
$ESP32_URL = "https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

# โฟลเดอร์ sketchbook/libraries -- กำหนดตายตัว
#   (ถ้าปล่อย default บางเครื่อง Documents ถูกย้ายไป OneDrive -> lvgl ไปลงที่หนึ่ง
#    lv_conf.h ไปอีกที่ -> คอมไพล์จอไม่ผ่าน)
$USERDIR = Join-Path $env:USERPROFILE "Documents\Arduino"
$LIBDIR  = Join-Path $USERDIR "libraries"

function Say($m){ Write-Host ""; Write-Host ">> $m" -ForegroundColor Cyan }

# เรียก arduino-cli แล้วเช็คผล -- ล้มเมื่อไหร่หยุดทันที ไม่โชว์ "เสร็จ" หลอก
function Invoke-Acli {
    & $CLI @args
    if ($LASTEXITCODE -ne 0) { throw "arduino-cli $($args -join ' ') ล้มเหลว (exit $LASTEXITCODE)" }
}

# --- 1) arduino-cli ---
if (-not (Test-Path $CLI)) {
    Say "โหลด arduino-cli..."
    New-Item -ItemType Directory -Force -Path $BIN | Out-Null
    $zip = Join-Path $BIN "acli.zip"
    Invoke-WebRequest -Uri "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Windows_64bit.zip" -OutFile $zip
    Expand-Archive -Path $zip -DestinationPath $BIN -Force
    Remove-Item $zip
} else { Say "arduino-cli มีแล้ว" }
Invoke-Acli version

# --- 2) cores ---
Say "ตั้งค่า arduino-cli (sketchbook = $USERDIR + URL ของ esp32)..."
Invoke-Acli config init --overwrite | Out-Null
Invoke-Acli config set directories.user $USERDIR
Invoke-Acli config add board_manager.additional_urls $ESP32_URL
Invoke-Acli core update-index

Say "ติดตั้ง core: arduino:avr (สำหรับ Nano)..."
Invoke-Acli core install arduino:avr

Say "ติดตั้ง core: esp32:esp32@3.3.11 (สำหรับจอ) -- โหลดใหญ่ ~2GB ใจเย็น ๆ..."
Invoke-Acli core install esp32:esp32@3.3.11

# --- 3) libraries ---
Say "ติดตั้ง library: lvgl@9.5.0 ..."
Invoke-Acli lib install "lvgl@9.5.0"
Say "ติดตั้ง library: GFX Library for Arduino@1.6.6 ..."
Invoke-Acli lib install "GFX Library for Arduino@1.6.6"

# --- 4) lv_conf.h (ต้องอยู่ข้าง ๆ โฟลเดอร์ lvgl) ---
Say "ก๊อป lv_conf.h ไปที่ $LIBDIR ..."
New-Item -ItemType Directory -Force -Path $LIBDIR | Out-Null
Copy-Item -Force (Join-Path $ROOT "lv_conf.h") (Join-Path $LIBDIR "lv_conf.h")
if (-not (Test-Path (Join-Path $LIBDIR "lvgl"))) {
    throw "ไม่พบ $LIBDIR\lvgl -- lvgl ไม่ได้ลงที่โฟลเดอร์นี้ ส่งข้อความนี้ให้ผู้ดูแล"
}

Say "เสร็จ! ติดตั้งครบแล้ว"
Write-Host ""
Write-Host "ขั้นต่อไป -- หา COM port:  .\tools-bin\arduino-cli.exe board list" -ForegroundColor Green
Write-Host "   powershell -ExecutionPolicy Bypass -File flash.ps1 display COM5   # จอ (ถอดสาย link Nano ก่อน)"
Write-Host "   powershell -ExecutionPolicy Bypass -File flash.ps1 nano COM5      # Nano (ถอดสายจอ D0/D1 ก่อน)"
Write-Host ""
Write-Host "COM port ไม่ขึ้น? -> ลงไดรเวอร์ CH340 (CH341SER.EXE จาก wch-ic.com) แล้วเสียบใหม่" -ForegroundColor Yellow
