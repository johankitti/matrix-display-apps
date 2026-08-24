// ============================================================================
//  Golf Live Update
//  Live PGA Tour leaderboard on a 64x64 HUB75 RGB LED matrix (ESP32-S3).
//
//  Data: ESPN's public scoreboard JSON, refreshed every 5 minutes.
//  See README.md for wiring and configuration.
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <time.h>

#include "config.h"
// secrets.h is optional: the real board gets Wi-Fi from its on-device setup
// portal and needs no credentials file, and the Wokwi build supplies WIFI_SSID/
// WIFI_PASS via build flags. Include it only when present so a fresh checkout
// builds without it. (It's still the place to hand-set creds if you want them.)
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#include "display.h"
#include "leaderboard.h"
#include "settings.h"

#ifndef WOKWI
#include <net_core.h>      // shared resilient Wi-Fi provisioning (WiFiManager)
#include "webconfig.h"     // settings web page (golfboard.local)
#endif

static Leaderboard board;
static bool haveData = false;      // ever fetched successfully?
static bool lastFetchOk = false;
static uint32_t lastAttemptMs = 0;
static uint32_t nextDelayMs = 0;   // 0 = fetch immediately
static volatile bool refreshRequested = false;  // set by the web "Refresh now" button

// ---------------------------------------------------------------------------
// Night schedule (sleep-core). The system clock is set from the HTTP Date
// header of each fetch (see leaderboard.cpp) and survives deep sleep on the
// internal RTC — until the first fetch after a cold boot the clock is invalid
// and sleepIsNight() stays out of the way on its own.
// ---------------------------------------------------------------------------

// If we're inside the night window, park the panel and deep-sleep till morning.
// The loading animation runs on its own task, so stop it first.
static void checkNight() {
  if (sleepIsNight(settings.night)) {
    displayLoadingStop();
    sleepUntilMorning(settings.night);
  }
}

#ifndef WOKWI
// --- Real board: resilient WiFiManager setup (via net-core) ----------------

// Shown while the board patiently retries its saved network on each attempt.
static void onWifiConnecting() { displayLoading("WIFI"); }

// Shown on the panel when the setup access point comes up, so Wi-Fi can be
// configured with just a phone — no serial console needed.
static void onWifiPortal(const char* ssid, const char* ip) {
  Serial.printf("[wifi] setup AP '%s' up — browse to http://%s\n", ssid, ip);
  displayMessage("WIFI SETUP", "JOIN WIFI", ssid);
}

static void connectWiFi() {
  // net-core cycles saved-network retries with timed portal windows and never
  // dead-ends — exactly what a headless board waking from deep sleep each
  // morning needs (the router may not be up the instant it wakes). Networks
  // are changed from the web page's "Reconfigure Wi-Fi" button.
  NetConfig cfg;
  cfg.apSsid = WIFI_SETUP_AP_NAME;
  cfg.connectTimeoutMs = 180000;   // patiently retry the saved network (3 min)
  cfg.portalTimeoutSec = 180;      // then a 3 min portal window, then retry
  cfg.onConnecting = onWifiConnecting;
  cfg.onPortal = onWifiPortal;
  netStart(cfg);
  Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
}

#else
// --- Wokwi simulator: no phone for a portal, use build-flag creds ----------

static void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  displayLoading("WIFI");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\n[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
}
#endif

void setup() {
  Serial.begin(115200);

#ifndef WOKWI
  neopixelWrite(ONBOARD_RGB_LED_PIN, 0, 0, 0);  // blank the unused onboard WS2812
#endif

  settingsLoad();

  // Timezone is a runtime setting (edited from the web page); config.h only
  // supplies the first-boot default. The clock itself stays UTC — this rule
  // just governs how times are displayed and when night mode kicks in.
  sleepApplyTimezone(settings.night);

  // Woke from deep sleep but morning hasn't actually arrived (clock kept
  // ticking through sleep): go straight back down without lighting the panel.
  checkNight();

  displayReleaseHolds();
  if (!displayInit()) {
    // Without a working panel there is nothing to show — log and halt.
    while (true) {
      Serial.println("[panel] DMA init failed — check wiring and pins in config.h");
      delay(5000);
    }
  }

  connectWiFi();
#ifndef WOKWI
  webconfigBegin(&board, &refreshRequested);
#endif
  displayLoading("FETCHING");
}

void loop() {
#ifndef WOKWI
  webconfigLoop();  // service the settings web page
#endif
  if (refreshRequested) {  // web "Refresh now": force an immediate fetch
    refreshRequested = false;
    nextDelayMs = 0;
  }

  // The clock ticked into the night window (or the first fetch of a late
  // cold boot just made the clock valid): power down until morning.
  checkNight();

  // Wi-Fi dropped: reconnect quietly, keep showing the last leaderboard.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[wifi] lost connection, reconnecting");
    if (!haveData) displayLoading("WIFI LOST");
    WiFi.disconnect();
    WiFi.reconnect();
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
    if (WiFi.status() != WL_CONNECTED) {
      delay(5000);
      return;
    }
  }

  if (millis() - lastAttemptMs >= nextDelayMs || nextDelayMs == 0) {
    lastAttemptMs = millis();

#if DEBUG_FAKE_LIVE
    Serial.println("[debug] DEBUG_FAKE_LIVE: loading fake live leaderboard");
    loadDebugLeaderboard(board);
    lastFetchOk = true;
#else
    Serial.println("[espn] fetching leaderboard...");
    lastFetchOk = fetchLeaderboard(board);
#endif
    // The fetch synced the clock — if it's actually night, sleep before
    // lighting the panel (matters after a cold boot at 3 a.m.).
    checkNight();
    if (lastFetchOk) {
      haveData = true;
      // Live golf changes every few minutes; an upcoming-event screen doesn't.
      nextDelayMs = (board.mode == MODE_LIVE) ? UPDATE_INTERVAL_MS
                                              : IDLE_UPDATE_INTERVAL_MS;
      if (board.mode == MODE_LIVE) {
        Serial.printf("[espn] live: %s (%s), %d leaders, %d pinned\n",
                      board.eventName, board.roundLabel, board.leaderCount,
                      board.pinnedCount);
      } else {
        Serial.printf("[espn] next up: %s (%s), %d pinned golfers\n",
                      board.nextName, board.nextDates, board.nextGolferCount);
      }
    } else {
      nextDelayMs = RETRY_INTERVAL_MS;
      Serial.println("[espn] fetch failed, retrying soon");
    }

    if (haveData) {
      displayLeaderboard(board, lastFetchOk);
    } else {
      displayLoading("RETRYING");  // fetch failed and nothing to show yet
    }
  }

  // Between refreshes, fill a dim meter along the bottom row: 0% right after a
  // fetch, 100% as the next one is due. `lastAttemptMs`/`nextDelayMs` are the
  // exact schedule the fetch gate above uses, so the bar tracks it precisely.
  static uint32_t lastProgressMs = 0;
  if (haveData && board.mode == MODE_LIVE && nextDelayMs > 0 &&
      millis() - lastProgressMs >= 1000) {
    lastProgressMs = millis();
    displayProgress((float)(millis() - lastAttemptMs) / (float)nextDelayMs);
  }

  delay(100);
}
