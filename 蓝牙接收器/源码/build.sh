#!/usr/bin/env bash
# 编译低功耗蓝牙 USB 接收器固件（ESP32-S3 N16R8，Pocket-Dongle-S3）。
# 一般由 GitHub Actions 自动编译（.github/workflows/蓝牙接收器编译.yml）。
# 本机编译：装好 ESP-IDF v6.1 并执行 export.sh 后，bash 蓝牙接收器/源码/build.sh
#
# 产物在 firmware/out/：
#   ble-hci-dongle-merged.bin  整包固件，从 0x0 地址整片烧录
#   sdkconfig                  这次的完整编译配置
#   BUILD_INFO.txt             ESP-IDF 版本、编译时间、校验值
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${OUT:-$HERE/out}"
NAME="ble-hci-dongle"

cd "$HERE"
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
idf.py merge-bin

# sdkconfig.defaults 里写错的选项只会被悄悄忽略，这里逐条确认真的生效了
for opt in CONFIG_BT_CONTROLLER_ONLY CONFIG_BT_CTRL_HCI_MODE_VHCI CONFIG_ESP_CONSOLE_NONE \
           CONFIG_BOOTLOADER_LOG_LEVEL_NONE CONFIG_ESPTOOLPY_FLASHSIZE_16MB; do
  grep -q "^${opt}=y$" sdkconfig || { echo "配置没有生效：$opt" >&2; exit 1; }
done

mkdir -p "$OUT"
cp build/merged-binary.bin "$OUT/$NAME-merged.bin"
cp sdkconfig "$OUT/sdkconfig"
{
  echo "target: esp32s3 (Pocket-Dongle-S3, N16R8)"
  echo "idf: $(idf.py --version 2>/dev/null | tail -n1)"
  echo "built_at_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "sha256: $(sha256sum "$OUT/$NAME-merged.bin" | cut -d' ' -f1)"
} > "$OUT/BUILD_INFO.txt"
cat "$OUT/BUILD_INFO.txt"
ls -l "$OUT"
