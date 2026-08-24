#pragma once
// =============================================================================
//  display-core — shared HUB75 panel plumbing for the matrix-display apps
//
//  Owns the one MatrixPanel_I2S_DMA instance (exposed as `dma_display`) plus the
//  boilerplate every app repeats: the identical HUB75 config/pin wiring, panel
//  bring-up, brightness, the proportional-text helpers, UTF-8 -> ASCII folding
//  (the LED fonts are ASCII-only), a generic centered status screen, and the
//  night-mode "park the panel dark through deep sleep" helpers.
//
//  App-specific rendering (leaderboards, sprite pipelines, departure rows) stays
//  in each app and just draws through `dma_display` + these helpers.
// =============================================================================

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// The shared panel. Valid (non-null) only after a successful displayCoreInit().
extern MatrixPanel_I2S_DMA* dma_display;

// Bring up the HUB75 panel using the pin map + geometry from board-config.
//   doubleBuff - allocate a back buffer and draw off-screen (needed for
//                tear-free full-frame animation, e.g. the golf row-shuffle).
//   font       - default GFX font to install (&TomThumb, or nullptr to keep the
//                built-in 5x7 font). Apps can still setFont() per-draw.
// Returns false if DMA setup fails (bad pin, no memory). Leaves the panel
// cleared and text-wrap off on success.
bool displayCoreInit(bool doubleBuff = false, const GFXfont* font = nullptr);

// Set panel brightness (0-255) live, e.g. from the web settings page or an
// encoder. No-op before displayCoreInit().
void displaySetBrightness(uint8_t b);

// Present the back buffer if double-buffered; a no-op otherwise. Call after a
// frame so app code doesn't have to branch on the buffering mode.
void displayFlip();

// color565 convenience (same packing as dma_display->color565).
uint16_t displayColor(uint8_t r, uint8_t g, uint8_t b);

// ---- Proportional-text helpers (operate on the shared panel + current font) --
// Width in pixels of `s` in the current font.
int  displayTextWidth(const char* s);
// Draw `s` with its left edge at x, baseline at baseY.
void displayText(int x, int baseY, const char* s, uint16_t color);
// Draw `s` right-aligned so its right edge sits at xRight.
void displayTextRight(int xRight, int baseY, const char* s, uint16_t color);

// Fold a UTF-8 string to ASCII in-place-safe form: Latin-1 accents map to their
// base letter (Å/Ä->A, ö->o, é->e, ...), other multibyte sequences are dropped.
// The LED fonts are ASCII-only, so display strings pass through here first.
void utf8Fold(const char* src, char* dst, size_t dstSize);

// Full-screen centered status message, up to 3 lines (yellow / white / gray).
// Pass nullptr to skip a line. Clears the screen and flips if double-buffered.
void displayMessage(const char* l1, const char* l2 = nullptr,
                    const char* l3 = nullptr);

// ---- Night mode / deep sleep ------------------------------------------------
// Blank the panel and park the HUB75 output-enable line high (active-low: rows
// disabled) with the level held through deep sleep, so floating inputs can't
// light stray pixels. Safe to call even before displayCoreInit().
void displayPowerOff();
// Release the deep-sleep pin holds. Call once at boot, before displayCoreInit().
void displayReleaseHolds();
