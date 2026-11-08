#include "net_core.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

static const uint32_t HTTP_TIMEOUT_MS = 8000;

static uint8_t* allocBuf(size_t n) {
  uint8_t* p = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  if (!p) p = (uint8_t*)malloc(n);   // fall back to internal RAM
  return p;
}

void netFree(uint8_t* buf) {
  if (buf) free(buf);   // free() routes to the right heap on ESP32
}

// Are Wi-Fi credentials stored in NVS? (esp_wifi is up once WiFi.mode() ran.)
static bool hasSavedCreds() {
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
  return conf.sta.ssid[0] != 0;
}

// Patiently try the SAVED network for up to timeoutMs. The driver keeps retrying
// in the background, so a router that comes up partway through the window still
// gets picked up. Returns true if connected.
static bool connectSaved(const NetConfig& cfg) {
  if (cfg.onConnecting) cfg.onConnecting();
  Serial.printf("[net] connecting to saved network (up to %lus)\n",
                (unsigned long)(cfg.connectTimeoutMs / 1000));
  WiFi.begin();   // no args -> use credentials stored in NVS
  uint32_t start = millis();
  while (millis() - start < cfg.connectTimeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Open the setup portal (with a timeout) so a new/changed network can be
// provisioned from a phone. Returns true if we ended up connected.
static bool runPortal(const NetConfig& cfg) {
  WiFiManager wm;
  wm.setDebugOutput(false);
  wm.setConfigPortalTimeout(cfg.portalTimeoutSec);
  wm.setAPCallback([&cfg](WiFiManager*) {
    String ip = WiFi.softAPIP().toString();
    if (cfg.onPortal) cfg.onPortal(cfg.apSsid, ip.c_str());
    Serial.printf("[net] setup AP '%s' open at http://%s\n", cfg.apSsid,
                  ip.c_str());
  });
  bool ok = wm.startConfigPortal(cfg.apSsid);
  return ok && WiFi.status() == WL_CONNECTED;
}

// Keep the setup AP up next to the station link, so the settings page is always
// reachable at http://192.168.4.1/ by joining the board's own Wi-Fi — even when
// the phone isn't on the home network or .local doesn't resolve. The ESP32 has
// one radio, so the AP simply follows the station's channel.
static void startSettingsAp(const NetConfig& cfg) {
  WiFi.mode(WIFI_AP_STA);   // adds the AP; the station stays associated
  if (!WiFi.softAP(cfg.apSsid)) {
    Serial.println("[net] settings AP failed to start");
    return;
  }
  Serial.printf("[net] settings AP '%s' at http://%s/\n", cfg.apSsid,
                WiFi.softAPIP().toString().c_str());
}

bool netStart(const NetConfig& cfg) {
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);           // modem sleep hurts TLS reliability
  WiFi.setAutoReconnect(true);

  // Cycle until connected: retry the saved network patiently, then offer the
  // portal (with a timeout), then retry the saved network again. A transient
  // outage resolves in the connect phase; a genuinely changed network is fixed
  // at the portal — neither phase is a dead end.
  for (;;) {
    if (hasSavedCreds() && connectSaved(cfg)) break;
    Serial.println("[net] not connected — opening setup portal");
    if (runPortal(cfg)) break;
    Serial.println("[net] portal timed out — retrying saved network");
  }
  Serial.printf("[net] IP %s\n", WiFi.localIP().toString().c_str());
  startSettingsAp(cfg);
  return true;
}

bool netEnsure() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.reconnect();                 // use saved creds; never opens the portal
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

// Howard Hinnant's days-from-civil algorithm.
time_t netUtcEpoch(int y, int m, int d, int hh, int mm, int ss) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  long days = (long)era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + hh * 3600 + mm * 60 + ss;
}

// "Sun, 08 Nov 2026 08:41:07 GMT" -> epoch, 0 on failure.
static time_t parseHttpDate(const char* s) {
  static const char* MON = "janfebmaraprmayjunjulaugsepoctnovdec";
  char mon[4] = {0};
  int d, y, hh, mm, ss;
  if (sscanf(s, "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6) return 0;
  for (int i = 0; i < 12; i++)
    if (strncasecmp(mon, MON + i * 3, 3) == 0) return netUtcEpoch(y, i + 1, d, hh, mm, ss);
  return 0;
}

bool httpGetBinary(const char* url, uint8_t** outBuf, size_t* outLen, time_t* serverDate) {
  *outBuf = nullptr;
  *outLen = 0;
  if (serverDate) *serverDate = 0;
  if (!netEnsure()) return false;

  WiFiClientSecure client;
  client.setInsecure();   // content is public; skip cert pinning

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);   // no chunked transfer encoding -> raw body on the stream
  if (!http.begin(client, url)) return false;
  static const char* headerKeys[] = {"Date"};
  if (serverDate) http.collectHeaders(headerKeys, 1);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[net] GET %d for %s\n", code, url);
    http.end();
    return false;
  }

  if (serverDate) *serverDate = parseHttpDate(http.header("Date").c_str());

  int len = http.getSize();             // Content-Length, or -1 if unknown
  WiFiClient* stream = http.getStreamPtr();

  size_t cap = (len > 0) ? (size_t)len : 8192;
  uint8_t* buf = allocBuf(cap);
  if (!buf) { http.end(); return false; }

  size_t got = 0;
  uint32_t lastData = millis();
  // Keep draining after the server closes: with HTTP/1.0 it hangs up right
  // after the body, often while the tail is still in the receive buffer.
  while ((http.connected() || stream->available()) && (len < 0 || got < (size_t)len)) {
    size_t avail = stream->available();
    if (avail) {
      if (got + avail > cap) {                 // grow when length unknown
        size_t ncap = cap * 2;
        while (ncap < got + avail) ncap *= 2;
        uint8_t* nbuf = allocBuf(ncap);
        if (!nbuf) { free(buf); http.end(); return false; }
        memcpy(nbuf, buf, got);
        free(buf);
        buf = nbuf;
        cap = ncap;
      }
      int r = stream->readBytes(buf + got, avail);
      if (r > 0) { got += r; lastData = millis(); }
    } else {
      if (millis() - lastData > HTTP_TIMEOUT_MS) break;
      delay(2);
    }
  }
  http.end();

  if (got == 0 || (len > 0 && got < (size_t)len)) {
    free(buf);
    return false;
  }
  *outBuf = buf;
  *outLen = got;
  return true;
}
