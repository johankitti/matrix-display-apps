<div align="center">

# 🔴 Pokédex Display

**A tiny desktop Pokédex.** Firmware for an ESP32-S3 driving a 64×64 HUB75 LED
panel — it cycles through the entire National Dex, one Pokémon at a time, with
animated Gen-V sprites and a Wi-Fi settings page.

Sprite on top, name across the bottom. Hold for a few seconds. Next.

```
      ╔══════════════════════╗
      ║   .:*~*:._.:*~*:.     ║
      ║      ( ^ᴥ^ )          ║      random or 1 → 1025
      ║     /  PIKACHU        ║      animated where it exists
      ║    #025              ║      configured over Wi-Fi
      ╚══════════════════════╝
        64 × 64 · P2 · HUB75E
```

![platform](https://img.shields.io/badge/platform-ESP32--S3-E7352C?style=flat-square)
![framework](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-blue?style=flat-square)
![panel](https://img.shields.io/badge/panel-64×64%20HUB75-informational?style=flat-square)
![dex](https://img.shields.io/badge/dex-1–1025-yellow?style=flat-square)

</div>

---

## ✨ What it does

- **Full National Dex** — draws from ids 1–1025, in **random** or **sequential**
  order (sequential wraps back to #1).
- **Animated sprites** — plays the Gen-V "Black/White" animated GIFs (ids 1–649),
  with an automatic fall back to the static PNG for everything past #649. Pick
  `full`, `only` (animated range), or `static` in the settings.
- **Zero-config Wi-Fi** — no `secrets.h`, nothing compiled in. A captive portal
  provisions your network on first boot, and the finished unit is **designed to
  never get stuck** — it rides out a router that's still booting after a power
  cut and self-heals a changed network, all with no setup button.
- **Web settings page** — `http://pokedex-display.local/` lets you tune seconds
  per slide, order, animation mode, brightness, and playback speed. Saved to NVS,
  applied on the next slide.
- **Physical brightness knob** — a rotary encoder controls brightness in real
  time (mirrored on the web page); the BOOT button skips to the next Pokémon.
- **Smooth animation** — sprites are prefetched over Wi-Fi on **core 0** so the
  network never stutters the animation running on **core 1**.

---

## 🧰 Hardware

| Part | Detail |
|---|---|
| **MCU** | [Waveshare ESP32-S3-Zero](https://www.waveshare.com/wiki/ESP32-S3-Zero) — ESP32-S3FH4R2, 4 MB flash / 2 MB PSRAM, USB-C |
| **Display** | [64×64 P2 HUB75 RGB LED matrix](https://www.electrokit.com/en/full-color-panel-2mm-rgb-led-matrix-64x64px-128x128mm-p2) (128×128 mm) |
| **Power** | 5 V DC, ≥2 A (3 A ideal), center-positive, 5.5×2.1 mm barrel |
| **Encoder** | Bourns PEC11R rotary encoder (24 detents + push) |
| **Wiring** | 16-pin IDC → Dupont female cable (ships with the panel) |

> 📖 Full wiring, power, and BOM: **[`docs/hardware-reference.md`](docs/hardware-reference.md)**

### Pin map

HUB75 takes GPIO1–13 + 43. That leaves exactly two free side pins for the encoder.

| HUB75 | GPIO | | HUB75 | GPIO | | Input | GPIO |
|---|---|---|---|---|---|---|---|
| R1 | 1 | | C | 9 | | Encoder A | 44 |
| G1 | 2 | | D | 10 | | Encoder B | 14 |
| B1 | 3 | | E | 11 | | Button | 0 (BOOT) |
| R2 | 4 | | CLK | 12 | | | |
| G2 | 5 | | LAT | 13 | | | |
| B2 | 6 | | OE | 43 | | | |
| A | 7 | | GND | any GND | | | |
| B | 8 | | | | | | |

> ⚡ **Power rule:** never have two 5 V sources at once — laptop USB **or** the
> 5 V supply, never both. Keep brightness ~20 while testing on USB; ~90 once on
> the dedicated supply.

---

## 🚀 Build & flash

Built with [PlatformIO](https://platformio.org/). From the repo root:

```bash
pio run -e s3mini                      # compile
pio run -e s3mini -t upload -t monitor # flash + serial monitor (USB power)
```

**First boot:**

1. The panel shows a **join screen**. Connect to the `Pokedex-Setup` Wi-Fi AP.
2. A captive portal opens — pick your network and enter the password.
3. The device reboots, connects, and starts the slideshow.
4. Open **`http://pokedex-display.local/`** (or the panel's IP) to configure it.

Need to move it to a new network later? The web page has a **"Reconfigure Wi-Fi"**
button — no reflashing, no button-holding.

---

## 🎛️ Settings

All configurable at `http://pokedex-display.local/` and saved to NVS:

| Setting | Range | What it does |
|---|---|---|
| **Seconds per slide** | 1–300 | How long each Pokémon is shown (default 10) |
| **Order** | random / sequential | Sequential runs 1 → 1025 and wraps |
| **Animation** | full / only / static | Whole dex, animated-only range, or all-static |
| **Brightness** | 2–140 | Slider — mirrors the physical encoder |
| **Animation speed** | 25–400 % | % of the GIF's authored timing (100 = original) |

> Brightness capped at 140/255 to keep panel current under a 5 V/3 A supply —
> see [`include/config.h`](include/config.h) for the reasoning and how to raise it.

---

## 🗂️ How it's built

Sprites are fetched over HTTPS from the [PokéAPI sprites CDN](https://github.com/PokeAPI/sprites)
into a PSRAM buffer, decoded, cropped, and blitted onto a shared canvas.

```
src/
├── main.cpp      · slideshow loop; core-0 prefetch task feeds core-1 animation
├── settings.*    · Settings struct + NVS load/save
├── input.*       · rotary encoder (brightness) + BOOT button (next Pokémon)
├── web.*         · WebServer + mDNS settings page
├── net.*         · Wi-Fi + HTTPS sprite fetch into PSRAM
├── display.*     · HUB75 init; AnimatedGIF + PNGdec render pipeline, text
├── pokemon.*     · next id, sprite URL/type, animated → static fallback
└── names.h       · auto-generated dex names (tools/gen_names.py)
```

**Libraries:** [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-I2S-DMA),
[AnimatedGIF](https://github.com/bitbank2/AnimatedGIF),
[PNGdec](https://github.com/bitbank2/PNGdec),
[ESP32Encoder](https://github.com/madhephaestus/ESP32Encoder),
[WiFiManager](https://github.com/tzapu/WiFiManager),
[Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library).

---

## 🔧 Tuning

The knobs live in [`include/config.h`](include/config.h) — dex range, slide
timing, animation floor/speed, hostname, brightness caps, and encoder counts per
detent are all documented inline.

If colors look swapped or the image is shifted, flip the `gif.begin()` palette
arg (`GIF_PALETTE_RGB565_LE` ↔ `_BE`) in `display.cpp`, and/or set `cfg.clkphase`
in `displayInit()`.

---

<div align="center">
<sub>Sprites courtesy of <a href="https://github.com/PokeAPI/sprites">PokéAPI</a>. Pokémon © Nintendo / Game Freak / The Pokémon Company. This is a personal, non-commercial project.</sub>
</div>
