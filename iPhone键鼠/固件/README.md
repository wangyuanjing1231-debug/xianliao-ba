# 固件说明与改动对照 (微雪 ESP32-S3-LCD-1.54)

本目录包含当前刷入微雪板子的完整固件源码 `hid_bridge.ino`。

## 一、相对 upstream legacy 原版固件的两大改动

### 1. 键盘与鼠标初始化顺序颠倒（键盘在前，鼠标在后）
- **改动位置**：全局变量声明与 `setup()` 中的 `begin()` 调用。
- **原因**：ESP32-S3 原生 USB 复合设备在 iOS 上枚举时，原版鼠标声明在前会导致 iOS 优先将板子仅识别为指向设备或忽略键盘接口，导致打字功能偶发失效；调换顺序后 iOS 能稳定识别为物理键盘 + 鼠标。
- **代码对比**：
  ```cpp
  // 原版：
  USBHIDMouse mouse;
  USBHIDKeyboard keyboard;
  ...
  mouse.begin();
  keyboard.begin();

  // 当前修改后：
  USBHIDKeyboard keyboard;
  USBHIDMouse mouse;
  ...
  keyboard.begin();
  mouse.begin();
  ```

### 2. 新增 `rclick`（右键）与 `mclick`（中键）指令
- **改动位置**：`run()` 命令解析函数。
- **原因**：用于支持在 iOS 辅助触控中把“设备附加按钮（右键）”绑定为“主屏幕”，通过向板子发送 `rclick` 命令即可稳定触发物理右键单击，瞬间回到 iOS 桌面。
- **代码对比**：
  ```cpp
  // 当前新增代码：
  if (cmd == "rclick") { mouse.press(MOUSE_RIGHT); delay(60); mouse.release(MOUSE_RIGHT); return "ok"; }
  if (cmd == "mclick") { mouse.press(MOUSE_MIDDLE); delay(60); mouse.release(MOUSE_MIDDLE); return "ok"; }
  ```

---

## 二、编译与烧录方法

如果以后需要重新刷入本固件：
1. 配置 `secrets.h`（可参考 `secrets.example.h`），填写 Wi-Fi SSID、密码以及电脑 YJW 服务端 IP（`192.168.6.170:7300`）。
2. 将板子通过 USB-C 插入电脑。
3. 执行烧录脚本：
   ```bash
   bash flash.sh
   ```
   或者直接使用 `arduino-cli`：
   ```bash
   arduino-cli compile -b esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=8M hid_bridge.ino
   arduino-cli upload -b esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,FlashSize=8M -p /dev/ttyACM0 hid_bridge.ino
   ```
