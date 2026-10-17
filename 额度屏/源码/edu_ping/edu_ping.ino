#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <Wire.h>
#include "ESP_I2S.h"
#include "es8311.h"
#include "es7210.h"

// ==================== 硬件引脚定义 ====================
// 屏幕 (ST7789 240x240, 4-wire SPI)
#define LCD_DC    45
#define LCD_CS    21
#define LCD_SCLK  38
#define LCD_MOSI  39
#define LCD_RST   40
#define LCD_BL    46

// I2C 总线 (ES8311 + ES7210 + Touch)
#define I2C_SDA   42
#define I2C_SCL   41

// 音频与麦克风 (I2S)
#define I2S_MCLK  8
#define I2S_BCLK  9
#define I2S_WS    10
#define I2S_DOUT  12
#define I2S_DIN   11
#define PA_CTRL   7

// 实体按键 (低电平有效)
#define BTN_BOOT  0
#define BTN_1     5
#define BTN_2     4

// Wi-Fi 与 YJW 服务配置
const char* WIFI_SSID     = "qianzhi";
const char* WIFI_PASS     = "qianzhi8888";
const char* QUOTA_API_URL = "http://192.168.6.170:8888/api/quota";
const char* CLEAR_ALARM_URL = "http://192.168.6.170:8888/api/clear-alarm";

// ==================== 全局驱动与状态 ====================
Arduino_DataBus* bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, -1);
Arduino_GFX* gfx = new Arduino_ST7789(bus, LCD_RST, 0 /* rotation */, true, 240, 240);
WebServer webServer(80);
I2SClass i2s;

es8311_handle_t es8311_handle = NULL;
es7210_dev_handle_t es7210_handle = NULL;
bool es8311_ready = false;
bool es7210_ready = false;

// 数据状态
float gemini_5h_remaining = 100.0f;
float gemini_5h_used = 0.0f;
int   gemini_5h_reset_secs = 0;
int   gemini_5h_reset_mins = 0;
float gemini_weekly_remaining = 100.0f;
int   server_hour = 12;
bool  test_alarm_flag = false;
String gemini_resets_at = "";

unsigned long last_data_received_ms = 0;
unsigned long last_poll_ms = 0;
const unsigned long POLL_INTERVAL_MS = 8000;       // 每 8 秒向 YJW 轮询一次
const unsigned long OFFLINE_TIMEOUT_MS = 180000;   // 超过 3 分钟未更新判定离线 (整屏变灰)

// 报警状态
bool alarm_active = false;
bool alarm_reported_for_current_window = false;
String last_window_id = "";
unsigned long alarm_start_ms = 0;
const unsigned long MAX_ALARM_DURATION_MS = 60000; // 最长响 1 分钟自停
int clap_threshold = 12000;                        // 拍手幅值触发阈值 (背景~40，拍手>15000)

// 颜色定义 (RGB565)
#define COLOR_BG_NORMAL     0x0000  // 纯黑底色
#define COLOR_BG_GRAY       0x18C3  // 离线灰色底色
#define COLOR_GREEN         0x07E0  // 充裕绿 (剩 > 50%)
#define COLOR_YELLOW        0xFFE0  // 过半黄 (20% ~ 50%)
#define COLOR_RED           0xF800  // 快用完红 (< 20%)
#define COLOR_GRAY_BAR      0x7BEF  // 离线指示灰
#define COLOR_TEXT_WHITE    0xFFFF  // 白色正文
#define COLOR_TEXT_MUTED    0x8410  // 暗灰提示文字

uint16_t current_theme_color = COLOR_GREEN;
bool is_screen_gray = false;

// ==================== 音频编解码初始化 ====================
void init_codecs() {
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, HIGH); // 打开功放

  // 初始化 ES8311 (喇叭)
  es8311_handle = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (es8311_handle) {
    const es8311_clock_config_t es_clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = 16000 * 256,
      .sample_frequency = 16000
    };
    if (es8311_init(es8311_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK) {
      es8311_sample_frequency_config(es8311_handle, 16000 * 256, 16000);
      es8311_voice_volume_set(es8311_handle, 85, NULL);
      es8311_ready = true;
      Serial.println("[AUDIO] ES8311 喇叭初始化成功");
    }
  }

  // 初始化 ES7210 (麦克风)
  es7210_i2c_config_t es7210_conf = { .i2c_addr = 0x40 };
  if (es7210_new_codec(&es7210_conf, &es7210_handle) == ESP_OK) {
    es7210_codec_config_t codec_conf = {};
    codec_conf.i2s_format = ES7210_I2S_FMT_I2S;
    codec_conf.mclk_ratio = 256;
    codec_conf.sample_rate_hz = 16000;
    codec_conf.bit_width = ES7210_I2S_BITS_16B;
    codec_conf.mic_bias = ES7210_MIC_BIAS_2V87;
    codec_conf.mic_gain = ES7210_MIC_GAIN_30DB;
    codec_conf.flags.tdm_enable = false;
    if (es7210_config_codec(es7210_handle, &codec_conf) == ESP_OK) {
      es7210_config_volume(es7210_handle, 50);
      es7210_ready = true;
      Serial.println("[AUDIO] ES7210 麦克风初始化成功");
    }
  }

  // 初始化 I2S 总线
  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
  i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  Serial.println("[AUDIO] I2S 双工总线已启动 (16kHz 16bit)");
}

// 播放短促提示音 (约 180ms，1000Hz 蜂鸣)
void play_short_beep() {
  if (!es8311_ready) return;
  digitalWrite(PA_CTRL, HIGH);
  const int sample_rate = 16000;
  const int duration_ms = 180;
  const int total_samples = (sample_rate * duration_ms) / 1000;
  int16_t stereo_buf[64 * 2]; // 64 frames (left + right)

  int frames_sent = 0;
  int phase = 0;
  while (frames_sent < total_samples) {
    int chunk = min(64, total_samples - frames_sent);
    for (int i = 0; i < chunk; i++) {
      // 1000Hz sine wave (16 samples per period at 16kHz)
      float s = sinf(2.0f * 3.14159265f * (float)phase / 16.0f);
      int16_t val = (int16_t)(s * 14000.0f);
      stereo_buf[i * 2]     = val; // L
      stereo_buf[i * 2 + 1] = val; // R
      phase = (phase + 1) % 16;
    }
    i2s.write((uint8_t*)stereo_buf, chunk * 4);
    frames_sent += chunk;
  }
  // 鸣叫完毕立即关闭功放，防止残余底噪
  digitalWrite(PA_CTRL, LOW);
}

String last_stop_reason = "none";
int last_detected_peak = 0;

// 监听麦克风 2 秒（检查拍手或按键）
// 返回 true 表示检测到拍手或按键被按下（立即停止报警）
bool listen_clap_for_2s() {
  // 鸣叫后等待 250ms 消除回响，并彻底清空 DMA 接收缓存
  delay(250);
  int16_t flush_buf[128 * 2];
  for (int f = 0; f < 8; f++) {
    i2s.readBytes((char*)flush_buf, sizeof(flush_buf));
  }

  unsigned long start = millis();
  int16_t rx_buf[128 * 2];

  while (millis() - start < 2000) {
    webServer.handleClient(); // 保持 Web 响应

    // 检查实体按键
    if (digitalRead(BTN_BOOT) == LOW || digitalRead(BTN_1) == LOW || digitalRead(BTN_2) == LOW) {
      last_stop_reason = "button_pressed";
      Serial.println("[ALARM] 检测到按键按下，停止报警");
      return true;
    }

    // 读取麦克风数据
    size_t bytes_read = i2s.readBytes((char*)rx_buf, sizeof(rx_buf));
    if (bytes_read > 0) {
      int frames = bytes_read / 4;
      int16_t max_peak = 0;
      for (int i = 0; i < frames; i++) {
        int16_t sample = abs(rx_buf[i * 2]); // 左声道
        if (sample > max_peak) max_peak = sample;
      }
      if (max_peak > clap_threshold) {
        last_detected_peak = max_peak;
        last_stop_reason = "clap_detected (peak=" + String(max_peak) + ")";
        Serial.printf("[ALARM] 检测到拍手声音! 峰值=%d (阈值=%d)，停止报警\n", max_peak, clap_threshold);
        return true;
      }
    }
    delay(20);
  }
  return false;
}

// 报警循环主逻辑
void handle_alarm_cycle() {
  if (!alarm_active) return;

  // 检查是否超过 1 分钟自停
  if (millis() - alarm_start_ms > MAX_ALARM_DURATION_MS) {
    Serial.println("[ALARM] 报警已持续 1 分钟无人接管，自动停响");
    stop_alarm();
    return;
  }

  // 检查时间段静音规则：0点到10点不出声，只变颜色
  // 如果是 test_alarm 则允许发声（供人工回家测试）
  bool mute_sound = (server_hour >= 0 && server_hour < 10 && !test_alarm_flag);

  if (!mute_sound) {
    Serial.println("[ALARM] 鸣叫一声 (180ms)...");
    play_short_beep();
  } else {
    Serial.println("[ALARM] 处于夜间/早晨静音时段 (0点-10点)，不发声只闪屏");
  }

  // 停 2 秒并监听拍手与按键
  Serial.println("[ALARM] 停响 2 秒，麦克风监听拍手/按键中...");
  if (listen_clap_for_2s()) {
    stop_alarm();
  }
}

void trigger_alarm() {
  if (!alarm_active) {
    alarm_active = true;
    alarm_start_ms = millis();
    Serial.println("=========================================");
    Serial.println(">>> 触发额度预警/测试报警! 循环短响 + 拍手监听 <<<");
    Serial.println("=========================================");
    render_display();
  }
}

void stop_alarm() {
  if (alarm_active) {
    alarm_active = false;
    Serial.println("[ALARM] 报警已停响，恢复常态。");
    // 通知 YJW 服务清除测试报警标记
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(CLEAR_ALARM_URL);
      http.GET();
      http.end();
    }
    test_alarm_flag = false;
    render_display();
  }
}

// ==================== 屏幕渲染 (Clawdmeter 风格) ====================
void render_display() {
  bool offline = (millis() - last_data_received_ms > OFFLINE_TIMEOUT_MS);
  is_screen_gray = offline;

  // 决定主题色
  if (offline) {
    current_theme_color = COLOR_GRAY_BAR;
  } else if (gemini_5h_remaining > 50.0f) {
    current_theme_color = COLOR_GREEN;
  } else if (gemini_5h_remaining >= 20.0f) {
    current_theme_color = COLOR_YELLOW;
  } else {
    current_theme_color = COLOR_RED;
  }

  uint16_t bg_color = offline ? COLOR_BG_GRAY : COLOR_BG_NORMAL;
  gfx->fillScreen(bg_color);

  // 1. 顶部 Header
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_TEXT_WHITE);
  gfx->setCursor(16, 12);
  gfx->print("GEMINI 5H");

  gfx->setTextSize(1);
  gfx->setCursor(160, 16);
  if (WiFi.status() == WL_CONNECTED) {
    gfx->setTextColor(COLOR_GREEN);
    gfx->print("WiFi OK");
  } else {
    gfx->setTextColor(COLOR_RED);
    gfx->print("WiFi LOST");
  }

  // 2. 大字百分比
  gfx->setTextSize(4);
  gfx->setTextColor(current_theme_color);
  gfx->setCursor(20, 38);
  if (offline) {
    gfx->print("--- %");
  } else {
    gfx->printf("%d%%", (int)round(gemini_5h_remaining));
  }

  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT_WHITE);
  gfx->setCursor(130, 48);
  gfx->print("REMAINING");
  gfx->setCursor(130, 60);
  gfx->print("5h 额度剩余");

  // 3. 大进度条 (200x24 像素，圆角矩形边框)
  int bar_x = 20, bar_y = 86, bar_w = 200, bar_h = 24;
  gfx->drawRoundRect(bar_x, bar_y, bar_w, bar_h, 4, current_theme_color);
  gfx->drawRoundRect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, 3, current_theme_color);

  // 进度填充
  int fill_max = bar_w - 8;
  int fill_w = offline ? 0 : (int)((gemini_5h_remaining / 100.0f) * fill_max);
  if (fill_w > fill_max) fill_w = fill_max;
  if (fill_w < 0) fill_w = 0;

  if (fill_w > 0) {
    gfx->fillRoundRect(bar_x + 4, bar_y + 4, fill_w, bar_h - 8, 2, current_theme_color);
  }

  // 4. 分割线
  gfx->drawFastHLine(20, 122, 200, 0x3186);

  // 5. 倒计时部分
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT_MUTED);
  gfx->setCursor(20, 132);
  gfx->print("5H WINDOW RESET / 重置倒计时:");

  gfx->setTextSize(2);
  gfx->setTextColor(current_theme_color);
  gfx->setCursor(20, 146);
  if (offline) {
    gfx->print("离线等待中");
  } else {
    int hrs = gemini_5h_reset_secs / 3600;
    int mins = (gemini_5h_reset_secs % 3600) / 60;
    gfx->printf("%d 小时 %02d 分钟", hrs, mins);
  }

  // 6. 每周额度小字
  gfx->setTextSize(1);
  gfx->setTextColor(COLOR_TEXT_MUTED);
  gfx->setCursor(20, 176);
  gfx->printf("每周额度: %.0f%% (Google AI Pro)", gemini_weekly_remaining);

  // 7. 底部状态横幅
  if (alarm_active) {
    gfx->fillRoundRect(16, 198, 208, 30, 4, COLOR_RED);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(28, 208);
    gfx->print("! 报警中: 拍手或按键即停 !");
  } else if (offline) {
    gfx->fillRoundRect(16, 198, 208, 30, 4, 0x4A69);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(24, 208);
    gfx->print("! 超过3分钟未更新 整屏变灰 !");
  } else {
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_MUTED);
    gfx->setCursor(20, 212);
    gfx->printf("IP: %s", WiFi.localIP().toString().c_str());
  }

  // 串口打印同步与画面信息
  print_serial_summary();
}

void print_serial_summary() {
  Serial.println("\n-------------------------------------------");
  Serial.printf("[额度屏画面] 5h额度: %.1f%% | 重置倒计时: %d分 | 每周: %.0f%%\n",
                gemini_5h_remaining, gemini_5h_reset_mins, gemini_weekly_remaining);
  Serial.printf("颜色主题: %s | 离线状态: %s | 报警中: %s\n",
                is_screen_gray ? "灰色(离线)" : (gemini_5h_remaining > 50 ? "绿色" : (gemini_5h_remaining >= 20 ? "黄色" : "红色")),
                is_screen_gray ? "是(已超3分钟)" : "否",
                alarm_active ? "是" : "否");
  Serial.println("-------------------------------------------");
}

// ==================== 从 YJW 小网页取数 ====================
void poll_yjw_quota() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] 未连接，尝试重连...");
    WiFi.reconnect();
    return;
  }

  HTTPClient http;
  http.begin(QUOTA_API_URL);
  http.setTimeout(4000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      last_data_received_ms = millis();
      gemini_5h_remaining = doc["gemini_5h_remaining_pct"] | 100.0f;
      gemini_5h_used      = doc["gemini_5h_used_pct"] | 0.0f;
      gemini_5h_reset_secs = doc["gemini_5h_reset_seconds"] | 0;
      gemini_5h_reset_mins = doc["gemini_5h_reset_minutes"] | 0;
      gemini_weekly_remaining = doc["gemini_weekly_remaining_pct"] | 100.0f;
      server_hour         = doc["server_hour"] | 12;
      bool test_req       = doc["test_alarm"] | false;
      String reset_at     = doc["gemini_5h_resets_at"] | "";

      // 检测新窗口：当重置时间更新，重置当前窗口报警标记
      if (reset_at.length() > 0 && reset_at != last_window_id) {
        last_window_id = reset_at;
        alarm_reported_for_current_window = false;
        Serial.println("[QUOTA] 新 5h 窗口开始，重置单窗口报警限制");
      }

      // 规则 1：5 小时额度剩 20% 以下时报警，每个窗口只报一次
      if (gemini_5h_remaining <= 20.0f && !alarm_reported_for_current_window) {
        Serial.println("[QUOTA] 5h 额度已低于 20%，触发本窗口唯一一次警报！");
        alarm_reported_for_current_window = true;
        trigger_alarm();
      }

      // 规则 2：远程测试报警触发
      if (test_req && !alarm_active) {
        Serial.println("[QUOTA] 收到 YJW 下发的测试报警指令！");
        test_alarm_flag = true;
        trigger_alarm();
      }

      render_display();
    } else {
      Serial.printf("[HTTP] JSON 解析失败: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[HTTP] 请求失败: code=%d\n", httpCode);
  }
  http.end();
}

// ==================== 内置 Web 服务（提供画面查看与远程控制） ====================
void setup_web_server() {
  webServer.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>额度屏</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:20px;}";
    html += ".card{background:#222;border-radius:12px;padding:24px;max-width:360px;margin:auto;}";
    html += ".pct{font-size:48px;font-weight:bold;margin:10px 0;color:" + String(current_theme_color == COLOR_GREEN ? "#2ecc71" : (current_theme_color == COLOR_YELLOW ? "#f1c40f" : "#e74c3c")) + ";}";
    html += ".bar{background:#333;height:20px;border-radius:10px;overflow:hidden;margin:15px 0;}";
    html += ".fill{background:" + String(current_theme_color == COLOR_GREEN ? "#2ecc71" : (current_theme_color == COLOR_YELLOW ? "#f1c40f" : "#e74c3c")) + ";height:100%;width:" + String((int)gemini_5h_remaining) + "%;}";
    html += ".btn{display:inline-block;padding:10px 20px;background:#e74c3c;color:#fff;border-radius:6px;text-decoration:none;margin-top:15px;}";
    html += "</style></head><body><div class='card'>";
    html += "<h2>Gemini 额度屏</h2>";
    html += "<div class='pct'>" + String(gemini_5h_remaining, 1) + "%</div>";
    html += "<div class='bar'><div class='fill'></div></div>";
    html += "<p>距重置: " + String(gemini_5h_reset_secs / 3600) + "小时 " + String((gemini_5h_reset_secs % 3600) / 60) + "分钟</p>";
    html += "<p>每周额度: " + String(gemini_weekly_remaining, 0) + "% (AI Pro)</p>";
    html += "<p>状态: " + String(is_screen_gray ? "已变灰 (离线)" : "正常显示") + "</p>";
    html += "<a href='/alarm' class='btn'>立即触发响铃测试 (拍手即停)</a>";
    html += "</div></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
  });

  webServer.on("/status", HTTP_GET, []() {
    StaticJsonDocument<512> doc;
    doc["device"] = "Waveshare-ESP32-S3-LCD-1.54";
    doc["mac"] = "80:45:6b:35:ae:ec";
    doc["ip"] = WiFi.localIP().toString();
    doc["gemini_5h_remaining_pct"] = gemini_5h_remaining;
    doc["gemini_5h_reset_minutes"] = gemini_5h_reset_mins;
    doc["gemini_weekly_remaining_pct"] = gemini_weekly_remaining;
    doc["is_screen_gray"] = is_screen_gray;
    doc["alarm_active"] = alarm_active;
    doc["last_stop_reason"] = last_stop_reason;
    doc["last_detected_peak"] = last_detected_peak;
    doc["es8311_ok"] = es8311_ready;
    doc["es7210_ok"] = es7210_ready;
    String res;
    serializeJson(doc, res);
    webServer.send(200, "application/json", res);
  });

  // ASCII 屏幕文字快照（供远程确认画面）
  webServer.on("/screen", HTTP_GET, []() {
    String txt = "+---------------------------------------+\n";
    txt += "| GEMINI 5H                     WiFi OK |\n";
    txt += "|                                       |\n";
    txt += "|               " + String(gemini_5h_remaining, 1) + "%                   |\n";
    txt += "|              剩余额度                 |\n";
    txt += "|                                       |\n";
    int bars = (int)(gemini_5h_remaining / 100.0f * 25.0f);
    txt += "| [";
    for (int i = 0; i < 25; i++) {
      txt += (i < bars) ? "=" : "-";
    }
    txt += "] |\n";
    txt += "|                                       |\n";
    txt += "| 距重置: " + String(gemini_5h_reset_secs / 3600) + " 小时 " + String((gemini_5h_reset_secs % 3600) / 60) + " 分钟               |\n";
    txt += "| 每周额度: " + String(gemini_weekly_remaining, 0) + "% (Google AI Pro)     |\n";
    txt += "|                                       |\n";
    if (alarm_active) {
      txt += "| >>> [报警中] 拍手或按键即停 <<<       |\n";
    } else if (is_screen_gray) {
      txt += "| [已离线] 超过3分钟未更新 整屏变灰     |\n";
    } else {
      String color_str = (gemini_5h_remaining > 50) ? "绿色(充裕)" : (gemini_5h_remaining >= 20 ? "黄色(过半)" : "红色(见底)");
      txt += "| 状态: 正常运行 (" + color_str + ")        |\n";
    }
    txt += "+---------------------------------------+\n";
    webServer.send(200, "text/plain; charset=utf-8", txt);
  });

  webServer.on("/alarm", HTTP_GET, []() {
    test_alarm_flag = true;
    trigger_alarm();
    webServer.send(200, "text/plain; charset=utf-8", "已触发报警！板子将短响并在停响间隙监听拍手或按键。");
  });

  webServer.on("/mic", HTTP_GET, []() {
    int16_t buf[128 * 2];
    int16_t peak = 0;
    for (int k = 0; k < 10; k++) {
      size_t br = i2s.readBytes((char*)buf, sizeof(buf));
      for (size_t i = 0; i < br / 4; i++) {
        int16_t v = abs(buf[i * 2]);
        if (v > peak) peak = v;
      }
      delay(10);
    }
    String msg = "{\"ambient_peak\": " + String(peak) + ", \"clap_threshold\": " + String(clap_threshold) + "}";
    webServer.send(200, "application/json", msg);
  });

  webServer.begin();
  Serial.println("[HTTP] 板载诊断网页已启动在 http://0.0.0.0:80/");
}

// ==================== 主流程 ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=========================================");
  Serial.println("  Antigravity 额度屏 (Waveshare 1.54寸)  ");
  Serial.println("=========================================");

  // 按键输入
  pinMode(BTN_BOOT, INPUT_PULLUP);
  pinMode(BTN_1,    INPUT_PULLUP);
  pinMode(BTN_2,    INPUT_PULLUP);

  // 初始化背光与 ST7789 屏幕
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(COLOR_BG_NORMAL);

  // 屏幕启动画面
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_GREEN);
  gfx->setCursor(35, 90);
  gfx->print("Antigravity");
  gfx->setTextColor(COLOR_TEXT_WHITE);
  gfx->setCursor(55, 120);
  gfx->print("额度屏 v1.0");

  // 初始化音响与麦克风
  init_codecs();

  // 连接 Wi-Fi
  Serial.printf("[WIFI] 正在连接 %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 25) {
    delay(400);
    Serial.print(".");
    retry++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] 连接成功! 板子 IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WIFI] 连接失败，将继续后台重试。");
  }

  // 启动板载网页
  setup_web_server();

  // 立即取一次数
  poll_yjw_quota();
  render_display();
}

void loop() {
  webServer.handleClient();

  // 定时向 YJW 取数
  if (millis() - last_poll_ms >= POLL_INTERVAL_MS) {
    last_poll_ms = millis();
    poll_yjw_quota();
  }

  // 检查是否超时 3 分钟变灰
  if (!is_screen_gray && (millis() - last_data_received_ms > OFFLINE_TIMEOUT_MS)) {
    Serial.println("[TIMEOUT] 超过 3 分钟未取到新数据，整屏变灰！");
    render_display();
  }

  // 处理报警循环
  if (alarm_active) {
    handle_alarm_cycle();
  }

  // 实体按键交互
  if (digitalRead(BTN_BOOT) == LOW || digitalRead(BTN_1) == LOW || digitalRead(BTN_2) == LOW) {
    if (alarm_active) {
      stop_alarm();
    }
    delay(200);
  }

  delay(20);
}
