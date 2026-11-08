#pragma once
// -----------------------------------------------------------------------------
// sports — build-time configuration
//
// The HUB75 pin map and panel geometry (PANEL_WIDTH/HEIGHT, PIN_*,
// PIN_STATUS_LED) are shared across all apps and live in the board-config
// package — see packages/board-config/src/board_config.h.
//
// The tracked teams themselves are listed in src/teams.cpp (TEAMS[]).
// -----------------------------------------------------------------------------

#include <board_config.h>   // pin map + panel geometry (shared)

// Brightness 0-255. First-boot default only; the live value is stored in NVS
// and set by the rotary encoder (input-core) or the settings page.
#define PANEL_BRIGHTNESS 20

#define BRIGHTNESS_MIN          2     // never fully off (0 looks like a dead panel)
#define BRIGHTNESS_MAX          140   // capped for the 5V/3A supply (see pokedex note)
#define BRIGHTNESS_STEP         8     // brightness change per encoder detent
#define BRIGHTNESS_SAVE_IDLE_MS 1500  // persist to NVS this long after the last turn

// ---- Night mode (sleep-core) -------------------------------------------------
// First-boot defaults; afterwards edited on the settings page and kept in NVS.
#define NIGHT_MODE_ENABLED true
#define NIGHT_START_HOUR   1     // panel off at 01:00...
#define NIGHT_END_HOUR     7     // ...back on at 07:00
#define TIMEZONE_POSIX     "CET-1CEST,M3.5.0,M10.5.0/3"   // Sweden (CET/CEST)

// ---- Data sources (both keyless) ---------------------------------------------
// ESPN team endpoint — NFL + soccer; its team.nextEvent is the next game
// (~20 KB, vs ~290 KB for the NFL schedule). %s = "<sport>/<league>/teams/<id>".
#define ESPN_TEAM_URL "https://site.api.espn.com/apis/site/v2/sports/%s"
// Live: one event's scoreboard (~10 KB). %s = "<sport>/<league>", %s = event id.
// The league must be the real competition ("soccer/swe.1"), not "all" — the
// "all" variant serves a stale match clock.
#define ESPN_EVENT_URL \
  "https://site.api.espn.com/apis/site/v2/sports/%s/scoreboard/%s"
// Today's games in a league — only used right after boot, to catch a game that
// is already in progress (the team schedule omits live games).
#define ESPN_LEAGUE_SCOREBOARD_URL \
  "https://site.api.espn.com/apis/site/v2/sports/%s/scoreboard"
// TheSportsDB — Swedish hockey (ESPN has no SHL/HockeyAllsvenskan). "123" is
// their public free-tier key. %s = TheSportsDB team id.
#define TSDB_NEXT_URL \
  "https://www.thesportsdb.com/api/v1/json/123/eventsnext.php?id=%s"
// HockeyAllsvenskan's own site API (what hockeyallsvenskan.se polls for its
// live games, ~4 KB): score, period, completed. %s = game slug.
#define HA_GAME_URL "https://hockeyallsvenskan.se/api/game?slug=%s"

#define HOSTNAME "sports-display"   // -> http://sports-display.local/

// ---- Wi-Fi (net-core) --------------------------------------------------------
#define AP_SETUP_SSID           "Sports-Settings"
#define NET_CONNECT_TIMEOUT_MS  180000   // 3 min retrying saved creds
#define NET_PORTAL_TIMEOUT_SEC  180      // then a 3 min portal window, then retry

// ---- Refresh cadence ---------------------------------------------------------
// Fixtures barely move, so the idle poll is slow. From kick-off a team
// switches to its live feed and polls it every minute for the score; after
// the final whistle the result stays up for FINAL_HOLD_S, then the next
// fixture rolls in.
#define POLL_IDLE_MS      (30UL * 60 * 1000)   // 30 min
#define POLL_LIVE_MS      (60UL * 1000)        // 1 min while live
#define POLL_RETRY_MS     (60UL * 1000)        // after a failed fetch
#define POLL_STALE_MS     (10UL * 60 * 1000)   // past kick-off with no live feed
#define HTTP_TIMEOUT_MS   15000

// A game whose kick-off passed this long ago, and that the feed still lists
// as upcoming, is assumed over (feeds lag) and stops showing as LIVE.
#define GAME_MAX_LENGTH_S (4 * 3600)
// How long the final score ("FT 2-1") stays up after the game ends.
#define FINAL_HOLD_S      (2 * 3600)
// Safety cap on following a game the live feed still calls in progress.
#define LIVE_MAX_S        (8 * 3600)

// ---- Layout on the 64x64 panel (TomThumb 3x5 font) ---------------------------
// Four 16px blocks, one per team. In each block:
//   rows 1-5   matchup "AIK-BKH" + date/time right-aligned
//   rows 8-12  countdown "3d 04:12:09"
//   row 15     dim divider (not after the last block)
// A 7x7 sport icon sits at the left, vertically centred on the two text rows.
#define BLOCK_H        16
#define ROW_A_BASE     6     // baseline offsets within a block (glyph rows 1-5)
#define ROW_B_BASE     13    //                                 (glyph rows 8-12)
#define DIVIDER_Y      15
#define ICON_X         1
#define ICON_Y         3     // 7px tall -> rows 3-9 of the block
#define TEXT_X         10
#define RIGHT_EDGE     (PANEL_WIDTH - 2)   // last lit column (1px pad on the right)

// ---- Colors (RGB565) ---------------------------------------------------------
#define COLOR_OPPONENT  dma_display->color565(190, 190, 190)
#define COLOR_DASH      dma_display->color565(110, 110, 110)
#define COLOR_WHEN      dma_display->color565(120, 120, 120)
#define COLOR_DIVIDER   dma_display->color565(35, 35, 35)
#define COLOR_LIVE      dma_display->color565(255, 40, 40)
#define COLOR_SCORE     dma_display->color565(255, 255, 255)
#define COLOR_CLOCK     dma_display->color565(150, 150, 150)
#define COLOR_FINAL     dma_display->color565(150, 150, 150)
#define COLOR_NOGAME    dma_display->color565(90, 90, 90)
#define COLOR_STALE     dma_display->color565(255, 0, 0)
