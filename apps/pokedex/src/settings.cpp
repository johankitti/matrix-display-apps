#include "settings.h"
#include "config.h"

#include <settings_core.h>   // shared PrefsStore (NVS) wrapper + clampBrightness

Settings g_settings;

// NVS namespace + keys — unchanged from the standalone app, so existing devices
// keep their saved settings after the monorepo migration.
static PrefsStore store("pokedex");

static int clampDur(int s) {
    if (s < DURATION_MIN_SEC) s = DURATION_MIN_SEC;
    if (s > DURATION_MAX_SEC) s = DURATION_MAX_SEC;
    return s;
}

static uint16_t clampSpeed(int s) {
    if (s < ANIM_SPEED_MIN) s = ANIM_SPEED_MIN;
    if (s > ANIM_SPEED_MAX) s = ANIM_SPEED_MAX;
    return (uint16_t)s;
}

void settingsLoad() {
    store.beginRead();
    g_settings.durationSec = clampDur(store.i32("dur", DEFAULT_DURATION_SEC));
    g_settings.randomOrder = store.boolean("rand", true);            // random by default
    g_settings.animMode    = store.u8("amode", ANIM_MODE_FULL);
    if (g_settings.animMode > ANIM_MODE_STATIC) g_settings.animMode = ANIM_MODE_FULL;
    g_settings.brightness  = clampBrightness(store.u8("bri", PANEL_BRIGHTNESS),
                                             BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    g_settings.speedPct    = clampSpeed(store.u16("spd", ANIM_SPEED_DEFAULT));
    sleepLoad(store, g_settings.night, NIGHT_MODE_ENABLED, NIGHT_START_HOUR,
              NIGHT_END_HOUR, TIMEZONE_POSIX);
    store.end();
}

void settingsSave() {
    g_settings.durationSec = clampDur(g_settings.durationSec);
    g_settings.brightness  = clampBrightness(g_settings.brightness,
                                             BRIGHTNESS_MIN, BRIGHTNESS_MAX);
    g_settings.speedPct    = clampSpeed(g_settings.speedPct);
    store.beginWrite();
    store.put("dur", (int)g_settings.durationSec);
    store.put("rand", g_settings.randomOrder);
    store.put("amode", (uint8_t)g_settings.animMode);
    store.put("bri", (uint8_t)g_settings.brightness);
    store.put("spd", (uint16_t)g_settings.speedPct);
    sleepSave(store, g_settings.night);
    store.end();
}
