#include "webconfig.h"

#include <web_core.h>   // shared WebServer + mDNS + Restart/Wi-Fi handlers + HTML chrome
#include <WiFi.h>

#include <time.h>

#include "config.h"
#include "display.h"
#include "settings.h"

// The night schedule + timezone controls (and their /night save handler) come
// from sleep-core, so every app's settings page shares them.
static const Leaderboard* g_board = nullptr;
static volatile bool* g_refresh = nullptr;

static String statusHtml() {
  String s = "<div class=st>";
  if (g_board && g_board->mode == MODE_LIVE)
    s += "<b>" + webEsc(g_board->eventName) + "</b> " + webEsc(g_board->roundLabel);
  else if (g_board && g_board->mode == MODE_NEXT)
    s += "Next: <b>" + webEsc(g_board->nextName) + "</b>";
  else
    s += "No live event";
  s += "<br>" + webStatusChrome();
  s += "</div>";
  return s;
}

static void handleRoot() {
  String h = webPageHead("Golf Board");
  h += F("<h1>&#9971; Golf Board</h1>");
  h += statusHtml();

  h += F("<form method=post action=/save><label>Brightness</label>");
  h += "<input type=range name=bright min=0 max=255 value=" + String(settings.brightness) + ">";

  h += F("<div class=sec></div><label>Tracked golfers (surname)</label>");
  for (uint8_t i = 0; i < MAX_PINNED_GOLFERS; i++) {
    String v = i < settings.pinnedCount ? webEsc(settings.pinned[i]) : String();
    h += "<input type=text name=pin" + String(i) + " value=\"" + v +
         "\" placeholder=\"e.g. Aberg\">";
  }
  h += F("<button type=submit>Save</button></form>");

  h += sleepWebSection(settings.night);   // night schedule + timezone (shared)

  h += F("<div class=sec></div>"
         "<form method=post action=/refresh><button class=alt>Refresh now</button></form>");
  h += webActionForms();   // Restart + Reconfigure Wi-Fi (shared)
  webServer().send(200, "text/html", h);
}

static void handleSave() {
  WebServer& server = webServer();
  if (server.hasArg("bright"))
    settings.brightness = constrain(server.arg("bright").toInt(), 0, 255);

  // Night schedule + timezone are their own form (sleep-core's /night handler).

  uint8_t pc = 0;
  for (uint8_t i = 0; i < MAX_PINNED_GOLFERS; i++) {
    String v = server.arg(("pin" + String(i)).c_str());
    v.trim();
    if (v.length()) {
      strlcpy(settings.pinned[pc], v.c_str(), PINNED_NAME_MAX);
      pc++;
    }
  }
  settings.pinnedCount = pc;

  settingsSave();
  displaySetBrightness(settings.brightness);

  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleRefresh() {
  if (g_refresh) *g_refresh = true;
  webServer().sendHeader("Location", "/");
  webServer().send(303);
}

void webconfigBegin(const Leaderboard* board, volatile bool* refreshFlag) {
  g_board = board;
  g_refresh = refreshFlag;

  webServer().on("/", HTTP_GET, handleRoot);
  webServer().on("/save", HTTP_POST, handleSave);
  webServer().on("/refresh", HTTP_POST, handleRefresh);
  sleepWebRegister(&settings.night, settingsSave);  // POST /night (shared)
  webCoreBegin("golfboard");   // mDNS + /restart + /wifi + server.begin()
}

void webconfigLoop() { webCoreHandle(); }
