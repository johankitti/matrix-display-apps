#include "display.h"
#include "settings.h"

#include <display_core.h>    // shared panel object (dma_display) + init/night helpers
#include <Fonts/TomThumb.h>  // 3x5 font from Adafruit GFX: 16 chars per line

// ---------------------------------------------------------------------------
// Layout constants (pixel coordinates; text y values are font baselines).
// TomThumb advances 4 px per character, so a 64 px row fits 16 characters.
// ---------------------------------------------------------------------------
static const int PAD           = 1;   // 1px margin around all content
static const int HEADER_BASE   = 6;   // baselines shifted +1 vs. edge for 1px top padding
static const int HEADER_RULE_Y = 8;
static const int LEADER_BASE0  = 15;  // first leader row baseline
static const int ROW_PITCH     = 6;   // up to 8 rows fit: baselines 15..57
static const int COL_GAP       = 1;   // 1px between every column
static const int NAME_GAP      = COL_GAP;
static const int TOTAL_RIGHT   = PANEL_WIDTH - 1 - PAD;  // far-right edge: total; also "NEXT UP" status

// Colors (initialised in displayInit, after the driver exists)
static uint16_t C_WHITE, C_GRAY, C_DIM, C_YELLOW, C_RED, C_GREEN, C_CYAN,
    C_ORANGE, C_METER;

// ---------------------------------------------------------------------------
// Live-board row-shuffle model. The visible board is treated as ONE ordered
// list (leaders then pinned), each row carrying its target baseline. When a
// fetch changes a player's row, they slide to the new one; players entering or
// leaving the visible set slide in/out the bottom edge (the off-screen "row 9").
// ---------------------------------------------------------------------------

static const int OFFSCREEN_Y = 71;  // a full row-pitch below the y=63 bottom edge

struct RowSlot {
  GolferRow row;
  int baseY;   // this row's target text baseline on the current board
};

struct RowScene {
  char header[40];       // event name (raw; truncated at draw time), matches eventName
  char roundLabel[6];    // "R1".."R4"
  RowSlot rows[BOARD_ROWS + MAX_PINNED_ROWS];  // leaders followed by pinned
  int rowCount;
  int dividerY;          // leader/pinned divider baseline, or -1 if no pinned block
};

// One element in a shuffle: a row (or the divider) sliding oldY -> newY.
struct AnimEntry {
  GolferRow row;
  int  oldY, newY;
  bool isDivider;
};

// The board currently on the panel, so the next fetch can diff against it.
static RowScene sShown;
static bool sHaveShownLive = false;  // true only while a live board is showing;
                                     // cleared by loading/message/next screens so
                                     // the first live render after them never slides

static int textWidth(const char* s) {
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->getTextBounds(s, 0, 20, &x1, &y1, &w, &h);
  return (int)w;
}

static void drawText(int x, int baseY, const char* s, uint16_t color) {
  dma_display->setTextColor(color);
  dma_display->setCursor(x, baseY);
  dma_display->print(s);
}

static void drawTextRight(int xRight, int baseY, const char* s, uint16_t color) {
  drawText(xRight - textWidth(s) + 1, baseY, s, color);
}

// Golf-TV convention: red for under par. Green for over, white for even.
static uint16_t scoreColor(const char* score) {
  if (strcmp(score, "-") == 0) return C_GRAY;  // no data yet (placeholder dash)
  if (score[0] == '-') return C_RED;    // under par
  if (score[0] == '+') return C_GREEN;  // over par
  return C_WHITE;                        // even
}

// Right-hand columns are each sized to their widest possible value and packed
// against the right edge with a 1px gap; the name flexes into the space left.
// Widths depend only on the (fixed) font, so the edges are computed once.
//   L→R:  NAME (flex) · thru(holes) · today(round score) · total  [far right]
struct ColLayout {
  int thruR, todayR, totalR;  // right edges (right-aligned text)
  int thruLeft;               // reserved left edge of the leftmost column
};

static const ColLayout& columns() {
  static ColLayout L;
  static bool ready = false;
  if (!ready) {
    int totalW = textWidth("-15");  // widest total to par (e.g. "-15", "+10")
    int todayW = textWidth("-15");  // widest single-round to par
    int thruW  = textWidth("18");   // widest holes-played ("17", then "F")
    L.totalR = TOTAL_RIGHT;
    L.todayR = L.totalR - totalW - COL_GAP;
    L.thruR  = L.todayR - todayW - COL_GAP;
    L.thruLeft = L.thruR - thruW + 1;
    ready = true;
  }
  return L;
}

static void drawRow(const GolferRow& row, int baseY) {
  const ColLayout& L = columns();

  // Out of the tournament (cut/withdrawn/DQ): orange badge in the rank column,
  // total still on the right, the per-round columns left blank. The name flexes
  // into the freed space so the row reads "MC  ABERG            -1".
  if (row.out) {
    drawText(PAD, baseY, row.pos, C_ORANGE);
    int nameX = PAD + textWidth(row.pos) + NAME_GAP;
    drawTextRight(L.totalR, baseY, row.score, scoreColor(row.score));
    int maxChars = (L.todayR - nameX + 1) / 4;
    char name[sizeof(row.name)];
    strlcpy(name, row.name, sizeof(name));
    if (maxChars >= 0 && maxChars < (int)strlen(name)) name[maxChars] = 0;
    drawText(nameX, baseY, name, row.selected ? C_CYAN : C_WHITE);
    return;
  }

  // Rank hard against the left margin; the name sits right after it.
  drawText(PAD, baseY, row.pos, C_GRAY);
  int nameX = PAD + textWidth(row.pos) + NAME_GAP;

  // Total (to par) is always the far-right column.
  drawTextRight(L.totalR, baseY, row.score, scoreColor(row.score));

  int nameRight;  // last x the name may use
  if (row.tee[0]) {
    // Yet to tee off: the tee time (gray, so it reads as status) fills the
    // holes + round-score columns; the total score stays on the right.
    drawTextRight(L.todayR, baseY, row.tee, C_GRAY);
    nameRight = (L.todayR - textWidth(row.tee) + 1) - COL_GAP;
  } else {
    // Playing / finished: holes played (left), then this round's score. Name
    // clips at the reserved column edge so rows stay vertically aligned.
    drawTextRight(L.todayR, baseY, row.today, scoreColor(row.today));
    drawTextRight(L.thruR,  baseY, row.thru,  C_GRAY);
    nameRight = L.thruLeft - COL_GAP;
  }

  int maxChars = (nameRight - nameX + 1) / 4;
  char name[sizeof(row.name)];
  strlcpy(name, row.name, sizeof(name));
  if (maxChars >= 0 && maxChars < (int)strlen(name)) name[maxChars] = 0;

  drawText(nameX, baseY, name, row.selected ? C_CYAN : C_WHITE);
}

// ---------------------------------------------------------------------------
// Loading animation: a golf ball rolls along the green toward the flag.
// Runs on its own task so it survives the main task blocking on TLS reads.
// The task only touches the ball band (x < 48, three rows above the ground)
// with stateless fillRect calls — text (shared GFX cursor) stays on the
// main task, so the two never contend.
// ---------------------------------------------------------------------------

static const int GROUND_Y   = 44;  // the green
static const int BALL_MIN_X = 2;
static const int BALL_MAX_X = 45;  // stops at the lip of the hole
static const int HOLE_X     = 47;
static const int FLAG_X     = 50;

static volatile bool animRun = false;
static TaskHandle_t animTask = nullptr;
static char sLoadingStatus[16] = "";  // current status word, e.g. "FETCHING"

static void drawLoadingScene();  // defined below; the anim task redraws it each frame

// Redraws the status line with `dots` trailing dots (0-3). The left edge is
// anchored on the widest form ("WORD...") so the word doesn't jitter sideways
// as the dots grow. Runs only on the animation task (see below), so it never
// contends with the main task for the shared GFX text cursor.
static void drawLoadingStatus(int dots) {
  char full[20];
  snprintf(full, sizeof(full), "%s...", sLoadingStatus);
  int x = (PANEL_WIDTH - textWidth(full)) / 2;
  if (x < 0) x = 0;
  char shown[20];
  snprintf(shown, sizeof(shown), "%s%.*s", sLoadingStatus, dots, "...");
  dma_display->fillRect(0, 54, PANEL_WIDTH, 10, 0);
  drawText(x, 61, shown, C_GRAY);
}

// With double buffering on, incremental band-erasing no longer works (the two
// buffers alternate, so a persistent scene can't live in just one). Instead the
// task redraws the whole scene each tick into the back buffer, then flips — a
// full 64x64 GFX repaint at 45ms is trivially cheap. The task owns all drawing
// and flipping while it runs, so main never shares the GFX cursor or the buffers.
static void loadingAnimTask(void*) {
  int x = BALL_MIN_X;
  int holdFrames = 0;           // brief pause after the ball drops in
  int dotFrame = 0, dots = 0;   // status dots cycle 0->1->2->3 on their own beat
  while (animRun) {
    drawLoadingScene();                          // clears + draws the static scene
    if (holdFrames == 0 && x <= BALL_MAX_X)
      dma_display->fillRect(x, GROUND_Y - 2, 2, 2, C_WHITE);  // the rolling ball
    drawLoadingStatus(dots);
    dma_display->flipDMABuffer();

    if (holdFrames > 0) {
      holdFrames--;                              // ball in the hole: hold
    } else if (x <= BALL_MAX_X) {
      x++;
    } else {
      x = BALL_MIN_X;                            // tee up again after a beat
      holdFrames = 11;                           // ~500ms at 45ms/frame
    }
    // Advance the dots ~every 400ms, independent of the ball, so "FETCHING..."
    // keeps animating even while the ball is paused in the hole.
    if (++dotFrame >= 9) {
      dotFrame = 0;
      dots = (dots + 1) % 4;
    }
    vTaskDelay(pdMS_TO_TICKS(45));
  }
  animTask = nullptr;
  vTaskDelete(nullptr);
}

static void drawLoadingScene() {
  dma_display->clearScreen();
  drawText((PANEL_WIDTH - textWidth("GOLF")) / 2, 12, "GOLF", C_YELLOW);
  drawText((PANEL_WIDTH - textWidth("LEADERBOARD")) / 2, 19, "LEADERBOARD",
           C_YELLOW);

  uint16_t green = dma_display->color565(0, 120, 40);
  dma_display->drawFastHLine(0, GROUND_Y, PANEL_WIDTH, green);
  dma_display->fillRect(HOLE_X, GROUND_Y, 3, 2, 0);                    // the hole
  dma_display->drawFastVLine(FLAG_X, GROUND_Y - 14, 14, C_WHITE);      // pin
  for (int r = 0; r < 4; r++) {                                // flag
    dma_display->drawFastHLine(FLAG_X - 4 + r, GROUND_Y - 14 + r, 4 - r, C_RED);
  }
}

void displayLoading(const char* status) {
  if (!dma_display) return;
  // Publish the status word; the animation task owns the actual drawing and
  // flipping (scene, ball and dots), so main and anim never share the GFX
  // cursor or the DMA buffers.
  strlcpy(sLoadingStatus, status, sizeof(sLoadingStatus));
  if (!animRun) {
    animRun = true;
    xTaskCreate(loadingAnimTask, "loadanim", 4096, nullptr, 1, &animTask);
  }
  sHaveShownLive = false;  // loading screen replaced the live board: no slide next
}

void displayLoadingStop() {
  if (!animRun) return;
  animRun = false;
  while (animTask != nullptr) delay(1);  // wait for the task's last frame
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool displayInit() {
  // Double buffering: the tear-free row-shuffle animation repaints the whole
  // board every frame, so it draws into a hidden back buffer and flips in one
  // shot (a single buffer would show half-drawn frames). TomThumb 3x5 font.
  if (!displayCoreInit(/*doubleBuff=*/true, &TomThumb)) return false;

  dma_display->setBrightness8(settings.brightness);

  C_WHITE  = dma_display->color565(255, 255, 255);
  C_GRAY   = dma_display->color565(140, 140, 140);
  C_DIM    = dma_display->color565(55, 55, 55);
  C_YELLOW = dma_display->color565(255, 200, 0);
  C_RED    = dma_display->color565(255, 60, 60);
  C_GREEN  = dma_display->color565(0, 210, 90);
  C_CYAN   = dma_display->color565(0, 210, 255);
  C_ORANGE = dma_display->color565(255, 130, 0);
  C_METER  = dma_display->color565(18, 18, 18);  // progress meter: just barely above black
  return true;
}

// displaySetBrightness() and displayMessage() now live in display-core. Golf's
// full-screen messages only appear during Wi-Fi setup (before any live board),
// so display-core's displayMessage() — which doesn't touch golf's shuffle state
// — is equivalent here.

// Greedy word-wrap of `src` into two lines of up to 16 chars each; anything
// that doesn't fit is dropped (the panel is only so big).
static void wrapTwoLines(const char* src, char* l1, char* l2) {
  const size_t MAXC = 16;
  l1[0] = l2[0] = 0;
  char* lines[2] = {l1, l2};
  int line = 0;
  const char* w = src;
  while (*w && line < 2) {
    while (*w == ' ') w++;
    const char* end = strchr(w, ' ');
    size_t wordLen = end ? (size_t)(end - w) : strlen(w);
    size_t curLen = strlen(lines[line]);
    size_t need = wordLen + (curLen ? 1 : 0);
    if (curLen + need > MAXC) {
      if (curLen == 0) {  // single word longer than a line: hard cut
        strncat(lines[line], w, MAXC);
        w += wordLen;
      }
      line++;
      continue;
    }
    if (curLen) strcat(lines[line], " ");
    strncat(lines[line], w, wordLen);
    w += wordLen;
  }
}

// MODE_NEXT: what's coming up, and whether the pinned golfers are playing.
static void renderNext(const Leaderboard& lb) {
  drawText(PAD, HEADER_BASE, "NEXT UP", C_YELLOW);
  dma_display->drawFastHLine(PAD, HEADER_RULE_Y, PANEL_WIDTH - 2 * PAD, C_DIM);

  char l1[17], l2[17];
  wrapTwoLines(lb.nextName, l1, l2);
  drawText(PAD, 16, l1, C_WHITE);
  if (l2[0]) drawText(PAD, 22, l2, C_WHITE);
  drawText(PAD, 30, lb.nextDates, C_GRAY);

  if (lb.nextGolferCount > 0) {
    dma_display->drawFastHLine(PAD, 35, PANEL_WIDTH - 2 * PAD, C_DIM);
    for (int i = 0; i < lb.nextGolferCount; i++) {
      const NextGolfer& g = lb.nextGolfers[i];
      int baseY = 43 + i * ROW_PITCH;
      drawText(PAD, baseY, g.name, C_CYAN);
      uint16_t c = C_GRAY;                        // TBD: field not out yet
      if (strcmp(g.status, "IN") == 0) c = C_GREEN;
      else if (strcmp(g.status, "OUT") == 0) c = C_ORANGE;
      drawTextRight(TOTAL_RIGHT, baseY, g.status, c);
    }
  }
}

// MODE_LIVE geometry: flatten the board into one ordered list of rows (leaders
// then pinned), each with its target baseline, plus the divider position. This
// is the single source of layout truth for both the static render and the
// animation, so the two can never drift. Mirrors the old renderLive() math.
static void buildScene(const Leaderboard& lb, RowScene& out) {
  strlcpy(out.header, lb.eventName, sizeof(out.header));
  strlcpy(out.roundLabel, lb.roundLabel, sizeof(out.roundLabel));
  out.rowCount = 0;
  for (int i = 0; i < lb.leaderCount; i++) {
    out.rows[out.rowCount].row   = lb.leaders[i];
    out.rows[out.rowCount].baseY = LEADER_BASE0 + i * ROW_PITCH;
    out.rowCount++;
  }
  if (lb.pinnedCount > 0) {
    int lastLeaderBase = LEADER_BASE0 + (lb.leaderCount - 1) * ROW_PITCH;
    out.dividerY = lastLeaderBase + 2;      // 2px below the last leader
    int pinnedBase0 = out.dividerY + 7;
    for (int i = 0; i < lb.pinnedCount; i++) {
      out.rows[out.rowCount].row   = lb.pinned[i];
      out.rows[out.rowCount].baseY = pinnedBase0 + i * ROW_PITCH;
      out.rowCount++;
    }
  } else {
    out.dividerY = -1;
  }
}

static int findByName(const RowScene& s, const char* name) {
  for (int i = 0; i < s.rowCount; i++)
    if (strcmp(s.rows[i].row.name, name) == 0) return i;
  return -1;
}

// Build the per-row slide list between two scenes, keyed by player name (the
// only identity a GolferRow carries). Returns whether anything actually moved,
// so a score-only update (same slots) can skip the animation. Capacity of
// `out` must be 2*(BOARD_ROWS+MAX_PINNED_ROWS)+1.
static bool diffScenes(const RowScene& oldS, const RowScene& newS,
                       AnimEntry* out, int& count) {
  count = 0;
  bool anyMoved = false;

  // Rows on the new board: staying (matched in old) or entering (from row 9).
  for (int i = 0; i < newS.rowCount; i++) {
    AnimEntry& e = out[count++];
    e.row = newS.rows[i].row;          // draw the new data at the destination
    e.isDivider = false;
    e.newY = newS.rows[i].baseY;
    int oi = findByName(oldS, newS.rows[i].row.name);
    e.oldY = (oi >= 0) ? oldS.rows[oi].baseY : OFFSCREEN_Y;
    if (e.oldY != e.newY) anyMoved = true;
  }

  // Rows only on the old board: leaving — slide down off the bottom edge.
  for (int i = 0; i < oldS.rowCount; i++) {
    if (findByName(newS, oldS.rows[i].row.name) >= 0) continue;
    AnimEntry& e = out[count++];
    e.row = oldS.rows[i].row;          // keep showing the old data as it exits
    e.isDivider = false;
    e.oldY = oldS.rows[i].baseY;
    e.newY = OFFSCREEN_Y;
    anyMoved = true;
  }

  // The leader/pinned divider slides too (its Y moves when leaderCount changes,
  // and it enters/leaves via the bottom edge when the pinned block appears or
  // disappears between fetches).
  AnimEntry& d = out[count++];
  d.isDivider = true;
  d.oldY = (oldS.dividerY >= 0) ? oldS.dividerY : OFFSCREEN_Y;
  d.newY = (newS.dividerY >= 0) ? newS.dividerY : OFFSCREEN_Y;
  if (d.oldY != d.newY) anyMoved = true;

  return anyMoved;
}

// Header (event name truncated to leave room for the round label) — never moves.
static void drawHeader(const RowScene& s) {
  int labelW = textWidth(s.roundLabel);
  int nameBudget = (PANEL_WIDTH - 2 * PAD - labelW - 3) / 4;
  char header[sizeof(s.header)];
  strlcpy(header, s.header, sizeof(header));
  if (nameBudget >= 0 && nameBudget < (int)strlen(header)) header[nameBudget] = 0;
  drawText(PAD, HEADER_BASE, header, C_YELLOW);
  drawTextRight(PANEL_WIDTH - 1 - PAD, HEADER_BASE, s.roundLabel, C_WHITE);
  dma_display->drawFastHLine(PAD, HEADER_RULE_Y, PANEL_WIDTH - 2 * PAD, C_DIM);
}

static int lerpY(int oldY, int newY, float eased) {
  return (int)lroundf(oldY + (newY - oldY) * eased);
}

// Draw one frame (header + every entry at its interpolated Y) into the back
// buffer. Rows sliding past the bottom edge clip for free (drawRow right-clips
// names; the driver bounds-checks each pixel). Caller flips.
static void renderSceneFrame(const RowScene& scene, const AnimEntry* entries,
                             int entryCount, float eased) {
  dma_display->clearScreen();
  drawHeader(scene);
  for (int i = 0; i < entryCount; i++) {
    const AnimEntry& e = entries[i];
    int y = lerpY(e.oldY, e.newY, eased);
    if (e.isDivider) {
      if (y > HEADER_RULE_Y && y < PANEL_HEIGHT)
        dma_display->drawFastHLine(PAD, y, PANEL_WIDTH - 2 * PAD, C_DIM);
    } else {
      drawRow(e.row, y);
    }
  }
}

// Static render of a scene (no motion): identity entries at eased=1.
static void renderSceneStatic(const RowScene& s) {
  AnimEntry entries[BOARD_ROWS + MAX_PINNED_ROWS + 1];
  int n = 0;
  for (int i = 0; i < s.rowCount; i++) {
    entries[n].row = s.rows[i].row;
    entries[n].isDivider = false;
    entries[n].oldY = entries[n].newY = s.rows[i].baseY;
    n++;
  }
  if (s.dividerY >= 0) {
    entries[n].isDivider = true;
    entries[n].oldY = entries[n].newY = s.dividerY;
    n++;
  }
  renderSceneFrame(s, entries, n, 1.0f);
}

// Play the ~1.5s reshuffle: smoothstep-eased slide, one flip per frame. The
// clamped final frame (t==1) is pixel-identical to renderSceneStatic(newS), so
// it doubles as the resting frame. Runs synchronously on the main task — the
// loading task is already stopped and nothing else touches the panel.
static void animateShuffle(const RowScene& newS, const AnimEntry* entries,
                           int n) {
  const uint32_t DURATION_MS = 1500;
  uint32_t start = millis();
  for (;;) {
    uint32_t elapsed = millis() - start;
    float t = elapsed >= DURATION_MS ? 1.0f : (float)elapsed / DURATION_MS;
    float e = t * t * (3.0f - 2.0f * t);   // smoothstep: soft ends, quick middle
    renderSceneFrame(newS, entries, n, e);
    dma_display->flipDMABuffer();
    if (t >= 1.0f) break;
    delay(30);                             // ~35 frames over 1.5s
  }
}

void displayLeaderboard(const Leaderboard& lb, bool fetchOk) {
  if (!dma_display) return;
  displayLoadingStop();
  (void)fetchOk;  // staleness indicator (the corner dot) removed for space

  if (lb.mode == MODE_LIVE) {
    RowScene next;
    buildScene(lb, next);
    // 2*(rows) covers every row possibly leaving as well as arriving, +1 divider.
    static AnimEntry entries[2 * (BOARD_ROWS + MAX_PINNED_ROWS) + 1];
    int n = 0;
    // Animate only when a previous live board is on screen and a slot changed;
    // otherwise (cold boot, after a loading/next screen, or score-only update)
    // just draw the resting frame.
    if (sHaveShownLive && diffScenes(sShown, next, entries, n)) {
      animateShuffle(next, entries, n);
    } else {
      renderSceneStatic(next);
      dma_display->flipDMABuffer();
    }
    sShown = next;
    sHaveShownLive = true;
    return;
  }

  dma_display->clearScreen();
  if (lb.mode == MODE_NEXT) {
    renderNext(lb);
  } else {  // MODE_NONE
    drawText((PANEL_WIDTH - textWidth("PGA TOUR")) / 2, 26, "PGA TOUR", C_YELLOW);
    drawText((PANEL_WIDTH - textWidth("SEASON OVER")) / 2, 36, "SEASON OVER", C_WHITE);
  }
  dma_display->flipDMABuffer();
  sHaveShownLive = false;
}

void displayProgress(float frac) {
  // Only meaningful over a live board (we keep its scene to redraw from). The
  // meter is a thin, near-black bar along the bottom edge that fills left->right
  // as the next refresh approaches, so it reads as a subtle "time to update".
  if (!dma_display || !sHaveShownLive) return;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  renderSceneStatic(sShown);                 // full board into the back buffer
  int w = (int)lroundf(frac * (PANEL_WIDTH - 2 * PAD));
  if (w > 0) dma_display->fillRect(PAD, PANEL_HEIGHT - 2, w, 2, C_METER);
  dma_display->flipDMABuffer();
}

// displayPowerOff() / displayReleaseHolds() now live in display-core. Golf's
// loading animation runs on its own task, so main.cpp calls displayLoadingStop()
// before displayPowerOff() to make sure nothing redraws the panel as it sleeps.
