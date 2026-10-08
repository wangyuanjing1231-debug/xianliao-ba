# 在 Windows 上给 Pocket-Dongle-S3 刷蓝牙接收器固件

## 刷之前你自己要做的两件事

1. 从 GitHub 仓库下载这两个文件，放进 Windows 的"下载"文件夹：
   - `ble-hci-dongle/release/ble-hci-dongle_esp32s3_flash-at-0x0.bin`（新固件）
   - `release/esp32-bridge-flash-at-0x0.bin`（原来的固件，以后刷回用）
2. **按住板子上的 BOOT 键不放，把板子插进电脑，插好后再松手。** 这样板子会进入"烧录模式"（只接收新固件、不运行任何程序的状态）。

然后把下面的提示词整段复制给 Windows 上的 AI。

---

## 提示词一：刷入蓝牙接收器固件

```
请在这台 Windows 电脑的 PowerShell 里依次完成下面的事，每一步都把原始输出贴给我。同一步失败两次就停下，告诉我卡在哪一步、原始报错是什么、你认为原因是什么，等我回复再继续。

1. 运行 py --version。如果提示找不到 py，运行 winget install -e --id Python.Python.3.12，装完后关闭并重新打开 PowerShell，再运行 py --version 确认。
2. 运行 py -m pip install --upgrade esptool，然后运行 py -m esptool version 确认装好了。
3. 运行 Get-PnpDevice -Class Ports -PresentOnly | Format-Table FriendlyName, InstanceId -AutoSize。找出 InstanceId 里含有 VID_303A 的那一行，它的 FriendlyName 括号里的 COM 编号就是板子（例如 COM5）。如果没有含 VID_303A 的行，停下告诉我。下面把这个编号记作 <端口>。
4. 运行 py -m esptool --chip esp32s3 -p <端口> flash-id，确认输出里有 Detected flash size: 16MB。
5. 运行 py -m esptool --chip esp32s3 -p <端口> read-flash 0 0x1000000 "$env:USERPROFILE\Downloads\pocket-dongle-backup.bin"，然后运行 Get-Item "$env:USERPROFILE\Downloads\pocket-dongle-backup.bin" | Select-Object Length，确认大小是 16777216。
6. 运行 py -m esptool --chip esp32s3 -p <端口> write-flash 0x0 "$env:USERPROFILE\Downloads\ble-hci-dongle_esp32s3_flash-at-0x0.bin"，确认输出最后有 Hash of data verified。
7. 告诉我已经完成，并提醒我把板子拔下来。
```

### 每一步在做什么

1. 看电脑上有没有 Python（一种编程语言，烧录工具要靠它运行），没有就用 Windows 自带的软件安装器 winget 装上。
2. 装乐鑫（ESP32 芯片的厂家）官方的烧录工具 esptool。
3. 列出电脑上所有串口，找出板子是哪一个。`VID_303A` 是乐鑫的厂商编号，含有它的那一行就是板子。Windows 10/11 自带这个串口的驱动，不用另外装。
4. 让工具跟板子"打个招呼"，读出存储芯片的大小。看到 16MB 说明连对了、板子也对。
5. **备份**：把板子 16MB 存储芯片从头到尾原样读出来，存成"下载"文件夹里的 `pocket-dongle-backup.bin`，要几分钟。16777216 字节正好是 16MB，大小对上了才说明备份完整。
6. 把新固件从地址 0x0（存储芯片的开头）写进去。`Hash of data verified` 的意思是写完后核对过，内容没错。
7. 刷完后**拔下板子**。这块板子下次插在哪台电脑上，就以蓝牙接收器的身份运行。

### 常见报错

- **第 3 步找不到 VID_303A**：板子没进烧录模式。拔掉，按住 BOOT 再插一次。
- **`Could not open port` / `拒绝访问`**：串口被别的程序占着（比如 Arduino 软件的串口监视器）。关掉它再试。
- **`Failed to connect to ESP32-S3`**：同样是没进烧录模式，按住 BOOT 重插。

---

## 提示词二：以后刷回原来的固件

刷回之前，同样**按住 BOOT 插上板子再松手**。然后把下面这段交给 AI：

```
请在这台 Windows 电脑的 PowerShell 里依次完成下面的事，每一步都把原始输出贴给我。同一步失败两次就停下，告诉我卡在哪一步、原始报错是什么、你认为原因是什么，等我回复再继续。

1. 运行 Get-PnpDevice -Class Ports -PresentOnly | Format-Table FriendlyName, InstanceId -AutoSize，找出 InstanceId 含 VID_303A 那一行的 COM 编号，记作 <端口>。没有就停下告诉我。
2. 检查 "$env:USERPROFILE\Downloads\pocket-dongle-backup.bin" 是否存在且大小是 16777216。如果是，运行 py -m esptool --chip esp32s3 -p <端口> write-flash 0x0 "$env:USERPROFILE\Downloads\pocket-dongle-backup.bin"。如果不是，改为运行 py -m esptool --chip esp32s3 -p <端口> write-flash 0x0 "$env:USERPROFILE\Downloads\esp32-bridge-flash-at-0x0.bin"，并告诉我用的是仓库里的原固件而不是备份。
3. 确认输出最后有 Hash of data verified，告诉我已经完成，并提醒我把板子拔下来再插上。
```

### 每一步在做什么

1. 找出板子是哪个串口，跟刷入时一样。
2. 优先用刷之前做的整片备份写回去，写回后和刷之前一模一样。备份不在或不完整时，就用仓库里的原固件。用原固件的话，板子上存的 Wi-Fi 设置等会回到默认。
3. 拔下再插上后，板子回到原来的样子：电脑会重新把它认成 USB 键盘加串口。
