#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Arduino_GFX_Library.h>

#include "apa102.h"
#include "robot_eyes.h"
#include "web_portal.h"

// Hardware Pinout for Pocket-Dongle-S3
#define LCD_MOSI     11
#define LCD_SCLK     10
#define LCD_CS       12
#define LCD_DC       13
#define LCD_RST      14
#define BOOT_BUTTON  0

#define SCREEN_W     160
#define SCREEN_H     80

// Global instances
Apa102Led petLed;
RobotEyes robotEyes;
Preferences prefs;
WebServer apServer(80);
DNSServer dnsServer;
WiFiUDP udp;

Arduino_DataBus *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
Arduino_GFX *tft = new Arduino_ST7735(bus, LCD_RST, 1 /* rotation */, true /* IPS */,
                                      80 /* w */, 160 /* h */,
                                      26 /* col_offset1 */, 1 /* row_offset1 */,
                                      26 /* col_offset2 */, 1 /* row_offset2 */,
                                      true /* bgr = true to fix color channels */);
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

const char* HOME_SSID   = "qianzhi";
const char* HOME_PASS   = "qianzhi8888";

void drawEyesFrame(PetState state, int step = 0) {
  canvas->fillScreen(0x0000); // Black background

  switch (state) {
    case STATE_SLEEP:
      robotEyes.drawSleep(canvas, animTick);
      break;
    case STATE_IDLE:
      robotEyes.drawIdle(canvas, animTick);
      break;
    case STATE_BUSY:
      robotEyes.drawBusy(canvas, animTick, step);
      break;
    case STATE_DONE:
      robotEyes.drawCelebrate(canvas, animTick);
      break;
  }

  canvas->flush();
}

void setLedForState(PetState s) {
  switch (s) {
    case STATE_SLEEP:
      petLed.setColor(0, 15, 70, 5); // Dim Blue
      break;
    case STATE_IDLE:
      petLed.setColor(0, 180, 0, 12); // Green
      break;
    case STATE_BUSY:
      petLed.setColor(200, 140, 0, 15); // Yellow
      break;
    case STATE_DONE:
      petLed.setColor(180, 0, 180, 15); // Purple
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
    drawEyesFrame(STATE_SLEEP, 0);
    delay(50);
  }

  // State 2: IDLE (3 seconds)
  setLedForState(STATE_IDLE);
  start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawEyesFrame(STATE_IDLE, 0);
    delay(50);
  }

  // State 3: BUSY (3 seconds)
  setLedForState(STATE_BUSY);
  start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawEyesFrame(STATE_BUSY, 2);
    delay(50);
  }

  // State 4: DONE (3 seconds)
  setLedForState(STATE_DONE);
  start = millis();
  while (millis() - start < 3000) {
    animTick++;
    drawEyesFrame(STATE_DONE, 0);
    delay(50);
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

  drawEyesFrame(currentState, currentStep);

  delay(50);
}
