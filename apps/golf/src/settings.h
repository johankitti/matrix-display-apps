#pragma once

#include <Arduino.h>
#include <sleep_core.h>   // shared NightSettings (night schedule + timezone)

// Runtime settings, persisted in NVS flash and edited from the web page
// (webconfig). config.h supplies the defaults the first time the device boots;
// after that these are the source of truth. Loaded once at startup into the
// global `settings`.

#define MAX_PINNED_GOLFERS 3
#define PINNED_NAME_MAX    20

// Which tour's leaderboard to show. Stored as a plain number in NVS, so the
// values are fixed: never renumber.
enum Tour : uint8_t {
  TOUR_PGA  = 0,  // PGA Tour
  TOUR_EUR  = 1,  // DP World Tour (European Tour)
  TOUR_AUTO = 2,  // follow the tracked golfers: whichever tour the first
                  // tracked golfer plays this week (then the second, ...)
};
#define TOUR_COUNT 3   // valid stored values are < TOUR_COUNT

struct Settings {
  uint8_t       brightness;   // 0-255
  NightSettings night;        // night-mode schedule + timezone (sleep-core)
  uint8_t       tour;         // a Tour value (TOUR_PGA / TOUR_EUR / TOUR_AUTO)
  uint8_t       pinnedCount;  // 0..MAX_PINNED_GOLFERS
  char          pinned[MAX_PINNED_GOLFERS][PINNED_NAME_MAX];  // surname match patterns
};

extern Settings settings;

void settingsLoad();   // NVS -> settings (config.h defaults if unset)
void settingsSave();   // settings -> NVS
