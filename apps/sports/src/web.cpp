#include "web.h"
#include "config.h"
#include "settings.h"
#include "teams.h"

#include <display_core.h>
#include <settings_core.h>   // clampBrightness
#include <web_core.h>   // shared WebServer + mDNS + /restart + /wifi handlers

static bool refreshRequested = false;

// The upcoming games as a table, with a live JS countdown (kick-off epochs are
// embedded as data attributes; the browser's clock does the ticking).
static String gamesTable() {
  String h = F("<table style='width:100%;border-collapse:collapse'>");
  for (int i = 0; i < TEAM_COUNT; i++) {
    const TeamDef& t = TEAMS[i];
    const TeamState& s = teamState[i];
    const Game& g = s.game;
    h += "<tr><td style='padding:.4rem 0'><b>" + webEsc(t.name) + "</b><br><small>";
    if (!s.everFetched) {
      h += F("not fetched yet");
    } else if (!g.known) {
      h += F("no upcoming game listed");
    } else {
      h += webEsc(homeAbbr(t, g)) + " &ndash; " + webEsc(awayAbbr(t, g));
      h += g.home ? F(" (home)") : F(" (away)");
    }
    if (s.everFetched && !s.lastOk) h += F(" &middot; <span class=warn>last fetch failed</span>");
    h += F("</small></td><td style='text-align:right;font-variant-numeric:tabular-nums'>");
    Phase ph = teamPhase(i, time(nullptr));
    if (ph == PH_LIVE || ph == PH_FINAL) {
      // Live/final: server-rendered (the page's 1-min-old score is as fresh
      // as the panel's anyway). Reload the page for updates.
      h += ph == PH_LIVE ? F("<b style='color:#ff4b4b'>&#9679; LIVE</b> ") : F("<b>FT</b> ");
      if (hasScore(g)) h += "<b>" + webEsc(g.homeScore) + "&ndash;" + webEsc(g.awayScore) + "</b>";
      h += "<br><small>" + webEsc(ph == PH_LIVE ? g.clock : g.decided) + "</small>";
    } else if (g.known) {
      h += "<span class=cd data-t='" + String((long)g.start) + "'></span><br><small class=kt data-t='" +
           String((long)g.start) + "'></small>";
    }
    h += F("</td></tr>");
  }
  h += F("</table>"
         "<script>"
         "function f(s){if(s<=0)return'LIVE / started';const d=Math.floor(s/86400),"
         "p=n=>String(n).padStart(2,'0');return(d?d+'d ':'')+p(Math.floor(s/3600)%24)+':'"
         "+p(Math.floor(s/60)%60)+':'+p(s%60)}"
         "document.querySelectorAll('.kt').forEach(e=>e.textContent="
         "new Date(e.dataset.t*1000).toLocaleString([],{weekday:'short',day:'numeric',"
         "month:'short',hour:'2-digit',minute:'2-digit'}));"
         "function tick(){const n=Date.now()/1000;document.querySelectorAll('.cd')"
         ".forEach(e=>e.textContent=f(Math.floor(e.dataset.t-n)))}tick();setInterval(tick,1000)"
         "</script>");
  return h;
}

static void handleRoot() {
  String h = webPageHead("Sports Display");
  h += F("<h1>&#9917; Sports Display</h1>");
  h += "<div class=st>" + webStatusChrome() + "</div>";
  h += F("<div class=sec>Next games</div>");
  h += gamesTable();
  h += F("<form method=POST action=/refresh><button type=submit>Refresh now</button></form>"
         "<div class=hint>Teams are set in firmware (apps/sports/src/teams.cpp).</div>");

  h += F("<form method=POST action=/save><div class=sec>Display</div>");
  h += "<label>Brightness (" + String(settings.brightness) + ")</label>";
  h += "<input type=range name=bri min=" + String(BRIGHTNESS_MIN) + " max=" +
       String(BRIGHTNESS_MAX) + " value=" + String(settings.brightness) +
       " oninput=\"this.previousElementSibling.textContent='Brightness ('+this.value+')'\">";
  h += F("<div class=hint>Also adjustable with the knob.</div>"
         "<button type=submit>Save</button></form>");

  h += sleepWebSection(settings.night);   // night schedule + timezone (shared)
  h += webActionForms();                  // Restart + Reconfigure Wi-Fi (shared)
  webServer().send(200, "text/html", h);
}

static void redirectHome() {
  webServer().sendHeader("Location", "/");
  webServer().send(303);
}

static void handleSave() {
  WebServer& server = webServer();
  if (server.hasArg("bri"))
    settings.brightness = clampBrightness(server.arg("bri").toInt(), BRIGHTNESS_MIN, BRIGHTNESS_MAX);
  settingsSave();
  displaySetBrightness(settings.brightness);
  redirectHome();
}

static void handleRefresh() {
  refreshRequested = true;
  redirectHome();
}

void webStart() {
  WebServer& server = webServer();
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/refresh", HTTP_POST, handleRefresh);
  sleepWebRegister(&settings.night, settingsSave);   // POST /night (shared)
  webCoreBegin(HOSTNAME);   // mDNS + /restart + /wifi + server.begin()
}

void webHandle() { webCoreHandle(); }

bool webRefreshRequested() {
  bool r = refreshRequested;
  refreshRequested = false;
  return r;
}
