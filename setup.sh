#!/usr/bin/env bash
# ============================================================================
#  setup.sh  --  ติดตั้งเครื่องมือ + cores + libraries (macOS / Linux)
#
#  วิธีใช้:  chmod +x setup.sh && ./setup.sh
#
#  *** macOS ต้องลงไดรเวอร์ CH340 ก่อน ***
#     โหลด "CH34xVCPDriver" จาก wch.cn (หรือ wch-ic.com) -> ติดตั้ง -> รีสตาร์ต
#     เช็คพอร์ต:  ls /dev/cu.*     (จะเห็น /dev/cu.wchusbserial... หรือ usbserial...)
# ============================================================================
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/tools-bin"
CLI="$BIN/arduino-cli"
ESP32_URL="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"

say(){ echo ""; echo ">> $*"; }

# --- 1) arduino-cli ---
if [ ! -x "$CLI" ]; then
    say "โหลด arduino-cli..."
    mkdir -p "$BIN"
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR="$BIN" sh
fi
"$CLI" version

# --- 2) cores ---
say "ตั้งค่า board manager (เพิ่ม URL ของ esp32)..."
"$CLI" config init --overwrite >/dev/null 2>&1 || true
"$CLI" config add board_manager.additional_urls "$ESP32_URL"
"$CLI" core update-index

say "ติดตั้ง core: arduino:avr (Nano)..."
"$CLI" core install arduino:avr

say "ติดตั้ง core: esp32:esp32@3.3.11 (จอ -- โหลดใหญ่ ~2GB)..."
"$CLI" core install esp32:esp32@3.3.11

# --- 3) libraries ---
say "ติดตั้ง library: lvgl@9.5.0 ..."
"$CLI" lib install "lvgl@9.5.0"
say "ติดตั้ง library: GFX Library for Arduino@1.6.6 ..."
"$CLI" lib install "GFX Library for Arduino@1.6.6"

# --- 4) lv_conf.h ---
say "ก๊อป lv_conf.h ไปโฟลเดอร์ libraries..."
LIBDIR="$HOME/Documents/Arduino/libraries"
mkdir -p "$LIBDIR"
cp -f "$ROOT/lv_conf.h" "$LIBDIR/lv_conf.h"

say "เสร็จ! ติดตั้งครบ"
echo ""
echo "ขั้นต่อไป -- แฟลช (หาพอร์ต: ls /dev/cu.*):"
echo "   ./flash.sh nano /dev/cu.wchusbserial10      # ถอดสายจอ D0/D1 ก่อน"
echo "   ./flash.sh display /dev/cu.wchusbserial10    # ถอดสาย link Nano ก่อน"
