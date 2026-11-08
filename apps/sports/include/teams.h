#pragma once
#include <Arduino.h>
#include <time.h>

// One tracked team: where its fixtures come from and how it looks on the panel.
enum Sport : uint8_t { SPORT_SOCCER, SPORT_HOCKEY, SPORT_FOOTBALL };
enum Source : uint8_t {
  SRC_ESPN,     // ESPN team nextEvent for fixtures, ESPN event scoreboard while live
  SRC_HA,       // TheSportsDB for fixtures, hockeyallsvenskan.se while live
};

struct Rgb { uint8_t r, g, b; };

struct TeamDef {
  const char* name;    // settings-page label
  Sport sport;         // picks the panel icon
  Source source;
  const char* feed;    // ESPN "<sport>/<league>/teams/<id>" | TheSportsDB team id
  const char* id;      // our id in that feed (ESPN team id | HA slug code "aik")
  const char* abbr;    // our short name on the panel
  Rgb color;           // our abbreviation
  Rgb countdown;       // countdown text
};

enum GameState : uint8_t { GS_PRE, GS_LIVE, GS_FINAL };

// The next (or current) game for a team.
struct Game {
  bool known;          // a game exists (false = off-season / none listed)
  time_t start;        // kick-off, UTC epoch
  bool home;
  GameState state;     // as last reported by the live feed (GS_PRE until then)
  char opp[6];         // opponent abbreviation, ASCII
  char homeScore[4];   // "" until the live feed reports one
  char awayScore[4];
  char clock[12];      // "67'", "HT", "Q3 8:12", "P2" — "" if unknown
  char decided[4];     // "OT" / "SO" on a final decided after regulation
  time_t finalSeen;    // when GS_FINAL was first seen (for the FT hold)
  // Live-feed handle, resolved from the schedule fetch:
  char liveLeague[48]; // ESPN "soccer/swe.1" / "football/nfl"; unused for HA
  char liveId[32];     // ESPN event id | HA game slug "20261111-lif-aik"
};

struct TeamState {
  Game game;
  bool tried;          // at least one fetch attempted
  bool everFetched;    // at least one successful fetch
  bool lastOk;         // most recent fetch succeeded
  bool liveChecked;    // ESPN: the after-boot "already live?" scoreboard check ran
  time_t doneThrough;  // end time of the last finished game (lagging-feed guard)
  uint32_t nextFetchMs;
};

inline bool hasScore(const Game& g) { return g.homeScore[0] && g.awayScore[0]; }
inline const char* homeAbbr(const TeamDef& t, const Game& g) { return g.home ? t.abbr : g.opp; }
inline const char* awayAbbr(const TeamDef& t, const Game& g) { return g.home ? g.opp : t.abbr; }

extern const TeamDef TEAMS[];
extern const int TEAM_COUNT;
extern TeamState teamState[];

// Refresh team i: the live feed while its game is on (or just finished),
// otherwise the fixture list. Returns success.
bool teamFetch(int i);

// True when the live feed should be polled for team i right now.
bool teamTracking(int i, time_t now);

// Display state derived from the feed state + the clock.
enum Phase : uint8_t {
  PH_NONE,       // no game known
  PH_COUNTDOWN,  // before kick-off
  PH_LIVE,       // in progress (or kick-off passed, feed not caught up)
  PH_FINAL,      // finished, within the FT hold
  PH_STALE,      // long past and no feed update — waiting for the next fixture
};
Phase teamPhase(int i, time_t now);
