#include "settings.h"

#include <settings_core.h>   // shared PrefsStore (NVS) wrapper

#include "config.h"

Settings settings;

static PrefsStore store("sports");

void settingsLoad() {
  store.beginRead();
  settings.brightness = clampBrightness(store.u8("bright", PANEL_BRIGHTNESS),
                                        BRIGHTNESS_MIN, BRIGHTNESS_MAX);
  sleepLoad(store, settings.night, NIGHT_MODE_ENABLED, NIGHT_START_HOUR,
            NIGHT_END_HOUR, TIMEZONE_POSIX);
  store.end();
}

void settingsSave() {
  store.beginWrite();
  store.put("bright", settings.brightness);
  sleepSave(store, settings.night);
  store.end();
}
