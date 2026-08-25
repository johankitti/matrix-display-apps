#pragma once
#include <Arduino.h>
#include <sleep_core.h>   // shared NightSettings (night schedule + timezone)

// Runtime settings, configured at http://buss-display.local/ and persisted in
// NVS. Defaults (config.h) apply on a fresh device.
struct Settings {
  uint32_t siteId;        // SL Transport site id
  char stationName[48];   // display header, UTF-8 as delivered by the API
  uint8_t direction;      // 0 = both directions, else direction_code 1 or 2
  uint8_t minMinutes;     // hide departures leaving sooner than this (won't make it)
  uint8_t brightness;     // panel brightness (BRIGHTNESS_MIN..MAX); rotary knob
  NightSettings night;    // night-mode schedule + timezone (sleep-core)
};

extern Settings settings;

void settingsLoad();
void settingsSave();
