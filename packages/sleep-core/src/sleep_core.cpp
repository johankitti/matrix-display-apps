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
  String h = F("<form method=post action=/night><div class=sec></div>"
               "<label><input type=checkbox name=nEn ");
  if (s.enabled) h += "checked";
  h += F("> Night mode (panel sleeps)</label>"
         "<div class=row><div><label>From (h)</label>");
  h += "<input type=number name=nStart min=0 max=23 value=" + String(s.startHour) + "></div>";
  h += F("<div><label>To (h)</label>");
  h += "<input type=number name=nEnd min=0 max=23 value=" + String(s.endHour) + "></div></div>";

  h += F("<label>Timezone (for the night window)</label><select name=tz>");
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
