# 锁定的版本（装好后都不再升级）

## 固件
| 部件 | 版本 |
| --- | --- |
| 小智固件上游 | 78/xiaozhi-esp32 @ `0d576d3d4c049c6f55eaf879725dc23e516511b4`（项目版本 2.5.1） |
| 编译环境 | ESP-IDF v6.1（Docker 镜像 `espressif/idf:v6.1`） |
| 依赖组件（LVGL 9.5 等） | 第一次编译后锁在 `firmware/dependencies.lock`，以后编译都用它 |
| 板子 | `keshi/keshi-desk-1.54`（由 `waveshare/esp32-s3-touch-lcd-1.54` 改来） |
| 语言 / 唤醒词 | 中文（zh-CN）/ 关闭 |
| 烧录工具 | esptool 5.4.0 |

## 服务端（科室电脑 FSK）
| 部件 | 版本 |
| --- | --- |
| Python | 3.12.10（64 位） |
| Python 包 | 见 `server/requirements.txt`（aiohttp 3.14.4、numpy 2.5.3、PyYAML 6.0.3、sherpa-onnx 1.13.8、opuslib-next 1.3.1 等，全部写死） |
| opus.dll | libopus 1.3.1，取自 discord.py 2.7.1 安装包，SHA-256 `c84da836…8131` |
| 断句模型 | silero_vad.onnx |
| 识别模型 | sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17 |
| 朗读模型 | matcha-icefall-zh-baker + vocos-22khz-univ.onnx |

模型下载地址和校验值都写在 `server/tools/download_models.py` 里。

## OpenClaw
| 部件 | 版本 |
| --- | --- |
| Node.js | 24.19.0 |
| OpenClaw | 2026.9.8 |
| DeepSeek 接入插件 | @openclaw/deepseek-provider 2026.9.8 |
| 模型 | deepseek/deepseek-flash，深度思考关闭 |
| 小智插件 | xiaozhi-voice 1.0.0（本仓库 `openclaw-plugin/`） |
