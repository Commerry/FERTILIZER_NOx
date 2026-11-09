# ============================================================================
#  flash.ps1  --  คอมไพล์ + อัปโหลด บอร์ด (Nano หรือ จอ ESP32)
#
#  วิธีใช้:
#     powershell -ExecutionPolicy Bypass -File flash.ps1 nano COM5      # Nano (*** ถอดสายจอ D0/D1 ก่อน ***)
#     powershell -ExecutionPolicy Bypass -File flash.ps1 display COM5   # จอ  (*** ถอดสาย link Nano ก่อน ***)
#
#  ต้องรัน setup.ps1 ก่อน 1 ครั้ง (ติดตั้ง cores + libraries)
#  *** ไฟล์นี้ต้องเซฟเป็น UTF-8 "มี BOM" (PowerShell 5.1 อ่านไทยไม่มี BOM แล้ว parse พัง) ***
# ============================================================================
param(
    [Parameter(Mandatory=$true)][ValidateSet("nano","display")][string]$target,
    [Parameter(Mandatory=$true)][string]$port
)
$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
$BIN  = Join-Path $ROOT "tools-bin"
$CLI  = Join-Path $BIN "arduino-cli.exe"
if (-not (Test-Path $CLI)) { Write-Host "ยังไม่ได้รัน setup.ps1 -- ไม่พบ arduino-cli" -ForegroundColor Red; exit 1 }

if ($target -eq "nano") {
    # arduino-cli บังคับ "ชื่อโฟลเดอร์ = ชื่อไฟล์ .ino หลัก" แต่ repo โคลนมาชื่อ FERTILIZER_NOx
    # -> ก๊อปไฟล์ของ Nano (.ino/.cpp/.h ที่ราก) ไปโฟลเดอร์ชื่อถูกต้องก่อนคอมไพล์
    $sketch = Join-Path $BIN "nano-sketch\Arduinonano_Display_O2_pressure"
    if (Test-Path $sketch) { Remove-Item -Recurse -Force $sketch }
    New-Item -ItemType Directory -Force -Path $sketch | Out-Null
    Copy-Item -Path (Join-Path $ROOT "*.ino"), (Join-Path $ROOT "*.cpp"), (Join-Path $ROOT "*.h") -Destination $sketch
    # Nano มี 2 แบบ: bootloader ใหม่ (115200) / แบบเก่า (57600, พบบ่อยในตัวก๊อป)
    # ลองแบบใหม่ก่อน ไม่ผ่านค่อยลองแบบเก่าให้เอง
    $fqbns  = @("arduino:avr:nano", "arduino:avr:nano:cpu=atmega328old")
    Write-Host ">> แฟลช Nano (ถอดสายจอ D0/D1 ออกก่อน!)" -ForegroundColor Yellow
} else {
    $fqbns  = @("esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,CDCOnBoot=default,PartitionScheme=huge_app,UploadSpeed=115200")
    $sketch = Join-Path $ROOT "GUITION ESP32-4848S040\Guition_HMI"
    Write-Host ">> แฟลชจอ ESP32 (ถอดสาย link Nano; ถ้าค้าง Connecting ให้กด BOOT ค้าง)" -ForegroundColor Yellow
}

for ($i = 0; $i -lt $fqbns.Count; $i++) {
    $fqbn = $fqbns[$i]
    Write-Host ">> คอมไพล์ ($fqbn)..." -ForegroundColor Cyan
    & $CLI compile --fqbn $fqbn "$sketch"
    if ($LASTEXITCODE -ne 0) { Write-Host "คอมไพล์ล้มเหลว" -ForegroundColor Red; exit 1 }

    Write-Host ">> อัปโหลดไป $port ..." -ForegroundColor Cyan
    & $CLI upload -p $port --fqbn $fqbn "$sketch"
    if ($LASTEXITCODE -eq 0) { Write-Host ">> เสร็จ! อัปโหลด $target สำเร็จ ($fqbn)" -ForegroundColor Green; exit 0 }

    if ($i -lt $fqbns.Count - 1) { Write-Host ">> อัปโหลดไม่ผ่าน -- ลอง bootloader แบบเก่า..." -ForegroundColor Yellow }
}
Write-Host "อัปโหลดล้มเหลว (เช็ค COM port / สาย USB / ถอดสาย D0-D1 หรือ link แล้วหรือยัง)" -ForegroundColor Red
exit 1
