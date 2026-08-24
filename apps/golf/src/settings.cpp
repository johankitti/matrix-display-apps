#include "settings.h"

#include <settings_core.h>   // shared PrefsStore (NVS) wrapper

#include "config.h"

Settings settings;

// NVS namespace — unchanged from the standalone app, so existing devices keep
// their saved settings after the monorepo migration.
static PrefsStore store("golf");

void settingsLoad() {
  store.beginRead();

  settings.brightness = store.u8("bright", PANEL_BRIGHTNESS);
  sleepLoad(store, settings.night, NIGHT_MODE_ENABLED, NIGHT_START_HOUR,
            NIGHT_END_HOUR, TIMEZONE_POSIX);

  int n = store.i32("pinN", -1);
  if (n < 0) {
    // First boot: seed the tracked list from config.h.
    settings.pinnedCount = min((size_t)MAX_PINNED_GOLFERS, PINNED_GOLFER_COUNT);
    for (uint8_t i = 0; i < settings.pinnedCount; i++)
      strlcpy(settings.pinned[i], PINNED_GOLFERS[i], PINNED_NAME_MAX);
  } else {
    settings.pinnedCount = min(n, (int)MAX_PINNED_GOLFERS);
    for (uint8_t i = 0; i < settings.pinnedCount; i++) {
      char key[8];
      snprintf(key, sizeof(key), "pin%u", i);
      store.strTo(key, "", settings.pinned[i], PINNED_NAME_MAX);
    }
  }

  store.end();
}

void settingsSave() {
  store.beginWrite();

  store.put("bright", settings.brightness);
  sleepSave(store, settings.night);
  store.put("pinN", (int)settings.pinnedCount);
  for (uint8_t i = 0; i < settings.pinnedCount; i++) {
    char key[8];
    snprintf(key, sizeof(key), "pin%u", i);
    store.put(key, settings.pinned[i]);
  }

  store.end();
}
