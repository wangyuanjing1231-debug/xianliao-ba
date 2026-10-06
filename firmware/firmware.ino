/*
 * ESP32-S3 Pocket Dongle Bridge Firmware
 *
 * 功能:
 *   1. WiFi AP 热点 + 网页 (手机操作)
 *   2. USB 键盘 (模拟打字, 所有字符都用 Alt+小键盘 打, 中文走 GBK 编码)
 *   3. USB 串口 (和 Windows 托盘程序通信)
 *
 * 工作方式 (排队打字):
 *   网页发来的文字先放进"待打队列", 板子马上回话;
 *   loop() 每轮只打一个字, 打完回头处理网页请求, 所以打长文时网页不会失联.
 *
 * 必须的开发板设置:
 *   USB Mode = USB-OTG (TinyUSB)   (即 ARDUINO_USB_MODE=0, 否则板子当不了键盘)
 *   USB CDC On Boot = Enabled
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "html_page.h"

#if ARDUINO_USB_MODE
#error "USB Mode 必须选 USB-OTG (TinyUSB), 选 Hardware CDC 时板子无法模拟键盘"
#endif

// ===================== 配置 =====================
#define AP_SSID     "ESP32-Bridge"
#define AP_PASS     "12345678"
#define BUF_SIZE    4096
#define QUEUE_SIZE  32768     // 待打队列容量 (字节), 约 16000 个汉字

// ===================== 全局变量 =====================
WebServer      server(80);
USBHIDKeyboard keyboard;

int keyDelay = 20;            // 每次按下/松开之后停多久 (ms)

// 串口接收缓冲
char serBuf[BUF_SIZE];
int  serLen = 0;

// 来自 PC 的文本 (显示到网页上)
char pcText[BUF_SIZE];
bool pcTextNew = false;

// 待打队列 (环形: 写到末尾再从头接着写)
uint8_t queueBuf[QUEUE_SIZE];
int     qHead  = 0;           // 下一个要打的字节位置
int     qTail  = 0;           // 下一个写入位置
int     qBytes = 0;           // 队列里现有字节数
int     qChars = 0;           // 队列里现有字符数 (一个汉字算一个)

// 网页分段编号, 用来识别重复发送的同一段
long curJob  = -1;
long lastSeq = -1;

// ===================== 队列 =====================

void clearQueue() {
  qHead = qTail = qBytes = qChars = 0;
}

int hexCharToVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// 把 hex 文本解码后放进队列
// 返回: 放进去的字符数; -1 = hex 格式错误; -2 = 队列空间不够 (整段都不放)
int enqueueHex(const char* hex, int hexLen) {
  if (hexLen <= 0 || hexLen % 2 != 0) return -1;
  for (int i = 0; i < hexLen; i++) {
    if (hexCharToVal(hex[i]) < 0) return -1;
  }
  int nBytes = hexLen / 2;
  if (nBytes > QUEUE_SIZE - qBytes) return -2;

  int nChars = 0;
  bool secondHalf = false;    // 正在写汉字的第二个字节
  for (int i = 0; i < hexLen; i += 2) {
    uint8_t b = (hexCharToVal(hex[i]) << 4) | hexCharToVal(hex[i + 1]);
    queueBuf[qTail] = b;
    qTail = (qTail + 1) % QUEUE_SIZE;
    if (secondHalf) {
      secondHalf = false;
    } else {
      nChars++;
      if (b >= 0x81) secondHalf = true;
    }
  }
  qBytes += nBytes;
  qChars += nChars;
  return nChars;
}

uint8_t popByte() {
  uint8_t b = queueBuf[qHead];
  qHead = (qHead + 1) % QUEUE_SIZE;
  qBytes--;
  return b;
}

// ===================== 键盘 =====================

// 按一下某个键
void tapKey(uint8_t k) {
  keyboard.press(k);
  delay(keyDelay);
  keyboard.release(k);
  delay(keyDelay);
}

// 按住 Alt, 在小键盘上依次按 code 的每一位数字, 松开 Alt
// Windows 中文系统会把这个数字按 GBK 编码换成对应的字
void altNumpadCode(uint16_t code) {
  const uint8_t kp[10] = {
    KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4,
    KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9
  };
  char numStr[6];
  snprintf(numStr, sizeof(numStr), "%u", code);

  keyboard.releaseAll();      // 保险: 先确保没有别的键按着
  delay(keyDelay);
  keyboard.press(KEY_LEFT_ALT);
  delay(keyDelay);
  for (int i = 0; numStr[i]; i++) {
    tapKey(kp[numStr[i] - '0']);
  }
  keyboard.release(KEY_LEFT_ALT);
  delay(keyDelay);
  keyboard.releaseAll();      // 保险: 万一上面"松开 Alt"的信号丢了, 这里再松一次
  delay(keyDelay);
}

// 从队列里取一个字打出来
void typeOneChar() {
  uint8_t b = popByte();
  if (b >= 0x81 && qBytes > 0) {
    // 汉字: 两个字节拼成一个数, 例如 "你" = 0xC4E3 = 50403
    uint16_t code = (uint16_t)b * 256 + popByte();
    altNumpadCode(code);
  } else if (b == '\n') {
    tapKey(KEY_RETURN);
  } else if (b == '\t') {
    tapKey(KEY_TAB);
  } else if (b == ' ') {
    tapKey(' ');
  } else if (b >= 0x21 && b <= 0x7E) {
    // 英文字母/数字/符号也用 Alt 码打, 这样不受电脑中文输入法状态影响
    altNumpadCode(b);
  }
  // 其它 (\r 等控制字符) 直接跳过
  qChars--;
}

// ===================== 网页处理 =====================

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// 回复格式: "<状态> <队列剩余字符数>"
void replyStatus(int code, const char* word) {
  server.send(code, "text/plain", String(word) + " " + String(qChars));
}

void handleSend() {
  String mode = server.arg("m");

  if (mode == "K") {
    // ---- 键盘模式: 只排队, 不在这里打字 ----
    String speed = server.arg("s");
    if (speed.length() > 0) {
      keyDelay = constrain(speed.toInt(), 10, 100);
    }

    long job = server.arg("j").toInt();
    long seq = server.arg("n").toInt();
    if (job != curJob) {      // 新的一次发送
      curJob  = job;
      lastSeq = -1;
    }
    if (seq <= lastSeq) {     // 这段已经收过 (手机重发), 不重复打
      replyStatus(200, "DUP");
      return;
    }
    if (seq != lastSeq + 1) { // 跳号了, 告诉网页
      replyStatus(409, "SEQ");
      return;
    }

    const String& hex = server.arg("h");
    int n = enqueueHex(hex.c_str(), hex.length());
    if (n == -2) {
      replyStatus(503, "FULL");
    } else if (n < 0) {
      replyStatus(400, "BADHEX");
    } else {
      lastSeq = seq;
      replyStatus(200, "OK");
    }
  }
  else if (mode == "C") {
    // ---- 串口模式: 转发给 PC 的 exe ----
    String text = server.arg("t");
    if (text.length() > 0) {
      Serial.print("T:");
      Serial.println(text);
      server.send(200, "text/plain", "OK: sent to PC");
    } else {
      server.send(400, "text/plain", "ERR: no text");
    }
  }
  else {
    server.send(400, "text/plain", "ERR: unknown mode");
  }
}

// 查询还剩多少字没打
void handleStatus() {
  replyStatus(200, "OK");
}

// 急停: 清空队列, 松开所有键
void handleStop() {
  clearQueue();
  curJob  = -1;
  lastSeq = -1;
  keyboard.releaseAll();
  replyStatus(200, "STOPPED");
}

// 轮询: PC 有没有发文字过来
void handlePoll() {
  if (pcTextNew) {
    pcTextNew = false;
    server.send(200, "text/plain; charset=utf-8", pcText);
    pcText[0] = '\0';
  } else {
    server.send(200, "text/plain", "");
  }
}

// ===================== 串口 (来自 PC 的 exe) =====================

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || serLen >= BUF_SIZE - 1) {
      serBuf[serLen] = '\0';
      if (serLen > 2 && serBuf[0] == 'T' && serBuf[1] == ':') {
        // T:text → PC 发来的文字, 放到网页上
        strncpy(pcText, serBuf + 2, BUF_SIZE - 1);
        pcText[BUF_SIZE - 1] = '\0';
        pcTextNew = true;
      } else if (serLen > 2 && serBuf[0] == 'K' && serBuf[1] == ':') {
        // K:hex → PC 让板子打字, 同样放进队列
        enqueueHex(serBuf + 2, serLen - 2);
      }
      serLen = 0;
    } else if (c != '\r') {
      serBuf[serLen++] = c;
    }
  }
}

// ===================== setup =====================
void setup() {
  Serial.begin(115200);
  keyboard.begin();
  USB.begin();
  delay(800);
  Serial.println("\n[ESP32-Bridge] Starting...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  Serial.print("[ESP32-Bridge] AP: ");
  Serial.print(AP_SSID);
  Serial.print(" IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/poll", HTTP_GET, handlePoll);
  server.begin();
  Serial.println("[ESP32-Bridge] === READY ===");
}

// ===================== loop =====================
// 每一轮: 先处理网页和串口, 再从队列里打一个字
void loop() {
  server.handleClient();
  handleSerial();
  if (qBytes > 0) {
    typeOneChar();
  }
}
