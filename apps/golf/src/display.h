#pragma once

#include <display_core.h>   // shared panel object + init/brightness/message/night helpers
#include "leaderboard.h"

// Initialises the HUB75 panel driver (via display-core) plus the golf color
// palette. Returns false if DMA setup fails.
bool displayInit();

// displaySetBrightness(), displayMessage(), displayPowerOff() and
// displayReleaseHolds() are provided by display-core (display_core.h).

// Draws the leaderboard. `fetchOk` is currently unused (it used to drive a
// small fresh/stale status dot in the corner, since removed); kept in the
// signature so a staleness indicator can be reintroduced without churn.
void displayLeaderboard(const Leaderboard& lb, bool fetchOk);

// Redraws the live board with a dim progress meter along the very bottom row,
// filling left->right as `frac` (0..1) counts the time toward the next refresh.
// No-op unless a live board is currently showing. Call periodically (~1 Hz).
void displayProgress(float frac);

// Redraws the MODE_NEXT screen so its countdown to the first tee ticks. No-op
// unless `lb` is a NEXT board. Call periodically (~1 Hz) between fetches.
void displayNextTick(const Leaderboard& lb);

// Animated loading screen (rolling golf ball) with a status line, e.g.
// "WIFI" / "FETCHING". The animation runs on its own FreeRTOS task, so it
// keeps moving while the main task blocks on the network. Calling it again
// just updates the status line. Any full-screen render stops it.
void displayLoading(const char* status);
void displayLoadingStop();
