#pragma once

#include <Arduino.h>
#include "config.h"
#include "settings.h"   // Tour + MAX_PINNED_GOLFERS

// Up to 2 tracked golfers who sit outside the top can be pinned to the bottom
// rows on a 64px-tall panel; the leader block fills whatever rows remain.
#define MAX_PINNED_ROWS 2

enum BoardMode : uint8_t {
  MODE_NONE,  // nothing on the calendar (deep off-season)
  MODE_LIVE,  // a tournament round is in progress -> leaderboard
  MODE_NEXT,  // between tournaments -> upcoming event + field status
};

struct GolferRow {
  char pos[5];    // "1", "T12", "-"
  char name[10];  // surname, ASCII-folded + uppercased, e.g. "ABERG"
  char today[6];  // this round's score to par: "-2", "+1", "E"
  char score[6];  // total to par: "-14", "+2", "E"
  char thru[4];   // holes played this round: "12", "F" (finished), "-" (not started)
  char tee[6];    // local tee time "HH:MM" when yet to start today; "" otherwise
  bool selected = false;  // one of the user's pinned golfers -> name highlighted anywhere
  bool out = false;  // cut/withdrawn/DQ -> `pos` holds a badge ("MC"/"WD"/"DQ"),
                     // per-round columns are blank
};

// Pinned golfer's status for the upcoming event.
struct NextGolfer {
  char name[10];
  char status[4];  // "IN" (in the field), "OUT" (not entered), "TBD"
  char tee[6];     // local tee time "HH:MM" once the draw is out; "" otherwise
};

struct Leaderboard {
  BoardMode mode = MODE_NONE;
  Tour tour = TOUR_PGA;  // which tour this board came from

  // Per tracked golfer (settings.pinned order): is that golfer in the field of
  // the event this board shows? Filled for both MODE_LIVE and MODE_NEXT (false
  // while the upcoming event's field is unpublished). Drives TOUR_AUTO's choice
  // between the PGA and DP World boards — see fetchLeaderboard.
  bool pickInField[MAX_PINNED_GOLFERS] = {};

  // MODE_LIVE
  char eventName[40];   // tournament name, ASCII-folded + uppercased
  char roundLabel[6];   // "R1".."R4", or "F" once the tournament is Final
  bool firstRound = false;  // showing R1: players yet to tee off carry only a tee
                            // time (no score), so their rows drop rank + total
  GolferRow leaders[BOARD_ROWS];
  uint8_t leaderCount = 0;
  GolferRow pinned[MAX_PINNED_ROWS];
  uint8_t pinnedCount = 0;

  // MODE_NEXT
  char nextName[40];    // upcoming tournament name
  char nextDates[16];   // "AUG 13-16", "AUG 30-SEP 2"
  time_t nextStart = 0; // first-tee UTC epoch, for the live countdown (0 if unknown)
  time_t nextStartDay = 0;  // the event's calendar start date as a UTC epoch, known
                            // as soon as the event is picked (unlike nextStart,
                            // which waits for the draw). Lets TOUR_AUTO compare how
                            // far off each tour's next event is — see chooseTour.
  NextGolfer nextGolfers[MAX_PINNED_ROWS];
  uint8_t nextGolferCount = 0;
};

// Fetches ESPN's scoreboard for the configured tour (settings.tour) and fills
// `out`. TOUR_AUTO fetches both tours and picks the one the tracked golfers
// are playing. Returns false on any network/parse error (in which case `out`
// is left untouched).
bool fetchLeaderboard(Leaderboard& out);

// Short uppercase display name for a tour: "PGA TOUR" / "DP WORLD TOUR" /
// "AUTO" (for the web page).
const char* tourLabel(uint8_t tour);

// Fills `out` with a synthetic MODE_LIVE leaderboard for display iteration
// (no network). Used by main.cpp when DEBUG_FAKE_LIVE is set in config.h.
void loadDebugLeaderboard(Leaderboard& out);
