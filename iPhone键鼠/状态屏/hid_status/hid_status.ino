// Waveshare ESP32-S3-LCD-1.54 dispatch status screen fed by vpn-us via floor2.

#include <math.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include <Arduino_GFX_Library.h>
#include "ESP_I2S.h"
#include "es8311.h"
#include "es7210.h"
#include "secrets.h"  // WIFI_SSID, WIFI_PASS
#include "fonts.h"
#include "audio_data.h"

// floor2 relays to the vpn-us ledger (TCP from the board straight over Tailscale is lost in the router's offload)
const char *STATUS_URL = "http://192.168.7.1/cgi-bin/paidan";
const unsigned long POLL_MS = 60000;
const unsigned long STALE_MS = 180000;
const int WATCHDOG_STATUS_MS = 25000;

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
// Display
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
int nRows = 0, nWaiting = 0;
String countStr = "-/-";
bool haveData = false, muted = false;
unsigned long lastOk = 0, lastPoll = 0;
bool shownStale = false;

// Configurable parameters pushed by vpn-us (/screen line 0 col 2)
int cfgWaitRepeats = 3;
int cfgStuckRepeats = 2;
int cfgTtsRepeats = 2;
int cfgQuietMin = 20;
int cfgMicThresh = 200;

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

// =====================================================================
// Sound & Microphone
// =====================================================================
I2SClass i2s;
es8311_handle_t es8311 = nullptr;
es7210_dev_handle_t es7210 = nullptr;
bool audioOk = false, micOk = false;

uint8_t *ttsBuf = nullptr;
size_t ttsCap = 0;
unsigned long speakerOffMs = 0;

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
  Serial.printf("[AUDIO] speaker=%d mic=%d psram=%u freeHeap=%u\n",
                audioOk, micOk, (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap());
}

bool anyButton() {
  return digitalRead(BTN_MAIN) == LOW || digitalRead(BTN_UP) == LOW || digitalRead(BTN_DN) == LOW;
}

void flushMic() {
  if (!micOk) return;
  int16_t buf[256 * 2];
  for (int i = 0; i < 8; i++) {
    esp_task_wdt_reset();
    i2s.readBytes((char *)buf, sizeof buf);
  }
}

void silence(int ms) {
  int16_t buf[160 * 2] = {0};
  int chunks = (ms + 9) / 10;
  for (int c = 0; c < chunks; c++) {
    esp_task_wdt_reset();
    i2s.write((uint8_t *)buf, sizeof buf);
  }
}

bool silenceAbortable(int ms) {
  int16_t buf[160 * 2] = {0};
  int chunks = (ms + 9) / 10;
  for (int c = 0; c < chunks; c++) {
    esp_task_wdt_reset();
    if (anyButton()) return true;
    i2s.write((uint8_t *)buf, sizeof buf);
  }
  return false;
}

bool playPcmChunk(const int16_t *pcm, int totalSamples) {
  if (!audioOk || !pcm || totalSamples <= 0) return false;
  bool stopped = false;
  int16_t frame[128 * 2];
  for (int done = 0; done < totalSamples;) {
    esp_task_wdt_reset();
    if (anyButton()) { stopped = true; break; }
    int n = min(128, totalSamples - done);
    for (int i = 0; i < n; i++) {
      int16_t s = pcm[done + i];
      frame[i * 2] = frame[i * 2 + 1] = s;
    }
    i2s.write((uint8_t *)frame, n * 4);
    done += n;
  }
  return stopped;
}

void playDoneOnce() {
  if (!audioOk) return;
  digitalWrite(PA_CTRL, HIGH);
  silence(30);
  bool stopped = playPcmChunk(audio_done, AUDIO_DONE_LEN);
  silence(stopped ? 50 : 30);
  digitalWrite(PA_CTRL, LOW);
  speakerOffMs = millis();
  flushMic();
}

class PcmSink : public Stream {
  uint8_t *buf;
  size_t cap, len;
 public:
  PcmSink(uint8_t *b, size_t c) : buf(b), cap(c), len(0) {}
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t *data, size_t n) override {
    esp_task_wdt_reset();
    if (!buf || len >= cap) return n;
    size_t take = min(n, cap - len);
    memcpy(buf + len, data, take);
    len += take;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  size_t size() const { return len & ~1U; }
};

size_t fetchTts(int orderId) {
  if (!ttsBuf || orderId <= 0 || WiFi.status() != WL_CONNECTED) return 0;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  String url = String(STATUS_URL) + "?tts=" + String(orderId);
  if (!http.begin(url)) return 0;
  int code = http.GET();
  size_t got = 0;
  if (code == 200) {
    PcmSink sink(ttsBuf, ttsCap);
    int wr = http.writeToStream(&sink);
    got = sink.size();
    Serial.printf("[TTS] id=%d http=%d write=%d bytes=%u\n", orderId, code, wr, (unsigned)got);
  } else {
    Serial.printf("[TTS] id=%d http=%d failed\n", orderId, code);
  }
  http.end();
  esp_task_wdt_reset();
  return got;
}

void alarm(char kind, int waitId, size_t ttsBytes) {
  if (muted || !kind || !audioOk) return;
  if (kind == 'd') {
    playDoneOnce();
    while (anyButton()) { delay(10); esp_task_wdt_reset(); }
    delay(150);
    return;
  }

  digitalWrite(PA_CTRL, HIGH);
  bool stopped = silenceAbortable(30);

  if (kind == 'w') {
    for (int i = 0; i < cfgWaitRepeats && !stopped; i++) {
      if (i > 0) stopped = silenceAbortable(1000);
      if (!stopped) stopped = playPcmChunk(audio_wait, AUDIO_WAIT_LEN);
    }
    if (!stopped && ttsBytes > 0 && cfgTtsRepeats > 0) {
      stopped = silenceAbortable(500);
      int samples = (int)(ttsBytes / 2);
      const int16_t *pcm = (const int16_t *)ttsBuf;
      for (int i = 0; i < cfgTtsRepeats && !stopped; i++) {
        if (i > 0) stopped = silenceAbortable(600);
        if (!stopped) stopped = playPcmChunk(pcm, samples);
      }
    }
  } else if (kind == 's') {
    for (int i = 0; i < cfgStuckRepeats && !stopped; i++) {
      if (i > 0) stopped = silenceAbortable(1000);
      if (!stopped) stopped = playPcmChunk(audio_stuck, AUDIO_STUCK_LEN);
    }
  }

  silence(stopped ? 50 : 30);
  digitalWrite(PA_CTRL, LOW);
  speakerOffMs = millis();
  flushMic();
  while (anyButton()) { delay(10); esp_task_wdt_reset(); }
  delay(150);
}

// =====================================================================
// Ledger polling & parsing
// =====================================================================
struct Seen {
  int id;
  char code;
};
Seen seen[64];
int nSeen = 0;
bool firstFetch = true;

char eventFrom(const String &list, int &outWaitId) {
  Seen now[64];
  int n = 0;
  char ev = 0;
  outWaitId = 0;
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
      bool found = false;

      for (int i = 0; i < nSeen; i++) {
        if (seen[i].id == id) {
          before = seen[i].code;
          found = true;
          break;
        }
      }

      if (!firstFetch && (!found || before != code)) {
        int rank = code == 'w' ? 3 : code == 's' ? 2 : code == 'd' ? 1 : 0;
        int cur = ev == 'w' ? 3 : ev == 's' ? 2 : ev == 'd' ? 1 : 0;
        if (rank > cur) {
          ev = code;
          if (code == 'w') outWaitId = id;
        }
      }

      now[n++] = {id, code};
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

void parseConfig(const String &s) {
  int vals[5];
  int idx = 0, pos = 0;
  while (idx < 5 && pos <= (int)s.length()) {
    int comma = s.indexOf(',', pos);
    if (comma < 0) comma = s.length();
    String part = s.substring(pos, comma);
    if (!part.length()) return;
    vals[idx++] = part.toInt();
    pos = comma + 1;
  }
  if (idx != 5 || vals[3] < 1 || vals[4] < 1) return;
  if (vals[0] != cfgWaitRepeats || vals[1] != cfgStuckRepeats ||
      vals[2] != cfgTtsRepeats || vals[3] != cfgQuietMin || vals[4] != cfgMicThresh) {
    cfgWaitRepeats = max(0, vals[0]);
    cfgStuckRepeats = max(0, vals[1]);
    cfgTtsRepeats = max(0, vals[2]);
    cfgQuietMin = vals[3];
    cfgMicThresh = vals[4];
    Serial.printf("[CFG] wait=%d stuck=%d tts=%d quiet=%dm thresh=%d\n",
                  cfgWaitRepeats, cfgStuckRepeats, cfgTtsRepeats, cfgQuietMin, cfgMicThresh);
  }
}

char poll(int &outWaitId) {
  outWaitId = 0;
  wl_status_t ws = WiFi.status();
  if (ws != WL_CONNECTED) {
    Serial.printf("[STATUS] Wi-Fi status: %d (not connected)\n", (int)ws);
    return 0;
  }
  Serial.printf("[STATUS] Wi-Fi connected (IP: %s)\n", WiFi.localIP().toString().c_str());
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
    if (ln == 0) {
      countStr = field(line, 0);
      nWaiting = field(line, 1).toInt();
      parseConfig(field(line, 2));
    } else if (line.startsWith("E\t")) {
      ev = eventFrom(line.substring(2), outWaitId);
    } else if (r < 4) {
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

// =====================================================================
// Microphone auto-mute & main loop
// =====================================================================
Preferences prefs;
bool mainWasDown = false;
bool autoQuiet = false;
unsigned long lastLoudMs = 0;
unsigned long lastMicLogMs = 0;

// Accumulator over 12 chunks * 256 frames (~192ms at 16kHz)
const int MIC_CHUNK_FRAMES = 256;
const int MIC_WIN_CHUNKS = 12;
const int MIC_WIN_SAMPLES = MIC_CHUNK_FRAMES * MIC_WIN_CHUNKS;
int16_t micL[MIC_WIN_SAMPLES];
int16_t micR[MIC_WIN_SAMPLES];
int micChunkIdx = 0;

void processMicChunk() {
  if (!micOk) return;
  int16_t buf[MIC_CHUNK_FRAMES * 2];
  size_t br = i2s.readBytes((char *)buf, sizeof buf);
  int frames = br / 4;
  if (frames <= 0) return;
  if (millis() - speakerOffMs <= 600) {
    micChunkIdx = 0;
    return;
  }

  int base = micChunkIdx * MIC_CHUNK_FRAMES;
  for (int i = 0; i < frames && base + i < MIC_WIN_SAMPLES; i++) {
    micL[base + i] = buf[i * 2];
    micR[base + i] = buf[i * 2 + 1];
  }
  micChunkIdx++;
  if (micChunkIdx < MIC_WIN_CHUNKS) return;
  micChunkIdx = 0;

  int n = MIC_WIN_SAMPLES;
  int64_t sumL = 0, sumR = 0;
  for (int i = 0; i < n; i++) { sumL += micL[i]; sumR += micR[i]; }
  int32_t meanL = (int32_t)(sumL / n);
  int32_t meanR = (int32_t)(sumR / n);

  uint64_t sqL = 0, sqR = 0;
  int peak = 0;
  for (int i = 0; i < n; i++) {
    int32_t dl = micL[i] - meanL;
    int32_t dr = micR[i] - meanR;
    sqL += (uint64_t)(dl * dl);
    sqR += (uint64_t)(dr * dr);
    int al = dl < 0 ? -dl : dl;
    int ar = dr < 0 ? -dr : dr;
    if (al > peak) peak = al;
    if (ar > peak) peak = ar;
  }
  int rmsL = (int)sqrtf((float)sqL / n);
  int rmsR = (int)sqrtf((float)sqR / n);
  int level = max(rmsL, rmsR);

  unsigned long nowMs = millis();
  unsigned long quietMs = (unsigned long)cfgQuietMin * 60UL * 1000UL;

  if (level >= cfgMicThresh) {
    lastLoudMs = nowMs;
    if (autoQuiet) {
      autoQuiet = false;
      if (muted) {
        muted = false;
        prefs.putBool("muted", false);
        render();
      }
      Serial.printf("[MIC] Sound detected (rms=%d >= %d) -> auto unmute\n", level, cfgMicThresh);
    }
  } else if (!autoQuiet && (nowMs - lastLoudMs >= quietMs)) {
    autoQuiet = true;
    if (!muted) {
      muted = true;
      prefs.putBool("muted", true);
      render();
    }
    Serial.printf("[MIC] Quiet for %d min -> auto mute\n", cfgQuietMin);
  }

  if (nowMs - lastMicLogMs >= 2000) {
    lastMicLogMs = nowMs;
    Serial.printf("[MIC] rms=%d (L=%d R=%d) peak=%d thresh=%d quiet=%lus/%lus autoQuiet=%d muted=%d\n",
                  level, rmsL, rmsR, peak, cfgMicThresh,
                  (nowMs - lastLoudMs) / 1000UL, quietMs / 1000UL,
                  autoQuiet ? 1 : 0, muted ? 1 : 0);
  }
}

void statusSetup() {
  pinMode(BTN_MAIN, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DN, INPUT_PULLUP);
  prefs.begin("status", false);
  muted = prefs.getBool("muted", false);
  fb = (uint16_t *)heap_caps_malloc(W * H * 2, MALLOC_CAP_8BIT);
  if (psramFound()) {
    ttsCap = 512 * 1024;
    ttsBuf = (uint8_t *)ps_malloc(ttsCap);
  } else {
    ttsCap = 96 * 1024;
    ttsBuf = (uint8_t *)malloc(ttsCap);
  }
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  gfx->begin();
  initAudio();
  lastLoudMs = millis();
  render();
}

void statusLoop() {
  bool down = digitalRead(BTN_MAIN) == LOW;
  if (down && !mainWasDown) {
    muted = !muted;
    prefs.putBool("muted", muted);
    if (!muted) {
      autoQuiet = false;
      lastLoudMs = millis();
    }
    render();
    if (!muted) playDoneOnce();
  }
  mainWasDown = down;

  if (!lastPoll || millis() - lastPoll >= (haveData ? POLL_MS : 5000UL)) {
    lastPoll = millis();
    int waitId = 0;
    char ev = poll(waitId);
    size_t ttsBytes = 0;
    if (ev == 'w' && waitId > 0 && !muted && cfgTtsRepeats > 0) {
      ttsBytes = fetchTts(waitId);
    }
    render();
    alarm(ev, waitId, ttsBytes);
    flushMic();
  } else if (!shownStale && haveData && millis() - lastOk > STALE_MS) {
    render();
  }

  if (micOk) processMicChunk();
  else delay(20);
}

void setup() {
  Serial.begin(115200);
  esp_task_wdt_config_t wdt = {.timeout_ms = WATCHDOG_STATUS_MS, .idle_core_mask = 0, .trigger_panic = true};
  esp_task_wdt_reconfigure(&wdt);
  esp_task_wdt_add(NULL);

  WiFi.setHostname("paidan-status");
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  statusSetup();
}

void loop() {
  esp_task_wdt_reset();
  statusLoop();
}
