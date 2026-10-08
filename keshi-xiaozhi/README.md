# 科室桌面语音小设备（keshi-xiaozhi）

按一下按键说话 → 科室电脑上本机识别 → 交给 OpenClaw → 本机朗读 → 小设备念出来。
屏幕上是一个像素小人（爸爸），按状态换动作：待机、聆听、思考、说话、干活中、干完了。

| 目录 | 是什么 |
| --- | --- |
| `server/` | 小智服务端（Python）：设备连接、语音识别、朗读、接 OpenClaw、长任务先回"收到"再主动通知 |
| `openclaw-plugin/xiaozhi-voice/` | OpenClaw 插件：让 OpenClaw 能主动叫小智念一句话 |
| `firmware/` | 固件改动：基于 78/xiaozhi-esp32 固定版本的补丁 + 新板子目录 + 像素小人素材 |
| `deploy/` | 给科室那台 Windows 电脑上的 AI 照着装的部署说明 |
| `release/` | 编好的固件（整包，从 0x0 地址烧） |

版本全部锁定，见 `VERSIONS.md`；做过的选择和理由见 `DECISIONS.md`。
