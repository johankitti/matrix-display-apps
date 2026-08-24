# Pokédex Display — project notes for agents

Firmware for an ESP32-S3-Zero driving a 64×64 HUB75 RGB LED panel. It shows a
Pokémon (sprite on top, name across the bottom), holds it for a configurable
number of seconds (default 10), then switches. Sprites are animated (Gen-V GIF)
or static (PNG) per the settings. Duration / order / animation are configured over
Wi-Fi (see **Settings web UI**); **brightness** is a physical rotary encoder (and
also on the web page).

## Hardware
Full wiring/power reference: `docs/hardware-reference.md`. Pin map lives in
`include/config.h`. HUB75 uses GPIO1–13 + 43. Inputs (`src/input.*`):
- **Rotary encoder** (Bourns PEC11R / electrokit art. 41021049) on GPIO44 (A) +
  GPIO14 (B), common→GND — the two remaining free pins. Turn = brightness.
- **Button** on GPIO0 = onboard BOOT: at boot it forces the Wi-Fi portal; at
  runtime a press skips to the next Pokémon. (Wire the encoder's push switch in
  parallel to GND↔GPIO0 if you want it on the knob — but never hold it at reset,
  GPIO0 is a strapping pin.) The encoder's A/B use both free GPIOs, so the switch
  has no dedicated pin.

## Wi-Fi setup (captive portal — no secrets file)
- Credentials are provisioned on-device via **WiFiManager** and stored in NVS by
  the ESP core. There is no `secrets.h` and nothing to compile in.
- The finished unit has **no setup button**, so boot is designed to never get
  stuck (`netStart()` in `src/net.*`), cycling until connected:
  1. **Saved creds:** patiently retry the saved network for `NET_CONNECT_TIMEOUT_MS`
     (3 min), showing a "connecting to Wi-Fi" screen. This rides out a router that's
     still booting after a power cut — it does **not** bail to the portal on the
     first miss.
  2. **No creds / still failing:** open the `Pokedex-Setup` AP (see `AP_SETUP_SSID`);
     the panel shows the join screen + portal address. The portal has a timeout
     (`NET_PORTAL_TIMEOUT_SEC`, 3 min); if unconfigured it **loops back to step 1**,
     so a transient outage self-heals and a changed network can still be set up.
- Mid-run drops reconnect silently (`netEnsure` + the driver's auto-reconnect)
  without reopening the portal.
- **Re-configure without the button:** the settings web UI has a **"Reconfigure
  Wi-Fi"** button (`/wifireset`) that erases saved creds and reboots into setup.

## Settings web UI
- After Wi-Fi connects, the device runs a small `WebServer` (`src/web.*`) + mDNS
  at **http://pokedex-display.local/** (`HOSTNAME`), or the IP if `.local` doesn't
  resolve. The page edits three settings, saved to NVS (`src/settings.*`):
  - **Seconds per slide** (`DURATION_MIN_SEC`..`MAX`).
  - **Order** — random, or sequential 1→last (wraps).
  - **Animation** — `full` (whole dex, animated where available + static fallback),
    `only` (just the animated range 1–`ANIM_MAX_ID`), or `static` (all PNG).
  - **Brightness** — slider (`BRIGHTNESS_MIN`..`MAX`); mirrors the encoder.
  - **Animation speed** — slider, % of the GIF's authored timing
    (`ANIM_SPEED_MIN`..`MAX`; 100 = original design). `displayAnimTick` scales each
    frame's authored delay and schedules drift-free from the previous target.
- Settings are read live by the core-0 fetch task; the web handler writes them on
  the main loop. Changes apply on the next slide. Brightness/speed apply immediately.

## Build / flash
```
pio run -e s3mini                      # compile
pio run -e s3mini -t upload -t monitor # flash + serial (USB power only)
```
Power rule: never have two 5 V sources at once (laptop USB **or** the 5 V supply,
never both). Keep brightness ~20 while testing on USB; ~90 once on the supply.

## Layout
- `src/main.cpp` — slideshow loop; sprites prefetched on a **core-0 task** so the
  network never blocks animation on core 1 (`pendReady` handshake). Serves the web
  UI via `webTick()`.
- `src/settings.*` — `Settings` struct (duration / order / animMode / brightness) + NVS load/save.
- `src/input.*` — ESP32Encoder quadrature (brightness) + BOOT button (next Pokémon).
- `src/web.*` — `WebServer` + mDNS settings page.
- `src/net.*` — Wi-Fi + HTTPS sprite fetch into a PSRAM buffer.
- `src/display.*` — HUB75 init; one shared PSRAM canvas + crop/blit pipeline used
  by both **AnimatedGIF** (`displayAnimStart`/`displayAnimTick`, looped for the
  slide, GIF disposal honored) and **PNGdec** static sprites (`displayShowStatic`,
  a single non-ticking frame). Name/duration/status text.
- `src/pokemon.*` — next id (random or sequential), sprite URL + type per settings,
  animated→static fallback, fetch orchestration.
- `src/names.h` — AUTO-GENERATED dex names; regenerate with `python3 tools/gen_names.py`.

## Tuning knobs (config.h)
- `DEX_MIN`/`DEX_MAX` — full dex range (1–1025). `ANIM_MAX_ID` (649) is the last id
  with a Gen-V animated GIF; past it the static PNG is used.
- `DEFAULT_DURATION_SEC`, `DURATION_MIN_SEC`/`MAX` — slide timing + clamp.
- `ANIM_MIN_FRAME_MS` — floor on GIF per-frame delay.
- `HOSTNAME` — mDNS name for the settings UI.
- `PANEL_BRIGHTNESS` (default for a fresh device), `BRIGHTNESS_MIN`/`MAX`/`STEP`,
  `ENC_COUNTS_PER_DETENT` — brightness + encoder tuning. `BRIGHTNESS_MAX` is capped
  (140, not 255) to keep panel current under a 5V/3A supply; raise it only with a
  bigger supply.
- `NAME_BOTTOM_PAD`, sprite layout constants.

If colors look swapped or the image is shifted, flip the `gif.begin()` palette
arg (`GIF_PALETTE_RGB565_LE` ↔ `_BE`) in `display.cpp` and/or set `cfg.clkphase`
in `displayInit()`.
