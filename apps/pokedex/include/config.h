#pragma once
// -----------------------------------------------------------------------------
// Pokédex Display — build-time configuration
//
// The HUB75 pin map and panel geometry (PANEL_WIDTH/HEIGHT/CHAIN, PIN_R1..PIN_OE,
// PIN_STATUS_LED) are shared across all apps and live in the board-config package
// — see packages/board-config/src/board_config.h. The rotary encoder pins below
// are app-specific and stay here.
// -----------------------------------------------------------------------------

#include <board_config.h>   // pin map + panel geometry (shared)

// ---- Night mode (sleep-core) -------------------------------------------------
// First-boot defaults for the night schedule; after that the values are stored
// in NVS and edited from the settings web page. Between these local hours the
// panel blanks and the device deep-sleeps (~µA), waking itself in the morning.
// The clock is set over NTP once Wi-Fi is up (see sleepBeginNtp in main.cpp).
#define NIGHT_MODE_ENABLED true
#define NIGHT_START_HOUR   1     // panel off at 01:00...
#define NIGHT_END_HOUR     7     // ...back on at 07:00
#define TIMEZONE_POSIX     "CET-1CEST,M3.5.0,M10.5.0/3"   // Sweden (CET/CEST)

// Brightness 0-255. ~20 is safe on USB power while testing; ~90 is comfortable
// indoors once running off the dedicated 5V supply. PANEL_BRIGHTNESS is only the
// default for a fresh device — the live value is stored in NVS and set by the
// rotary encoder / web slider (see settings.*).
#define PANEL_BRIGHTNESS 20
#define BRIGHTNESS_MIN          2     // never fully off (0 looks like a dead panel)
// Capped (not 255) to bound current draw to the 5V/3A supply: brightness is ~linear
// with panel current, whose full-white max is ~4A (20W) at 255. 140/255 keeps even
// an all-white frame near ~2.2A panel + ~0.5A ESP = ~2.7A, under 3A with margin.
// Real sprites (black background) draw far less. Raise only with a bigger supply
// (or after measuring actual draw at max brightness on a bright test image).
#define BRIGHTNESS_MAX          140
#define BRIGHTNESS_STEP         8     // brightness change per encoder detent
#define BRIGHTNESS_SAVE_IDLE_MS 1500  // persist to NVS this long after the last turn

// ---- Rotary encoder (electrokit art. 41021049, 24 detents + push) ------------
// HUB75 consumes GPIO1-13 + 43, so only GPIO44 is free on the side rows; the
// encoder's second signal lands on a bottom-edge pin (see hardware-reference §2).
// The encoder is a passive mechanical switch: common pin -> GND, internal
// pull-ups on A/B/SW, no VCC required.
#define PIN_ENC_A   44   // CLK  (free side-row pad, was "RX")
#define PIN_ENC_B   14   // DT   (bottom edge — back-side solder)
#define PIN_ENC_SW  0    // push: onboard BOOT button (no extra wire). Set to the
                         // encoder's own SW pin (e.g. 15) if you wire it instead.

// A 24-detent encoder driven full-quadrature yields ~4 counts per physical click.
// Tune if one detent doesn't map cleanly to one step.
#define ENC_COUNTS_PER_DETENT 4

// ---- Slideshow behaviour -----------------------------------------------------
#define DEX_MIN               1     // national dex range to draw ids from
#define DEX_MAX               1025  // full national dex. In "animated" mode ids past
                                    // ANIM_MAX_ID fall back to the static sprite.
#define ANIM_MAX_ID           649   // last id with a Gen-V animated GIF (1-649)
#define DEFAULT_DURATION_SEC  10    // seconds each Pokémon is shown
#define DURATION_MIN_SEC      1
#define DURATION_MAX_SEC      300

// Runtime settings (duration, order, animation) are configured over Wi-Fi at
// http://<HOSTNAME>.local/ and persisted in NVS — see settings.* / web.*.
#define HOSTNAME              "pokedex-display"

// ---- Sprite + name layout on the 64x64 panel ---------------------------------
// Default GFX font @ size 1: glyph 5x7 in a 6x8 cell -> 6px advance/char, 8px
// pitch between lines (7px visible + 1px). The name is pinned to the bottom rows
// (one or two lines with a 1px gap) and the sprite fills the rest above it. The
// exact split is computed at draw time from the name (see display.cpp), so a
// short name gets a taller sprite than a wrapped one.
#define TEXT_CHAR_W         6
#define TEXT_LINE_H         8   // vertical pitch between the two name lines
#define TEXT_GLYPH_H        7   // visible glyph height
#define NAME_MAX_LINES      2
#define NAME_CHARS_PER_LINE (PANEL_WIDTH / TEXT_CHAR_W)   // 10
#define LAYOUT_GAP          0   // no gap between sprite and name (sprite fills down to the name)
#define NAME_BOTTOM_PAD     1   // blank rows below the name (matches the ~1px inset
                                // above the dex number), so the name isn't flush
                                // against the very bottom edge
#define SPRITE_OFFSET_Y     3   // nudge the sprite down N px (bottom rows clip under name)

// ---- Networking --------------------------------------------------------------
// Sprite sources on the PokéAPI sprites CDN:
//  - ANIM: animated Gen-V "Black/White" pixel GIF (~96px front), ids 1-649 only.
//  - STATIC: classic front_default pixel PNG (96x96 RGBA), full national dex.
#define ANIM_SPRITE_URL_FMT   "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/versions/generation-v/black-white/animated/%d.gif"
#define STATIC_SPRITE_URL_FMT "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon/%d.png"
#define HTTP_TIMEOUT_MS   8000
#define FETCH_MAX_RETRIES 4        // try up to N different Pokémon before giving up

// ---- Animation ---------------------------------------------------------------
// Floor on per-frame delay: some GIFs encode 0ms frames, which would spin the CPU
// and blur the motion. Clamp so playback never exceeds ~50 fps.
#define ANIM_MIN_FRAME_MS 20

// Playback speed as a percentage of the GIF's authored per-frame timing. 100 = the
// sprites' original design (Gen 1-4 ~10 fps, native Gen-5 ~16.7 fps, hold frames
// preserved). Higher = faster. Adjustable via the web slider (settings.*/web.*).
#define ANIM_SPEED_DEFAULT 100
#define ANIM_SPEED_MIN     25    // 4x slower
#define ANIM_SPEED_MAX     400   // 4x faster

// Delay after a slide appears before the NEXT sprite is prefetched, so the network
// download doesn't compete with the fresh sprite's decode during the opening
// moment (keeps animation starting cleanly). Capped at half the slide for short
// durations. There's still plenty of lead time before a normal slide ends.
#define PREFETCH_LEAD_MS 1200

// ---- Wi-Fi setup portal ------------------------------------------------------
// Credentials are provisioned via WiFiManager's captive portal (stored in NVS),
// not compiled in. Join this AP during setup to configure your network.
#define AP_SETUP_SSID   "Pokedex-Setup"

// Headless resilience (no setup button on the finished unit): on boot, patiently
// retry the SAVED network for NET_CONNECT_TIMEOUT_MS (covers a router still coming
// up after a power cut) before offering the setup portal. The portal itself times
// out after NET_PORTAL_TIMEOUT_SEC and we retry the saved network again — so the
// device can never get permanently stuck in either "connecting" or "setup".
#define NET_CONNECT_TIMEOUT_MS  180000   // 3 min retrying saved creds
#define NET_PORTAL_TIMEOUT_SEC  180      // then a 3 min portal window, then retry
