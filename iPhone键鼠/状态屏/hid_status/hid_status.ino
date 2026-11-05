// Dual-mode board: plugged into a USB host (the iPhone) it is the HID bridge
// (mouse + keyboard driven over TCP, same as hid_bridge); powered by a charger
// (no USB host) it is the dispatch status screen fed by vpn-us.

#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <Arduino_GFX_Library.h>
#include "ESP_I2S.h"
#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDKeyboard.h"
#include "tusb.h"
#include "es8311.h"
#include "es7210.h"
#include "secrets.h"  // WIFI_SSID, WIFI_PASS, SERVER_HOST, SERVER_PORT
#include "fonts.h"
#include "audio_data.h"

#ifndef FORCE_STATUS
#define FORCE_STATUS 0  // 1: status screen even when a USB host is present (bench test)
#endif

// floor2 relays to the vpn-us ledger (TCP from the board straight over Tailscale is lost in the router's offload)
const char *STATUS_URL = "http://192.168.7.1/cgi-bin/paidan";
const unsigned long POLL_MS = 60000;
const unsigned long STALE_MS = 180000;
const int WATCHDOG_HID_MS = 8000;
const int WATCHDOG_STATUS_MS = 20000;

// ---- pins (Waveshare ESP32-S3-LCD-1.54) ----
#define LCD_DC 45
#define LCD_CS 21
#define LCD_SCLK 38
#define LCD_MOSI 39
#define LCD_RST 40
#define LCD_BL 46
#define I2C_SDA 42
#define I2C_SCL 41
#define I2S_MCLK 8
#define I2S_BCLK 9
#define I2S_WS 10
#define I2S_DOUT 12
#define I2S_DIN 11
#define PA_CTRL 7
#define BTN_MAIN 5
#define BTN_UP 4
#define BTN_DN 0

// =====================================================================
// HID bridge (unchanged behaviour from hid_bridge.ino)
// =====================================================================
const int STEP = 10;
const int STEP_MS = 8;
const int HOME_REPORTS = 50;

USBHIDKeyboard keyboard;
USBHIDMouse mouse;
WiFiClient server;

void movePaced(long dx, long dy) {
  while (dx || dy) {
    int sx = constrain(dx, -STEP, STEP), sy = constrain(dy, -STEP, STEP);
    mouse.move(sx, sy);
    dx -= sx;
    dy -= sy;
    delay(STEP_MS);
  }
}

uint8_t keyCode(const String &k) {
  if (k == "enter") return KEY_RETURN;
  if (k == "esc") return KEY_ESC;
  if (k == "tab") return KEY_TAB;
  if (k == "backspace") return KEY_BACKSPACE;
  if (k == "space") return ' ';
  if (k == "up") return KEY_UP_ARROW;
  if (k == "down") return KEY_DOWN_ARROW;
  if (k == "left") return KEY_LEFT_ARROW;
  if (k == "right") return KEY_RIGHT_ARROW;
  if (k == "cmd") return KEY_LEFT_GUI;
  if (k == "ctrl") return KEY_LEFT_CTRL;
  if (k == "alt") return KEY_LEFT_ALT;
  if (k == "shift") return KEY_LEFT_SHIFT;
  return k.length() == 1 ? k[0] : 0;
}

bool pressCombo(String combo) {
  combo += '+';
  for (int i; (i = combo.indexOf('+')) >= 0; combo = combo.substring(i + 1)) {
    uint8_t c = keyCode(combo.substring(0, i));
    if (!c) { keyboard.releaseAll(); return false; }
    keyboard.press(c);
  }
  delay(30);
  keyboard.releaseAll();
  return true;
}

bool usbReady() {
  if (tud_suspended()) {
    tud_remote_wakeup();
    for (int i = 0; i < 50 && tud_suspended(); i++) delay(10);
  }
  return tud_mounted() && !tud_suspended();
}

String run(const String &line) {
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  long a = 0, b = 0;
  sscanf(arg.c_str(), "%ld %ld", &a, &b);

  if (cmd == "ping") return "ok";
  if (!usbReady()) return "err phone usb asleep (unlock the phone)";
  if (cmd == "home") {
    for (int i = 0; i < HOME_REPORTS; i++) { mouse.move(-127, -127); delay(2); }
    return "ok";
  }
  if (cmd == "move") { movePaced(a, b); return "ok"; }
  if (cmd == "press") { mouse.press(MOUSE_LEFT); return "ok"; }
  if (cmd == "release") { mouse.release(MOUSE_LEFT); return "ok"; }
  if (cmd == "rclick") { mouse.press(MOUSE_RIGHT); delay(60); mouse.release(MOUSE_RIGHT); return "ok"; }
  if (cmd == "mclick") { mouse.press(MOUSE_MIDDLE); delay(60); mouse.release(MOUSE_MIDDLE); return "ok"; }
  if (cmd == "click") { mouse.press(MOUSE_LEFT); delay(60); mouse.release(MOUSE_LEFT); return "ok"; }
  if (cmd == "scroll") { mouse.move(0, 0, a, b); return "ok"; }
  if (cmd == "type") { keyboard.print(arg); return "ok"; }
  if (cmd == "key") return pressCombo(arg) ? "ok" : "err unknown key";
  return "err unknown command";
}

const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_TASK_WDT: case ESP_RST_INT_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_SW: return "software";
    case ESP_RST_USB: return "usb";
    default: return "other";
  }
}

void hidLoop() {
  if (WiFi.status() != WL_CONNECTED) { mouse.release(MOUSE_LEFT); delay(50); return; }
  if (!server.connected()) {
    mouse.release(MOUSE_LEFT);
    keyboard.releaseAll();
    if (!server.connect(SERVER_HOST, SERVER_PORT, 1000)) { delay(200); return; }
    server.setNoDelay(true);
    server.printf("hello %s %lu\n", resetReason(), millis());
  }
  if (server.available()) {
    String line = server.readStringUntil('\n');
    line.trim();
    if (!line.length()) return;
    server.print(run(line) + "\n");
  }
}

// =====================================================================
// Status screen
// =====================================================================
#define W 240
#define H 240
#define HEAD 28
#define ROW 53

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t C_BG = rgb(14, 15, 18);
const uint16_t C_BG_WAIT = rgb(46, 24, 4);
const uint16_t C_LINE = rgb(34, 36, 42);
const uint16_t C_MUTED = rgb(128, 132, 142);
const uint16_t C_WHITE = rgb(236, 238, 242);
const uint16_t C_FADED = rgb(78, 81, 88);
const uint16_t C_HEAD = rgb(24, 26, 31);
const uint16_t C_HEAD_STALE = rgb(70, 72, 78);
const uint16_t C_HEAD_WAIT_FG = rgb(30, 16, 0);
const uint16_t C_WAIT = rgb(255, 159, 28);
const uint16_t C_STUCK = rgb(255, 90, 95);
const uint16_t C_RUN = rgb(61, 220, 151);
const uint16_t C_DONE = rgb(139, 143, 247);

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, -1);
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RST, 0, true, W, H);
uint16_t *fb = nullptr;

void fill(int x0, int y0, int x1, int y1, uint16_t c) {  // inclusive
  x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, W - 1); y1 = min(y1, H - 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) fb[y * W + x] = c;
}

static inline uint16_t blend(uint16_t bg, uint16_t fg, int a) {  // a 0..15
  if (a >= 15) return fg;
  if (a <= 0) return bg;
  int br = bg >> 11, bgc = (bg >> 5) & 63, bb = bg & 31;
  int fr = fg >> 11, fgc = (fg >> 5) & 63, fbb = fg & 31;
  return ((br + (fr - br) * a / 15) << 11) | ((bgc + (fgc - bgc) * a / 15) << 5) | (bb + (fbb - bb) * a / 15);
}

const Glyph *findGlyph(const Font &f, uint32_t cp) {
  int lo = 0, hi = f.n - 1;
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    if (f.cp[m] == cp) return &f.g[m];
    if (f.cp[m] < cp) lo = m + 1; else hi = m - 1;
  }
  return nullptr;
}

uint32_t nextCp(const char *&s) {
  uint8_t c = *s++;
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
  uint32_t cp = c & (0x3F >> n);
  while (n-- && (*s & 0xC0) == 0x80) cp = (cp << 6) | (*s++ & 0x3F);
  return cp;
}

const Glyph *glyphFor(const Font &f, const Font *fb2, uint32_t cp, const Font **used) {
  const Glyph *g = findGlyph(f, cp);
  *used = &f;
  if (!g && fb2) { g = findGlyph(*fb2, cp); *used = fb2; }
  if (!g) { g = findGlyph(f, '?'); *used = &f; }
  return g;
}

int textWidth(const Font &f, const String &s, const Font *fb2 = nullptr) {
  int w = 0;
  const char *p = s.c_str();
  const Font *u;
  while (*p) { const Glyph *g = glyphFor(f, fb2, nextCp(p), &u); if (g) w += g->adv; }
  return w;
}

// y is the vertical middle of the line (PIL anchor "m"); returns pen x after the text
int drawText(const Font &f, int x, int y, const String &s, uint16_t color, const Font *fb2 = nullptr) {
  const char *p = s.c_str();
  while (*p) {
    const Font *u;
    const Glyph *g = glyphFor(f, fb2, nextCp(p), &u);
    if (!g) continue;
    int base = y + u->mid;
    const uint8_t *bm = u->bm + g->off;
    int stride = (g->w + 1) / 2;
    for (int r = 0; r < g->h; r++) {
      int py = base + g->dy + r;
      if (py < 0 || py >= H) continue;
      for (int c = 0; c < g->w; c++) {
        int px = x + g->dx + c;
        if (px < 0 || px >= W) continue;
        uint8_t v = bm[r * stride + c / 2];
        int a = (c & 1) ? (v & 15) : (v >> 4);
        if (a) fb[py * W + px] = blend(fb[py * W + px], color, a);
      }
    }
    x += g->adv;
  }
  return x;
}

String fit(const Font &f, String s, int width, const Font *fb2 = nullptr) {
  if (textWidth(f, s, fb2) <= width) return s;
  // drop whole UTF-8 characters from the end until "…" fits
  while (s.length()) {
    int cut = s.length() - 1;
    while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;
    s = s.substring(0, cut);
    if (textWidth(f, s + "…", fb2) <= width) break;
  }
  return s + "…";
}

String dur(long sec) {
  long m = sec / 60;
  if (m < 0) m = 0;
  char buf[24];
  if (m < 60) snprintf(buf, sizeof buf, "%ld分", m);
  else if (m < 1440) snprintf(buf, sizeof buf, "%ld时%02ld", m / 60, m % 60);
  else snprintf(buf, sizeof buf, "%ld天", m / 1440);
  return buf;
}

struct Row { String state, worker, task, why; long age; bool faded; };
Row rows[4];
int nRows = 0, nWaiting = 0, nExtra = 0;
String countStr = "-/-";
bool haveData = false, muted = false;
unsigned long lastOk = 0, lastPoll = 0;
bool shownStale = false;

uint16_t stateColor(const String &s) {
  if (s == "等你") return C_WAIT;
  if (s == "卡住") return C_STUCK;
  if (s == "完成") return C_DONE;
  return C_RUN;
}

void render() {
  bool stale = haveData && millis() - lastOk > STALE_MS;
  fill(0, 0, W - 1, H - 1, nWaiting && !stale ? C_BG_WAIT : C_BG);

  uint16_t hbg = C_HEAD, hfg = C_MUTED;
  String left;
  int stuck = 0, running = 0;
  for (int i = 0; i < nRows; i++) {
    if (rows[i].state == "卡住") stuck++;
    if (rows[i].state == "在跑") running++;
  }
  if (!haveData) left = "连接中";
  else if (stale) { hbg = C_HEAD_STALE; hfg = C_WHITE; left = "过时"; }
  else if (nWaiting) { hbg = C_WAIT; hfg = C_HEAD_WAIT_FG; left = "等你 " + String(nWaiting); }
  else if (stuck) { hfg = C_STUCK; left = "卡住 " + String(stuck); }
  else left = running ? "在跑 " + String(running) : "派单";
  fill(0, 0, W - 1, HEAD - 1, hbg);
  drawText(F_HEAD, 10, HEAD / 2, left, hfg);
  String right = countStr;
  int rx = W - 10 - textWidth(F_HEAD, right);
  drawText(F_HEAD, rx, HEAD / 2, right, hfg);
  if (muted) drawText(F_HEAD, rx - 8 - textWidth(F_HEAD, "静音"), HEAD / 2, "静音", hfg);

  if (!nRows && haveData) {
    String e = "没有在跑的单";
    drawText(F_EMPTY, W / 2 - textWidth(F_EMPTY, e) / 2, HEAD + (H - HEAD) / 2, e, C_FADED);
  }
  for (int i = 0; i < nRows; i++) {
    Row &r = rows[i];
    int y = HEAD + i * ROW;
    if (i) fill(12, y, W - 12, y, C_LINE);
    uint16_t col = r.faded ? C_FADED : stateColor(r.state);
    fill(8, y + 10, 12, y + ROW - 10, col);
    fill(9, y + 9, 11, y + ROW - 9, col);

    int x = 22;
    int px = drawText(F_SMALLB, x, y + 14, r.worker, r.faded ? C_FADED : C_WHITE, &F_SMALL);
    drawText(F_SMALL, px + 6, y + 14, r.state, col);
    String d = dur(r.age);
    drawText(F_SMALL, W - 10 - textWidth(F_SMALL, d), y + 14, d, r.faded ? C_FADED : C_MUTED);

    String big = r.task, sub;
    uint16_t bigCol = r.faded ? C_FADED : C_WHITE;
    if (r.state == "等你" && r.why.length()) { big = r.why; bigCol = C_WAIT; sub = r.task; }
    big = fit(F_BIG, big, W - x - 10);
    int bx = drawText(F_BIG, x, y + 36, big, bigCol);
    if (sub.length()) {
      int room = W - 10 - (bx + 8);
      if (room > 30) drawText(F_SMALL, bx + 8, y + 38, fit(F_SMALL, sub, room), C_MUTED);
    }
  }
  gfx->draw16bitRGBBitmap(0, 0, fb, W, H);
  shownStale = stale;
}

// ---- sound ----
I2SClass i2s;
es8311_handle_t es8311 = nullptr;
es7210_dev_handle_t es7210 = nullptr;
bool audioOk = false, micOk = false;

void initAudio() {
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, LOW);
  es8311 = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
  if (es8311) {
    const es8311_clock_config_t clk = {false, false, true, 16000 * 256, 16000};
    if (es8311_init(es8311, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) == ESP_OK) {
      es8311_sample_frequency_config(es8311, 16000 * 256, 16000);
      es8311_voice_volume_set(es8311, 85, NULL);
      audioOk = true;
    }
  }
  es7210_i2c_config_t mc = {.i2c_addr = 0x40};
  if (es7210_new_codec(&mc, &es7210) == ESP_OK) {
    es7210_codec_config_t cc = {};
    cc.i2s_format = ES7210_I2S_FMT_I2S;
    cc.mclk_ratio = 256;
    cc.sample_rate_hz = 16000;
    cc.bit_width = ES7210_I2S_BITS_16B;
    cc.mic_bias = ES7210_MIC_BIAS_2V87;
    cc.mic_gain = ES7210_MIC_GAIN_30DB;
    cc.flags.tdm_enable = false;
    if (es7210_config_codec(es7210, &cc) == ESP_OK) { es7210_config_volume(es7210, 50); micOk = true; }
  }
  i2s.setPins(I2S_BCLK, I2S_WS, I2S_DOUT, I2S_DIN, I2S_MCLK);
  i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
}

bool anyButton() {
  return digitalRead(BTN_MAIN) == LOW || digitalRead(BTN_UP) == LOW || digitalRead(BTN_DN) == LOW;
}

void silence(int ms) {
  int16_t buf[128 * 2] = {0};
  int total = 16000 * ms / 1000;
  for (int done = 0; done < total; done += 128) i2s.write((uint8_t *)buf, min(128, total - done) * 4);
}

// play recorded wav audio. Returns true if aborted early by button press.
bool playPattern(char kind) {
  if (!audioOk) return false;
  const int16_t *pcm = nullptr;
  int total = 0;
  if (kind == 'd') { pcm = audio_done; total = AUDIO_DONE_LEN; }
  else if (kind == 'w') { pcm = audio_wait; total = AUDIO_WAIT_LEN; }
  else if (kind == 's') { pcm = audio_stuck; total = AUDIO_STUCK_LEN; }
  else return false;

  digitalWrite(PA_CTRL, HIGH);
  silence(30);

  bool stopped = false;
  int16_t frame[128 * 2];
  for (int done = 0; done < total;) {
    esp_task_wdt_reset();
    if (anyButton()) { stopped = true; break; }
    int n = min(128, total - done);
    for (int i = 0; i < n; i++) {
      int16_t s = pcm[done + i];
      frame[i * 2] = frame[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)frame, n * 4);
    done += n;
  }

  silence(stopped ? 50 : 250);  // drain DMA before amp turns off
  digitalWrite(PA_CTRL, LOW);
  esp_task_wdt_reset();
  return stopped;
}

// listen for a clap or a button press for ms; true if the person reacted
bool listenStop(int ms) {
  int16_t buf[128 * 2];
  unsigned long t0 = millis();
  while (millis() - t0 < 250) {  // skip the tail of our own sound
    if (micOk) i2s.readBytes((char *)buf, sizeof buf); else delay(10);
    if (anyButton()) return true;
  }
  while (millis() - t0 < (unsigned long)ms) {
    esp_task_wdt_reset();
    if (anyButton()) return true;
    if (!micOk) { delay(10); continue; }
    size_t br = i2s.readBytes((char *)buf, sizeof buf);
    for (size_t i = 0; i < br / 4; i++)
      if (abs(buf[i * 2]) > 12000) return true;
  }
  return false;
}

void alarm(char kind) {
  if (muted || !kind) return;
  if (kind == 'd') { playPattern('d'); return; }
  unsigned long t0 = millis();
  while (millis() - t0 < 60000) {
    if (playPattern(kind)) break;
    if (listenStop(1000)) break;
  }
  while (anyButton()) { delay(10); esp_task_wdt_reset(); }  // the stopping press must not also toggle mute
  delay(150);
}

// ---- data ----
const unsigned long REMIND_INTERVAL_MS = 10UL * 60UL * 1000UL;  // 10 minutes

struct Seen {
  int id;
  char code;
  unsigned long enterTime;
  bool reminded;
};
Seen seen[64];
int nSeen = 0;
bool firstFetch = true;

char eventFrom(const String &list) {
  Seen now[64];
  int n = 0;
  char ev = 0;
  int start = 0;
  while (start < (int)list.length() && n < 64) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String item = list.substring(start, comma);
    int colon = item.indexOf(':');
    if (colon > 0) {
      int id = item.substring(0, colon).toInt();
      char code = item[colon + 1];
      char before = 0;
      unsigned long enterTime = 0;
      bool reminded = false;
      bool found = false;

      for (int i = 0; i < nSeen; i++) {
        if (seen[i].id == id) {
          before = seen[i].code;
          enterTime = seen[i].enterTime;
          reminded = seen[i].reminded;
          found = true;
          break;
        }
      }

      if (!found || before != code) {
        enterTime = (code == 'w' || code == 's') ? millis() : 0;
        reminded = false;
        if (!firstFetch && before != code) {
          int rank = code == 'w' ? 3 : code == 's' ? 2 : code == 'd' ? 1 : 0;
          int cur = ev == 'w' ? 3 : ev == 's' ? 2 : ev == 'd' ? 1 : 0;
          if (rank > cur) ev = code;
        }
      } else {
        if ((code == 'w' || code == 's') && !reminded) {
          if (millis() - enterTime >= REMIND_INTERVAL_MS) {
            reminded = true;
            Serial.printf("[STATUS] Order %d (%c) 10m reminder triggered\n", id, code);
            int rank = code == 'w' ? 3 : code == 's' ? 2 : code == 'd' ? 1 : 0;
            int cur = ev == 'w' ? 3 : ev == 's' ? 2 : ev == 'd' ? 1 : 0;
            if (rank > cur) ev = code;
          }
        }
      }

      now[n++] = {id, code, enterTime, reminded};
    }
    start = comma + 1;
  }
  memcpy(seen, now, sizeof(Seen) * n);
  nSeen = n;
  firstFetch = false;
  return ev;
}

String field(const String &line, int idx) {
  int s = 0;
  for (int i = 0; i < idx; i++) { s = line.indexOf('\t', s); if (s < 0) return ""; s++; }
  int e = line.indexOf('\t', s);
  return e < 0 ? line.substring(s) : line.substring(s, e);
}

char poll() {
  wl_status_t ws = WiFi.status();
  if (ws != WL_CONNECTED) {
    Serial.printf("[STATUS] Wi-Fi status: %d (not connected)\n", (int)ws);
    return 0;
  }
  Serial.printf("[STATUS] Wi-Fi status: %d (connected, IP: %s)\n", (int)ws, WiFi.localIP().toString().c_str());
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(STATUS_URL)) {
    Serial.println("[STATUS] http.begin failed");
    return 0;
  }
  int code = http.GET();
  String body = code == 200 ? http.getString() : "";
  int len = body.length();
  Serial.printf("[STATUS] HTTP code: %d, body length: %d\n", code, len);
  http.end();
  esp_task_wdt_reset();
  if (code != 200) return 0;

  char ev = 0;
  int ln = 0, pos = 0, r = 0;
  while (pos < (int)body.length()) {
    int e = body.indexOf('\n', pos);
    if (e < 0) e = body.length();
    String line = body.substring(pos, e);
    pos = e + 1;
    if (!line.length()) continue;
    if (ln == 0) { countStr = field(line, 0); nWaiting = field(line, 1).toInt(); nExtra = field(line, 2).toInt(); }
    else if (line.startsWith("E\t")) ev = eventFrom(line.substring(2));
    else if (r < 4) {
      rows[r] = {field(line, 0), field(line, 1), field(line, 2), field(line, 3), field(line, 4).toInt(), field(line, 5) == "1"};
      r++;
    }
    ln++;
  }
  nRows = r;
  haveData = true;
  lastOk = millis();
  return ev;
}

Preferences prefs;
bool mainWasDown = false;

void statusSetup() {
  Serial.begin(115200);
  pinMode(BTN_MAIN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DN, INPUT_PULLUP);
  prefs.begin("status", false);
  muted = prefs.getBool("muted", false);
  fb = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_8BIT);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->begin();
  initAudio();
  render();
}

void statusLoop() {
  bool down = digitalRead(BTN_MAIN) == LOW;
  if (down && !mainWasDown) {
    muted = !muted;
    prefs.putBool("muted", muted);
    render();
    if (!muted) playPattern('d');
  }
  mainWasDown = down;

  if (!lastPoll || millis() - lastPoll >= (haveData ? POLL_MS : 5000UL)) {
    lastPoll = millis();
    char ev = poll();
    render();
    alarm(ev);
  } else if (!shownStale && haveData && millis() - lastOk > STALE_MS) {
    render();
  }
  delay(20);
}

// =====================================================================
bool hidMode = false;

void setup() {
  keyboard.begin();
  mouse.begin();
  USB.productName("HID Bridge");
  USB.begin();
  esp_task_wdt_config_t wdt = {.timeout_ms = WATCHDOG_HID_MS, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt);
  esp_task_wdt_add(NULL);

  for (int i = 0; i < 30 && !tud_mounted(); i++) delay(100);  // a USB host shows up within ~3 s
  hidMode = tud_mounted() && !FORCE_STATUS;

  WiFi.setHostname("hid-bridge");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  if (hidMode) {
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, LOW);
  } else {
    esp_task_wdt_config_t wdt_status = {.timeout_ms = WATCHDOG_STATUS_MS, .idle_core_mask = 0, .trigger_panic = true};
    esp_task_wdt_reconfigure(&wdt_status);
    statusSetup();
  }
}

void loop() {
  esp_task_wdt_reset();
  if (hidMode) hidLoop(); else statusLoop();
}
