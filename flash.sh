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
BIN="$ROOT/tools-bin"
CLI="$BIN/arduino-cli"
TARGET="$1"; PORT="$2"

[ -x "$CLI" ] || { echo "ยังไม่ได้รัน setup.sh -- ไม่พบ arduino-cli"; exit 1; }
[ -n "$PORT" ] || { echo "usage: ./flash.sh nano|display /dev/cu.xxxx"; exit 1; }

if [ "$TARGET" = "nano" ]; then
    # arduino-cli บังคับ "ชื่อโฟลเดอร์ = ชื่อไฟล์ .ino หลัก" แต่ repo โคลนมาชื่อ FERTILIZER_NOx
    # -> ก๊อปไฟล์ของ Nano (.ino/.cpp/.h ที่ราก) ไปโฟลเดอร์ชื่อถูกต้องก่อนคอมไพล์
    SKETCH="$BIN/nano-sketch/Arduinonano_Display_O2_pressure"
    rm -rf "$SKETCH"; mkdir -p "$SKETCH"
    cp "$ROOT"/*.ino "$ROOT"/*.cpp "$ROOT"/*.h "$SKETCH"/
    # Nano มี 2 แบบ: bootloader ใหม่ / แบบเก่า (พบบ่อยในตัวก๊อป) -- ลองใหม่ก่อน ไม่ผ่านค่อยลองเก่า
    FQBNS="arduino:avr:nano arduino:avr:nano:cpu=atmega328old"
    echo ">> แฟลช Nano (ถอดสายจอ D0/D1 ก่อน!)"
elif [ "$TARGET" = "display" ]; then
    FQBNS="esp32:esp32:esp32s3:FlashSize=16M,FlashMode=qio,PSRAM=opi,CDCOnBoot=default,PartitionScheme=huge_app,UploadSpeed=115200"
    SKETCH="$ROOT/GUITION ESP32-4848S040/Guition_HMI"
    echo ">> แฟลชจอ ESP32 (ถอด link Nano; ค้าง Connecting = กด BOOT ค้าง)"
else
    echo "usage: ./flash.sh nano|display /dev/cu.xxxx"; exit 1
fi

for FQBN in $FQBNS; do
    echo ">> คอมไพล์ ($FQBN)..."
    "$CLI" compile --fqbn "$FQBN" "$SKETCH"
    echo ">> อัปโหลดไป $PORT ..."
    if "$CLI" upload -p "$PORT" --fqbn "$FQBN" "$SKETCH"; then
        echo ">> เสร็จ! อัปโหลด $TARGET สำเร็จ ($FQBN)"; exit 0
    fi
    echo ">> อัปโหลดไม่ผ่าน -- ลองแบบถัดไป (ถ้ามี)..."
done
echo "อัปโหลดล้มเหลว (เช็คพอร์ต / สาย USB / ถอดสาย D0-D1 หรือ link แล้วหรือยัง)"
exit 1
