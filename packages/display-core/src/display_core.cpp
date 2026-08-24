#include "display_core.h"

#include <board_config.h>
#include <driver/gpio.h>

MatrixPanel_I2S_DMA* dma_display = nullptr;

static bool s_doubleBuff = false;

// Default palette for the generic status screen, filled in at init (color565
// needs the driver to exist first).
static uint16_t C_YELLOW = 0, C_WHITE = 0, C_GRAY = 0;

bool displayCoreInit(bool doubleBuff, const GFXfont* font) {
  HUB75_I2S_CFG::i2s_pins pins = {
      HUB75_R1, HUB75_G1, HUB75_B1, HUB75_R2, HUB75_G2, HUB75_B2,
      HUB75_A,  HUB75_B,  HUB75_C,  HUB75_D,  HUB75_E,
      HUB75_LAT, HUB75_OE, HUB75_CLK,
  };
  HUB75_I2S_CFG cfg(PANEL_WIDTH, PANEL_HEIGHT, PANEL_CHAIN, pins);
  // If your panel stays black or shows garbage it likely uses FM6126A driver
  // chips — uncomment the next line.
  // cfg.driver = HUB75_I2S_CFG::FM6126A;
  cfg.double_buff = doubleBuff;
  s_doubleBuff = doubleBuff;

  dma_display = new MatrixPanel_I2S_DMA(cfg);
  if (!dma_display->begin()) return false;

  if (font) dma_display->setFont(font);
  dma_display->setTextWrap(false);
  dma_display->clearScreen();

  C_YELLOW = dma_display->color565(255, 200, 0);
  C_WHITE  = dma_display->color565(255, 255, 255);
  C_GRAY   = dma_display->color565(140, 140, 140);
  return true;
}

void displaySetBrightness(uint8_t b) {
  if (dma_display) dma_display->setBrightness8(b);
}

void displayFlip() {
  if (dma_display && s_doubleBuff) dma_display->flipDMABuffer();
}

uint16_t displayColor(uint8_t r, uint8_t g, uint8_t b) {
  return MatrixPanel_I2S_DMA::color565(r, g, b);
}

int displayTextWidth(const char* s) {
  if (!dma_display) return 0;
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->getTextBounds(s, 0, 20, &x1, &y1, &w, &h);
  return (int)w;
}

void displayText(int x, int baseY, const char* s, uint16_t color) {
  if (!dma_display) return;
  dma_display->setTextColor(color);
  dma_display->setCursor(x, baseY);
  dma_display->print(s);
}

void displayTextRight(int xRight, int baseY, const char* s, uint16_t color) {
  displayText(xRight - displayTextWidth(s) + 1, baseY, s, color);
}

// TomThumb / the built-in font cover ASCII only, but the live data is UTF-8
// ("Sköndal", "Ludvig Åberg"). Fold the common Latin-1 letters to their base
// form and drop anything else multibyte.
void utf8Fold(const char* src, char* dst, size_t dstSize) {
  size_t o = 0;
  for (const uint8_t* p = (const uint8_t*)src; *p && o < dstSize - 1;) {
    if (*p < 0x80) {
      dst[o++] = *p++;
    } else if (*p == 0xC3 && p[1]) {
      char c = '?';
      switch (p[1]) {
        case 0xA5: case 0xA4: c = 'a'; break;  // å ä
        case 0x85: case 0x84: c = 'A'; break;  // Å Ä
        case 0xB6:            c = 'o'; break;  // ö
        case 0x96:            c = 'O'; break;  // Ö
        case 0xA9: case 0xA8: c = 'e'; break;  // é è
        case 0xBC:            c = 'u'; break;  // ü
      }
      dst[o++] = c;
      p += 2;
    } else {
      p++;                                   // skip a lead byte...
      while ((*p & 0xC0) == 0x80) p++;       // ...and its continuation bytes
    }
  }
  dst[o] = '\0';
}

void displayMessage(const char* l1, const char* l2, const char* l3) {
  if (!dma_display) return;
  dma_display->clearScreen();
  const char* lines[3]    = {l1, l2, l3};
  const uint16_t colors[3] = {C_YELLOW, C_WHITE, C_GRAY};
  int y = 24;
  for (int i = 0; i < 3; i++) {
    if (!lines[i]) continue;
    displayText((PANEL_WIDTH - displayTextWidth(lines[i])) / 2, y, lines[i],
                colors[i]);
    y += 10;
  }
  displayFlip();
}

void displayPowerOff() {
  if (dma_display) {
    // With double buffering, clearScreen() only blanks the back buffer — clear
    // and flip twice so BOTH buffers are dark before DMA stops, otherwise the
    // still-shown front buffer keeps the panel lit into deep sleep.
    dma_display->clearScreen();
    dma_display->flipDMABuffer();
    dma_display->clearScreen();
    dma_display->flipDMABuffer();
    dma_display->stopDMAoutput();
    delay(10);
  }
  // With DMA stopped (or never started) the HUB75 inputs float. OE is
  // active-low: park it high so every row stays disabled, and hold the level
  // through deep sleep.
  pinMode(HUB75_OE, OUTPUT);
  digitalWrite(HUB75_OE, HIGH);
  gpio_hold_en((gpio_num_t)HUB75_OE);
  gpio_deep_sleep_hold_en();
}

void displayReleaseHolds() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)HUB75_OE);
}
