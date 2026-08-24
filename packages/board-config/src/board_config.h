#pragma once
// =============================================================================
//  board-config — shared HUB75 <-> ESP32-S3-Zero pin map + panel geometry
//
//  Every app in this monorepo drives the same 64x64 P2 HUB75 panel from the same
//  Waveshare ESP32-S3-Zero ("S3 mini") board, so the wiring lives here once
//  instead of being copy-pasted into each app's config.h. See
//  docs/hardware-reference.md for the board photo, wiring and power notes.
//
//  The default map uses GPIO 1-13 + 43: exactly the pins on the two side header
//  rows of the S3-Zero. GPIO 43 is the pad silkscreened "TX" at the top of the
//  right column. Any output-capable GPIO works on the S3 — override any HUB75_*
//  macro *before* including this header to rewire.
//
//  AVOID: 0 / 45 / 46 (strapping), 19 / 20 (native USB), 26-37 (flash/PSRAM on
//  some modules), 21 (onboard WS2812 RGB LED on the S3-Zero).
//  GPIO 43 / 44 are UART0 TX/RX but free to use, since logging goes over native
//  USB (ARDUINO_USB_CDC_ON_BOOT=1).
// =============================================================================

// ---- Panel geometry ---------------------------------------------------------
#ifndef PANEL_WIDTH
#define PANEL_WIDTH   64
#endif
#ifndef PANEL_HEIGHT
#define PANEL_HEIGHT  64
#endif
#ifndef PANEL_CHAIN
#define PANEL_CHAIN   1
#endif

// ---- Onboard WS2812 RGB status LED (S3-Zero, GPIO21) -------------------------
#ifndef PIN_STATUS_LED
#define PIN_STATUS_LED 21
#endif

// ---- HUB75 pin map ----------------------------------------------------------
#ifdef WOKWI
// Wokwi simulator build (classic ESP32 devkit — the board whose HUB75 emulation
// is proven). GPIO 1-13 collide with UART0/flash on that chip, so the simulator
// uses the pin map from Wokwi's own HUB75 examples (diagram.json mirrors it).
#ifndef HUB75_R1
#define HUB75_R1  25
#define HUB75_G1  26
#define HUB75_B1  27
#define HUB75_R2  14
#define HUB75_G2  12
#define HUB75_B2  13
#define HUB75_A   23
#define HUB75_B   19
#define HUB75_C   5
#define HUB75_D   17
#define HUB75_E   32
#define HUB75_CLK 16
#define HUB75_LAT 4
#define HUB75_OE  15
#endif
#else
#ifndef HUB75_R1
#define HUB75_R1  1    // red,   top half
#define HUB75_G1  2    // green, top half
#define HUB75_B1  3    // blue,  top half
#define HUB75_R2  4    // red,   bottom half
#define HUB75_G2  5    // green, bottom half
#define HUB75_B2  6    // blue,  bottom half
#define HUB75_A   7    // row address bits: 64px panels use A..E (1/32 scan)
#define HUB75_B   8
#define HUB75_C   9
#define HUB75_D   10
#define HUB75_E   11
#define HUB75_CLK 12
#define HUB75_LAT 13
#define HUB75_OE  43   // the "TX" pad, top of the right column
#endif
#endif

// ---- Compatibility aliases --------------------------------------------------
// The pokedex/buss apps historically used PIN_* names; keep them as aliases so
// their code (and anyone's muscle memory) keeps working against the shared map.
#define PIN_R1   HUB75_R1
#define PIN_G1   HUB75_G1
#define PIN_B1   HUB75_B1
#define PIN_R2   HUB75_R2
#define PIN_G2   HUB75_G2
#define PIN_B2   HUB75_B2
#define PIN_A    HUB75_A
#define PIN_B    HUB75_B
#define PIN_C    HUB75_C
#define PIN_D    HUB75_D
#define PIN_E    HUB75_E
#define PIN_CLK  HUB75_CLK
#define PIN_LAT  HUB75_LAT
#define PIN_OE   HUB75_OE
