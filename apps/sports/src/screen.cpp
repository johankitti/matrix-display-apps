#include "screen.h"

#include <display_core.h>
#include <Fonts/TomThumb.h>

#include "config.h"
#include "teams.h"

// ---- 7x7 sport icons --------------------------------------------------------
// One char per pixel; '.' = off. Palette letters are resolved in drawIcon().
static const char* const ICON_SOCCER[7] = {
    "..WGW..",
    ".WWWWW.",
    "GWGGGWG",
    "WWGGGWW",
    "WWWGWWW",
    ".GWWWG.",
    "..WWW..",
};
static const char* const ICON_HOCKEY[7] = {
    "......S",
    ".....S.",
    "....S..",
    "...S...",
    "..S....",
    "SSS.PP.",
    "....PP.",
};
static const char* const ICON_FOOTBALL[7] = {
    ".......",
    "..BBB..",
    ".BWBWB.",
    "BBWWWBB",
    ".BWBWB.",
    "..BBB..",
    ".......",
};

static void drawIcon(Sport sport, int x, int y) {
  const char* const* art = sport == SPORT_HOCKEY   ? ICON_HOCKEY
                         : sport == SPORT_FOOTBALL ? ICON_FOOTBALL
                                                   : ICON_SOCCER;
  for (int r = 0; r < 7; r++) {
    for (int c = 0; c < 7; c++) {
      uint16_t col;
      switch (art[r][c]) {
        case 'W': col = dma_display->color565(220, 220, 220); break;  // white
        case 'G': col = dma_display->color565(70, 70, 70); break;     // ball patch
        case 'S': col = dma_display->color565(210, 150, 70); break;   // stick
        case 'P': col = dma_display->color565(110, 110, 140); break;  // puck
        case 'B': col = dma_display->color565(170, 80, 25); break;    // leather
        default: continue;
      }
      dma_display->drawPixel(x + c, y + r, col);
    }
  }
}

static uint16_t rgb(Rgb c) { return dma_display->color565(c.r, c.g, c.b); }

// ---- Text helpers -----------------------------------------------------------
// "3d 04:12:09" / "04:12:09". Days only when there is at least one.
static void formatCountdown(long secs, char* out, size_t n) {
  if (secs < 0) secs = 0;
  long d = secs / 86400, h = secs / 3600 % 24, m = secs / 60 % 60, s = secs % 60;
  if (d > 0) snprintf(out, n, "%ldd %02ld:%02ld:%02ld", d, h, m, s);
  else snprintf(out, n, "%02ld:%02ld:%02ld", h, m, s);
}

// Kick-off today -> "21:25", otherwise "15/11" (the countdown has the rest).
static void formatWhen(time_t start, time_t now, char* out, size_t n) {
  struct tm ls, ln;
  localtime_r(&start, &ls);
  localtime_r(&now, &ln);
  if (ls.tm_year == ln.tm_year && ls.tm_yday == ln.tm_yday)
    snprintf(out, n, "%02d:%02d", ls.tm_hour, ls.tm_min);
  else
    snprintf(out, n, "%d/%d", ls.tm_mday, ls.tm_mon + 1);
}

// "2-1" in one colour; returns the x after it.
static int drawScore(int x, int y, const Game& g, uint16_t c) {
  x = displayText(x, y, g.homeScore, c);
  x = displayText(x, y, "-", c);
  return displayText(x, y, g.awayScore, c);
}

// ---- Live / final rows ------------------------------------------------------
// "● 2-1      67'": blinking red dot, score (home-away, same order as the
// matchup above), match clock right-aligned. Without a score yet (feed lag at
// kick-off, or no live feed for this team): "● LIVE".
static void drawLive(const Game& g, int yB, time_t now) {
  if (now % 2 == 0) dma_display->fillRect(TEXT_X, yB - 4, 3, 3, COLOR_LIVE);
  int x = hasScore(g) ? drawScore(TEXT_X + 5, yB, g, COLOR_SCORE)
                      : displayText(TEXT_X + 5, yB, "LIVE", COLOR_LIVE);

  // Clock, shortened ("Q4 10:12" -> "Q4") or dropped if it would collide.
  char clock[12];
  strlcpy(clock, g.clock, sizeof(clock));
  if (clock[0] && x + 2 + displayTextWidth(clock) > RIGHT_EDGE) {
    char* sp = strchr(clock, ' ');
    if (sp) *sp = '\0';
  }
  if (clock[0] && x + 2 + displayTextWidth(clock) <= RIGHT_EDGE)
    displayTextRight(RIGHT_EDGE, yB, clock, COLOR_CLOCK);
}

// "FT 3-2 SO" — result held on screen for FINAL_HOLD_S after the game.
static void drawFinal(const Game& g, int yB) {
  int x = displayText(TEXT_X, yB, "FT ", COLOR_FINAL);
  if (hasScore(g)) x = drawScore(x, yB, g, COLOR_SCORE);
  if (g.decided[0]) displayText(displayText(x, yB, " ", COLOR_FINAL), yB, g.decided, COLOR_FINAL);
}

// ---- One team block ---------------------------------------------------------
static void drawBlock(int i, int y0, time_t now, bool clockOk) {
  const TeamDef& t = TEAMS[i];
  const TeamState& s = teamState[i];
  const Game& g = s.game;
  const int yA = y0 + ROW_A_BASE, yB = y0 + ROW_B_BASE;

  drawIcon(t.sport, ICON_X, y0 + ICON_Y);

  // Row A: "AIK-BKH" (home first, ours in team colour) + when, right-aligned.
  int x = TEXT_X;
  if (!g.known) {
    displayText(x, yA, t.abbr, rgb(t.color));
  } else {
    uint16_t ours = rgb(t.color), theirs = COLOR_OPPONENT;
    x = displayText(x, yA, homeAbbr(t, g), g.home ? ours : theirs);
    x = displayText(x, yA, "-", COLOR_DASH);
    x = displayText(x, yA, awayAbbr(t, g), g.home ? theirs : ours);

    if (clockOk) {
      char when[8];
      formatWhen(g.start, now, when, sizeof(when));
      // Skip it rather than collide with a long matchup ("GAIS-AIK").
      if (x + 2 + displayTextWidth(when) <= RIGHT_EDGE)
        displayTextRight(RIGHT_EDGE, yA, when, COLOR_WHEN);
    }
  }

  // Row B: countdown / live score / final score / status.
  Phase ph = clockOk ? teamPhase(i, now) : PH_NONE;
  if (!s.everFetched) {
    displayText(TEXT_X, yB, s.tried ? "no data" : "...", COLOR_NOGAME);
  } else if (!g.known) {
    displayText(TEXT_X, yB, "no games", COLOR_NOGAME);
  } else if (!clockOk) {
    displayText(TEXT_X, yB, "...", COLOR_NOGAME);
  } else if (ph == PH_LIVE) {
    drawLive(g, yB, now);
  } else if (ph == PH_FINAL) {
    drawFinal(g, yB);
  } else if (ph == PH_STALE) {
    displayText(TEXT_X, yB, "--", COLOR_NOGAME);
  } else {
    char cd[16];
    formatCountdown((long)(g.start - now), cd, sizeof(cd));
    displayText(TEXT_X, yB, cd, rgb(t.countdown));
  }

  // Last fetch for this team failed: red pixel in the block's top-right corner.
  if (s.everFetched && !s.lastOk)
    dma_display->drawPixel(PANEL_WIDTH - 1, y0, COLOR_STALE);
}

void screenRender(time_t now, bool clockOk) {
  dma_display->fillScreen(0);
  dma_display->setFont(&TomThumb);
  dma_display->setTextWrap(false);
  for (int i = 0; i < TEAM_COUNT && i * BLOCK_H < PANEL_HEIGHT; i++) {
    int y0 = i * BLOCK_H;
    drawBlock(i, y0, now, clockOk);
    if (i < TEAM_COUNT - 1)
      dma_display->drawFastHLine(TEXT_X, y0 + DIVIDER_Y, PANEL_WIDTH - TEXT_X - 1,
                                 COLOR_DIVIDER);
  }
  displayFlip();
}

void screenStatus(const char* line1, const char* line2) {
  displayMessage("sports", line1, line2);
}
