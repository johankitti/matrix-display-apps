<div align="center">

# 🟩 matrix-display-apps

**One 64×64 LED matrix. One ESP32-S3 board. Three apps — and a shared toolkit that powers them all.**

![Platform](https://img.shields.io/badge/platform-ESP32--S3-111?logo=espressif&logoColor=white)
![Framework](https://img.shields.io/badge/framework-Arduino-00979D?logo=arduino&logoColor=white)
![Built with](https://img.shields.io/badge/PlatformIO-passing-brightgreen?logo=platformio&logoColor=white)
![Panel](https://img.shields.io/badge/panel-64×64%20HUB75-ff3860)
![License](https://img.shields.io/badge/license-MIT-3273dc)

</div>

---

A monorepo of ESP32-S3 firmware apps that all drive the **same** 64×64 HUB75 RGB
LED matrix from the **same** board (a Waveshare ESP32-S3-Zero / "S3 mini"). Each
app is a self-contained, independently-flashable PlatformIO project — but the
code they have in common lives in shared **packages**, so a fix or feature
written once benefits every app.

<div align="center">
<table>
<tr>
<th>⛳ &nbsp;golf</th>
<th>🔴 &nbsp;pokedex</th>
<th>🚌 &nbsp;buss</th>
</tr>
<tr valign="top">
<td><pre>
WYNDHAM CHMP    R3
──────────────────
1  SCHEFFLER -14 F
2  MCILROY   -11 15
3  RAHM      -10 12
4  MORIKAWA   -8 F
5  FLEETWOOD  -7 16
──────────────────
T23 ABERG     -2 9
</pre></td>
<td><pre>
      #025

    \  ^__^  /
     ( >‿< )
    /  |  |  \

   P I K A C H U
</pre></td>
<td><pre>
Norra Sköndal
──────────────────
172 Hallunda      Nu
807 Brandbergen   Nu
802 Tyresö cen  2min
181 Farsta str  5min
875 Tyresö kyr  9min
</pre></td>
</tr>
<tr>
<td align="center"><sub>live PGA leaderboard</sub></td>
<td align="center"><sub>animated Pokédex</sub></td>
<td align="center"><sub>live bus departures</sub></td>
</tr>
</table>
</div>

---

## 📱 The apps

| App | What it shows | Data source | Details |
|-----|---------------|-------------|:-------:|
| **⛳ golf** | PGA Tour leaderboard — top 5 + your pinned golfers, or the next event | ESPN scoreboard *(keyless)* | [→](apps/golf) |
| **🔴 pokedex** | A Pokémon sprite (animated GIF / static PNG) + name, on a timer | PokéAPI sprites CDN | [→](apps/pokedex) |
| **🚌 buss** | The next bus departures from a chosen Stockholm stop | SL Transport API *(keyless)* | [→](apps/buss) |

Every app:

- 📶 **Provisions Wi-Fi on-device** via a captive portal — no credentials in the source
- 🌐 **Serves a settings page** at `http://<app>.local/`
- 🌙 **Sleeps at night** — between two local hours the panel blanks and the board
  deep-sleeps (~µA), waking itself in the morning

---

## 🗂️ Repository layout

```
matrix-display-apps/
├── apps/                     # one flashable firmware per app
│   ├── golf/                 # ⛳ live PGA leaderboard
│   ├── pokedex/              # 🔴 animated Pokédex slideshow
│   └── buss/                 # 🚌 live Stockholm bus departures
│
├── packages/                 # shared libraries (PlatformIO lib_extra_dirs)
│   ├── board-config/         # HUB75 ↔ ESP32-S3-Zero pin map + panel geometry
│   ├── display-core/         # panel bring-up, text/color helpers, UTF-8 fold,
│   │                         #   status screens, deep-sleep panel parking
│   ├── net-core/             # resilient WiFiManager provisioning + HTTP fetch
│   ├── web-core/             # settings-server shell (mDNS, Restart / Wi-Fi reset)
│   ├── settings-core/        # NVS/Preferences wrapper + brightness clamp
│   └── sleep-core/           # night schedule: settings + web control + NTP +
│                             #   deep-sleep-until-morning
│
├── docs/                     # shared hardware reference (board, wiring, power)
└── README.md
```

---

## 🚀 Build & flash

Each app is its own PlatformIO project. Point `pio` at its directory with `-d`:

```bash
# Build the default (ESP32-S3 mini) firmware
pio run -d apps/golf

# Flash it and open the serial monitor
pio run -d apps/pokedex -t upload -t monitor

# buss
pio run -d apps/buss -t upload -t monitor
```

> **Environments:** the default `s3mini` targets the Waveshare ESP32-S3-Zero.
> golf also ships `devkitc` (full-size dev board) and `wokwi` (a hardware-free
> [browser simulator](https://wokwi.com) — `pio run -d apps/golf -e wokwi`).

---

## 🧩 How the sharing works

The apps **agree on infrastructure but disagree on rendering** — so the package
boundary follows exactly that seam:

<table>
<tr><th>Shared → <code>packages/</code></th><th>Per-app → <code>apps/&lt;name&gt;/src</code></th></tr>
<tr valign="top"><td>

- HUB75 init & the pin map
- Wi-Fi provisioning + HTTP fetch loop
- The settings-server shell & HTML chrome
- NVS persistence
- UTF-8 → ASCII folding
- The night-mode deep-sleep schedule

</td><td>

- golf's row-shuffle leaderboard animation
- pokedex's GIF/PNG canvas-blit pipeline
- buss's departure-row layout
- …the pixels, basically

</td></tr>
</table>

Packages are plain PlatformIO libraries (each with a `library.json`). Apps pull
them in with `lib_extra_dirs = ../../packages` and list them in `lib_deps`.
Where the apps differ, the shared code is **parameterized** rather than forked —
e.g. `displayCoreInit(doubleBuff, font)` serves golf (double-buffered, TomThumb),
buss (single-buffered, TomThumb) and pokedex (built-in font) from one function.

| Package | Responsibility |
|---------|----------------|
| `board-config` | HUB75 ↔ ESP32-S3-Zero pin map + `PANEL_*` geometry *(header-only)* |
| `display-core` | The one `dma_display` object, panel init, text/color helpers, `utf8Fold`, status screens, night-mode panel parking |
| `net-core` | `netStart()` resilient WiFiManager loop (never dead-ends), `httpGetBinary()` |
| `web-core` | `WebServer` + mDNS, shared Restart / Reconfigure-Wi-Fi handlers, HTML chrome |
| `settings-core` | `PrefsStore` NVS wrapper + `clampBrightness()` |
| `sleep-core` | `NightSettings`, the night-window check, deep-sleep-till-morning, NTP clock, and a drop-in `/night` web control |

---

## 🔌 Hardware

Every app uses the **identical** wiring, documented once in
[`docs/hardware-reference.md`](docs/hardware-reference.md). The pin map itself is
*code*, in [`packages/board-config`](packages/board-config/src/board_config.h) —
change a pin there and all three apps pick it up.

| Part | Notes |
|------|-------|
| 64×64 RGB LED matrix, P2, HUB75 | 128×128 mm, 1/32 scan |
| ESP32-S3-Zero ("S3 mini") | S3FH4R2 — 4 MB flash / 2 MB PSRAM |
| 5 V power supply, ≥ 4 A | Powers the panel **directly** — not through the dev board |
| Dupont wires ×16 | Panel usually ships with an IDC data cable + power harness |

> ⚡ **Power, read once:** feed the panel's 5 V terminals directly from the
> supply, tie **all grounds together**, and keep brightness low (~20) while
> bench-testing on USB. A P2 panel can pull ~4 A at full white.

---

## 📶 First-boot Wi-Fi

No credentials live in the source. On a fresh board (or after a network change)
the app opens a `*-Setup` access point; join it from a phone and pick your
network. Each app alternates saved-network retries with portal windows, so a
headless unit **can never get permanently stuck**. Change networks later from
the settings page's **Reconfigure Wi-Fi** button.

---

## 📄 License

MIT — do whatever you like. A ⭐ is always nice.
