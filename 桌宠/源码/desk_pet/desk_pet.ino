#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>

#include "apa102.h"
#include "buddy_cat.h"
#include "web_portal.h"

// Hardware Pinout for Pocket-Dongle-S3 (LilyGo T-Dongle-S3 Plus)
#define LCD_DC       2
#define LCD_CS       4
#define LCD_SCK      5
#define LCD_MOSI     3
#define LCD_RST      1
#define LCD_BL       38
#define BOOT_BUTTON  0

#define SCREEN_W     160
#define SCREEN_H     80

// Global instances
Apa102Led petLed;
BuddyCat catBuddy;
Preferences prefs;
WebServer apServer(80);
DNSServer dnsServer;
WiFiUDP udp;

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *tft = new Arduino_ST7735(bus, LCD_RST, 1 /* rotation */, true /* IPS */,
                                      80 /* w */, 160 /* h */,
                                      26 /* col_offset1 */, 1 /* row_offset1 */,
                                      26 /* col_offset2 */, 1 /* row_offset2 */,
                                      false /* bgr */);
Arduino_Canvas *canvas = new Arduino_Canvas(SCREEN_W, SCREEN_H, tft);

// States
enum PetState {
  STATE_SLEEP = 0,
  STATE_IDLE  = 1,
  STATE_BUSY  = 2,
  STATE_DONE  = 3
};

PetState currentState = STATE_SLEEP;
uint32_t animTick = 0;
uint32_t lastContactTime = 0;
uint32_t lastPollTime = 0;
uint32_t lastDiscoveryTime = 0;

int currentStep = 0;
String currentTool = "Ready";
String currentSummary = "Waiting task";
String xperiaIP = "";

bool inApMode = false;
bool isConnected = false;

// Pre-configured Known Networks
const char* OFFICE_SSID = "chuanliu";
const char* OFFICE_PASS = "chuanliu123";

const char* HOME_SSID   = "前置";
const char* HOME_PASS   = "qianzhi8888";

void drawFrame(PetState state, const char* stateLabel, uint16_t stateColor,
               const String& line2, const String& line3, const String& line4) {
  canvas->fillScreen(0x0000); // Black background

  // 1. Draw Pet on Left
  switch (state) {
    case STATE_SLEEP:
      catBuddy.drawSleep(canvas, animTick);
      break;
    case STATE_IDLE:
      catBuddy.drawIdle(canvas, animTick);
      break;
    case STATE_BUSY:
      catBuddy.drawBusy(canvas, animTick);
      break;
    case STATE_DONE:
      catBuddy.drawCelebrate(canvas, animTick);
      break;
  }

  // 2. Vertical separator
  canvas->drawFastVLine(74, 8, 64, 0x3186);

  // 3. Right side info
  // Line 1: State Badge
  canvas->setTextSize(1);
  canvas->setTextColor(stateColor);
  canvas->setCursor(80, 10);
  canvas->print(stateLabel);

  // Line 2: Step
  canvas->setTextColor(0xFFFF);
  canvas->setCursor(80, 28);
  canvas->print(line2);

  // Line 3: Tool / Summary
  canvas->setTextColor(0xDEE6);
  canvas->setCursor(80, 46);
  String l3 = line3;
  if (l3.length() > 11) l3 = l3.substring(0, 10) + "..";
  canvas->print(l3);

  // Line 4: Status / Network
  canvas->setTextColor(0x7BEF);
  canvas->setCursor(80, 64);
  String l4 = line4;
  if (l4.length() > 11) l4 = l4.substring(0, 10) + "..";
  canvas->print(l4);

  canvas->flush();
}

void setLedForState(PetState s) {
  switch (s) {
    case STATE_SLEEP:
      petLed.setColor(0, 15, 80, 5); // Dim Blue
      break;
    case STATE_IDLE:
      petLed.setColor(0, 80, 0, 10); // Soft Green
      break;
    case STATE_BUSY:
      petLed.setColor(160, 80, 0, 15); // Amber/Yellow
      break;
    case STATE_DONE:
      petLed.setColor(140, 0, 140, 20); // Magenta/Purple
      break;
  }
}

// ──────────────── Boot Sequence Demo ────────────────
void runBootDemo() {
  Serial.println("Starting 12-second boot demo for states: SLEEP, IDLE, BUSY, DONE");

  // State 1: SLEEP (3 seconds)
  setLedForState(STATE_SLEEP);
  uint32_t start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawFrame(STATE_SLEEP, "[SLEEP]", 0x07FF, "Step: -", "Offline", "Zzz...");
    delay(80);
  }

  // State 2: IDLE (3 seconds)
  setLedForState(STATE_IDLE);
  start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawFrame(STATE_IDLE, "[IDLE]", 0x07E0, "Step: -", "Ready", "Waiting");
    delay(80);
  }

  // State 3: BUSY (3 seconds)
  setLedForState(STATE_BUSY);
  start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawFrame(STATE_BUSY, "[BUSY]", 0xFFE0, "Step: 2", "run_cmd", "Thinking..");
    delay(80);
  }

  // State 4: DONE (3 seconds)
  setLedForState(STATE_DONE);
  start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawFrame(STATE_DONE, "[DONE]", 0xF81F, "Finished!", "All done", "Yay!");
    delay(80);
  }
}

// ──────────────── WiFi Management ────────────────
bool attemptConnect(const char* ssid, const char* pass, int timeoutSec = 7) {
  if (!ssid || strlen(ssid) == 0) return false;

  Serial.printf("Connecting to WiFi: %s ...\n", ssid);
  canvas->fillScreen(0x0000);
  canvas->setTextSize(1);
  canvas->setTextColor(0xFFE0);
  canvas->setCursor(10, 15);
  canvas->println("Connecting WiFi...");
  canvas->setTextColor(0xFFFF);
  canvas->setCursor(10, 35);
  canvas->println(ssid);
  canvas->flush();

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < timeoutSec * 1000)) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    canvas->fillScreen(0x0000);
    canvas->setTextColor(0x07E0);
    canvas->setCursor(10, 20);
    canvas->println("WiFi Connected!");
    canvas->setTextColor(0xFFFF);
    canvas->setCursor(10, 45);
    canvas->println(WiFi.localIP().toString());
    canvas->flush();
    delay(1500);
    return true;
  }
  return false;
}

void startApConfigMode() {
  inApMode = true;
  Serial.println("Entering AP Config Mode...");
  petLed.setColor(0, 100, 100, 15); // Cyan LED

  WiFi.mode(WIFI_AP);
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("Pocket-Pet-AP", "12345678");

  dnsServer.start(53, "*", apIP);
  setupApPortal();

  canvas->fillScreen(0x0000);
  canvas->setTextSize(1);
  canvas->setTextColor(0xFFE0);
  canvas->setCursor(8, 10);
  canvas->println("[AP Config Mode]");
  canvas->setTextColor(0xFFFF);
  canvas->setCursor(8, 30);
  canvas->println("SSID: Pocket-Pet-AP");
  canvas->setCursor(8, 46);
  canvas->println("Pass: 12345678");
  canvas->setTextColor(0x07FF);
  canvas->setCursor(8, 64);
  canvas->println("Web: 192.168.4.1");
  canvas->flush();
}

void connectWifiMulti() {
  prefs.begin("deskpet", true);
  String customSSID = prefs.getString("ssid", "");
  String customPass = prefs.getString("pass", "");
  xperiaIP = prefs.getString("xperia_ip", "");
  prefs.end();

  // 1. Try Custom WiFi if configured
  if (customSSID.length() > 0) {
    if (attemptConnect(customSSID.c_str(), customPass.c_str(), 8)) {
      isConnected = true;
      return;
    }
  }

  // 2. Try Office WiFi
  if (attemptConnect(OFFICE_SSID, OFFICE_PASS, 6)) {
    isConnected = true;
    return;
  }

  // 3. Try Home WiFi
  if (attemptConnect(HOME_SSID, HOME_PASS, 7)) {
    isConnected = true;
    return;
  }

  // 4. Fallback to AP Mode
  startApConfigMode();
}

// ──────────────── Discovery & Polling ────────────────
void sendDiscoveryPacket() {
  if (WiFi.status() != WL_CONNECTED) return;
  IPAddress broadcastIP = ~WiFi.subnetMask() | WiFi.gatewayIP();
  udp.beginPacket(broadcastIP, 8888);
  udp.write((const uint8_t*)"DISCOVER_DESK_PET", 17);
  udp.endPacket();
}

void checkUdpResponse() {
  int packetSize = udp.parsePacket();
  if (packetSize > 0) {
    char buf[256];
    int len = udp.read(buf, sizeof(buf) - 1);
    if (len > 0) {
      buf[len] = '\0';
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, buf) == DeserializationError::Ok) {
        if (doc.containsKey("service") && doc["service"] == "desk-pet") {
          xperiaIP = udp.remoteIP().toString();
          Serial.printf("Discovered Xperia at: %s\n", xperiaIP.c_str());
          prefs.begin("deskpet", false);
          prefs.putString("xperia_ip", xperiaIP);
          prefs.end();
        }
      }
    }
  }
}

void pollXperia() {
  if (xperiaIP.length() == 0) {
    // If we don't know the IP yet, broadcast discovery
    if (millis() - lastDiscoveryTime > 4000) {
      lastDiscoveryTime = millis();
      sendDiscoveryPacket();
    }
    return;
  }

  HTTPClient http;
  String url = "http://" + xperiaIP + ":8088/desk-pet-state.json";
  http.begin(url);
  http.setTimeout(1200);

  int httpCode = http.GET();
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      lastContactTime = millis();
      String st = doc["state"].as<String>();
      currentStep = doc["step"] | 0;
      currentTool = doc["tool"].as<String>();
      currentSummary = doc["summary"].as<String>();

      if (st == "busy") {
        currentState = STATE_BUSY;
      } else if (st == "done") {
        currentState = STATE_DONE;
      } else {
        currentState = STATE_IDLE;
      }
      static String lastStateStr = "";
      static int lastStepNum = -1;
      if (st != lastStateStr || currentStep != lastStepNum) {
        lastStateStr = st;
        lastStepNum = currentStep;
        Serial.printf("State update: %s (step %d, tool: %s)\n", st.c_str(), currentStep, currentTool.c_str());
      }
    }
  } else {
    // If request failed, periodically resend discovery in case IP changed
    if (millis() - lastDiscoveryTime > 5000) {
      lastDiscoveryTime = millis();
      sendDiscoveryPacket();
    }
  }
  http.end();
}

// ──────────────── Setup & Main Loop ────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON, INPUT_PULLUP);

  // Initialize APA102 LED
  petLed.begin();
  petLed.setColor(0, 40, 0, 5);

  // Turn ON LCD Backlight (Active LOW on GPIO 38)
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LOW);

  // Initialize Screen
  if (!tft->begin()) {
    Serial.println("TFT init failed!");
  }
  tft->setRotation(1);

  if (!canvas->begin()) {
    Serial.println("Canvas init failed!");
  }

  // 1. Run the requested 4-state boot sequence demo
  runBootDemo();

  // 2. Check if BOOT button is held down to force AP mode
  if (digitalRead(BOOT_BUTTON) == LOW) {
    delay(500);
    if (digitalRead(BOOT_BUTTON) == LOW) {
      startApConfigMode();
      return;
    }
  }

  // 3. Connect to Wi-Fi
  connectWifiMulti();
  if (isConnected) {
    udp.begin(8888);
    sendDiscoveryPacket();
    lastContactTime = millis();
    currentState = STATE_IDLE;
  }
}

void loop() {
  // If in AP Config Mode
  if (inApMode) {
    dnsServer.processNextRequest();
    apServer.handleClient();
    delay(10);
    return;
  }

  // Check BOOT button (long press > 2.5s enters AP mode)
  if (digitalRead(BOOT_BUTTON) == LOW) {
    uint32_t pressStart = millis();
    while (digitalRead(BOOT_BUTTON) == LOW) {
      delay(50);
      if (millis() - pressStart > 2500) {
        startApConfigMode();
        return;
      }
    }
  }

  // Serial command handler
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("SET_XPERIA_IP ")) {
      xperiaIP = cmd.substring(14);
      xperiaIP.trim();
      prefs.begin("deskpet", false);
      prefs.putString("xperia_ip", xperiaIP);
      prefs.end();
      Serial.printf("OK: Xperia IP set to %s\n", xperiaIP.c_str());
    } else if (cmd == "INFO") {
      Serial.printf("INFO: WiFi=%s, IP=%s, XperiaIP=%s, State=%d, Step=%d\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                    xperiaIP.c_str(), currentState, currentStep);
    } else if (cmd == "DEMO") {
      Serial.println("OK: Running boot demo");
      runBootDemo();
    } else if (cmd == "PORTAL") {
      Serial.println("OK: Entering AP portal");
      startApConfigMode();
    }
  }

  // WiFi Connection Watchdog
  if (WiFi.status() == WL_CONNECTED) {
    checkUdpResponse();

    // Poll every 1 second
    if (millis() - lastPollTime > 1000) {
      lastPollTime = millis();
      pollXperia();
    }
  }

  // 30s unreachable timeout -> SLEEP
  if (millis() - lastContactTime > 30000) {
    currentState = STATE_SLEEP;
  }

  // Update LED and Display
  setLedForState(currentState);
  animTick++;

  String stepStr = (currentStep > 0) ? ("Step: " + String(currentStep)) : "Step: -";
  String label = "";
  uint16_t color = 0xFFFF;

  switch (currentState) {
    case STATE_SLEEP:
      label = "[SLEEP]";
      color = 0x07FF;
      drawFrame(STATE_SLEEP, label.c_str(), color, stepStr, "Offline", "Zzz...");
      break;
    case STATE_IDLE:
      label = "[IDLE]";
      color = 0x07E0;
      drawFrame(STATE_IDLE, label.c_str(), color, stepStr, currentTool, "Ready");
      break;
    case STATE_BUSY:
      label = "[BUSY]";
      color = 0xFFE0;
      drawFrame(STATE_BUSY, label.c_str(), color, stepStr, currentTool, currentSummary);
      break;
    case STATE_DONE:
      label = "[DONE]";
      color = 0xF81F;
      drawFrame(STATE_DONE, label.c_str(), color, stepStr, "Complete!", "Yay!");
      break;
  }

  delay(80);
}
