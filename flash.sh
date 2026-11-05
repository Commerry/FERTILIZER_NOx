#!/usr/bin/env bash
# ============================================================================
#  flash.sh  --  คอมไพล์ + อัปโหลด (macOS / Linux)
#
#     ./flash.sh nano /dev/cu.wchusbserial10      # Nano  (*** ถอดสายจอ D0/D1 ***)
#     ./flash.sh display /dev/cu.wchusbserial10    # จอ    (*** ถอด link Nano ***)
#
#  หาพอร์ต:  ls /dev/cu.*
#  ต้องรัน ./setup.sh ก่อน 1 ครั้ง
# ============================================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
CLI="$ROOT/tools-bin/arduino-cli"
TARGET="$1"; PORT="$2"

[ -x "$CLI" ] || { echo "ยังไม่ได้รัน setup.sh -- ไม่พบ arduino-cli"; exit 1; }
[ -n "$PORT" ] || { echo "usage: ./flash.sh nano|display /dev/cu.xxxx"; exit 1; }

if [ "$TARGET" = "nano" ]; then
    FQBN="arduino:avr:nano"
    SKETCH="$ROOT"
    echo ">> แฟลช Nano (ถอดสายจอ D0/D1 ก่อน!)"
elif [ "$TARGET" = "display" ]; then
    FQBN="esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,CDCOnBoot=default,PartitionScheme=huge_app"
    SKETCH="$ROOT/GUITION ESP32-4848S040/Guition_HMI"
    echo ">> แฟลชจอ ESP32 (ถอด link Nano; ค้าง Connecting = กด BOOT ค้าง)"
else
    echo "usage: ./flash.sh nano|display /dev/cu.xxxx"; exit 1
fi

echo ">> คอมไพล์..."
"$CLI" compile --fqbn "$FQBN" "$SKETCH"
echo ">> อัปโหลดไป $PORT ..."
"$CLI" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"
echo ">> เสร็จ! อัปโหลด $TARGET สำเร็จ"
