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
// GPIO 5: 主按键 (PWR / Center)
// GPIO 4: 左侧音量键 (PLUS / Vol+)
// GPIO 0: 右侧音量键 (BOOT / Vol-)
#define BTN_MAIN    5
#define BTN_VOL_UP  4
#define BTN_VOL_DN  0

// Wi-Fi 与 YJW 服务配置
const char* WIFI_SSID       = "qianzhi";
const char* WIFI_PASS       = "qianzhi8888";
const char* QUOTA_API_URL   = "http://192.168.6.170:8888/api/quota";
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

// 页面状态
// 0: 5H (Antigravity 5小时额度)
// 1: WEEKLY (Antigravity 每周额度)
// 2: GEMINI WEB (Gemini 网页版额度)
int current_page = 0;
unsigned long last_page_switch_ms = 0;
const unsigned long AUTO_RETURN_HOME_MS = 30000; // 30秒无人操作自动回第1页

// 数据状态
// 1. Antigravity 5H
float gemini_5h_remaining = -1.0f;
float gemini_5h_used = 0.0f;
int   gemini_5h_reset_secs = 0;
int   gemini_5h_reset_mins = 0;
String gemini_resets_at = "";

// 2. Antigravity Weekly
float gemini_weekly_remaining = -1.0f;
int   gemini_weekly_reset_hrs = -1;
bool  gemini_weekly_valid = false;

// 3. Gemini Web
float gemini_web_5h_remaining = -1.0f;
float gemini_web_5h_used = 0.0f;
String gemini_web_5h_reset_str = "--:--";
float gemini_web_weekly_remaining = -1.0f;
float gemini_web_weekly_used = 0.0f;
String gemini_web_weekly_reset_str = "---";
bool  gemini_web_valid = false;

int   server_hour = 12;
bool  test_alarm_flag = false;

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
int clap_threshold = 12000;                        // 拍手幅值触发阈值

// 颜色定义 (RGB565)
#define COLOR_BG_NORMAL     0x0000  // 纯黑底色
#define COLOR_BG_GRAY       0x18C3  // 离线灰色底色
#define COLOR_GREEN         0x07E0  // 充裕绿 (剩 > 50%)
#define COLOR_YELLOW        0xFFE0  // 过半黄 (20% ~ 50%)
#define COLOR_RED           0xF800  // 快用完红 (< 20%)
#define COLOR_GRAY_BAR      0x7BEF  // 离线指示灰
#define COLOR_TEXT_WHITE    0xFFFF  // 白色正文
#define COLOR_TEXT_MUTED    0x8410  // 暗灰辅助

bool is_screen_gray = false;

// 统一页面渲染数据结构（实物液晶屏与 /screen 严格共享此数据及格式化结果）
struct PageRenderData {
  int page_num;        // 1, 2, 3
  String title;        // "5H", "WEEKLY", "GEMINI WEB"
  bool is_offline;     // 是否离线/等待数据
  bool is_valid;       // 额度数据是否有效
  float remaining_pct; // 剩余百分比
  String pct_str;      // 大数字："67%", "-- %", "--- %"
  int bar_fill_w;      // 液晶进度条填充像素 0~192
  int bar_bars_ascii;  // ASCII进度条等号数 0~25
  String line1;        // 重置时间主行
  String line2;        // 副行 (仅第3页有)
  uint16_t theme_color;// 主题色
  uint16_t bg_color;   // 底色
  String status_desc;  // 状态说明
  bool alarm;          // 是否在报警
};

// 前置声明
bool is_system_offline();
PageRenderData get_page_render_data(int page_idx);
void render_display();
void stop_alarm();

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
      Serial.println("[AUDIO] ES8311 speaker initialized");
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
      Serial.println("[AUDIO] ES7210 microphone initialized");
    }
  }

  // 初始化 I2S 总线
  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
  i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  Serial.println("[AUDIO] I2S bus started (16kHz 16bit)");
}

// 播放短促提示音 (约 180ms，1000Hz 蜂鸣)
void play_short_beep() {
  if (!es8311_ready) return;
  digitalWrite(PA_CTRL, HIGH);
  const int sample_rate = 16000;
  const int duration_ms = 180;
  const int total_samples = (sample_rate * duration_ms) / 1000;
  int16_t stereo_buf[64 * 2];

  int frames_sent = 0;
  int phase = 0;
  while (frames_sent < total_samples) {
    int chunk = min(64, total_samples - frames_sent);
    for (int i = 0; i < chunk; i++) {
      float s = sinf(2.0f * 3.14159265f * (float)phase / 16.0f);
      int16_t val = (int16_t)(s * 14000.0f);
      stereo_buf[i * 2]     = val;
      stereo_buf[i * 2 + 1] = val;
      phase = (phase + 1) % 16;
    }
    i2s.write((uint8_t*)stereo_buf, chunk * 4);
    frames_sent += chunk;
  }
  digitalWrite(PA_CTRL, LOW);
}

String last_stop_reason = "none";
int last_detected_peak = 0;

// 监听麦克风 2 秒（检查拍手或按键）
bool listen_clap_for_2s() {
  delay(250);
  int16_t flush_buf[128 * 2];
  for (int f = 0; f < 8; f++) {
    i2s.readBytes((char*)flush_buf, sizeof(flush_buf));
  }

  unsigned long start = millis();
  int16_t rx_buf[128 * 2];

  while (millis() - start < 2000) {
    webServer.handleClient();

    // 检查实体按键：报警时任何按键都能关报警
    if (digitalRead(BTN_MAIN) == LOW || digitalRead(BTN_VOL_UP) == LOW || digitalRead(BTN_VOL_DN) == LOW) {
      last_stop_reason = "button_pressed";
      Serial.println("[ALARM] Button pressed, stopping alarm");
      return true;
    }

    size_t bytes_read = i2s.readBytes((char*)rx_buf, sizeof(rx_buf));
    if (bytes_read > 0) {
      int frames = bytes_read / 4;
      int16_t max_peak = 0;
      for (int i = 0; i < frames; i++) {
        int16_t sample = abs(rx_buf[i * 2]);
        if (sample > max_peak) max_peak = sample;
      }
      if (max_peak > clap_threshold) {
        last_detected_peak = max_peak;
        last_stop_reason = "clap_detected (peak=" + String(max_peak) + ")";
        Serial.printf("[ALARM] Clap detected! Peak=%d, stopping alarm\n", max_peak);
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

  if (millis() - alarm_start_ms > MAX_ALARM_DURATION_MS) {
    Serial.println("[ALARM] Alarm duration reached 1 minute, auto-stopping");
    stop_alarm();
    return;
  }

  // 0点到10点夜间静音只变颜色
  bool mute_sound = (server_hour >= 0 && server_hour < 10 && !test_alarm_flag);

  if (!mute_sound) {
    Serial.println("[ALARM] Beeping (180ms)...");
    play_short_beep();
  } else {
    Serial.println("[ALARM] Muted period (00:00-10:00), visual only");
  }

  if (listen_clap_for_2s()) {
    stop_alarm();
  }
}

void trigger_alarm() {
  if (!alarm_active) {
    alarm_active = true;
    alarm_start_ms = millis();
    Serial.println("=========================================");
    Serial.println(">>> ALARM TRIGGERED: Beep + Clap listening <<<");
    Serial.println("=========================================");
    render_display();
  }
}

void stop_alarm() {
  if (alarm_active) {
    alarm_active = false;
    Serial.println("[ALARM] Alarm cleared.");
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

// ==================== 屏幕渲染 (全英文三页极简风格) ====================
// 统一格式化与数据生成函数：实物液晶屏与 /screen 均调用此函数
bool is_system_offline() {
  if (last_data_received_ms == 0) return true;
  return (millis() - last_data_received_ms > OFFLINE_TIMEOUT_MS);
}

PageRenderData get_page_render_data(int page_idx) {
  PageRenderData d;
  d.page_num = page_idx + 1;
  d.is_offline = is_system_offline();
  d.alarm = alarm_active;
  d.bg_color = d.is_offline ? COLOR_BG_GRAY : COLOR_BG_NORMAL;
  d.line2 = "";

  if (page_idx == 0) {
    d.title = "5H";
    d.is_valid = !d.is_offline && (gemini_5h_remaining >= 0.0f);
    d.remaining_pct = d.is_valid ? gemini_5h_remaining : 0.0f;
    
    if (d.is_offline) {
      d.pct_str = "--- %";
      d.bar_fill_w = 0;
      d.bar_bars_ascii = 0;
      d.line1 = "Offline / Waiting";
      d.theme_color = COLOR_GRAY_BAR;
      d.status_desc = "Offline";
    } else if (!d.is_valid) {
      d.pct_str = "-- %";
      d.bar_fill_w = 0;
      d.bar_bars_ascii = 0;
      d.line1 = "Resets: --";
      d.theme_color = COLOR_GRAY_BAR;
      d.status_desc = "Gray (No Data)";
    } else {
      d.pct_str = String((int)round(d.remaining_pct)) + "%";
      d.bar_fill_w = constrain((int)((d.remaining_pct / 100.0f) * 192), 0, 192);
      d.bar_bars_ascii = constrain((int)((d.remaining_pct / 100.0f) * 25), 0, 25);
      int hrs = gemini_5h_reset_secs / 3600;
      int mins = (gemini_5h_reset_secs % 3600) / 60;
      d.line1 = "Resets in " + String(hrs) + "h " + String(mins) + "m";
      if (d.remaining_pct > 50.0f) {
        d.theme_color = COLOR_GREEN;
        d.status_desc = "Green (>50%)";
      } else if (d.remaining_pct >= 20.0f) {
        d.theme_color = COLOR_YELLOW;
        d.status_desc = "Yellow (20-50%)";
      } else {
        d.theme_color = COLOR_RED;
        d.status_desc = "Red (<20%)";
      }
    }
  } else if (page_idx == 1) {
    d.title = "WEEKLY";
    d.is_valid = !d.is_offline && gemini_weekly_valid && (gemini_weekly_remaining >= 0.0f);
    d.remaining_pct = d.is_valid ? gemini_weekly_remaining : 0.0f;
    
    if (d.is_offline) {
      d.pct_str = "--- %";
      d.bar_fill_w = 0;
      d.bar_bars_ascii = 0;
      d.line1 = "Offline / Waiting";
      d.theme_color = COLOR_GRAY_BAR;
      d.status_desc = "Offline";
    } else if (!d.is_valid) {
      d.pct_str = "-- %";
      d.bar_fill_w = 0;
      d.bar_bars_ascii = 0;
      d.line1 = "Resets: --";
      d.theme_color = COLOR_GRAY_BAR;
      d.status_desc = "Gray (No Data)";
    } else {
      d.pct_str = String((int)round(d.remaining_pct)) + "%";
      d.bar_fill_w = constrain((int)((d.remaining_pct / 100.0f) * 192), 0, 192);
      d.bar_bars_ascii = constrain((int)((d.remaining_pct / 100.0f) * 25), 0, 25);
      int days = gemini_weekly_reset_hrs / 24;
      int hrs = gemini_weekly_reset_hrs % 24;
      d.line1 = "Resets in " + String(days) + "d " + String(hrs) + "h";
      if (d.remaining_pct > 50.0f) {
        d.theme_color = COLOR_GREEN;
        d.status_desc = "Green (>50%)";
      } else if (d.remaining_pct >= 20.0f) {
        d.theme_color = COLOR_YELLOW;
        d.status_desc = "Yellow (20-50%)";
      } else {
        d.theme_color = COLOR_RED;
        d.status_desc = "Red (<20%)";
      }
    }
  } else {
    d.title = "GEMINI WEB";
    d.is_valid = !d.is_offline && gemini_web_valid && (gemini_web_5h_remaining >= 0.0f);
    d.remaining_pct = d.is_valid ? gemini_web_5h_remaining : 0.0f;
    
    if (d.is_offline) {
      d.pct_str = "--- %";
      d.bar_fill_w = 0;
      d.bar_bars_ascii = 0;
      d.line1 = "Offline / Waiting";
      d.theme_color = COLOR_GRAY_BAR;
      d.status_desc = "Offline";
    } else if (!d.is_valid) {
      d.pct_str = "-- %";
      d.bar_fill_w = 0;
      d.bar_bars_ascii = 0;
      d.line1 = "Resets: --";
      d.theme_color = COLOR_GRAY_BAR;
      d.status_desc = "Gray (No Data)";
    } else {
      d.pct_str = String((int)round(d.remaining_pct)) + "%";
      d.bar_fill_w = constrain((int)((d.remaining_pct / 100.0f) * 192), 0, 192);
      d.bar_bars_ascii = constrain((int)((d.remaining_pct / 100.0f) * 25), 0, 25);
      d.line1 = "Resets at " + gemini_web_5h_reset_str;
      d.line2 = "Weekly: " + String((int)round(gemini_web_weekly_remaining)) + "% (Reset " + gemini_web_weekly_reset_str + ")";
      if (d.remaining_pct > 50.0f) {
        d.theme_color = COLOR_GREEN;
        d.status_desc = "Green (>50%)";
      } else if (d.remaining_pct >= 20.0f) {
        d.theme_color = COLOR_YELLOW;
        d.status_desc = "Yellow (20-50%)";
      } else {
        d.theme_color = COLOR_RED;
        d.status_desc = "Red (<20%)";
      }
    }
  }

  return d;
}

void render_display() {
  PageRenderData data = get_page_render_data(current_page);
  is_screen_gray = data.is_offline;

  gfx->fillScreen(data.bg_color);

  // 1. 顶部标题 (全英文，居中)
  if (data.page_num == 3) {
    gfx->setTextSize(2); // GEMINI WEB (10 chars * 12 = 120px)
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(60, 24);
    gfx->print(data.title);
  } else if (data.page_num == 2) {
    gfx->setTextSize(3); // WEEKLY (6 chars * 18 = 108px)
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(66, 20);
    gfx->print(data.title);
  } else {
    gfx->setTextSize(3); // 5H (2 chars * 18 = 36px)
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(102, 20);
    gfx->print(data.title);
  }

  // 2. 大数字百分比 (居中显示，使用统一计算好的 pct_str)
  gfx->setTextSize(5); // 30x40 per char
  gfx->setTextColor(data.theme_color);
  int num_x = (240 - data.pct_str.length() * 30) / 2;
  if (num_x < 0) num_x = 0;
  gfx->setCursor(num_x, 66);
  gfx->print(data.pct_str);

  // 3. 进度条 (200x22 像素，圆角矩形边框)
  int bar_x = 20, bar_y = 124, bar_w = 200, bar_h = 22;
  gfx->drawRoundRect(bar_x, bar_y, bar_w, bar_h, 4, data.theme_color);
  gfx->drawRoundRect(bar_x + 1, bar_y + 1, bar_w - 2, bar_h - 2, 3, data.theme_color);

  if (data.bar_fill_w > 0) {
    gfx->fillRoundRect(bar_x + 4, bar_y + 4, data.bar_fill_w, bar_h - 8, 2, data.theme_color);
  }

  // 4. 重置时间行 (全英文，居中)
  if (data.line2.length() > 0) {
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_TEXT_WHITE);
    int rst_x = (240 - data.line1.length() * 12) / 2;
    if (rst_x < 4) rst_x = 4;
    gfx->setCursor(rst_x, 160);
    gfx->print(data.line1);

    int sub_x = (240 - data.line2.length() * 12) / 2;
    if (sub_x < 4) sub_x = 4;
    gfx->setCursor(sub_x, 184);
    gfx->print(data.line2);
  } else {
    gfx->setTextSize(2);
    gfx->setTextColor(COLOR_TEXT_WHITE);
    int rst_x = (240 - data.line1.length() * 12) / 2;
    if (rst_x < 4) rst_x = 4;
    gfx->setCursor(rst_x, 168);
    gfx->print(data.line1);
  }

  // 5. 底部报警横幅 (仅在报警或离线时提示)
  if (data.alarm) {
    gfx->fillRoundRect(12, 206, 216, 26, 4, COLOR_RED);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(20, 215);
    gfx->print("! ALARM: CLAP OR PRESS BTN !");
  } else if (data.is_offline) {
    gfx->fillRoundRect(12, 206, 216, 26, 4, 0x4A69);
    gfx->setTextSize(1);
    gfx->setTextColor(COLOR_TEXT_WHITE);
    gfx->setCursor(22, 215);
    gfx->print("! OFFLINE: WAITING DATA !");
  }

  // 串口打印同步
  Serial.printf("[DISPLAY] Page %d (%s): %s | %s\n",
                data.page_num, data.title.c_str(), data.pct_str.c_str(), data.line1.c_str());
}

// ==================== 从 YJW 小网页取数 ====================
void poll_yjw_quota() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI] Disconnected, reconnecting...");
    WiFi.reconnect();
    return;
  }

  HTTPClient http;
  http.begin(QUOTA_API_URL);
  http.setTimeout(4000);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    StaticJsonDocument<1536> doc;
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      last_data_received_ms = millis();
      
      // 1. Antigravity 5H
      if (doc.containsKey("gemini_5h_remaining_pct") && !doc["gemini_5h_remaining_pct"].isNull()) {
        gemini_5h_remaining = doc["gemini_5h_remaining_pct"].as<float>();
        gemini_5h_used      = doc["gemini_5h_used_pct"] | 0.0f;
        gemini_5h_reset_secs = doc["gemini_5h_reset_seconds"] | 0;
        gemini_5h_reset_mins = doc["gemini_5h_reset_minutes"] | 0;
      } else {
        gemini_5h_remaining = -1.0f;
      }
      String reset_at     = doc["gemini_5h_resets_at"] | "";
      
      // 2. Antigravity Weekly
      if (doc.containsKey("gemini_weekly_remaining_pct") && !doc["gemini_weekly_remaining_pct"].isNull()) {
        gemini_weekly_remaining = doc["gemini_weekly_remaining_pct"].as<float>();
        gemini_weekly_reset_hrs = doc["gemini_weekly_reset_hours"].as<int>();
        gemini_weekly_valid     = true;
      } else {
        gemini_weekly_remaining = -1.0f;
        gemini_weekly_reset_hrs = -1;
        gemini_weekly_valid     = false;
      }
      
      // 3. Gemini Web
      if (doc.containsKey("gemini_web_5h_remaining_pct") && !doc["gemini_web_5h_remaining_pct"].isNull()) {
        gemini_web_5h_remaining = doc["gemini_web_5h_remaining_pct"].as<float>();
        gemini_web_5h_used      = doc["gemini_web_5h_used_pct"] | 0.0f;
        gemini_web_5h_reset_str = (const char*)(doc["gemini_web_5h_reset_str"] | "--:--");
        gemini_web_weekly_remaining = doc["gemini_web_weekly_remaining_pct"] | 100.0f;
        gemini_web_weekly_used      = doc["gemini_web_weekly_used_pct"] | 0.0f;
        gemini_web_weekly_reset_str = (const char*)(doc["gemini_web_weekly_reset_str"] | "---");
        gemini_web_valid = true;
      } else {
        gemini_web_5h_remaining = -1.0f;
        gemini_web_valid = false;
      }
      
      server_hour         = doc["server_hour"] | 12;
      bool test_req       = doc["test_alarm"] | false;

      // 检测新窗口：重置报警标记
      if (reset_at.length() > 0 && reset_at != last_window_id) {
        last_window_id = reset_at;
        alarm_reported_for_current_window = false;
        Serial.println("[QUOTA] New 5h window started, reset window alarm flag");
      }

      // 规则 1：5 小时额度剩 20% 以下时报警，每个窗口只报一次
      if (gemini_5h_remaining <= 20.0f && !alarm_reported_for_current_window) {
        Serial.println("[QUOTA] 5h quota <= 20%, trigger window alarm!");
        alarm_reported_for_current_window = true;
        trigger_alarm();
      }

      // 规则 2：远程测试报警触发
      if (test_req && !alarm_active) {
        Serial.println("[QUOTA] Test alarm requested by server");
        test_alarm_flag = true;
        trigger_alarm();
      }

      render_display();
    } else {
      Serial.printf("[HTTP] JSON parse failed: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[HTTP] Request failed: code=%d\n", httpCode);
  }
  http.end();
}

// 格式化固定宽度 ASCII 行（每行内容占 37 字符，加上两端 "| " 和 " |" 恰好 41 字符）
static String format_ascii_line(const String& left_str, const String& right_str) {
  int space_needed = 37 - left_str.length() - right_str.length();
  if (space_needed < 1) space_needed = 1;
  String line = "| " + left_str;
  for (int i = 0; i < space_needed; i++) line += " ";
  line += right_str + " |\n";
  return line;
}

static String format_ascii_centered(const String& text) {
  int total_pad = 37 - text.length();
  if (total_pad < 0) total_pad = 0;
  int l_pad = total_pad / 2;
  int r_pad = total_pad - l_pad;
  String line = "| ";
  for (int i = 0; i < l_pad; i++) line += " ";
  line += text;
  for (int i = 0; i < r_pad; i++) line += " ";
  line += " |\n";
  return line;
}

// ASCII 屏幕文字快照生成器（与液晶渲染严格共用 PageRenderData 及格式化）
String get_ascii_screen_page(int page_idx) {
  PageRenderData data = get_page_render_data(page_idx);
  
  String txt = "+---------------------------------------+\n";
  txt += format_ascii_line("PAGE " + String(data.page_num) + "/3: " + data.title, (WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi NO"));
  txt += format_ascii_centered("");
  
  txt += format_ascii_centered(data.pct_str);
  txt += format_ascii_centered("REMAINING");
  txt += format_ascii_centered("");
  
  String bar_str = "[";
  for (int i = 0; i < 25; i++) bar_str += (i < data.bar_bars_ascii) ? "=" : "-";
  bar_str += "]";
  txt += format_ascii_centered(bar_str);
  txt += format_ascii_centered("");
  
  txt += format_ascii_centered(data.line1);
  if (data.line2.length() > 0) {
    txt += format_ascii_centered(data.line2);
  } else {
    txt += format_ascii_centered("");
  }
  
  if (data.alarm) {
    txt += format_ascii_centered(">>> [ALARM] CLAP OR PRESS BTN <<<");
  } else if (data.is_offline) {
    txt += format_ascii_centered(">>> [OFFLINE] WAITING DATA <<<");
  } else {
    txt += format_ascii_centered("Status: " + data.status_desc);
  }
  txt += "+---------------------------------------+\n";
  return txt;
}

// ==================== 内置 Web 服务（提供画面查看与远程控制） ====================
void setup_web_server() {
  webServer.on("/", HTTP_GET, []() {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Quota Screen</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{background:#111;color:#eee;font-family:sans-serif;text-align:center;padding:20px;}";
    html += ".card{background:#222;border-radius:12px;padding:20px;max-width:340px;margin:15px auto;}";
    html += ".btn{display:inline-block;padding:8px 16px;background:#3498db;color:#fff;border-radius:6px;text-decoration:none;margin:6px;}";
    html += "</style></head><body><h2>Quota Meter ESP32</h2>";
    html += "<div class='card'>";
    html += "<p>Current Page: <b>Page " + String(current_page + 1) + " (" + ((current_page==0)?"5H":((current_page==1)?"WEEKLY":"GEMINI WEB")) + ")</b></p>";
    html += "<a href='/switch?page=1' class='btn'>Page 1 (5H)</a>";
    html += "<a href='/switch?page=2' class='btn'>Page 2 (WEEKLY)</a>";
    html += "<a href='/switch?page=3' class='btn'>Page 3 (GEMINI WEB)</a>";
    html += "<br><br><a href='/screen' class='btn' style='background:#2ecc71'>View Screen ASCII</a>";
    html += "<a href='/alarm' class='btn' style='background:#e74c3c'>Trigger Alarm Test</a>";
    html += "</div></body></html>";
    webServer.send(200, "text/html; charset=utf-8", html);
  });

  webServer.on("/switch", HTTP_GET, []() {
    if (webServer.hasArg("page")) {
      int p = webServer.arg("page").toInt();
      if (p >= 1 && p <= 3) {
        current_page = p - 1;
        last_page_switch_ms = millis();
        render_display();
      }
    }
    webServer.sendHeader("Location", "/screen");
    webServer.send(303, "text/plain", "Switched");
  });

  webServer.on("/status", HTTP_GET, []() {
    StaticJsonDocument<1024> doc;
    doc["device"] = "Waveshare-ESP32-S3-LCD-1.54";
    doc["mac"] = "80:45:6b:35:ae:ec";
    doc["ip"] = WiFi.localIP().toString();
    doc["current_page"] = current_page + 1;
    doc["page_name"] = (current_page == 0) ? "5H" : ((current_page == 1) ? "WEEKLY" : "GEMINI WEB");
    doc["gemini_5h_remaining_pct"] = gemini_5h_remaining;
    doc["gemini_5h_reset_minutes"] = gemini_5h_reset_mins;
    if (gemini_weekly_valid && gemini_weekly_remaining >= 0) {
      doc["gemini_weekly_remaining_pct"] = gemini_weekly_remaining;
      doc["gemini_weekly_reset_hours"] = gemini_weekly_reset_hrs;
    } else {
      doc["gemini_weekly_remaining_pct"] = (char*)NULL;
      doc["gemini_weekly_reset_hours"] = (char*)NULL;
    }
    doc["gemini_web_5h_remaining_pct"] = gemini_web_5h_remaining;
    doc["gemini_web_5h_reset_str"] = gemini_web_5h_reset_str;
    doc["gemini_web_weekly_remaining_pct"] = gemini_web_weekly_remaining;
    doc["gemini_web_weekly_reset_str"] = gemini_web_weekly_reset_str;
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
  // 支持 ?page=1, ?page=2, ?page=3 或 ?all=1
  webServer.on("/screen", HTTP_GET, []() {
    if (webServer.hasArg("switch")) {
      int p = webServer.arg("page").toInt();
      if (p >= 1 && p <= 3) {
        current_page = p - 1;
        last_page_switch_ms = millis();
        render_display();
      }
    }
    
    if (webServer.hasArg("all")) {
      String all_txt = get_ascii_screen_page(0) + "\n" + get_ascii_screen_page(1) + "\n" + get_ascii_screen_page(2);
      webServer.send(200, "text/plain; charset=utf-8", all_txt);
      return;
    }
    
    int target_p = current_page;
    if (webServer.hasArg("page")) {
      int p = webServer.arg("page").toInt();
      if (p >= 1 && p <= 3) target_p = p - 1;
    }
    
    webServer.send(200, "text/plain; charset=utf-8", get_ascii_screen_page(target_p));
  });

  webServer.on("/alarm", HTTP_GET, []() {
    test_alarm_flag = true;
    trigger_alarm();
    webServer.send(200, "text/plain; charset=utf-8", "Alarm triggered! Beep + clap/button listening started.");
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
  Serial.println("[HTTP] Web server started on port 80");
}

// ==================== 主流程 ====================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\n=========================================");
  Serial.println("  Antigravity Quota Meter v2.0 (3-Pages) ");
  Serial.println("=========================================");

  // 按键输入
  pinMode(BTN_MAIN,   INPUT_PULLUP);
  pinMode(BTN_VOL_UP, INPUT_PULLUP);
  pinMode(BTN_VOL_DN, INPUT_PULLUP);

  // 初始化背光与 ST7789 屏幕
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(COLOR_BG_NORMAL);

  // 屏幕启动画面 (全英文)
  gfx->setTextSize(3);
  gfx->setTextColor(COLOR_GREEN);
  gfx->setCursor(35, 80);
  gfx->print("QUOTA");
  gfx->setTextColor(COLOR_TEXT_WHITE);
  gfx->setCursor(35, 115);
  gfx->print("METER");
  gfx->setTextSize(2);
  gfx->setTextColor(COLOR_TEXT_MUTED);
  gfx->setCursor(35, 150);
  gfx->print("v2.0 3-PAGES");

  // 初始化音响与麦克风
  init_codecs();

  // 连接 Wi-Fi
  Serial.printf("[WIFI] Connecting to %s ...\n", WIFI_SSID);
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
    Serial.printf("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WIFI] Connect failed, will retry in background.");
  }

  setup_web_server();

  // 立即取数并渲染默认 Page 1 (5H)
  current_page = 0;
  last_page_switch_ms = millis();
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
    Serial.println("[TIMEOUT] >3 mins without data, screen turned gray!");
    render_display();
  }

  // 处理报警循环
  if (alarm_active) {
    handle_alarm_cycle();
  }

  // 30秒无人操作自动回第1页
  if (current_page != 0 && (millis() - last_page_switch_ms >= AUTO_RETURN_HOME_MS)) {
    Serial.println("[UI] 30s timeout, auto-returning to Page 1 (5H)");
    current_page = 0;
    render_display();
  }

  // 实体按键交互
  // 左右音量键翻页，主按键关报警
  bool btn_main = (digitalRead(BTN_MAIN) == LOW);
  bool btn_up   = (digitalRead(BTN_VOL_UP) == LOW);
  bool btn_dn   = (digitalRead(BTN_VOL_DN) == LOW);

  if (btn_main || btn_up || btn_dn) {
    if (alarm_active) {
      Serial.println("[BUTTON] Any button pressed during alarm -> stopping alarm");
      stop_alarm();
    } else {
      if (btn_up) {
        // Next page
        current_page = (current_page + 1) % 3;
        last_page_switch_ms = millis();
        Serial.printf("[BUTTON] Vol+ pressed -> Page %d\n", current_page + 1);
        render_display();
      } else if (btn_dn) {
        // Prev page
        current_page = (current_page + 2) % 3;
        last_page_switch_ms = millis();
        Serial.printf("[BUTTON] Vol- pressed -> Page %d\n", current_page + 1);
        render_display();
      } else if (btn_main) {
        Serial.println("[BUTTON] Main button pressed (alarm not active)");
      }
    }
    // 按键防抖与长按延时
    delay(250);
  }

  delay(20);
}
