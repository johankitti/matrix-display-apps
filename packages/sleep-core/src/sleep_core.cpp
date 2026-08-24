#include "sleep_core.h"

#include <display_core.h>   // displayPowerOff
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>

// Friendly label -> POSIX TZ string for the settings dropdown. The board's
// times and the night window are evaluated in whichever zone is picked here.
struct TzOption { const char* label; const char* posix; };
static const TzOption TZ_OPTIONS[] = {
    {"Sweden / Central Europe (CET/CEST)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"UK (GMT/BST)",                        "GMT0BST,M3.5.0/1,M10.5.0"},
    {"US Eastern (ET)",                     "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central (CT)",                     "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain (MT)",                    "MST7MDT,M3.2.0,M11.1.0"},
    {"US Arizona (no DST)",                 "MST7"},
    {"US Pacific (PT)",                     "PST8PDT,M3.2.0,M11.1.0"},
    {"UTC",                                 "UTC0"},
};
static const size_t TZ_OPTION_COUNT = sizeof(TZ_OPTIONS) / sizeof(TZ_OPTIONS[0]);

// App state wired in by sleepWebRegister(), used by the /night handler.
static NightSettings* g_night = nullptr;
static void (*g_saveCb)() = nullptr;

// ---- Clock ------------------------------------------------------------------
void sleepApplyTimezone(const NightSettings& s) {
  setenv("TZ", s.timezone, 1);
  tzset();
}

void sleepBeginNtp(const NightSettings& s) {
  // configTzTime sets the zone AND starts SNTP in one call; the clock becomes
  // valid a few seconds later once the first packet lands.
  configTzTime(s.timezone, "pool.ntp.org", "time.nist.gov");
}

bool sleepClockValid() {
  return time(nullptr) > 1700000000;   // ~2023-11; anything earlier is unset
}

// ---- Night window -----------------------------------------------------------
bool sleepIsNight(const NightSettings& s) {
  if (!s.enabled || !sleepClockValid()) return false;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  if (s.startHour < s.endHour) {
    return lt.tm_hour >= s.startHour && lt.tm_hour < s.endHour;
  }
  return lt.tm_hour >= s.startHour || lt.tm_hour < s.endHour;  // spans midnight
}

void sleepUntilMorning(const NightSettings& s) {
  time_t now = time(nullptr);
  struct tm morning;
  localtime_r(&now, &morning);
  morning.tm_hour = s.endHour;
  morning.tm_min = 0;
  morning.tm_sec = 0;
  morning.tm_isdst = -1;             // let mktime resolve DST
  time_t wake = mktime(&morning);    // local -> epoch (TZ is set)
  if (wake <= now) wake += 24 * 3600;

  // +60 s margin: the RTC's sleep timer drifts a little over hours, and waking
  // just after the boundary beats waking just before it (setup() re-checks).
  uint64_t secs = (uint64_t)(wake - now) + 60;
  Serial.printf("[night] display off, sleeping %llu min\n", secs / 60);
  displayPowerOff();
  WiFi.disconnect(true);
  esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
  esp_deep_sleep_start();            // wakes via reset into setup()
}

// ---- NVS --------------------------------------------------------------------
void sleepLoad(PrefsStore& store, NightSettings& s, bool defEnabled,
               uint8_t defStart, uint8_t defEnd, const char* defTz) {
  s.enabled   = store.boolean("nEn", defEnabled);
  s.startHour = store.u8("nStart", defStart);
  s.endHour   = store.u8("nEnd", defEnd);
  store.strTo("tz", defTz, s.timezone, NIGHT_TZ_MAX);
}

void sleepSave(PrefsStore& store, const NightSettings& s) {
  store.put("nEn", s.enabled);
  store.put("nStart", s.startHour);
  store.put("nEnd", s.endHour);
  store.put("tz", s.timezone);
}

// ---- Web UI -----------------------------------------------------------------
String sleepWebSection(const NightSettings& s) {
  // Self-contained, theme-agnostic night-window control: a 24-hour timeline with
  // two draggable handles and a live "sleeps HH:MM → HH:MM · N h" summary. The
  // handles write hidden nStart/nEnd fields (hours), so the POST /night handler
  // is unchanged. Styles/JS are nm-prefixed and scoped to this widget so they
  // sit happily inside any host page (golf's light theme, buss's dark one, ...).
  String h = F(
      "<form method=post action=/night>"
      "<style>"
      ".nm-sec{border-top:1px solid rgba(127,127,127,.3);margin-top:16px;padding-top:12px}"
      ".nm-tgl{display:flex;align-items:center;gap:8px;font-weight:600;text-transform:none;letter-spacing:0}"
      ".nm-tgl input{width:auto;margin:0}"
      ".nm-sum{font-weight:600;margin:12px 0 6px;font-variant-numeric:tabular-nums}"
      ".nm-tl{position:relative;height:46px;border-radius:9px;background:rgba(127,127,127,.18);"
      "touch-action:none;user-select:none;cursor:pointer}"
      ".nm-fill{position:absolute;top:0;bottom:0;border-radius:9px;"
      "background:linear-gradient(180deg,#6366f1,#4338ca);display:none}"
      ".nm-h{position:absolute;top:-3px;width:14px;height:52px;margin-left:-7px;border-radius:5px;"
      "background:#fff;border:1px solid rgba(0,0,0,.3);box-shadow:0 1px 5px rgba(0,0,0,.4);"
      "cursor:ew-resize;touch-action:none;z-index:2}"
      ".nm-h:focus{outline:2px solid #6366f1;outline-offset:2px}"
      ".nm-h::after{content:'';position:absolute;left:5px;top:19px;width:4px;height:14px;"
      "border-left:1px solid #bbb;border-right:1px solid #bbb}"
      ".nm-scale{display:flex;justify-content:space-between;font-size:11px;opacity:.6;margin:6px 2px 0;"
      "font-variant-numeric:tabular-nums}"
      ".nm-off{opacity:.4}"
      "</style>"
      "<div class=nm-sec><label class=nm-tgl><input type=checkbox name=nEn id=nmEn ");
  if (s.enabled) h += "checked";
  h += F("> Night mode (panel sleeps)</label>"
         "<div class=nm-sum id=nmSum></div>"
         "<div class=nm-tl id=nmTl>"
         "<div class=nm-fill id=nmFa></div><div class=nm-fill id=nmFb></div>"
         "<div class=nm-h id=nmHs tabindex=0 role=slider aria-label='Sleep start'></div>"
         "<div class=nm-h id=nmHe tabindex=0 role=slider aria-label='Sleep end'></div>"
         "</div>"
         "<div class=nm-scale><span>00</span><span>06</span><span>12</span><span>18</span><span>24</span></div>");
  h += "<input type=hidden name=nStart id=nmS value=" + String(s.startHour) + ">";
  h += "<input type=hidden name=nEnd id=nmE value=" + String(s.endHour) + ">";
  h += F(
      "<script>(function(){"
      "var tl=document.getElementById('nmTl'),hs=document.getElementById('nmHs'),he=document.getElementById('nmHe'),"
      "fa=document.getElementById('nmFa'),fb=document.getElementById('nmFb'),"
      "iS=document.getElementById('nmS'),iE=document.getElementById('nmE'),"
      "sum=document.getElementById('nmSum'),en=document.getElementById('nmEn');"
      "var st=+iS.value||0,ed=+iE.value||0,drag=null;"
      "function p(x){return x/24*100}"
      "function f(x){return (x<10?'0':'')+x+':00'}"
      "function dur(){var d=ed-st;if(d<=0)d+=24;return d}"
      "function draw(){"
      "hs.style.left=p(st)+'%';he.style.left=p(ed)+'%';"
      "if(st<=ed){fa.style.display='block';fa.style.left=p(st)+'%';fa.style.width=p(ed-st)+'%';fb.style.display='none';}"
      "else{fa.style.display='block';fa.style.left=p(st)+'%';fa.style.width=p(24-st)+'%';"
      "fb.style.display='block';fb.style.left='0';fb.style.width=p(ed)+'%';}"
      "iS.value=st;iE.value=ed;"
      "sum.innerHTML=en.checked?('\\uD83C\\uDF19 Sleeps '+f(st)+' \\u2192 '+f(ed)+' \\u00B7 '+dur()+' h'):'Night mode off \\u2014 panel stays on';"
      "tl.classList.toggle('nm-off',!en.checked);}"
      "function hAt(cx){var r=tl.getBoundingClientRect();var v=Math.round((cx-r.left)/r.width*24);"
      "return v<0?0:v>23?23:v;}"
      "function set(cx){var v=hAt(cx);if(drag=='s')st=v;else ed=v;draw();}"
      "tl.addEventListener('pointerdown',function(e){var r=tl.getBoundingClientRect();"
      "var v=(e.clientX-r.left)/r.width*24;drag=Math.abs(v-st)<=Math.abs(v-ed)?'s':'e';"
      "tl.setPointerCapture(e.pointerId);set(e.clientX);e.preventDefault();});"
      "tl.addEventListener('pointermove',function(e){if(drag)set(e.clientX);});"
      "tl.addEventListener('pointerup',function(){drag=null;});"
      "tl.addEventListener('pointercancel',function(){drag=null;});"
      "function key(w){return function(e){var d=e.key=='ArrowLeft'||e.key=='ArrowDown'?-1:"
      "e.key=='ArrowRight'||e.key=='ArrowUp'?1:0;if(!d)return;e.preventDefault();"
      "if(w=='s')st=(st+d+24)%24;else ed=(ed+d+24)%24;draw();};}"
      "hs.addEventListener('keydown',key('s'));he.addEventListener('keydown',key('e'));"
      "en.addEventListener('change',draw);draw();"
      "})();</script>");

  h += F("<label class=nm-tzlbl style='display:block;margin:14px 0 3px;font-weight:600;text-transform:none;letter-spacing:0'>"
         "Timezone (for the night window)</label><select name=tz>");
  bool tzKnown = false;
  for (size_t i = 0; i < TZ_OPTION_COUNT; i++) {
    bool sel = strcmp(s.timezone, TZ_OPTIONS[i].posix) == 0;
    tzKnown = tzKnown || sel;
    h += "<option value=\"" + webEsc(TZ_OPTIONS[i].posix) + "\"" + (sel ? " selected" : "") +
         ">" + webEsc(TZ_OPTIONS[i].label) + "</option>";
  }
  if (!tzKnown)  // a custom TZ flashed via config.h — keep it selectable
    h += "<option value=\"" + webEsc(s.timezone) + "\" selected>" +
         webEsc(s.timezone) + "</option>";
  h += F("</select><button type=submit>Save night settings</button></form>");
  return h;
}

// Parse the posted /night form into `s`; returns true if the timezone changed.
static bool parseNight(WebServer& server, NightSettings& s) {
  s.enabled = server.hasArg("nEn");
  if (server.hasArg("nStart"))
    s.startHour = constrain(server.arg("nStart").toInt(), 0, 23);
  if (server.hasArg("nEnd"))
    s.endHour = constrain(server.arg("nEnd").toInt(), 0, 23);
  bool tzChanged = false;
  if (server.hasArg("tz")) {
    String tz = server.arg("tz");
    if (tz.length() && tz.length() < NIGHT_TZ_MAX &&
        strcmp(tz.c_str(), s.timezone) != 0) {
      strlcpy(s.timezone, tz.c_str(), NIGHT_TZ_MAX);
      tzChanged = true;
    }
  }
  return tzChanged;
}

static void handleNight() {
  WebServer& server = webServer();
  if (g_night) {
    bool tzChanged = parseNight(server, *g_night);
    if (g_saveCb) g_saveCb();
    if (tzChanged) sleepApplyTimezone(*g_night);   // take effect without a reboot
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void sleepWebRegister(NightSettings* s, void (*saveCb)()) {
  g_night = s;
  g_saveCb = saveCb;
  webServer().on("/night", HTTP_POST, handleNight);
}
