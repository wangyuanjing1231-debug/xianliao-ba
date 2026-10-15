# 把刷好的 Pocket-Dongle-S3 接进 Ubuntu 24.04，给 Muse 当蓝牙接收器

前提：板子已经按《说明-Windows刷机.md》刷好了蓝牙接收器固件。

工作原理一句话：板子里的蓝牙芯片只当"无线电"（叫蓝牙控制器），配对、加密这些"大脑"工作仍由 Ubuntu 自带的蓝牙服务 BlueZ 来做。两者之间通过 USB 串口传话，传话格式叫 H4（蓝牙标准规定的串口传话格式）。电脑上要靠一个叫 `btattach` 的小程序，把这个串口"接"给 BlueZ。

下面的命令都在"终端"里输入（Ubuntu 里按 Ctrl+Alt+T 打开）。

---

## 第 1 步：拔掉绿联接收器，确认系统里没有别的蓝牙接收器

拔掉绿联接收器，然后运行：

```
ls /sys/class/bluetooth/
```
（大白话：列出系统现在认得的蓝牙接收器。**应该什么都不显示。** 如果还显示 `hci0`，说明电脑里还有别的蓝牙接收器（比如主板自带的）。这时先停下，新板子会被排成 hci1，后面的步骤就对不上了。）

## 第 2 步：插上板子，找到它的串口

插上板子（**不要按 BOOT 键**），然后：

```
ls -l /dev/ttyACM*
```
（大白话：看看电脑上多出来的串口叫什么，正常是 `/dev/ttyACM0`。下文都按 `/dev/ttyACM0` 写，如果你看到的是 ttyACM1 之类，就把命令里的名字换掉。）

## 第 3 步：暂停会乱碰串口的服务

```
sudo systemctl stop ModemManager
```
（大白话：Ubuntu 有个"上网卡管理"服务，看见新串口会往里发试探命令，会把蓝牙传话搅乱。这里只是暂停，重启电脑后会自动恢复。）

## 第 4 步：清掉开机杂讯，把板子接给蓝牙服务

```
sudo stty -F /dev/ttyACM0 raw -echo
sudo timeout 1 cat /dev/ttyACM0 > /dev/null
sudo btattach -B /dev/ttyACM0 -P h4 -N
```
（大白话：
- 第一行：把串口设成"原样传输、不回显"。不回显，是指收到的字节不原样发回给板子。
- 第二行：读 1 秒并扔掉。板子开机时，芯片自带的启动程序会往 USB 口吐几行字，这些不是蓝牙数据，要先清掉。
- 第三行：把这个串口接给 BlueZ。`-P h4` 指定传话格式，`-N` 表示不用硬件流控（一种防止数据发太快的信号线，USB 串口用不上）。

**第三行运行后会一直停在那里不结束，这是正常的。这个终端窗口要一直开着。** 关掉它或按 Ctrl+C，板子就从系统里断开了。）

## 第 5 步：确认新板子是 hci0

另开一个终端窗口：

```
ls /sys/class/bluetooth/
sudo btmgmt info
```
（大白话：
- 第一行应该只显示 `hci0`，它就是新板子。
- 第二行显示 hci0 的详细信息：`addr` 后面是板子的蓝牙地址，`current settings` 后面列着它当前的状态。

Muse 用的是 `/org/bluez/hci0`，也就是系统里的 hci0。现在 hci0 是新板子，所以 Muse 用的就是它。）

如果 `current settings` 里没有 `powered`（已通电），运行：

```
bluetoothctl power on
```

## 第 6 步：重启 Muse 服务，打开配对

```
sudo systemctl restart musegadget
systemctl status musegadget --no-pager
```
（大白话：
- 第一行：重启 Muse 服务，让它重新去找 hci0。它原来连的是绿联接收器，绿联已经拔掉了。
- 第二行：看服务状态，应该显示 `active (running)`，意思是正在运行。

如果第一行报 `Unit musegadget.service not found`，说明它可能是装在你个人账号下的服务。改用 `systemctl --user restart musegadget`，第二行也同样加上 `--user`。我没见过这个服务的安装方式，这一点是推测。）

然后按 Muse 平常的方式打开配对，用手机去连。

**想亲眼确认配对走的是新板子**：配对前再开一个终端，运行：

```
sudo btmon -i hci0
```
（大白话：btmon 是蓝牙"监听器"，会实时打印 hci0 上来往的每一条消息。手机连上时，这里会刷出 `LE Connection Complete`（低功耗蓝牙连接建立）、`Encryption Change`（开始加密）之类的行。看完按 Ctrl+C 退出。）

配对成功后，可以查当前连着谁：

```
sudo btmgmt con
```
（大白话：列出 hci0 上当前连着的设备，应该能看到手机的地址。）

## 第 7 步：用完恢复原状

1. 在运行 `btattach` 的终端按 Ctrl+C，然后拔下板子。
2. 运行 `sudo systemctl start ModemManager`，或者直接重启电脑。
3. 插回绿联接收器，再运行 `sudo systemctl restart musegadget`。

注意：配对记录存在电脑的 BlueZ 里，**是跟接收器的地址绑定的**。换回绿联后，这次用板子做的配对记录不会自动转过去。

---

## 常见问题

- **第 4 步 btattach 报错，或者第 5 步看不到 hci0**：拔插板子，从第 2 步重新做。开机杂讯没清干净时会这样。
- **板子每次拔插后都要重做第 2～4 步**：btattach 只在它运行的这段时间里有效，不会被系统记住。
- **这块板子只支持低功耗蓝牙**：ESP32-S3 的蓝牙硬件本身不支持经典蓝牙（耳机、音箱用的那种）。
