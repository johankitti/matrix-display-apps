#pragma once
// -----------------------------------------------------------------------------
// buss-display — build-time configuration
//
// The HUB75 pin map and panel geometry (PANEL_WIDTH/HEIGHT/CHAIN, PIN_*,
// PIN_STATUS_LED) are shared across all apps and live in the board-config
// package — see packages/board-config/src/board_config.h.
// -----------------------------------------------------------------------------

#include <board_config.h>   // pin map + panel geometry (shared)

// Brightness 0-255. ~20 is safe on USB power while testing; ~90 is comfortable
// indoors once running off the dedicated 5V supply. This display is mostly-dark
// text so current draw is far below the full-white worst case. PANEL_BRIGHTNESS
// is only the first-boot default; the live value is stored in NVS and set by the
// rotary encoder (input-core).
#define PANEL_BRIGHTNESS 20

// Rotary-encoder brightness knob (input-core). Pins + counts-per-detent come
// from board-config (GPIO44/14, BOOT switch). These bound + pace the knob:
#define BRIGHTNESS_MIN          2     // never fully off (0 looks like a dead panel)
#define BRIGHTNESS_MAX          140   // capped for the 5V/3A supply (see pokedex note)
#define BRIGHTNESS_STEP         8     // brightness change per encoder detent
#define BRIGHTNESS_SAVE_IDLE_MS 1500  // persist to NVS this long after the last turn

// ---- Night mode (sleep-core) -------------------------------------------------
// First-boot defaults for the night schedule; after that the values live in NVS
// and are edited from the settings web page. Between these local hours the panel
// blanks and the device deep-sleeps (~µA), waking itself in the morning. The
// clock is set over NTP once Wi-Fi is up (see sleepBeginNtp in main.cpp).
#define NIGHT_MODE_ENABLED true
#define NIGHT_START_HOUR   1     // panel off at 01:00...
#define NIGHT_END_HOUR     7     // ...back on at 07:00
#define TIMEZONE_POSIX     "CET-1CEST,M3.5.0,M10.5.0/3"   // Sweden (CET/CEST)

// ---- SL Transport API (free, no key — see README) ----------------------------
// Station + direction are runtime settings (http://buss-display.local/, stored
// in NVS); these are only the defaults for a fresh device.
// Plain HTTP works on this API (verified) and skips ~40KB of TLS heap.
#define SL_API_BASE          "http://transport.integration.sl.se/v1"
#define SL_FORECAST_MIN      90     // look-ahead window, minutes
#define DEFAULT_SITE_ID      1810   // Norra Sköndal
#define DEFAULT_STATION_NAME "Norra Sköndal"
#define DEFAULT_MIN_MINUTES  3      // hide buses you can't reach anyway
#define HOSTNAME             "buss-display"   // -> http://buss-display.local/

// ---- Wi-Fi (same pattern as the pokedex/golf builds) --------------------------
// Credentials live in the ESP32's NVS, provisioned once via WiFiManager's
// captive portal — nothing compiled in. This board already has them saved.
// Headless resilience: on boot, patiently retry the SAVED network (covers a
// router still coming up after a power cut) before offering the setup portal;
// the portal times out and we retry the saved network again — the device can
// never get permanently stuck in either phase.
#define AP_SETUP_SSID           "Buss-Setup"
#define NET_CONNECT_TIMEOUT_MS  180000   // 3 min retrying saved creds
#define NET_PORTAL_TIMEOUT_SEC  180      // then a 3 min portal window, then retry

#define POLL_INTERVAL_MS  30000   // matches SL's own ~30s cache guidance
#define RETRY_INTERVAL_MS 10000   // faster retry after a failed fetch
#define HTTP_TIMEOUT_MS   8000

// ---- Layout on the 64x64 panel (TomThumb 3x5 font, 4px advance, 6px pitch) ---
// 1px padding on all four edges. Header glyphs sit on rows 1-5 (baseline 6),
// then a 1px gap, the divider on row 7, another 1px gap, and departure rows
// every 6px from baseline 14 -> 9 rows (14..62). SHOW_HEADER 0 gives 10 rows.
#define PAD            1
#define SHOW_HEADER    1
#define ROW_PITCH      6
#define HEADER_BASE_Y  (PAD + 5)             // glyph rows 1-5
#define SEPARATOR_Y    (HEADER_BASE_Y + 1)   // 1px gap above and below the line
#define FIRST_ROW_Y    (SHOW_HEADER ? (SEPARATOR_Y + 2 + 5) : (PAD + 5))
#define MAX_ROWS       (SHOW_HEADER ? 9 : 10)

#define COL_LINE_X   PAD         // line number, up to 4 chars ("812C")
#define COL_DEST_X   (PAD + 16)  // 5px gap after a 3-char line, 1px after "181C"
#define COL_GAP_PX   2           // min gap between destination and time

// ---- Colors (RGB565) ---------------------------------------------------------
// SL paints its buses red, and the trunk lines ("Blåbuss", e.g. 172/173) blue —
// the line-number color mirrors that. group_of_lines comes from the API.
#define COLOR_HEADER     dma_display->color565(255, 255, 255)
#define COLOR_SEPARATOR  dma_display->color565(80, 80, 80)
#define COLOR_LINE_RED   dma_display->color565(255, 55, 40)
#define COLOR_LINE_BLUE  dma_display->color565(60, 130, 255)
#define COLOR_DEST       dma_display->color565(200, 200, 200)
#define COLOR_TIME       dma_display->color565(120, 255, 120)
#define COLOR_STALE      dma_display->color565(255, 0, 0)
