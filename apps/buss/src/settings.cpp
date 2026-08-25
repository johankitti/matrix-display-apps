#include "settings.h"

#include <settings_core.h>   // shared PrefsStore (NVS) wrapper

#include "config.h"

Settings settings;

// NVS namespace — unchanged from the standalone app, so existing devices keep
// their saved settings after the monorepo migration.
static PrefsStore store("buss");

void settingsLoad() {
  store.beginRead();
  settings.siteId = store.u32("siteId", DEFAULT_SITE_ID);
  store.strTo("name", DEFAULT_STATION_NAME, settings.stationName,
              sizeof(settings.stationName));
  settings.direction = store.u8("dir", 0);
  settings.minMinutes = store.u8("minMin", DEFAULT_MIN_MINUTES);
  settings.brightness = store.u8("bright", PANEL_BRIGHTNESS);
  sleepLoad(store, settings.night, NIGHT_MODE_ENABLED, NIGHT_START_HOUR,
            NIGHT_END_HOUR, TIMEZONE_POSIX);
  store.end();
}

void settingsSave() {
  store.beginWrite();
  store.put("siteId", (uint32_t)settings.siteId);
  store.put("name", settings.stationName);
  store.put("dir", settings.direction);
  store.put("minMin", settings.minMinutes);
  store.put("bright", settings.brightness);
  sleepSave(store, settings.night);
  store.end();
}
