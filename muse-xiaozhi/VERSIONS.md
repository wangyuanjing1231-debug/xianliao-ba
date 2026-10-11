# 锁定的版本（装好后都不再升级）

## 固件
| 部件 | 版本 |
| --- | --- |
| 小智固件上游 | 78/xiaozhi-esp32 @ `0d576d3d4c049c6f55eaf879725dc23e516511b4`（项目版本 2.5.1） |
| 编译环境 | ESP-IDF v6.1（Docker 镜像 `espressif/idf:v6.1`） |
| 依赖组件（LVGL 9.5 等） | 第一次编译后锁在 `firmware/dependencies.lock`，以后编译都用它 |
| 板子 | `keshi/keshi-desk-1.54`（由 `waveshare/esp32-s3-touch-lcd-1.54` 改来） |
| 语言 / 唤醒词 | 中文（zh-CN）/ 小明同学 `wn9_xiaomingtongxue_tts2`（esp-sr 2.4.7） |
| 烧录工具 | esptool 5.4.0 |

## 服务端（家里 Ubuntu 24.04）
| 部件 | 版本 |
| --- | --- |
| Python | Ubuntu 24.04 自带 3.12 |
| Python 包 | 见 `server/requirements.txt`（aiohttp 3.14.4、numpy 2.5.3、PyYAML 6.0.3、sherpa-onnx 1.13.8、opuslib-next 1.3.1 等，全部写死） |
| libopus | Ubuntu 软件源的 libopus0 |
| 断句模型 | silero_vad.onnx |
| 识别模型 | sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17 |
| 朗读模型 | matcha-icefall-zh-baker + vocos-22khz-univ.onnx |

模型下载地址和校验值都写在 `server/tools/download_models.py` 里。

## Muse
| 部件 | 说明 |
| --- | --- |
| musegadget | Ubuntu 上已装好的官方命令行（/usr/local/bin/musegadget），只调用不改 |
| 去程 | `musegadget send-user-msg --session-id <UUID> "消息"` |
| 回程 | Muse 调本设备的 `file.write` 写 `~/muse-xiaozhi/inbox/<编号>.txt` |
