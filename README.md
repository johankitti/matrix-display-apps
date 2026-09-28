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
│   ├── input-core/           # rotary-encoder brightness knob (live + debounced save)
│   └── sleep-core/           # night schedule: settings + web control + NTP +
│                             #   deep-sleep-until-morning
│
├── hardware/                 # 3D-printable case
│   └── enclosure/            # parametric generator + ready-to-slice STLs
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
| 64×64 RGB LED matrix, P3, HUB75 | 192×192 mm, 1/32 scan |
| ESP32-S3-Zero ("S3 mini") | S3FH4R2 — 4 MB flash / 2 MB PSRAM |
| 5 V power supply, 3 A | Powers the panel **directly** — not through the dev board |
| Dupont wires ×16 | Panel usually ships with an IDC data cable + power harness |

> ⚡ **Power, read once:** feed the panel's 5 V terminals directly from the
> supply, tie **all grounds together**, and keep brightness low (~20) while
> bench-testing on USB. A P3 panel is rated ~4 A at full white, which is why every
> app caps `BRIGHTNESS_MAX` at 140/255 for the 3 A supply.

---

## 🖨️ Enclosure

A 3D-printable back box that the panel drops into, plus a tilting desk stand,
in [`hardware/enclosure/`](hardware/enclosure). Everything prints without
supports:

| File | What it is | Size |
|------|------------|------|
| [`enclosure.stl`](hardware/enclosure/enclosure.stl) | The case — prints back-down | 196.4 × 196.4 × 46.4 mm |
| [`esp32_dock.stl`](hardware/enclosure/esp32_dock.stl) | Snap-in cradle for the ESP32-S3-Zero | 21.8 × 25.7 × 11.6 mm |
| [`stand_yoke.stl`](hardware/enclosure/stand_yoke.stl) | Tilt-stand yoke the case hangs in — prints feet-down | 235 × 130 × 133 mm |
| [`stand_stud.stl`](hardware/enclosure/stand_stud.stl) | Pivot stud, hex head + printed thread — print **2**, head-down | ø14 × 30 mm |
| [`stand_knob.stl`](hardware/enclosure/stand_knob.stl) | Twist knob with the female thread — print **2**, face-down | ø34 × 16 mm |
| [`make_enclosure.py`](hardware/enclosure/make_enclosure.py) | The parametric model all of them are generated from | — |

> 📐 **How the panel sits:** its 191 mm plastic back frame drops into the
> pocket and the 192.8 mm LED face rests on the rim, outside the box — so the
> case is cut for the 192 × 192 mm P3 panel in the table above, and nothing
> else.
>
> 🔩 The right wall is cut for exactly the parts in the
> [hardware reference](docs/hardware-reference.md#1-bill-of-materials): the
> switched DC-022 jack (Electrokit `41019430`), the 13.0 × 19.8 mm rocker
> switch (`41002801`) and the encoder bushing. Swap a part and re-run the
> generator with its dimensions.

**How it goes together**

- Eight posts around the inside back up the panel frame (the two on the side
  walls double as the stand's pivot blocks); behind it a **32 mm cavity** holds
  the board, the HUB75 ribbon and the power wiring.
- The **ESP32 dock prints separately** and slides down a dovetail rail on the
  left wall, so the board can come out without touching the case. The board
  sits *upside down*: USB-C and components in a well against the wall, header
  pins pointing inward. Tilt the USB end in first, then press the far end past
  the snap hook.
- **All connectors leave through the right wall**, keeping the bottom edge flat
  so the case can just stand on a desk. From the bottom up: the 5.5 × 2.1 mm DC
  barrel jack, the power rocker switch just above it, and the
  [rotary encoder](packages/input-core) knob near the top. All three are
  centred on the wall's height, so they line up from outside; the dock is on
  the opposite wall so its USB plug never fights the connectors for room.
- Two **keyhole slots** near the top edge for wall hanging, and vent slots
  through the back plate.

**Tilt stand**

- The case hangs between the yoke's arms on a pivot through its exact centre,
  so it is balanced at any angle and floats about 14 mm above the desk. It
  tilts forward and back in **15° steps**; the model checks the swing to ±45°.
- Per side, inside to out: drop a **stud** hex-head-first into the pocket in
  the side wall's mid block (before the panel goes in). Its thread pokes out
  through the wall inside a ring of 24 teeth. Hang the yoke arm over the
  thread, its own tooth ring facing the case, and screw the **knob** on until
  the teeth mesh. To re-tilt, loosen the knob about a third of a turn, tilt,
  tighten. No metal parts anywhere.
- Printing: yoke feet-down, studs head-down, knobs face-down. The threads are
  printed with 0.35 mm clearance, sized for a 0.4 mm nozzle — adjust `THR_CLR`
  if a knob is tight or sloppy.
- The keyholes and the flat bottom edge still work without the stand. Set
  `STAND = False` to drop the pivot blocks and tooth rings from the case.

**Changing it**

The STLs are generated, not hand-modelled — edit the `PARAMS` block at the top
of `make_enclosure.py` and re-run. It self-checks as it builds (connector holes
must clear the posts and the dock, the dock must fit the cavity and not
intersect the case, the stand's teeth must mesh, the stud must fit its pocket
and the knob, and the case must swing ±45° without touching the yoke), so a
bad parameter fails loudly instead of printing wrong:

```bash
cd hardware/enclosure
python3 -m venv .venv && .venv/bin/pip install numpy manifold3d
.venv/bin/python make_enclosure.py      # rewrites all the .stl files
```

> Useful knobs: `MOUNT_HOLES` (empty by default) adds M3 standoffs lining up
> with the panel's brass inserts; `USB_JACK = True` cuts a panel-mount USB-C
> hole; `KEYHOLES` / `VENTS` turn those features off.

---

## 📶 First-boot Wi-Fi

No credentials live in the source. On a fresh board (or after a network change)
the app opens a `*-Settings` access point; join it from a phone and pick your
network. Each app alternates saved-network retries with portal windows, so a
headless unit **can never get permanently stuck**. Change networks later from
the settings page's **Reconfigure Wi-Fi** button.

Once connected, the `*-Settings` access point **stays on** alongside the home
network, so the settings page (sleep hours, brightness, …) is always reachable:
join the board's hotspot and open **`http://192.168.4.1/`** — no need to be on
the same Wi-Fi or for `.local` to resolve.

---

## 📄 License

MIT — do whatever you like. A ⭐ is always nice.
