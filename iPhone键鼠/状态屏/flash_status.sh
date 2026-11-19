#!/usr/bin/env bash
# 编译并刷派单状态屏固件。用法: ./flash_status.sh
set -euo pipefail
cd "$(dirname "$0")"
source ./ports.sh
FQBN="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi"
OUT=build_status
port() { ls /dev/serial/by-id/*80456B35AEEC* /dev/serial/by-id/*80:45:6B:35:AE:EC* 2>/dev/null | head -n1 || true; }
~/.local/bin/arduino-cli compile -b "$FQBN" --output-dir "$OUT" hid_status | grep -E "Sketch uses|Global variables|rror" || true
P=$(port); [[ -n "$P" ]] || { echo "板子没插在本机"; exit 1; }
touch_1200 "$P"; sleep 2
for _ in $(seq 20); do P=$(port); [[ -n "$P" ]] && break; sleep 0.5; done
echo "刷到 $P"
~/.local/bin/arduino-cli upload -b "$FQBN" -p "$P" --input-dir "$OUT" hid_status 2>&1 | tail -5
