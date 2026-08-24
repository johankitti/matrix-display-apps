#pragma once
#include <Arduino.h>
#include <sleep_core.h>   // shared NightSettings (night schedule + timezone)

// How sprites are shown:
enum AnimMode : uint8_t {
    ANIM_MODE_FULL   = 0,   // full dex 1..DEX_MAX; animated where available, else static
    ANIM_MODE_ONLY   = 1,   // only Pokémon that HAVE animations (1..ANIM_MAX_ID)
    ANIM_MODE_STATIC = 2,   // static sprites for the whole dex
};

// User-facing runtime settings, edited over Wi-Fi (see web.*) and persisted in
// NVS. Read freely from any core; writes happen on the main loop (web handler).
struct Settings {
    int     durationSec;   // seconds each Pokémon is shown (clamped to DURATION_*_SEC)
    bool    randomOrder;   // true = random ids, false = sequential 1..max (wraps)
    uint8_t  animMode;     // one of AnimMode
    uint8_t  brightness;   // panel brightness (BRIGHTNESS_MIN..MAX); knob + web slider
    uint16_t speedPct;     // GIF playback speed % (ANIM_SPEED_MIN..MAX; 100 = original)
    NightSettings night;   // night-mode schedule + timezone (sleep-core)
};

extern Settings g_settings;

// Load settings from NVS into g_settings (applying defaults + clamps). Call once
// at boot before anything reads g_settings.
void settingsLoad();

// Persist the current g_settings back to NVS.
void settingsSave();
