# 服务端改动说明 (相对 upstream legacy 原版)

本目录记录当前运行在 YJW 电脑上的 `esp32-iphone-agent`（`legacy` 分支）服务端的改动与补丁。

## 一、改动清单汇总

| 改动项 | 涉及文件 / 系统路径 | 改动说明 | 解决的问题 |
|---|---|---|---|
| **1. 右键 API** | `server/hidagent/app.py` | 增加 `@app.post("/api/rclick")` 路由，调用 `await device.send("rclick")` | 允许外部通过 HTTP 发送右键指令，用于配合 iPhone 辅助触控右键回桌面 |
| **2. UxPlay 启动参数** | `server/hidagent/screen.py` | 增加 `stdbuf -oL -eL`，增加 `-avdec -d` 参数 | 解决 Python 管道日志缓冲延迟，强制使用 libavcodec 软解避开硬件解码崩溃 |
| **3. UxPlay 版本升级** | `/usr/bin/uxplay` (系统级) | 源码编译安装官方最新正式版 `v1.73.7`（备份原版 `1.68` 至 `/usr/bin/uxplay.orig-1.68`） | 修复旧版 UxPlay 在 iOS 26 上 AirPlay 握手兼容性与崩溃问题 |
| **4. 局域网监听配置** | `server/.env` | 末尾增加 `HOST=0.0.0.0`（原配置仅本机 127.0.0.1 访问） | 允许局域网内其他设备（如 iPad、手机、同一局域网电脑）直接打开网页控制面板 |

---

## 二、关键改动细节与代码对比

### 1. `server/hidagent/app.py`
在文件末尾增加右键端点：
```python
@app.post("/api/rclick")
async def rclick():
    """Right mouse button; on iPhone assign it to Home in AssistiveTouch device settings."""
    async def send():
        await device.send("rclick")
    return await act(send)
```

### 2. `server/hidagent/screen.py`
修改 UxPlay 镜像子进程的拉起方式：
```python
# 原版：
self._proc = subprocess.Popen([uxplay, "-n", self.name, "-nh", "-a", "-reset", "0", "-vs", sink],
                              **popen)

# 修改后：
self._proc = subprocess.Popen(["stdbuf", "-oL", "-eL", uxplay, "-avdec", "-d", "-n", self.name, "-nh", "-a", "-reset", "0", "-vs", sink],
                              **popen)
```
- `stdbuf -oL -eL`：使标准输出与标准错误变为行缓冲，让 UxPlay 启动、客户端连接与断开的实时日志能立即刷进 journalctl。
- `-avdec`：采用 libavcodec 软件解码器（`avdec_h264`），彻底避开 AMD VA-API 在 headless/systemd 服务无显示会话环境下的初始化失败（`-13` 权限）与硬解 sink 协商冲突。
- `-d`：开启详细调试日志，方便排查投屏连接问题。

### 3. UxPlay 二进制升级 (1.73.7)
- 系统 apt 源自带的 UxPlay 版本为 `1.68`，面对新版 iOS 投屏协议存在较多握手问题。
- 从官方源码构建升级到 `v1.73.7`：
  ```bash
  # 升级验证：
  uxplay -v
  # 输出: UxPlay version 1.73.7; for help, use option "-h"
  ```
- 备份位置：`/usr/bin/uxplay.orig-1.68`。

### 4. `server/.env` 监听地址
- 在配置文件末尾加入：
  ```ini
  HOST=0.0.0.0
  ```
- 使得 FastAPI / Uvicorn 监听所有网络接口，可以在局域网中通过 `http://192.168.6.170:8890/` 访问。

---

## 三、补丁文件
本目录下的 `server_legacy_changes.patch` 包含上述代码改动的完整 diff，可用如下命令直接打入新检出的 legacy 分支：
```bash
git apply server_legacy_changes.patch
```
