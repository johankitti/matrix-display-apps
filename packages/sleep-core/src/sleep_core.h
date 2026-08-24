#pragma once
// =============================================================================
//  sleep-core — shared night-mode schedule for the matrix-display apps
//
//  Every app wants the same behavior: between two local hours, blank the panel
//  and deep-sleep the whole device (ESP32 at ~µA), waking itself in the morning.
//  That spans settings, NVS, a web control, the clock and deep sleep, so it
//  lives here once. Each app embeds a NightSettings in its own Settings struct
//  and wires up three things:
//
//     sleepBeginNtp(s.night);                       // once, after Wi-Fi is up
//     if (sleepIsNight(s.night)) sleepUntilMorning(s.night);   // top of loop()
//     sleepWebRegister(&s.night, settingsSave);     // once, at web setup
//     page += sleepWebSection(s.night);             // in the settings page
//
//  The night *check* just reads the system clock, so an app that sets time some
//  other way (golf reads the HTTP Date header) can skip sleepBeginNtp().
// =============================================================================

#include <Arduino.h>
#include <settings_core.h>   // PrefsStore
#include <web_core.h>        // WebServer

#define NIGHT_TZ_MAX 40

struct NightSettings {
  bool    enabled;
  uint8_t startHour;                 // local hour 0-23 (panel off at this hour)
  uint8_t endHour;                   // local hour 0-23 (panel back on)
  char    timezone[NIGHT_TZ_MAX];    // POSIX TZ string; window is evaluated in it
};

// ---- Clock ------------------------------------------------------------------
// Apply the timezone to the C runtime (setenv TZ + tzset). Call after load and
// whenever the timezone changes.
void sleepApplyTimezone(const NightSettings& s);

// Start SNTP for the timezone in `s` (also applies the zone). Non-blocking; the
// clock becomes valid a few seconds after Wi-Fi is up. Call once after connect.
void sleepBeginNtp(const NightSettings& s);

// True once the system clock has actually been set (so night checks don't fire
// on the bogus epoch-zero clock right after boot).
bool sleepClockValid();

// ---- Night window -----------------------------------------------------------
// True if night mode is enabled, the clock is valid, and the local time is in
// [startHour, endHour) — a window that may span midnight (23..7).
bool sleepIsNight(const NightSettings& s);

// Blank the panel (display-core's displayPowerOff) and deep-sleep until endHour
// local time, then wake via reset into setup(). Stop any app render tasks first.
void sleepUntilMorning(const NightSettings& s);

// ---- NVS (shared keys: nEn / nStart / nEnd / tz) ----------------------------
// `store` must already be open (beginRead for load, beginWrite for save).
void sleepLoad(PrefsStore& store, NightSettings& s,
               bool defEnabled, uint8_t defStart, uint8_t defEnd, const char* defTz);
void sleepSave(PrefsStore& store, const NightSettings& s);

// ---- Web UI -----------------------------------------------------------------
// Register a self-contained POST /night handler on the shared web server: it
// parses the posted form into *s, calls saveCb() to persist, applies the
// timezone, and redirects to /. Call once (order vs. webCoreBegin doesn't
// matter). saveCb is the app's settingsSave().
void sleepWebRegister(NightSettings* s, void (*saveCb)());

// An HTML <form> (styled with web-core's classes) that posts to /night — the
// night toggle, from/to hours and a timezone picker. Append it to your page.
String sleepWebSection(const NightSettings& s);
