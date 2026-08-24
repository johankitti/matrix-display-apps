#pragma once

#include <Arduino.h>
#include <sleep_core.h>   // shared NightSettings (night schedule + timezone)

// Runtime settings, persisted in NVS flash and edited from the web page
// (webconfig). config.h supplies the defaults the first time the device boots;
// after that these are the source of truth. Loaded once at startup into the
// global `settings`.

#define MAX_PINNED_GOLFERS 3
#define PINNED_NAME_MAX    20

struct Settings {
  uint8_t       brightness;   // 0-255
  NightSettings night;        // night-mode schedule + timezone (sleep-core)
  uint8_t       pinnedCount;  // 0..MAX_PINNED_GOLFERS
  char          pinned[MAX_PINNED_GOLFERS][PINNED_NAME_MAX];  // surname match patterns
};

extern Settings settings;

void settingsLoad();   // NVS -> settings (config.h defaults if unset)
void settingsSave();   // settings -> NVS
