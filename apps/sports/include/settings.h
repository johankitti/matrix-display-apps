#pragma once
#include <Arduino.h>
#include <sleep_core.h>   // shared NightSettings (night schedule + timezone)

// Runtime settings, edited at http://sports-display.local/ and kept in NVS.
// The tracked teams are compile-time (src/teams.cpp).
struct Settings {
  uint8_t brightness;     // panel brightness (BRIGHTNESS_MIN..MAX); rotary knob
  NightSettings night;    // night-mode schedule + timezone (sleep-core)
};

extern Settings settings;

void settingsLoad();
void settingsSave();
