#include "web_core.h"

#include <ESPmDNS.h>
#include <WiFi.h>

static WebServer s_server(80);

WebServer& webServer() { return s_server; }

String webEsc(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default:  o += *s;
    }
  }
  return o;
}

String webPageHead(const char* title) {
  String h = F(
      "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
      "<title>");
  h += webEsc(title);
  h += F(
      "</title><style>"
      "body{font-family:system-ui,sans-serif;max-width:430px;margin:14px auto;padding:0 12px;color:#111}"
      "h1{font-size:20px}label{display:block;margin:10px 0 3px;font-weight:600;font-size:14px}"
      "input[type=text],input[type=number]{width:100%;padding:7px;box-sizing:border-box;font-size:15px}"
      "input[type=range]{width:100%}.row{display:flex;gap:8px}.row>div{flex:1}"
      "button{margin-top:14px;padding:10px;font-size:15px;border:0;border-radius:6px;background:#116;color:#fff;width:100%}"
      ".st{background:#f1f1f5;border-radius:6px;padding:10px;font-size:13px;margin:8px 0}"
      ".sec{border-top:1px solid #ddd;margin-top:16px}"
      "select{width:100%;padding:7px;font-size:15px}"
      "button.alt{background:#555}button.warn{background:#a30}"
      "</style>");
  return h;
}

String webStatusChrome() {
  String s = "IP " + WiFi.localIP().toString();
  s += " &middot; " + String(WiFi.RSSI()) + " dBm";
  s += " &middot; up " + String(millis() / 60000) + " min";
  return s;
}

String webActionForms() {
  return F(
      "<div class=sec></div>"
      "<form method=post action=/restart><button class=alt>Restart</button></form>"
      "<form method=post action=/wifi onsubmit=\"return confirm('Reconfigure Wi-Fi? "
      "The board will open its setup hotspot.')\">"
      "<button class=warn>Reconfigure Wi-Fi</button></form>");
}

static void handleRestart() {
  s_server.send(200, "text/html",
                "<meta http-equiv=refresh content='5;url=/'>Restarting&hellip;");
  delay(300);
  ESP.restart();
}

static void handleWifiReset() {
  s_server.send(200, "text/html",
                "Join the board's setup Wi-Fi to reconfigure. Restarting&hellip;");
  delay(400);
  WiFi.disconnect(true, true);  // erase stored credentials
  delay(200);
  ESP.restart();
}

void webCoreBegin(const char* hostname) {
  if (MDNS.begin(hostname)) MDNS.addService("http", "tcp", 80);
  s_server.on("/restart", HTTP_POST, handleRestart);
  s_server.on("/wifi", HTTP_POST, handleWifiReset);
  s_server.begin();
  Serial.printf("[web] settings at http://%s.local/  (http://%s/)\n",
                hostname, WiFi.localIP().toString().c_str());
}

void webCoreHandle() { s_server.handleClient(); }
