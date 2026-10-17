#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

extern WebServer apServer;
extern DNSServer dnsServer;
extern Preferences prefs;
extern bool inApMode;

static const char HTML_CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>DeskPet WiFi 配网</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #121212; color: #fff; padding: 20px; margin: 0; }
.card { background: #1e1e1e; border-radius: 12px; padding: 20px; max-width: 360px; margin: 20px auto; box-shadow: 0 4px 12px rgba(0,0,0,0.5); }
h2 { color: #f39c12; margin-top: 0; text-align: center; }
p { font-size: 14px; color: #aaa; text-align: center; }
label { display: block; margin-top: 14px; font-size: 13px; color: #ddd; }
input { width: 100%; box-sizing: border-box; padding: 10px; margin-top: 6px; border-radius: 8px; border: 1px solid #333; background: #2a2a2a; color: #fff; font-size: 15px; }
input:focus { border-color: #f39c12; outline: none; }
button { width: 100%; padding: 12px; margin-top: 22px; border: none; border-radius: 8px; background: #f39c12; color: #000; font-weight: bold; font-size: 16px; cursor: pointer; }
button:hover { background: #e67e22; }
.footer { margin-top: 16px; font-size: 12px; color: #666; text-align: center; }
</style>
</head>
<body>
<div class="card">
  <h2>🐱 桌宠配网</h2>
  <p>为 ESP32 桌宠设置新的 Wi-Fi 网络</p>
  <form action="/save" method="POST">
    <label>Wi-Fi 名称 (SSID)</label>
    <input type="text" name="ssid" placeholder="输入 Wi-Fi 名称" required>
    
    <label>Wi-Fi 密码</label>
    <input type="password" name="pass" placeholder="输入密码 (若无留空)">

    <label>Xperia 局域网 IP (可选)</label>
    <input type="text" name="xperia_ip" placeholder="例如 192.168.1.50 (留空则自动查找)">

    <button type="submit">保存并重启</button>
  </form>
  <div class="footer">Pocket-Dongle-S3 DeskPet v1.0</div>
</div>
</body>
</html>
)rawliteral";

inline void setupApPortal() {
  apServer.on("/", HTTP_GET, []() {
    apServer.send_P(200, "text/html", HTML_CONFIG_PAGE);
  });

  apServer.on("/save", HTTP_POST, []() {
    String ssid = apServer.arg("ssid");
    String pass = apServer.arg("pass");
    String xip  = apServer.arg("xperia_ip");

    prefs.begin("deskpet", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    if (xip.length() > 0) {
      prefs.putString("xperia_ip", xip);
    }
    prefs.end();

    String reply = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{background:#121212;color:#fff;text-align:center;padding:50px;font-family:sans-serif;}</style></head><body><h2>✅ 配置已保存！</h2><p>桌宠正在重启并连接 Wi-Fi: " + ssid + "</p></body></html>";
    apServer.send(200, "text/html", reply);
    delay(1500);
    ESP.restart();
  });

  // Captive portal redirects
  auto redirect = []() {
    apServer.sendHeader("Location", "http://192.168.4.1/", true);
    apServer.send(302, "text/plain", "");
  };
  apServer.on("/generate_204", redirect);
  apServer.on("/hotspot-detect.html", redirect);
  apServer.onNotFound(redirect);

  apServer.begin();
}
