# Muse 版小智（muse-xiaozhi）

说"小明同学"或按一下按键 → 家里 Ubuntu 上本机识别成文字 → 交给 Muse（进手机 App 的一个副聊天）→
Muse 回答后把"念的版本"写回 Ubuntu → 本机朗读 → 板子念出来。
屏幕上是 Muse 的小怪兽（竖放、充电口朝右时是正的），按状态换模样：待机、聆听、思考、说话、出错；后台等 Muse 时是思考，回答念完是开心脸。

**部署和出问题先看：[`部署.md`](部署.md)**

| 目录 | 是什么 |
| --- | --- |
| `server/` | 小智服务端（Python）：设备连接、语音识别、朗读、桥接 Muse、慢的先回"收到"再主动念 |
| `firmware/` | 固件改动：基于 78/xiaozhi-esp32 固定版本的补丁 + 板子目录（含 Muse 小怪兽渲染代码，Apache 2.0） |
| `deploy/linux/` | Ubuntu 开机自启（systemd） |
| `release/` | 编好的固件（整包，从 0x0 地址烧；GitHub Actions 编完自动放进来） |

版本全部锁定，见 `VERSIONS.md`；做过的选择和理由见 `DECISIONS.md`。
前身是"科室小智"（接 OpenClaw，2026-10-11 删除），旧内容在 Git 历史提交 `0e5573b` 之前。
