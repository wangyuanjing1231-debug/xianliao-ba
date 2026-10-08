# xiaozhi-voice（OpenClaw 插件）

让 OpenClaw 能主动叫科室小智念一句话。只调用小智服务端的 `/api/push` 接口，不引用 OpenClaw 内部模块。
在 OpenClaw 2026.9.8 上测试过（工具调用 → 服务端 → 设备播放）。

提供两样东西：
- 工具 `xiaozhi_say`：智能体想"通知大家 / 念给科室听"时调用。
- 命令 `/xiaozhi 文字`：不经过大模型，原样念出来。只在聊天渠道里生效（以后接了聊天软件才用得上）。

## 安装（Windows，PowerShell）

```powershell
openclaw plugins install --link --force --accept-capabilities C:\keshi-xiaozhi\openclaw-plugin\xiaozhi-voice
openclaw config set plugins.entries.xiaozhi-voice.config.serverUrl http://127.0.0.1:8000
openclaw config set plugins.entries.xiaozhi-voice.config.apiToken <server\config.yaml 里的 api_token>
openclaw gateway restart
```

`--link` 表示直接用这个文件夹里的代码，不复制；以后改这里的文件，重启网关就生效。

## 配置项

| 键 | 默认 | 说明 |
| --- | --- | --- |
| `serverUrl` | `http://127.0.0.1:8000` | 小智服务端地址（同一台电脑就用这个） |
| `apiToken` | 无（必填） | 和服务端 `config.yaml` 的 `server.api_token` 一致 |
| `defaultDevice` | `all` | 念给哪台设备：`all` 或设备 MAC |
| `maxChars` | `300` | 一次最多念多少字 |
