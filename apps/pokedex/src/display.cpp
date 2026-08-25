#include "display.h"
#include "config.h"

#include <display_core.h>   // shared panel object (dma_display) + init/brightness
#include <AnimatedGIF.h>
#include <PNGdec.h>
#include <esp_heap_caps.h>

static AnimatedGIF gif;
static PNG png;

// Persistent RGB565 canvas for the currently-playing GIF (allocated in PSRAM at
// displayAnimStart, freed at displayAnimStop). GIF frames are often partial and
// composite onto the prior frame, so the full canvas must survive across frames.
static uint16_t* g_canvas = nullptr;
static int g_cw = 0, g_ch = 0;

// Frozen crop window + panel placement for the current slide. Computed once from
// the first frame so limb movement between frames doesn't make the sprite jitter.
static int s_srcX0 = 0, s_srcY0 = 0, s_outW = 0, s_outH = 0, s_offx = 0, s_offy = 0;

// Bounding box of the top-right dex label. The per-frame blit skips this rect so
// the animated sprite never bleeds through behind the number: it stays black (from
// the slide's initial fillScreen) and the label is painted on top just once.
static int s_numLeft = PANEL_WIDTH, s_numBottom = 0;

// Animation playback state: whether a GIF is open and when the next frame is due.
static bool     s_animOpen = false;
static uint32_t s_nextFrameMs = 0;
static uint16_t s_speedPct = ANIM_SPEED_DEFAULT;   // 100 = authored/original timing

// Disposal of the frame currently on the canvas — captured during its draw and
// applied BEFORE the next frame. Method 2 ("restore to background") means the
// frame's rect must be cleared to black before compositing the next one, else
// moving sprites smear their previous frame across the canvas.
static int     s_dispX = 0, s_dispY = 0, s_dispW = 0, s_dispH = 0;
static uint8_t s_dispMethod = 0;

// Dex label ("#NNN") for the current sprite. Redrawn after every frame because the
// sprite region is repainted each frame and would otherwise erase it.
static char s_dexLabel[8] = "";

// Top row of the current name band (1 or 2 lines), pinned to the bottom of the
// panel. Set whenever the sprite or name is drawn; the seconds overlay uses it to
// clear exactly the name band and never the sprite above it.
static int s_nameTopY = PANEL_HEIGHT - TEXT_GLYPH_H - NAME_BOTTOM_PAD;

static uint16_t white()  { return MatrixPanel_I2S_DMA::color565(255, 255, 255); }
static uint16_t yellow() { return MatrixPanel_I2S_DMA::color565(255, 210, 0); }
static uint16_t blue()   { return MatrixPanel_I2S_DMA::color565(110, 170, 255); }

// AnimatedGIF callback: composite one line of the current frame onto g_canvas.
// Transparent pixels are skipped so the previous frame shows through (correct for
// leave-in-place disposal, which the Gen-V sprites use). Palette is RGB565-LE to
// match the panel's native color565() packing (flip GIF_PALETTE below if swapped).
static void gifDraw(GIFDRAW* d) {
    if (!g_canvas) return;
    if (d->y == 0) {                     // first line of a frame: record its extent
        s_dispX = d->iX; s_dispY = d->iY;
        s_dispW = d->iWidth; s_dispH = d->iHeight;
        s_dispMethod = d->ucDisposalMethod;
    }
    int yy = d->iY + d->y;
    if (yy < 0 || yy >= g_ch) return;
    int w = d->iWidth;
    if (d->iX + w > g_cw) w = g_cw - d->iX;   // clip to canvas width
    if (w <= 0) return;
    uint16_t* dst = &g_canvas[yy * g_cw + d->iX];
    const uint8_t* src = d->pPixels;
    const uint16_t* pal = d->pPalette;
    if (d->ucHasTransparency) {
        const uint8_t tr = d->ucTransparent;
        for (int x = 0; x < w; x++) {
            uint8_t p = src[x];
            if (p != tr) dst[x] = pal[p];
        }
    } else {
        for (int x = 0; x < w; x++) dst[x] = pal[src[x]];
    }
}

// PNGdec callback: composite one decoded line over black into g_canvas. Uses the
// same RGB565-LE packing as the GIF path so both share the crop/blit pipeline.
static int pngDraw(PNGDRAW* p) {
    if (!g_canvas) return 0;
    uint16_t* dst = &g_canvas[p->y * g_cw];
    png.getLineAsRGB565(p, dst, PNG_RGB565_LITTLE_ENDIAN, 0x00000000);
    return 1;
}

void displayInit() {
    // display-core brings up the HUB75 panel from the shared board-config pin map
    // and geometry: no double buffer (single-buffer draws are fine here) and the
    // built-in 5x7 GFX font (nullptr), which all the layout math assumes.
    // displayCoreInit already clears the screen and sets textWrap(false).
    displayCoreInit(/*doubleBuff=*/false, /*font=*/nullptr);
    displaySetBrightness(PANEL_BRIGHTNESS);

    // Emit palette entries as little-endian RGB565 so pPalette[i] read as a uint16_t
    // on this (LE) MCU equals what dma_display->drawPixel expects. Switch to
    // GIF_PALETTE_RGB565_BE if colors come out swapped.
    gif.begin(GIF_PALETTE_RGB565_LE);
}

void displayClear() {
    if (dma_display) dma_display->clearScreen();
}

// displaySetBrightness() now lives in display-core (display_core.h).

// ---- Name layout helpers -----------------------------------------------------

// A name only needs the animated marquee if it can't fit on two static lines.
bool displayNameScrolls(const char* name) {
    return (int)strlen(name) > NAME_CHARS_PER_LINE * NAME_MAX_LINES;   // >20 chars
}

// Split `name` into up to two lines (each <= NAME_CHARS_PER_LINE). Prefer a space
// break nearest the middle with both halves fitting; else a balanced hard split.
// Returns the number of lines used (1 or 2). Assumes len <= 2*NAME_CHARS_PER_LINE.
static int wrapName(const char* name, char* l1, char* l2, size_t cap) {
    int n = (int)strlen(name);
    if (n <= NAME_CHARS_PER_LINE) {
        strncpy(l1, name, cap - 1); l1[cap - 1] = 0; l2[0] = 0;
        return 1;
    }
    const int mid = n / 2;
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (name[i] != ' ') continue;
        int left = i, right = n - i - 1;
        if (left <= NAME_CHARS_PER_LINE && right <= NAME_CHARS_PER_LINE &&
            (best < 0 || abs(i - mid) < abs(best - mid))) {
            best = i;
        }
    }
    int split;
    bool dropSpace;
    if (best >= 0) {
        split = best; dropSpace = true;          // break on the space
    } else {
        split = mid;                             // balanced hard split, clamped
        if (split > NAME_CHARS_PER_LINE) split = NAME_CHARS_PER_LINE;
        if (n - split > NAME_CHARS_PER_LINE) split = n - NAME_CHARS_PER_LINE;
        dropSpace = false;
    }
    int len1 = split;
    int start2 = split + (dropSpace ? 1 : 0);
    int len2 = n - start2;
    if (len1 > (int)cap - 1) len1 = cap - 1;
    if (len2 > (int)cap - 1) len2 = cap - 1;
    memcpy(l1, name, len1); l1[len1] = 0;
    memcpy(l2, name + start2, len2); l2[len2] = 0;
    return 2;
}

// Rows the name will occupy (1 or 2). Marquee names live on a single bottom row.
static int planLines(const char* name) {
    if (displayNameScrolls(name)) return 1;
    char a[24], b[24];
    return wrapName(name, a, b, sizeof(a));
}

// Visible height of the name band for a given line count (15px for 2, 7px for 1).
static int nameHeightFor(int lines) {
    return (lines == 2) ? (TEXT_LINE_H + TEXT_GLYPH_H) : TEXT_GLYPH_H;
}

static void drawCentered(const char* s, int y) {
    int w = (int)strlen(s) * TEXT_CHAR_W;
    dma_display->setCursor((PANEL_WIDTH - w) / 2, y);
    dma_display->print(s);
}

// ---- State 1: "POKEDEX" splash + animated status ----------------------------
// A static scene (title header + Poké Ball, mirroring golf's leaderboard splash)
// with an animated status line whose dots cycle on a core-0 task, so it keeps
// moving while the main loop blocks on a network fetch. Only the status band is
// repainted each tick; the header + ball are drawn once and left untouched.

static TaskHandle_t s_loadingTask = nullptr;
static volatile bool s_loadingRun = false;
static char s_loadingLabel[16] = "Loading";

// Baseline row for the status word: near the bottom, clear of the ball.
static const int LOADING_STATUS_Y = PANEL_HEIGHT - TEXT_GLYPH_H - 3;   // y=54

// A small Poké Ball centered on (cx, cy): red top, white bottom, black equator
// and a white centre button. Overhanging pixels land on the black background,
// so the square band + circles read as a clean ball without extra masking.
static void drawPokeball(int cx, int cy, int r) {
    const uint16_t red = MatrixPanel_I2S_DMA::color565(230, 45, 45);
    dma_display->fillCircle(cx, cy, r, red);                     // whole ball red
    for (int dy = 1; dy <= r; dy++) {                            // repaint lower half white
        int half = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
        dma_display->drawFastHLine(cx - half, cy + dy, 2 * half + 1, white());
    }
    dma_display->fillRect(cx - r, cy - 1, 2 * r + 1, 2, 0);      // black equator band
    dma_display->drawCircle(cx, cy, r, 0);                       // crisp outline
    dma_display->fillCircle(cx, cy, 3, 0);                       // button ring (black)
    dma_display->fillCircle(cx, cy, 1, white());                 // button centre
}

// Draw the static parts of the splash (everything except the animated status).
static void drawLoadingScene() {
    dma_display->fillScreen(0);
    dma_display->setTextSize(1);
    dma_display->setTextColor(yellow());
    drawCentered("POKEDEX", 2);              // title header, mirrors golf's splash
    drawPokeball(PANEL_WIDTH / 2, 29, 10);
}

static void loadingTaskFn(void*) {
    int step = 0;
    while (s_loadingRun) {
        int dots = step % 4;
        // Anchor on the widest form ("<label>...") so the word never jumps sideways.
        char full[20];
        snprintf(full, sizeof(full), "%s...", s_loadingLabel);
        int x = (PANEL_WIDTH - (int)strlen(full) * TEXT_CHAR_W) / 2;
        if (x < 0) x = 0;
        dma_display->fillRect(0, LOADING_STATUS_Y - 1, PANEL_WIDTH, TEXT_GLYPH_H + 2, 0);
        dma_display->setTextSize(1);
        dma_display->setTextColor(blue());
        dma_display->setCursor(x, LOADING_STATUS_Y);
        dma_display->print(s_loadingLabel);
        for (int i = 0; i < dots; i++) dma_display->print('.');
        step++;
        vTaskDelay(pdMS_TO_TICKS(350));
    }
    s_loadingTask = nullptr;
    vTaskDelete(nullptr);
}

void displayLoadingStart(const char* label) {
    if (!dma_display) return;
    strlcpy(s_loadingLabel, (label && *label) ? label : "Loading", sizeof(s_loadingLabel));
    if (s_loadingTask) return;               // already animating; label swapped above
    drawLoadingScene();
    s_loadingRun = true;
    xTaskCreatePinnedToCore(loadingTaskFn, "loading", 4096, nullptr, 1, &s_loadingTask, 0);
}

void displayLoadingStop() {
    if (!s_loadingTask) return;
    s_loadingRun = false;
    while (s_loadingTask) vTaskDelay(pdMS_TO_TICKS(5));   // wait for it to exit
}

// ---- State 2: animated sprite + name ----------------------------------------

// Dex number label, top-right, over a small black backing so it stays legible
// against the sprite. The sprite region is repainted every frame, so this is
// redrawn after each blit (not just once per slide like the still-PNG version).
static void drawDexLabel() {
    if (!dma_display || !s_dexLabel[0]) return;
    int nw = (int)strlen(s_dexLabel) * TEXT_CHAR_W;
    int nx = PANEL_WIDTH - nw;                 // right-align to the panel edge
    dma_display->fillRect(nx - 1, 0, nw + 1, TEXT_GLYPH_H + 1, 0);
    dma_display->setTextSize(1);
    dma_display->setTextColor(yellow());
    dma_display->setCursor(nx, 1);
    dma_display->print(s_dexLabel);
}

// Blit the frozen crop window of g_canvas onto the panel (1:1, no scaling). Pixels
// inside the dex-label box are skipped so they stay black and the number (drawn
// once by drawDexLabel) never has the sprite flickering through behind it.
static void blitCanvas() {
    if (!dma_display || !g_canvas) return;
    for (int dy = 0; dy < s_outH; dy++) {
        int sy = s_srcY0 + dy;
        int py = s_offy + dy;
        const uint16_t* row = &g_canvas[sy * g_cw + s_srcX0];
        bool inLabelRow = (py < s_numBottom);
        for (int dx = 0; dx < s_outW; dx++) {
            int px = s_offx + dx;
            if (inLabelRow && px >= s_numLeft) continue;   // leave the label box black
            dma_display->drawPixel(px, py, row[dx]);
        }
    }
}

// Compute the frozen crop window from the content bounding box of the frame
// currently in g_canvas (see the still-PNG rationale: crop around the character's
// own bounds so it lands centered). Called once per slide from the first frame.
static void computeCrop(const char* name) {
    const int lines = planLines(name);
    s_nameTopY = PANEL_HEIGHT - nameHeightFor(lines) - NAME_BOTTOM_PAD;
    const int spriteAreaH = s_nameTopY - LAYOUT_GAP;

    int minX = g_cw, minY = g_ch, maxX = -1, maxY = -1;
    for (int y = 0; y < g_ch; y++) {
        const uint16_t* row = &g_canvas[y * g_cw];
        for (int x = 0; x < g_cw; x++) {
            if (row[x] != 0) {                    // 0x0000 = transparent->black bg
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    const int ccx = (maxX >= 0) ? (minX + maxX) / 2 : g_cw / 2;
    const int ccy = (maxY >= 0) ? (minY + maxY) / 2 : g_ch / 2;

    s_outW = g_cw < PANEL_WIDTH ? g_cw : PANEL_WIDTH;
    s_outH = g_ch < spriteAreaH ? g_ch : spriteAreaH;
    s_srcX0 = ccx - s_outW / 2;
    s_srcY0 = ccy - s_outH / 2;
    if (s_srcX0 < 0) s_srcX0 = 0;
    if (s_srcY0 < 0) s_srcY0 = 0;
    if (s_srcX0 > g_cw - s_outW) s_srcX0 = g_cw - s_outW;
    if (s_srcY0 > g_ch - s_outH) s_srcY0 = g_ch - s_outH;
    s_offx = (PANEL_WIDTH - s_outW) / 2;          // center on the panel
    s_offy = (spriteAreaH - s_outH) / 2 + SPRITE_OFFSET_Y;
    // Keep the blit strictly above the name band, preserving the LAYOUT_GAP so the
    // sprite never touches the name (SPRITE_OFFSET_Y can nudge the bottom rows down;
    // the still version relied on the name drawing last).
    const int spriteBottomLimit = s_nameTopY - LAYOUT_GAP;
    if (s_offy + s_outH > spriteBottomLimit) s_outH = spriteBottomLimit - s_offy;

    // Dex-label box the blit must leave black (matches drawDexLabel's fillRect).
    int nw = (int)strlen(s_dexLabel) * TEXT_CHAR_W;
    s_numLeft = nw ? (PANEL_WIDTH - nw - 1) : PANEL_WIDTH;
    s_numBottom = nw ? (TEXT_GLYPH_H + 1) : 0;
}

// Clear the previous frame's rect to black if it asked to be disposed to the
// background. Called before compositing the next frame onto the canvas.
static void applyDisposal() {
    if (s_dispMethod != 2 || !g_canvas) return;   // 2 = restore to background
    for (int y = 0; y < s_dispH; y++) {
        int yy = s_dispY + y;
        if (yy < 0 || yy >= g_ch) continue;
        int w = s_dispW;
        if (s_dispX + w > g_cw) w = g_cw - s_dispX;
        if (w <= 0) continue;
        uint16_t* row = &g_canvas[yy * g_cw + s_dispX];
        for (int x = 0; x < w; x++) row[x] = 0;
    }
}

void displayAnimStop() {
    if (s_animOpen) { gif.close(); s_animOpen = false; }
    if (g_canvas) { free(g_canvas); g_canvas = nullptr; }
    g_cw = g_ch = 0;
    s_dexLabel[0] = 0;
    s_dispMethod = 0;
}

bool displayAnimStart(const uint8_t* data, size_t len, const char* name, int id) {
    if (!dma_display) return false;
    displayAnimStop();   // drop any previous slide's GIF + canvas

    if (!gif.open((uint8_t*)data, (int)len, gifDraw)) return false;
    g_cw = gif.getCanvasWidth();
    g_ch = gif.getCanvasHeight();
    if (g_cw <= 0 || g_ch <= 0) { gif.close(); return false; }

    size_t bytes = (size_t)g_cw * g_ch * sizeof(uint16_t);
    g_canvas = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!g_canvas) g_canvas = (uint16_t*)malloc(bytes);
    if (!g_canvas) { gif.close(); return false; }
    memset(g_canvas, 0, bytes);   // black background (transparent pixels rest here)
    s_animOpen = true;

    // Decode the first frame so we can size the crop from real content. The decoder
    // is left positioned at frame 1, so the first tick continues smoothly (no
    // double-showing frame 0). A single-frame GIF ends here — rewind so ticks loop it.
    int delayMs = 0;
    int rc0 = gif.playFrame(false, &delayMs);
    if (rc0 < 0) { displayAnimStop(); return false; }
    if (rc0 == 0) gif.reset();

    displayLoadingStop();   // first frame is ready — end State 1 before we draw

    snprintf(s_dexLabel, sizeof(s_dexLabel), "#%03d", id);
    computeCrop(name);

    dma_display->fillScreen(0);   // fresh frame (clears prior sprite / loading pixels)
    blitCanvas();
    drawDexLabel();         // once — the blit leaves its box black, so it persists

    // Start motion instantly: advance to frame 1 on the very next tick rather than
    // holding frame 0 for its encoded delay. Some sprites open on a long "rest"
    // frame, which looked like the animation was frozen for a moment at slide start.
    (void)delayMs;
    s_nextFrameMs = millis();
    return true;
}

bool displayShowStatic(const uint8_t* data, size_t len, const char* name, int id) {
    if (!dma_display) return false;
    displayAnimStop();   // drop any previous slide's GIF + canvas

    if (png.openRAM((uint8_t*)data, (int)len, pngDraw) != PNG_SUCCESS) return false;
    g_cw = png.getWidth();
    g_ch = png.getHeight();
    if (g_cw <= 0 || g_ch <= 0) { png.close(); return false; }

    size_t bytes = (size_t)g_cw * g_ch * sizeof(uint16_t);
    g_canvas = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    if (!g_canvas) g_canvas = (uint16_t*)malloc(bytes);
    if (!g_canvas) { png.close(); return false; }
    memset(g_canvas, 0, bytes);   // black background (transparent -> black)

    int rc = png.decode(nullptr, 0);
    png.close();
    if (rc != PNG_SUCCESS) { free(g_canvas); g_canvas = nullptr; return false; }

    displayLoadingStop();   // sprite is ready — end State 1 before we draw

    snprintf(s_dexLabel, sizeof(s_dexLabel), "#%03d", id);
    computeCrop(name);

    dma_display->fillScreen(0);
    blitCanvas();
    drawDexLabel();
    s_animOpen = false;     // static: displayAnimTick() is a no-op for this slide
    return true;
}

void displayAnimTick() {
    if (!s_animOpen || !dma_display) return;
    uint32_t now = millis();
    if ((int32_t)(now - s_nextFrameMs) < 0) return;   // next frame not due yet

    applyDisposal();                                  // clear prior frame if needed
    int delayMs = 0;
    int rc = gif.playFrame(false, &delayMs);          // composites onto g_canvas
    if (rc < 0) { s_animOpen = false; return; }        // decode error: freeze frame
    blitCanvas();
    if (rc == 0) {                                     // last frame: restart the loop
        gif.reset();
        memset(g_canvas, 0, (size_t)g_cw * g_ch * sizeof(uint16_t));
        s_dispMethod = 0;                              // fresh canvas, no prior frame
    }

    // Scale the authored delay by the speed setting (100 = original), then floor.
    if (s_speedPct != 100 && s_speedPct > 0)
        delayMs = (int)((long)delayMs * 100 / s_speedPct);
    if (delayMs < ANIM_MIN_FRAME_MS) delayMs = ANIM_MIN_FRAME_MS;

    // Advance the schedule from the previous target, not from `now`, so loop latency
    // doesn't accumulate and playback holds the GIF's authored cadence. Resync if we
    // fell more than a frame behind (e.g. after a stall) to avoid a catch-up burst.
    s_nextFrameMs += delayMs;
    if ((int32_t)(now - s_nextFrameMs) > delayMs) s_nextFrameMs = now + delayMs;
}

void displaySetSpeed(uint16_t pct) {
    if (pct < ANIM_SPEED_MIN) pct = ANIM_SPEED_MIN;
    if (pct > ANIM_SPEED_MAX) pct = ANIM_SPEED_MAX;
    s_speedPct = pct;
}

void displayDrawName(const char* name) {
    if (!dma_display) return;
    const int lines = planLines(name);
    s_nameTopY = PANEL_HEIGHT - nameHeightFor(lines) - NAME_BOTTOM_PAD;
    dma_display->fillRect(0, s_nameTopY, PANEL_WIDTH, PANEL_HEIGHT - s_nameTopY, 0);
    dma_display->setTextSize(1);
    dma_display->setTextColor(white());

    // Too long for two lines: single-line marquee on the bottom row.
    if (displayNameScrolls(name)) {
        const int y = PANEL_HEIGHT - TEXT_GLYPH_H - NAME_BOTTOM_PAD;
        const int w = (int)strlen(name) * TEXT_CHAR_W;
        const int total = w + 12;
        const int off = (int)((millis() / 40) % total);   // ~25 px/s
        dma_display->setCursor(-off, y);         dma_display->print(name);
        dma_display->setCursor(-off + total, y); dma_display->print(name);
        return;
    }

    char l1[24], l2[24];
    if (wrapName(name, l1, l2, sizeof(l1)) == 1) {
        drawCentered(l1, PANEL_HEIGHT - TEXT_GLYPH_H - NAME_BOTTOM_PAD);   // single bottom line
    } else {
        drawCentered(l1, s_nameTopY);                      // top line
        drawCentered(l2, s_nameTopY + TEXT_LINE_H);        // bottom line
    }
}

// ---- State 3: duration overlay ----------------------------------------------

void displayDrawDuration(int seconds) {
    if (!dma_display) return;
    // Clear only the current name band, leaving the sprite above intact.
    dma_display->fillRect(0, s_nameTopY, PANEL_WIDTH, PANEL_HEIGHT - s_nameTopY, 0);
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", seconds);
    dma_display->setTextSize(1);
    dma_display->setTextColor(yellow());
    drawCentered(buf, PANEL_HEIGHT - TEXT_GLYPH_H - NAME_BOTTOM_PAD);   // bottom-most row
}

void displaySetup(const char* ssid, const char* ip) {
    displayLoadingStop();
    if (!dma_display) return;
    dma_display->fillScreen(0);
    dma_display->setTextSize(1);
    dma_display->setTextWrap(true);            // long SSID / IP flow onto a second line
    dma_display->setCursor(0, 1);
    dma_display->setTextColor(yellow()); dma_display->print("WiFi setup\n");
    dma_display->setTextColor(white());  dma_display->print("join AP:\n");
    dma_display->setTextColor(blue());   dma_display->print(ssid); dma_display->print("\n");
    dma_display->setTextColor(white());  dma_display->print("open:\n");
    dma_display->setTextColor(blue());   dma_display->print(ip);
    dma_display->setTextWrap(false);
}
