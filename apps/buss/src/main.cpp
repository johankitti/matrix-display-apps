// buss-display — live SL bus departures on a 64x64 HUB75 panel.
//
// Loop: poll the SL Transport API every 30s, keep {line, destination, display,
// group_of_lines} for the next departures, render them as one row each.
// SL pre-computes the countdown string ("Nu", "5 min", "17:42") server-side,
// so there is no clock handling here at all.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <display_core.h>    // shared panel object (dma_display) + text/utf8 helpers
#include <input_core.h>      // rotary-encoder brightness knob + button
#include <Fonts/TomThumb.h>

#include "config.h"
#include "net.h"
#include "settings.h"
#include "web.h"

struct Departure {
  char line[6];    // "802", "812C"
  char dest[24];   // folded to ASCII, truncated at render
  char time[8];    // "Nu", "5min", "17:42"
  bool isBlue;     // Blåbuss -> blue line number, otherwise SL red
};

static Departure rows[MAX_ROWS];
static int rowCount = 0;
static bool stale = true;          // no successful fetch yet / last fetch failed
static unsigned long nextFetchAt = 0;

// ---- status LED (onboard WS2812) --------------------------------------------
static void statusLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(PIN_STATUS_LED, r, g, b);
}

// utf8Fold() (UTF-8 -> ASCII, folding the Swedish accents TomThumb can't draw)
// now lives in display-core; SL data ("Sköndal", "Tyresö") passes through it.

// Minutes until departure, from SL's display string: "Nu" -> 0, "5 min" -> 5,
// clock times like "17:42" only appear far in the future -> effectively infinite.
static int displayMinutes(const char *s) {
  if (strchr(s, ':')) return 999;
  if (isdigit((uint8_t)s[0])) return atoi(s);
  return 0;  // "Nu"
}

// "5 min" -> "5m"; "Nu" and "17:42" pass through unchanged.
static void compactTime(const char *src, char *dst, size_t dstSize) {
  size_t o = 0;
  for (const char *p = src; *p && o < dstSize - 1; p++) {
    if (*p == ' ') continue;
    dst[o++] = *p;
  }
  dst[o] = '\0';
  if (o >= 3 && strcmp(dst + o - 3, "min") == 0) dst[o - 2] = '\0';
}

// ---- fetch + parse -----------------------------------------------------------
static bool fetchDepartures() {
  char url[160];
  snprintf(url, sizeof(url), "%s/sites/%u/departures?transport=BUS&forecast=%d",
           SL_API_BASE, settings.siteId, SL_FORECAST_MIN);

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.useHTTP10(true);  // ArduinoJson streams the body; HTTP/1.0 avoids chunked encoding
  if (!http.begin(url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP error: %d\n", code);
    http.end();
    return false;
  }

  // Filtered parse: of the ~40KB response, only these fields are retained.
  JsonDocument filter;
  filter["departures"][0]["destination"] = true;
  filter["departures"][0]["display"] = true;
  filter["departures"][0]["state"] = true;
  filter["departures"][0]["direction_code"] = true;
  filter["departures"][0]["line"]["designation"] = true;
  filter["departures"][0]["line"]["group_of_lines"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return false;
  }

  // Filter cancellations and unwanted directions BEFORE truncating to
  // MAX_ROWS, so dropped departures are backfilled by later ones.
  int n = 0;
  for (JsonObject dep : doc["departures"].as<JsonArray>()) {
    if (n >= MAX_ROWS) break;
    const char *state = dep["state"] | "";
    if (strcmp(state, "CANCELLED") == 0) continue;
    if (settings.direction != 0 &&
        (dep["direction_code"] | 0) != settings.direction)
      continue;
    // Too soon to catch — hide it (settings.minMinutes, default 3).
    if (displayMinutes(dep["display"] | "") < settings.minMinutes) continue;

    Departure &d = rows[n];
    utf8Fold(dep["line"]["designation"] | "?", d.line, sizeof(d.line));
    utf8Fold(dep["destination"] | "?", d.dest, sizeof(d.dest));
    char rawTime[10];
    utf8Fold(dep["display"] | "?", rawTime, sizeof(rawTime));
    compactTime(rawTime, d.time, sizeof(d.time));
    d.isBlue = strcmp(dep["line"]["group_of_lines"] | "", "Blåbuss") == 0;
    n++;
  }
  rowCount = n;
  Serial.printf("fetched %d departures\n", n);
  return true;  // zero rows is still a good fetch (e.g. night time) -> "no buses"
}

// ---- rendering ---------------------------------------------------------------
// Thin wrappers over display-core's text helpers. Kept because buss's call sites
// use drawText(s, x, y, color) — argument order differs from display-core's
// displayText(x, baseY, s, color), so the wrapper just reorders and delegates.
static uint16_t textWidth(const char *s) { return displayTextWidth(s); }

static void drawText(const char *s, int x, int baselineY, uint16_t color) {
  displayText(x, baselineY, s, color);
}

static void render() {
  dma_display->clearScreen();
  dma_display->setFont(&TomThumb);
  dma_display->setTextWrap(false);

  int y = FIRST_ROW_Y;
#if SHOW_HEADER
  char header[24];
  utf8Fold(settings.stationName, header, sizeof(header));
  drawText(header, PAD, HEADER_BASE_Y, COLOR_HEADER);
  dma_display->drawFastHLine(PAD, SEPARATOR_Y, PANEL_WIDTH - 2 * PAD,
                             COLOR_SEPARATOR);
#endif

  if (rowCount == 0) {
    drawText(stale ? "no data" : "no buses", COL_LINE_X, y, COLOR_DEST);
  }

  for (int i = 0; i < rowCount; i++, y += ROW_PITCH) {
    Departure &d = rows[i];

    drawText(d.line, COL_LINE_X, y, d.isBlue ? COLOR_LINE_BLUE : COLOR_LINE_RED);

    uint16_t tw = textWidth(d.time);
    int timeX = PANEL_WIDTH - PAD - tw;
    drawText(d.time, timeX, y, COLOR_TIME);

    // TomThumb is proportional; truncate the destination by measured width.
    char dest[24];
    strlcpy(dest, d.dest, sizeof(dest));
    int destMax = timeX - COL_GAP_PX - COL_DEST_X;
    while (dest[0] && (int)textWidth(dest) > destMax) dest[strlen(dest) - 1] = '\0';
    drawText(dest, COL_DEST_X, y, COLOR_DEST);
  }

  if (stale) dma_display->drawPixel(PANEL_WIDTH - 1, 0, COLOR_STALE);
}

// Connection progress on the panel, called from net.cpp.
void netShowStatus(const char *line1, const char *line2) {
  dma_display->clearScreen();
  dma_display->setFont(&TomThumb);
  drawText("buss-display", PAD, HEADER_BASE_Y, COLOR_HEADER);
  drawText(line1, PAD, FIRST_ROW_Y, COLOR_DEST);
  if (line2) drawText(line2, PAD, FIRST_ROW_Y + ROW_PITCH, COLOR_DEST);
}

// Rotary encoder -> live brightness, persisted once the knob goes idle. The
// read/step/clamp/debounced-save logic is shared (input-core); we hand it buss's
// brightness field, the clamp/step tuning, and settingsSave to persist.
static void handleBrightnessKnob() {
  if (brightnessKnobTick(settings.brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX,
                         BRIGHTNESS_STEP, BRIGHTNESS_SAVE_IDLE_MS, settingsSave))
    Serial.printf("[main] brightness=%u\n", settings.brightness);
}

// ---- arduino entry points ----------------------------------------------------
void setup() {
  Serial.begin(115200);
  settingsLoad();

  // display-core does the HUB75 config/pins/begin using board-config's pin map;
  // buss draws with the 3x5 TomThumb font.
  displayCoreInit(false, &TomThumb);
  displaySetBrightness(settings.brightness);   // saved brightness (knob-adjustable)
  inputInit();                                 // rotary-encoder brightness knob

  statusLed(0, 0, 32);  // blue: connecting
  netStart();           // NVS creds + portal fallback; blocks until connected
  statusLed(0, 8, 0);   // dim green: ok

  // Start the clock over NTP so the night schedule knows the local hour.
  sleepBeginNtp(settings.night);

  webStart();
  Serial.printf("settings: http://%s.local/\n", HOSTNAME);
}

void loop() {
  // Inside the night window: blank the panel and deep-sleep till morning.
  if (sleepIsNight(settings.night)) sleepUntilMorning(settings.night);

  webHandle();
  handleBrightnessKnob();   // rotary encoder -> live brightness (+ debounced save)

  // Transient drops recover via WiFi.setAutoReconnect; fetches just fail and
  // retry meanwhile, keeping the last good rows + stale marker on screen.
  if (webSettingsChanged()) nextFetchAt = 0;  // new station/direction: refetch now

  if (millis() >= nextFetchAt) {
    statusLed(16, 16, 0);  // yellow: fetching
    bool ok = fetchDepartures();
    stale = !ok;
    statusLed(0, ok ? 8 : 0, 0);
    if (!ok) statusLed(32, 0, 0);  // red: fetch failed, showing stale data
    nextFetchAt = millis() + (ok ? POLL_INTERVAL_MS : RETRY_INTERVAL_MS);
    render();
  }

  delay(50);
}
