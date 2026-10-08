# 科室小智部署说明（给 FSK 上的 AI 照着做）

## 0. 先读

**目标**：在科室这台 Windows 电脑（FSK，海信大屏里的 OPS 电脑，无线网）上装好两样东西，让桌上的小智设备能对话：

1. **小智服务端**（`server\`，Python）：设备连它；语音识别、朗读都在本机跑；把问题转给 OpenClaw。
2. **OpenClaw**（大脑）：收到文字问题，调用国产大模型 API（DeepSeek）回答。

**规则**
- 按顺序做。每一步先贴命令的原始输出，再用大白话总结。
- 同一个步骤失败两次，就停下，不要换别的办法继续试。停下后告诉我：卡在哪一步、原始报错是什么、你认为原因是什么。等我回复再继续。
- 版本全部写死（见 `VERSIONS.md`），**不要升级、不要装 latest、不要换成别的软件**。
- 标着【人】的步骤需要人来做，到了就停下叫人。
- 不要把 API Key、口令（token）贴进回复里，说"已填好"即可。

**约定**
- 项目文件夹放在 `C:\keshi-xiaozhi`（下面所有路径都按这个写）。
- 命令都在 PowerShell 里执行。标"管理员"的要用"以管理员身份运行"的 PowerShell。
- 本机局域网 IP 记作 `<IP>`（第 1 步查到）。

---

## 1. 查本机 IP，确认网络

```powershell
ipconfig
```
找到**无线网卡**（WLAN）的 "IPv4 地址"，例如 `192.168.31.25`，这就是 `<IP>`。记下来。

说明：
- 设备只认这个 IP。电脑从不关机，IP 一般不会变；如果哪天变了，设备会连不上（要重新配网，见第 6 步）。最好请医院网管给这台电脑做"DHCP 地址保留"。
- 设备只能连 **2.4G** 无线网，并且要和这台电脑在同一个局域网里、能互相访问（第 5 步有测试方法）。

## 2. 放好项目文件

项目在 GitHub 私有仓库 `wangyuanjing1231-debug/xianliao-ba` 里的 `keshi-xiaozhi` 文件夹。任选一种：
- 有 git 且能登录 GitHub：克隆到 `C:\xianliao-ba`，再把 `C:\xianliao-ba\keshi-xiaozhi` 整个复制成 `C:\keshi-xiaozhi`。
- 【人】浏览器登录 GitHub → 打开仓库 → Code → Download ZIP → 解压，把里面的 `keshi-xiaozhi` 文件夹放到 `C:\keshi-xiaozhi`。

检查：`C:\keshi-xiaozhi\server\main.py` 存在。

## 3. 装小智服务端

### 3.1 Python 3.12（64 位）

装 **Python 3.12.10 64 位**（python.org 官方安装包 `python-3.12.10-amd64.exe`；下载慢可以找国内镜像）。安装时勾选 "Add python.exe to PATH" 和 "py launcher"。

```powershell
py -3.12 --version
```
应输出 `Python 3.12.10`。

### 3.2 建虚拟环境、装依赖

```powershell
cd C:\keshi-xiaozhi\server
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt -i https://pypi.tuna.tsinghua.edu.cn/simple
```
清华镜像不通就去掉 `-i ...` 用官方源。

### 3.3 opus.dll 和语音模型

```powershell
.venv\Scripts\python tools\get_opus_dll.py
.venv\Scripts\python tools\download_models.py
```
- 第一条把语音压缩库放到 `server\bin\opus.dll`，最后应打印 `加载成功： libopus 1.3.1...`。
- 第二条下载约 300MB 模型到 `server\models\`，每个文件都会核对校验值。GitHub 慢时脚本会自动换加速地址。都不通时：【人】在别的电脑上下载 `tools\download_models.py` 里列的 4 个文件，拷到 `server\models\` 再运行一次（它会解压和核对）。

### 3.4 配置文件

```powershell
copy config.example.yaml config.yaml
.venv\Scripts\python -c "import secrets; print(secrets.token_hex(16))"
```
用记事本打开 `config.yaml`，把 `server:` 下面的 `api_token` 改成刚才打印的那串随机字符（这是"让小智开口"接口的口令，第 4.5 步插件也要用）。`openclaw:` 下面的 `token` 第 5.1 步再填。其他不用改。

### 3.5 试运行

```powershell
.venv\Scripts\python main.py
```
等十几秒（加载模型），另开一个 PowerShell：
```powershell
Invoke-RestMethod http://127.0.0.1:8000/
```
应返回 `xiaozhi server ok`。确认后回到第一个窗口按 Ctrl+C 停掉（第 5 步会装成开机自启）。

## 4. 装 OpenClaw（大脑）

### 4.1 Node.js 24

装 **Node.js 24.19.0 64 位**（nodejs.org 的 `node-v24.19.0-x64.msi`；慢就用 npmmirror 镜像）。不要装 25 或 26。
```powershell
node --version
npm --version
```
应输出 `v24.19.0`。

### 4.2 OpenClaw 2026.9.8

```powershell
npm install -g openclaw@2026.9.8 --registry=https://registry.npmmirror.com
openclaw --version
```
- 如果 `npm --version` 是 11.16 或更高，在第一条命令末尾加 `--allow-scripts=openclaw`。
- 第二条应输出 `OpenClaw 2026.9.8`。

### 4.3 接 DeepSeek（国产大模型）

【人】提供 DeepSeek API Key（在 platform.deepseek.com 注册、充值、创建 Key）。

```powershell
openclaw plugins install @openclaw/deepseek-provider@2026.9.8 --pin --accept-capabilities
openclaw onboard --non-interactive --mode local --auth-choice deepseek-api-key --deepseek-api-key "<Key>" --skip-health --accept-risk
openclaw models set deepseek/deepseek-flash
```
非交互方式失败两次的话，改用 `openclaw onboard --auth-choice deepseek-api-key`，让人在屏幕上照提示填。

为什么用 `deepseek-flash` 并关掉"深度思考"：语音要快。Kimi K3 在 OpenClaw 里每次都强制最长思考，问一句要等很久，所以语音这条线不用它。

### 4.4 必要设置

```powershell
openclaw config set gateway.http.endpoints.chatCompletions.enabled true --strict-json
openclaw config set agents.defaults.thinkingDefault off
openclaw config set update.checkOnStart false --strict-json
openclaw config set commands.restart false --strict-json
openclaw config set tools.exec.mode deny
openclaw config get gateway.auth.mode
openclaw config get gateway.bind
```
- 第 1 条：打开 OpenClaw 的问答接口，小智服务端靠它提问。
- 第 2 条：关掉深度思考，回答快。
- 第 3、4 条：不自动检查更新，聊天里也不能触发重启/升级（版本要锁死）。
- 第 5 条：不让它在这台电脑上执行命令。语音谁都能对着说，先收紧；以后要放开再说。
- `gateway.auth.mode` 应该是 `token`；`gateway.bind` 应该是 `loopback`（只允许本机访问）。不是的话停下来问人。

再用记事本打开 `%USERPROFILE%\.openclaw\openclaw.json`，在 `tools` 里加一项 `"deny": ["gateway"]`（不让大模型调用升级/改配置的工具），保存后运行：
```powershell
openclaw config validate
```

### 4.5 装小智插件

```powershell
openclaw plugins install --link --force --accept-capabilities C:\keshi-xiaozhi\openclaw-plugin\xiaozhi-voice
openclaw config set plugins.entries.xiaozhi-voice.config.serverUrl http://127.0.0.1:8000
openclaw config set plugins.entries.xiaozhi-voice.config.apiToken <config.yaml 里的 api_token>
```

### 4.6 开机自启、试一下

```powershell
openclaw gateway install
openclaw gateway status
openclaw config get gateway.auth.token
```
最后一条输出的是网关口令（下面记作 `<网关口令>`），第 5.1 步要填进 `config.yaml`。

试问一句：
```powershell
$t = "<网关口令>"
$body = '{"model":"openclaw/default","messages":[{"role":"user","content":"用一句话介绍你自己"}]}'
Invoke-RestMethod -Uri http://127.0.0.1:18789/v1/chat/completions -Method Post -Headers @{Authorization = "Bearer $t"} -ContentType "application/json; charset=utf-8" -Body ([Text.Encoding]::UTF8.GetBytes($body))
```
返回内容里 `choices[0].message.content` 有一句中文回答就对了。

## 5. 连起来，装成开机自启

### 5.1 填网关口令

`config.yaml` 里 `openclaw:` 下面的 `token` 改成 `<网关口令>`，保存。

### 5.2 开机自启（管理员）

```powershell
powershell -ExecutionPolicy Bypass -File C:\keshi-xiaozhi\deploy\windows\install-autostart.ps1
```
它会：防火墙放行 8000 端口（只允许同一局域网访问）；建计划任务 `XiaozhiServer`，开机就启动服务端，挂了自动重启。

### 5.3 测试

1. 本机：`Invoke-RestMethod http://127.0.0.1:8000/` → `xiaozhi server ok`。
2. 【人】手机连科室同一个 Wi-Fi，浏览器打开 `http://<IP>:8000/`，能看到 `xiaozhi server ok`。
   **打不开 = 设备也连不上**。先查防火墙规则 `Xiaozhi Server 8000` 在不在；还不行，多半是 Wi-Fi 开了"客户端隔离"，要找网管。
3. 模拟设备问一句（会用本机朗读把问题念成语音发过去，再把收到的回答识别成文字）：
   ```powershell
   cd C:\keshi-xiaozhi\server
   .venv\Scripts\python tools\fake_device.py ws://127.0.0.1:8000 ask "今天星期几"
   ```
   最后一行 JSON 里 `heard` 有回答的文字就对了。
4. 主动开口：开两个窗口。第一个运行
   `.venv\Scripts\python tools\fake_device.py ws://127.0.0.1:8000 idle 40`，
   40 秒内在第二个运行 `.venv\Scripts\python tools\say.py "测试一下主动开口"`。
   第一个窗口最后的 JSON 里 `notifies` 有这句话就对了。

## 6. 设备：烧固件、配网【人】

### 6.1 烧固件

固件在仓库 `keshi-xiaozhi\release\` 里（`.bin` 文件，从 0x0 地址整包烧）。

```powershell
cd C:\keshi-xiaozhi\server
.venv\Scripts\python -m pip install esptool==5.4.0 -i https://pypi.tuna.tsinghua.edu.cn/simple
[System.IO.Ports.SerialPort]::GetPortNames()
```
【人】用 USB 线把小智插到这台电脑。再运行一次最后一条，多出来的那个 COM 口就是设备（下面记作 `COM5`）。

```powershell
.venv\Scripts\python -m esptool --chip esp32s3 -p COM5 erase-flash
.venv\Scripts\python -m esptool --chip esp32s3 -p COM5 write-flash 0x0 C:\keshi-xiaozhi\release\<固件文件名>.bin
```
- **必须先整片擦除**（第一条）：板子出厂固件里可能存着官方云服务的地址，擦掉才保证只连科室电脑。
- 连不上设备时：【人】按住板子上的 BOOT 键再插 USB，松开，重试。
- 烧完【人】拔掉 USB 再插上（或接充电头）。

### 6.2 配网（手机上做）

1. 设备开机后屏幕会提示连接热点（名字类似 `Xiaozhi-XXXX`）。手机连上这个热点，浏览器打开 `http://192.168.4.1`。
2. 选科室的 **2.4G** Wi-Fi，填密码。
3. 找到 **OTA 地址**一栏（可能在"高级"里），填 `http://<IP>:8000/xiaozhi/ota/`（`<IP>` 换成第 1 步的 IP，末尾斜杠别漏）。
4. 保存。设备重启后连上科室电脑，屏幕上出现像素小人（待机动作）。

以后要换 Wi-Fi 或电脑 IP 变了：**连按三下主按键**，设备回到配网模式，重新做一遍 6.2。

### 6.3 只认这台设备（建议）

```powershell
Invoke-RestMethod http://127.0.0.1:8000/api/status -Headers @{Authorization = "Bearer <api_token>"}
```
记下 `device_id`（设备的 MAC 地址），填进 `config.yaml` 的 `allowed_devices`，例如 `allowed_devices: ["aa:bb:cc:dd:ee:ff"]`，然后重启服务端：
```powershell
Stop-ScheduledTask XiaozhiServer; Get-Process python -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "C:\keshi-xiaozhi\*" } | Stop-Process -Force; Start-ScheduledTask XiaozhiServer
```

## 7. 日常用法

| 操作 | 作用 |
| --- | --- |
| 主按键按一下 | 开始说话 |
| 说话时再按一下 | 说完了（不按也行，停顿约 0.8 秒自动结束） |
| 它在说话时按一下 | 打断 |
| 双击主按键 | 开关屏幕背光 |
| 连按三下主按键 | 重新配网 |
| 音量 +/- 键 | 调音量（长按：最大 / 静音） |

屏幕上的小人：待机（偶尔眨眼）、聆听（惊讶脸 + 声波）、思考（冒点点）、说话（嘴动）、干活中（抱着文件走）、干完了（绿勾，有人再按键就收起）、出错（灰云，10 秒后恢复）。

长任务：OpenClaw 6 秒内没开口，小智先说"收到，这个要花点时间……"，小人切到"干活中"；办完后小智主动开口念结果。

让小智念一句话（不经过大模型）：`.venv\Scripts\python tools\say.py "下午三点开科会"`。

## 8. 出问题先看

| 现象 | 先查 |
| --- | --- |
| 设备屏幕提示连不上服务器 | 本机 `http://127.0.0.1:8000/` 通不通；手机 `http://<IP>:8000/` 通不通；电脑 IP 变没变 |
| 按键说话后说"大脑那边没连上" | `openclaw gateway status`；`config.yaml` 的 `openclaw.token` 对不对 |
| 回答总是很慢 | `openclaw config get agents.defaults.thinkingDefault` 是不是 `off`；模型是不是 `deepseek/deepseek-flash` |
| 识别不准 | 离设备近一点说；看 `server\logs\server.log` 里识别出的原话 |
| 服务端没起来 | `server\logs\last-stderr.txt` 和 `server\logs\server.log` |

日志：`C:\keshi-xiaozhi\server\logs\server.log`（自动轮换，最多 6 个 5MB 文件）。
