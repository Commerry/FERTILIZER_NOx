# ============================================================================
#  flash.ps1  --  คอมไพล์ + อัปโหลด บอร์ด (Nano หรือ จอ ESP32)
#
#  วิธีใช้:
#     .\flash.ps1 nano COM15        # Arduino Nano  (*** ถอดสายจอ D0/D1 ก่อน ***)
#     .\flash.ps1 display COM15      # จอ ESP32      (*** ถอดสาย link Nano ก่อน ***)
#
#  ต้องรัน setup.ps1 ก่อน 1 ครั้ง (ติดตั้ง cores + libraries)
# ============================================================================
param(
    [Parameter(Mandatory=$true)][ValidateSet("nano","display")][string]$target,
    [Parameter(Mandatory=$true)][string]$port
)
$ErrorActionPreference = "Stop"
$ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
$CLI  = Join-Path $ROOT "tools-bin\arduino-cli.exe"
if (-not (Test-Path $CLI)) { Write-Host "ยังไม่ได้รัน setup.ps1 -- ไม่พบ arduino-cli" -ForegroundColor Red; exit 1 }

if ($target -eq "nano") {
    $fqbn   = "arduino:avr:nano"
    $sketch = $ROOT
    Write-Host ">> แฟลช Nano (ถอดสายจอ D0/D1 ออกก่อน!)" -ForegroundColor Yellow
} else {
    $fqbn   = "esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,CDCOnBoot=default,PartitionScheme=huge_app,UploadSpeed=115200"
    $sketch = Join-Path $ROOT "GUITION ESP32-4848S040\Guition_HMI"
    Write-Host ">> แฟลชจอ ESP32 (ถอดสาย link Nano; ถ้าค้าง Connecting ให้กด BOOT ค้าง)" -ForegroundColor Yellow
}

Write-Host ">> คอมไพล์..." -ForegroundColor Cyan
& $CLI compile --fqbn $fqbn "$sketch"
if ($LASTEXITCODE -ne 0) { Write-Host "คอมไพล์ล้มเหลว" -ForegroundColor Red; exit 1 }

Write-Host ">> อัปโหลดไป $port ..." -ForegroundColor Cyan
& $CLI upload -p $port --fqbn $fqbn "$sketch"
if ($LASTEXITCODE -ne 0) { Write-Host "อัปโหลดล้มเหลว (เช็ค COM/สาย/BOOT)" -ForegroundColor Red; exit 1 }

Write-Host ">> เสร็จ! อัปโหลด $target สำเร็จ" -ForegroundColor Green
