#!/usr/bin/env bash
# 编译 Muse 版小智固件（微雪 ESP32-S3-Touch-LCD-1.54）：按键说话 + "小明同学"唤醒。
#
# 一般不用在本机跑：GitHub 仓库的 Actions 会自动编译（见 .github/workflows/muse-xiaozhi-firmware.yml）。
# 想在本机编译：需要 Linux 或 WSL，并装好 ESP-IDF v6.1，然后：
#   OTA_URL=http://192.168.1.100:8800/xiaozhi/ota/ bash firmware/build.sh
# 换唤醒词：WAKE_WORD=乐鑫的模型名（例如 wn9_hilexin），关掉唤醒词：WAKE_WORD=disabled
#
# 产物在 firmware/out/：
#   keshi-desk-1.54-merged.bin  整包固件，从 0x0 地址整片烧录
#   dependencies.lock           这次用到的所有组件的精确版本（锁版本用）
#   sdkconfig                   这次的完整编译配置
#   BUILD_INFO.txt              上游版本、OTA 地址、唤醒词、ESP-IDF 版本
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM_REPO="${UPSTREAM_REPO:-https://github.com/78/xiaozhi-esp32.git}"
UPSTREAM_COMMIT="$(tr -d ' \r\n' < "$HERE/UPSTREAM_COMMIT")"
OTA_URL="${OTA_URL:-http://192.168.1.100:8800/xiaozhi/ota/}"
WAKE_WORD="${WAKE_WORD:-wn9_xiaomingtongxue_tts2}"   # 小明同学（乐鑫 WakeNet9 预设模型）
WORK="${WORK:-$HERE/.build}"
OUT="${OUT:-$HERE/out}"
SRC="$WORK/xiaozhi-esp32"
BOARD="keshi/keshi-desk-1.54"
NAME="keshi-desk-1.54"

# 设备第一次开机去这个地址拿服务器配置；绝不能留官方云端地址
case "$OTA_URL" in
  http://*/xiaozhi/ota/ | https://*/xiaozhi/ota/) ;;
  *) echo "OTA_URL 格式不对：$OTA_URL（应类似 http://192.168.1.100:8800/xiaozhi/ota/）" >&2; exit 2 ;;
esac
if [[ "$OTA_URL" == *tenclass* || "$OTA_URL" == *xiaozhi.me* ]]; then
  echo "OTA_URL 不能指向小智官方云端：$OTA_URL" >&2
  exit 2
fi

if ! command -v idf.py >/dev/null 2>&1; then
  if [ -n "${IDF_PATH:-}" ] && [ -f "$IDF_PATH/export.sh" ]; then
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" >/dev/null
  else
    echo "没找到 ESP-IDF（idf.py）。请先安装 ESP-IDF v6.1 并执行 export.sh。" >&2
    exit 2
  fi
fi

echo "== 1/4 取上游源码 $UPSTREAM_COMMIT"
rm -rf "$SRC"
mkdir -p "$SRC"
git -C "$SRC" init -q
git -C "$SRC" remote add origin "$UPSTREAM_REPO"
git -C "$SRC" fetch -q --depth 1 origin "$UPSTREAM_COMMIT"
git -C "$SRC" checkout -q --detach FETCH_HEAD

echo "== 2/4 打补丁、放板子文件"
for p in "$HERE"/patches/*.patch; do
  git -C "$SRC" apply --whitespace=nowarn "$p"
done
mkdir -p "$SRC/main/boards/keshi"
cp -r "$HERE/board/keshi-desk-1.54" "$SRC/main/boards/keshi/"
# 把 OTA 地址写进这块板子的编译配置；唤醒后不把唤醒词录音发给服务端（服务端用不上，发了反而要等回复）
python3 - "$SRC/main/boards/$BOARD/config.json" "$OTA_URL" <<'PY'
import json, sys
path, url = sys.argv[1], sys.argv[2]
cfg = json.load(open(path, encoding="utf-8"))
for b in cfg["builds"]:
    items = [x for x in b.get("sdkconfig_append", [])
             if not x.startswith(("CONFIG_OTA_URL=", "CONFIG_SEND_WAKE_WORD_DATA="))]
    items.append(f'CONFIG_OTA_URL="{url}"')
    items.append("CONFIG_SEND_WAKE_WORD_DATA=n")
    b["sdkconfig_append"] = items
json.dump(cfg, open(path, "w", encoding="utf-8"), ensure_ascii=False, indent=4)
PY
# 锁定组件版本：仓库里有 dependencies.lock 就用它（第一次编译后生成并提交）
if [ -f "$HERE/dependencies.lock" ]; then
  cp "$HERE/dependencies.lock" "$SRC/dependencies.lock"
  echo "   使用锁定的组件版本 firmware/dependencies.lock"
else
  echo "   还没有 firmware/dependencies.lock，这次按上游的版本范围取组件，编完会生成一份"
fi

echo "== 3/4 编译（第一次要下载组件，十几分钟）"
cd "$SRC"
python3 scripts/build.py "$BOARD" --name "$NAME" --language zh-CN --wake-word "$WAKE_WORD"

echo "== 4/4 收集产物"
mkdir -p "$OUT"
cp build/merged-binary.bin "$OUT/$NAME-merged.bin"
cp dependencies.lock "$OUT/dependencies.lock"
cp sdkconfig "$OUT/sdkconfig"
grep -q "^CONFIG_OTA_URL=\"$OTA_URL\"$" sdkconfig || { echo "OTA 地址没有写进配置" >&2; exit 1; }
grep -q "^CONFIG_KESHI_VOICE_MODE=y$" sdkconfig || { echo "按键一问一答模式没有打开" >&2; exit 1; }
if [ "$WAKE_WORD" != "disabled" ]; then
  ww_symbol="CONFIG_SR_WN_$(echo "$WAKE_WORD" | tr '[:lower:]' '[:upper:]')"
  grep -q "^${ww_symbol}=y$" sdkconfig || { echo "唤醒词 $WAKE_WORD 没有编进去" >&2; exit 1; }
  grep -q "^CONFIG_USE_AFE_WAKE_WORD=y$" sdkconfig || { echo "唤醒功能没有打开" >&2; exit 1; }
fi
{
  echo "upstream: $UPSTREAM_REPO @ $UPSTREAM_COMMIT"
  echo "board: $BOARD ($NAME)"
  echo "ota_url: $OTA_URL"
  echo "wake_word: $WAKE_WORD"
  echo "idf: $(idf.py --version 2>/dev/null | tail -n1)"
  echo "project_version: $(sed -n 's/^set(PROJECT_VER "\(.*\)")/\1/p' CMakeLists.txt)"
  echo "built_at_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "sha256: $(sha256sum "$OUT/$NAME-merged.bin" | cut -d' ' -f1)"
} > "$OUT/BUILD_INFO.txt"
cat "$OUT/BUILD_INFO.txt"
ls -l "$OUT"
