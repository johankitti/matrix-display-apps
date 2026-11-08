// sports — next game + live countdown for a handful of favourite teams on a
// 64x64 HUB75 panel.
//
// Each team gets a 16px block: sport icon, "HOME-AWAY" matchup, kick-off
// date/time, and a countdown ticking every second underneath. Fixtures come
// from ESPN (NFL + soccer) and TheSportsDB (Swedish hockey) — both keyless.
// Teams are polled independently: slowly when the game is days away, fast
// around kick-off so LIVE appears and the next fixture rolls in afterwards.

#include <Arduino.h>
#include <WiFi.h>
#include <display_core.h>   // shared panel object + text helpers
#include <input_core.h>     // rotary-encoder brightness knob
#include <Fonts/TomThumb.h>

#include "config.h"
#include "net.h"
#include "screen.h"
#include "settings.h"
#include "teams.h"
#include "web.h"

static time_t lastRenderSec = 0;

static void statusLed(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(PIN_STATUS_LED, r, g, b);
}

// Connection progress on the panel, called from net.cpp.
void netShowStatus(const char* line1, const char* line2) { screenStatus(line1, line2); }

// How long until team i should be fetched again, given what we now know.
static uint32_t nextPollDelay(int i, bool ok) {
  if (!ok) return POLL_RETRY_MS;
  const Game& g = teamState[i].game;
  time_t now = time(nullptr);
  if (!g.known || !sleepClockValid()) return POLL_IDLE_MS;

  if (teamTracking(i, now)) {
    // Live: score every minute. Final: nothing changes until the FT hold
    // runs out, then the next fetch rolls on to the next fixture.
    if (g.state != GS_FINAL) return POLL_LIVE_MS;
    long left = (long)(g.finalSeen + FINAL_HOLD_S - now);
    return left > 0 ? (uint32_t)left * 1000UL + 1000UL : 1000UL;
  }
  switch (teamPhase(i, now)) {
    case PH_COUNTDOWN: {
      // Wake right at kick-off so the live feed takes over promptly.
      long untilStart = (long)(g.start - now);
      if ((uint32_t)untilStart * 1000UL < POLL_IDLE_MS) return (uint32_t)untilStart * 1000UL + 1000UL;
      return POLL_IDLE_MS;
    }
    case PH_LIVE:   // live by the clock but no live feed for this team
    case PH_STALE:  // feed lagging; don't hammer
      return POLL_STALE_MS;
    default:
      return POLL_IDLE_MS;
  }
}

// Fetch at most one due team per call so the countdown keeps ticking between
// fetches (each HTTPS fetch blocks for a second or two).
static void pollTeams(bool forceAll) {
  if (forceAll)
    // teamState starts zeroed (nextFetchMs = 0), so every team is due at once.

  for (int i = 0; i < TEAM_COUNT; i++) {
    if ((int32_t)(millis() - teamState[i].nextFetchMs) < 0) continue;
    if (WiFi.status() != WL_CONNECTED) {
      teamState[i].nextFetchMs = millis() + POLL_RETRY_MS;
      continue;
    }
    statusLed(16, 16, 0);  // yellow: fetching
    bool ok = teamFetch(i);
    statusLed(ok ? 0 : 32, ok ? 8 : 0, 0);  // dim green ok / red failed
    teamState[i].nextFetchMs = millis() + nextPollDelay(i, ok);
    lastRenderSec = 0;  // redraw with the new data right away
    return;
  }
}

static void handleBrightnessKnob() {
  if (brightnessKnobTick(settings.brightness, BRIGHTNESS_MIN, BRIGHTNESS_MAX,
                         BRIGHTNESS_STEP, BRIGHTNESS_SAVE_IDLE_MS, settingsSave))
    Serial.printf("[main] brightness=%u\n", settings.brightness);
}

void setup() {
  Serial.begin(115200);
  displayReleaseHolds();   // undo the deep-sleep pin holds from a night sleep
  settingsLoad();

  // Double-buffered: the whole frame is redrawn every second for the
  // countdown, and drawing off-screen keeps that flicker-free.
  displayCoreInit(true, &TomThumb);
  displaySetBrightness(settings.brightness);
  inputInit();

  statusLed(0, 0, 32);  // blue: connecting
  netStart();           // NVS creds + portal fallback; blocks until connected
  statusLed(0, 8, 0);

  // NTP for the clock; the first HTTPS fetch also sets it from the Date header.
  sleepBeginNtp(settings.night);

  webStart();
  Serial.printf("settings: http://%s.local/\n", HOSTNAME);

  for (int i = 0; i < TEAM_COUNT; i++) teamState[i].nextFetchMs = millis();
  screenRender(time(nullptr), false);
}

void loop() {
  // Inside the night window: blank the panel and deep-sleep till morning.
  if (sleepIsNight(settings.night)) sleepUntilMorning(settings.night);

  webHandle();
  handleBrightnessKnob();
  pollTeams(webRefreshRequested());

  time_t now = time(nullptr);
  if (now != lastRenderSec) {
    lastRenderSec = now;
    screenRender(now, sleepClockValid());
  }

  delay(20);
}
