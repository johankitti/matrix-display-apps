#include "teams.h"

#include <ArduinoJson.h>
#include <sys/time.h>
#include <display_core.h>   // utf8Fold
#include <net_core.h>       // httpGetBinary, netUtcEpoch
#include <sleep_core.h>     // sleepClockValid

#include "config.h"

// ---- The teams --------------------------------------------------------------
// Order here is the order on the panel (top to bottom). To add/swap a team:
//   ESPN:  feed = "<sport>/<league>/teams/<abbr|id>", id = ESPN team id. Find ids at
//          https://site.api.espn.com/apis/site/v2/sports/soccer/<league>/teams
//          (league "all" covers every competition a team plays in — handy for
//          national teams that hop between Nations League, qualifiers, etc.)
//   HA:    feed = TheSportsDB team id (searchteams.php?t=<name>), id = the
//          team's hockeyallsvenskan.se slug code (see HA_TEAMS below).
const TeamDef TEAMS[] = {
    {"AIK Fotboll", SPORT_SOCCER, SRC_ESPN, "soccer/all/teams/994", "994", "AIK",
     {255, 200, 0}, {255, 200, 0}},
    {"AIK Hockey", SPORT_HOCKEY, SRC_HA, "137307", "aik", "AIK",
     {255, 200, 0}, {255, 200, 0}},
    {"San Francisco 49ers", SPORT_FOOTBALL, SRC_ESPN, "football/nfl/teams/sf", "25", "SF",
     {255, 40, 30}, {230, 175, 70}},
    {"Sverige (herr)", SPORT_SOCCER, SRC_ESPN, "soccer/all/teams/466", "466", "SWE",
     {40, 120, 255}, {255, 205, 0}},
};
const int TEAM_COUNT = sizeof(TEAMS) / sizeof(TEAMS[0]);
TeamState teamState[TEAM_COUNT];

// ---- Small helpers ----------------------------------------------------------
// "2026-11-08T15:30Z" / "2026-11-11T18:00:00" / "...T18:00:00.000Z" (UTC) -> epoch.
// netUtcEpoch, not mktime(): mktime would apply the board's (Swedish) TZ.
static time_t parseIso(const char* s) {
  int y, m, d, hh, mm, ss = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) < 5) return 0;
  return netUtcEpoch(y, m, d, hh, mm, ss);
}

// Copy a JSON string *or* number into out ("" for null/missing).
static void jsonStr(JsonVariantConst v, char* out, size_t n) {
  if (v.is<const char*>()) strlcpy(out, v.as<const char*>(), n);
  else if (v.is<int>()) snprintf(out, n, "%d", v.as<int>());
  else if (v.is<float>()) snprintf(out, n, "%d", (int)v.as<float>());
  else out[0] = '\0';
}

// UTF-8 -> ASCII, upper-cased ("MoDo" -> "MODO", "ÖIK" -> "OIK").
static void foldUpper(const char* s, char* out, size_t n) {
  utf8Fold(s, out, n);
  for (char* p = out; *p; p++) *p = toupper((uint8_t)*p);
}

// ---- HTTP -------------------------------------------------------------------
// GET (buffered whole into PSRAM by net-core — parsing big bodies straight off
// the TLS stream drops connections) + filtered JSON parse. Until NTP has set
// the clock, the response's Date header seeds it so countdowns work right away.
static bool getJson(const char* url, JsonDocument& doc, const JsonDocument& filter) {
  uint8_t* body;
  size_t len;
  time_t serverNow;
  if (!httpGetBinary(url, &body, &len, &serverNow)) return false;
  if (!sleepClockValid() && serverNow > 1700000000) {
    struct timeval tv = {.tv_sec = serverNow, .tv_usec = 0};
    settimeofday(&tv, nullptr);
  }
  DeserializationError err =
      deserializeJson(doc, (const char*)body, len, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(30));
  netFree(body);
  if (err) {
    Serial.printf("[http] JSON error %s (%u bytes) for %s\n", err.c_str(), (unsigned)len, url);
    return false;
  }
  return true;
}

// A fixture that started before the last game we saw finish is that same game
// (or older), still listed by a lagging feed. "Long past kick-off" ones are
// postponed/abandoned games the feed never updated.
static bool fixtureOver(const TeamState& s, time_t start, time_t now) {
  return start <= s.doneThrough || (sleepClockValid() && start + GAME_MAX_LENGTH_S < now);
}

// =============================================================================
//  ESPN
// =============================================================================

// Filter for one competition object (team nextEvent, event scoreboard and
// league scoreboard all share this shape).
static void espnCompetitionFilter(JsonObject fc) {
  fc["status"]["type"]["state"] = true;
  fc["status"]["type"]["name"] = true;
  fc["status"]["displayClock"] = true;
  fc["status"]["period"] = true;
  JsonObject fp = fc["competitors"].add<JsonObject>();
  fp["id"] = true;
  fp["homeAway"] = true;
  fp["score"] = true;
  fp["team"]["abbreviation"] = true;
}

// Filter for one event object (team nextEvent[] / league scoreboard events[]).
static void espnEventFilter(JsonObject fe) {
  fe["id"] = true;
  fe["date"] = true;
  fe["league"]["slug"] = true;
  espnCompetitionFilter(fe["competitions"].add<JsonObject>());
}

static GameState espnState(JsonObjectConst c) {
  const char* state = c["status"]["type"]["state"] | "pre";
  return !strcmp(state, "in") ? GS_LIVE : !strcmp(state, "post") ? GS_FINAL : GS_PRE;
}

// The score is {"value":7.0,"displayValue":"7"} on the team endpoint and a
// plain string on the scoreboards — accept both.
static void espnScore(JsonVariantConst s, char* out, size_t n) {
  if (s.is<const char*>()) strlcpy(out, s.as<const char*>(), n);
  else strlcpy(out, s["displayValue"] | "", n);
}

// Compact clock for the panel: "67'" / "HT" (soccer), "Q3 8:12" / "Q2 END" /
// "HT" (NFL), "P2" (hockey).
static void espnClock(Sport sport, JsonObjectConst st, char* out, size_t n) {
  const char* name = st["type"]["name"] | "";
  const char* dc = st["displayClock"] | "";
  int period = st["period"] | 0;
  if (strstr(name, "HALFTIME")) { strlcpy(out, "HT", n); return; }
  if (sport == SPORT_SOCCER) { strlcpy(out, dc, n); return; }
  char per[4];
  if (sport == SPORT_FOOTBALL) {
    if (period > 4) strlcpy(per, "OT", sizeof(per));
    else snprintf(per, sizeof(per), "Q%d", period);
    if (strstr(name, "END_PERIOD")) snprintf(out, n, "%s END", per);
    else snprintf(out, n, "%s %s", per, dc);
  } else {
    snprintf(out, n, period > 3 ? "OT" : "P%d", period);
  }
}

// Fill g's live fields (state, scores, clock, opponent, home/away) from an
// ESPN competition object. Leaves start/liveId alone.
static void espnApply(const TeamDef& t, JsonObjectConst c, Game& g) {
  JsonObjectConst st = c["status"];
  const char* name = st["type"]["name"] | "";
  g.state = espnState(c);
  g.clock[0] = g.decided[0] = '\0';
  if (g.state == GS_LIVE) espnClock(t.sport, st, g.clock, sizeof(g.clock));
  if (g.state == GS_FINAL) {
    if (strstr(name, "OVERTIME")) strlcpy(g.decided, "OT", sizeof(g.decided));
    else if (strstr(name, "PEN")) strlcpy(g.decided, "PEN", sizeof(g.decided));
    else if (strstr(name, "AET")) strlcpy(g.decided, "AET", sizeof(g.decided));
  }
  for (JsonObjectConst p : c["competitors"].as<JsonArrayConst>()) {
    bool isHome = strcmp(p["homeAway"] | "", "home") == 0;
    if (strcmp(p["id"] | "", t.id) == 0) g.home = isHome;
    else utf8Fold(p["team"]["abbreviation"] | "?", g.opp, sizeof(g.opp));
    if (g.state != GS_PRE)
      espnScore(p["score"], isHome ? g.homeScore : g.awayScore, sizeof(g.homeScore));
  }
}

// "<sport>/<league>" the live scoreboard lives under: the event's own
// competition when it names one (an "all" feed spans several), else the feed's
// league. ("soccer/all/teams/994" + "swe.1" -> "soccer/swe.1".)
static void espnLeague(const char* feed, const char* eventLeague, char* out, size_t n) {
  if (eventLeague && *eventLeague) {
    snprintf(out, n, "%.*s/%s", (int)strcspn(feed, "/"), feed, eventLeague);
  } else {
    strlcpy(out, feed, n);
    char* p = strstr(out, "/teams");
    if (p) *p = '\0';
  }
}

static bool espnHasTeam(const TeamDef& t, JsonObjectConst c) {
  for (JsonObjectConst p : c["competitors"].as<JsonArrayConst>())
    if (strcmp(p["id"] | "", t.id) == 0) return true;
  return false;
}

// The team endpoint's nextEvent (like its schedule) leaves out a game while it
// is in progress. After a boot that could mean booting mid-game and skipping
// straight to the next fixture — so on the first fetch, also look at today's
// league scoreboard for a live game of ours.
// Returns false only if the scoreboard couldn't be fetched; *found says
// whether a live game of ours was picked up into `out`.
static bool espnFindLiveInLeague(const TeamDef& t, const char* league, Game& out, bool* found) {
  *found = false;
  char url[160];
  snprintf(url, sizeof(url), ESPN_LEAGUE_SCOREBOARD_URL, league);
  JsonDocument filter;
  espnEventFilter(filter["events"].add<JsonObject>());
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  for (JsonObjectConst e : doc["events"].as<JsonArrayConst>()) {
    JsonObjectConst c = e["competitions"][0];
    if (espnState(c) != GS_LIVE || !espnHasTeam(t, c)) continue;
    memset(&out, 0, sizeof(out));
    out.known = true;
    out.start = parseIso(e["date"] | "");
    strlcpy(out.liveLeague, league, sizeof(out.liveLeague));
    strlcpy(out.liveId, e["id"] | "", sizeof(out.liveId));
    espnApply(t, c, out);
    Serial.printf("[%s] booted mid-game, picked up live event %s\n", t.name, out.liveId);
    *found = true;
    return true;
  }
  return true;
}

// Next game from the team endpoint's nextEvent (~20 KB; the NFL schedule
// endpoint would be ~290 KB for the same answer).
static bool fetchEspnSchedule(int i, Game& out) {
  const TeamDef& t = TEAMS[i];
  char url[160];
  snprintf(url, sizeof(url), ESPN_TEAM_URL, t.feed);

  JsonDocument filter;
  espnEventFilter(filter["team"]["nextEvent"].add<JsonObject>());
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;

  // Earliest listed game that isn't over.
  time_t now = time(nullptr);
  JsonObjectConst best;
  time_t bestStart = 0;
  for (JsonObjectConst e : doc["team"]["nextEvent"].as<JsonArrayConst>()) {
    GameState st = espnState(e["competitions"][0]);
    time_t start = parseIso(e["date"] | "");
    if (st == GS_FINAL || !start) continue;
    if (st == GS_PRE && fixtureOver(teamState[i], start, now)) continue;
    if (!bestStart || start < bestStart) { best = e; bestStart = start; }
  }

  char league[sizeof(out.liveLeague)];
  espnLeague(t.feed, bestStart ? (best["league"]["slug"] | "") : "", league, sizeof(league));

  // After boot: is a game of ours already live? Needs a concrete league
  // ("soccer/all/scoreboard" is ~1 MB); retried until the check actually ran.
  size_t ll = strlen(league);
  bool concrete = ll < 4 || strcmp(league + ll - 4, "/all") != 0;
  if (!teamState[i].liveChecked) {
    bool found = false;
    if (!concrete || espnFindLiveInLeague(t, league, out, &found))
      teamState[i].liveChecked = true;
    if (found) return true;
  }

  memset(&out, 0, sizeof(out));
  if (!bestStart) return true;  // good fetch, just nothing scheduled
  out.known = true;
  out.start = bestStart;
  strlcpy(out.liveLeague, league, sizeof(out.liveLeague));
  strlcpy(out.liveId, best["id"] | "", sizeof(out.liveId));
  espnApply(t, best["competitions"][0], out);
  return true;
}

// Live poll: the single-event scoreboard (~10 KB) — score, clock, state.
static bool fetchEspnLive(const TeamDef& t, Game& g) {
  char url[160];
  snprintf(url, sizeof(url), ESPN_EVENT_URL, g.liveLeague, g.liveId);
  JsonDocument filter;
  espnCompetitionFilter(filter["competitions"].add<JsonObject>());
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  JsonObjectConst c = doc["competitions"][0];
  if (c.isNull()) return false;
  espnApply(t, c, g);
  return true;
}

// =============================================================================
//  Hockey: TheSportsDB (fixtures) + hockeyallsvenskan.se (live)
// =============================================================================

// TheSportsDB name -> HockeyAllsvenskan slug code. HA's game slugs are
// "<local yyyymmdd>-<home>-<away>" with these codes, e.g. "20261111-lif-aik".
// The league changes a little every season (promotion/relegation): a team
// missing here still shows its fixtures, just without a live score.
static const struct { const char* tsdb; const char* code; } HA_TEAMS[] = {
    {"AIK IF", "aik"},          {"Almtuna IS", "ais"},       {"BIK Karlskoga", "bik"},
    {"IK Oskarshamn", "iko"},   {"Kalmar HC", "khc"},        {"Leksands IF", "lif"},
    {"Modo Hockey", "modo"},    {"Mora IK", "mora"},         {"Nybro Vikings", "nvif"},
    {"Sodertalje SK", "ssk"},   {"VIK Vasteras HK", "vik"},  {"Vimmerby", "vhc"},
    {"Visby-Roma HK", "vis"},   {"Ostersunds IK", "oik"},
};

static const char* haCode(const char* tsdbName) {
  char folded[40];
  utf8Fold(tsdbName, folded, sizeof(folded));
  for (const auto& h : HA_TEAMS)
    if (strcmp(folded, h.tsdb) == 0) return h.code;
  return nullptr;
}

// Fallback abbreviation when the team isn't in HA_TEAMS: an all-caps word
// ("BIK Karlskoga" -> BIK), else the first 3 letters of the first real word.
static void deriveShortName(const char* full, char* out, size_t outSize) {
  static const char* GENERIC[] = {"IF", "IK", "HC", "SK", "IS", "HK", "BK", "AIF"};
  char buf[40];
  utf8Fold(full, buf, sizeof(buf));
  const char* firstReal = nullptr;
  for (char* w = strtok(buf, " -"); w; w = strtok(nullptr, " -")) {
    bool generic = false;
    for (const char* g : GENERIC) generic |= strcmp(w, g) == 0;
    if (generic) continue;
    size_t n = strlen(w);
    bool caps = n >= 2 && n <= 4;
    for (size_t i = 0; i < n && caps; i++) caps = isupper((uint8_t)w[i]);
    if (caps) { strlcpy(out, w, outSize); return; }
    if (!firstReal) firstReal = w;
  }
  size_t o = 0;
  for (const char* p = firstReal ? firstReal : ""; *p && o < 3 && o < outSize - 1; p++)
    if (isalpha((uint8_t)*p)) out[o++] = toupper((uint8_t)*p);
  out[o] = '\0';
}

// HA period -> "P1".."P3" / "OT" / "SO". The feed's exact spelling isn't
// documented, so accept both numbers and labels.
static void haClock(const char* period, char* out, size_t n) {
  if (!period || !*period) { out[0] = '\0'; return; }
  int p = atoi(period);
  if (p >= 1 && p <= 3) snprintf(out, n, "P%d", p);
  // Shootout first: "Shootout" contains "oot", which a case-blind "OT" matches.
  else if (p == 5 || strcasestr(period, "shoot") || !strcasecmp(period, "SO")) strlcpy(out, "SO", n);
  else if (p == 4 || strcasestr(period, "over") || !strcasecmp(period, "OT")) strlcpy(out, "OT", n);
  else foldUpper(period, out, n > 4 ? 4 : n);
}

// Fetch the HA game for g.liveId and apply it. Returns false on a network
// error or an unknown slug.
static bool fetchHaGame(const TeamDef& t, Game& g) {
  char url[160];
  snprintf(url, sizeof(url), HA_GAME_URL, g.liveId);
  JsonDocument filter;
  JsonObject fd = filter["data"].add<JsonObject>();
  for (const char* k : {"scheduledDateTime", "endedDateTime", "currentPeriod", "homeScore",
                        "awayScore", "isCompleted", "decidedIn"})
    fd[k] = true;
  fd["homeTeam"]["shortName"] = true;
  fd["awayTeam"]["shortName"] = true;
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;
  JsonObjectConst d = doc["data"][0];
  if (d.isNull()) {
    Serial.printf("[%s] HA has no game %s\n", t.name, g.liveId);
    return false;
  }

  time_t now = time(nullptr);
  time_t sched = parseIso(d["scheduledDateTime"] | "");
  if (sched) g.start = sched;  // HA is the authority on face-off time
  char opp[12];
  strlcpy(opp, (g.home ? d["awayTeam"]["shortName"] : d["homeTeam"]["shortName"]) | "",
          sizeof(opp));
  if (opp[0]) foldUpper(opp, g.opp, sizeof(g.opp));
  char period[12];
  jsonStr(d["currentPeriod"], period, sizeof(period));
  jsonStr(d["homeScore"], g.homeScore, sizeof(g.homeScore));
  jsonStr(d["awayScore"], g.awayScore, sizeof(g.awayScore));

  if (d["isCompleted"] | false) {
    if (!g.finalSeen) {
      time_t ended = parseIso(d["endedDateTime"] | "");
      g.finalSeen = ended ? ended : now;
    }
    g.state = GS_FINAL;
    jsonStr(d["decidedIn"], g.decided, sizeof(g.decided));
    if (strcmp(g.decided, "OT") != 0 && strcmp(g.decided, "SO") != 0) g.decided[0] = '\0';
    g.clock[0] = '\0';
  } else if (sleepClockValid() && now >= g.start && (g.homeScore[0] || period[0])) {
    g.state = GS_LIVE;
    haClock(period, g.clock, sizeof(g.clock));
  } else {
    g.state = GS_PRE;
  }
  return true;
}

static bool fetchHaSchedule(int i, Game& out) {
  const TeamDef& t = TEAMS[i];
  char url[160];
  snprintf(url, sizeof(url), TSDB_NEXT_URL, t.feed);

  JsonDocument filter;
  JsonObject fe = filter["events"].add<JsonObject>();
  for (const char* k : {"strTimestamp", "idHomeTeam", "strHomeTeam", "strAwayTeam", "strStatus"})
    fe[k] = true;
  JsonDocument doc;
  if (!getJson(url, doc, filter)) return false;

  memset(&out, 0, sizeof(out));
  // Free tier returns the single next event ("events": null off-season).
  time_t now = time(nullptr);
  for (JsonObjectConst e : doc["events"].as<JsonArrayConst>()) {
    time_t start = parseIso(e["strTimestamp"] | "");
    if (!start || fixtureOver(teamState[i], start, now)) continue;
    static const char* DONE[] = {"FT", "AOT", "AP", "PST", "CANC"};
    const char* status = e["strStatus"] | "NS";
    bool done = false;
    for (const char* d : DONE) done |= strcmp(status, d) == 0;
    if (done) continue;

    const char* homeName = e["strHomeTeam"] | "?";
    const char* awayName = e["strAwayTeam"] | "?";
    out.known = true;
    out.start = start;
    out.home = strcmp(e["idHomeTeam"] | "", t.feed) == 0;
    const char* oppName = out.home ? awayName : homeName;
    const char* oppCode = haCode(oppName);
    if (oppCode) foldUpper(oppCode, out.opp, sizeof(out.opp));
    else deriveShortName(oppName, out.opp, sizeof(out.opp));

    // HA slug: local calendar date of the game + home/away codes.
    if (oppCode) {
      struct tm lt;
      localtime_r(&start, &lt);
      snprintf(out.liveId, sizeof(out.liveId), "%04d%02d%02d-%s-%s", lt.tm_year + 1900,
               lt.tm_mon + 1, lt.tm_mday, out.home ? t.id : oppCode,
               out.home ? oppCode : t.id);
      // Resolve it once per game: proper short name + exact face-off, and it
      // confirms the slug works before game time. A miss = no live score.
      // (Reuse keeps TheSportsDB's fresh start time; a same-day reschedule
      // still lands, and HA corrects it again once live polling starts.)
      const Game& prev = teamState[i].game;
      if (strcmp(prev.liveId, out.liveId) == 0) strlcpy(out.opp, prev.opp, sizeof(out.opp));
      else if (!fetchHaGame(t, out)) out.liveId[0] = '\0';
    }
    break;
  }
  return true;
}

// =============================================================================
//  Public
// =============================================================================

bool teamTracking(int i, time_t now) {
  const Game& g = teamState[i].game;
  if (!g.known || !g.liveId[0] || !sleepClockValid() || now < g.start - 60) return false;
  if (g.state == GS_FINAL) return now < g.finalSeen + FINAL_HOLD_S;
  if (g.state == GS_LIVE) return now < g.start + LIVE_MAX_S;
  return now < g.start + GAME_MAX_LENGTH_S;  // kick-off passed, feed not live yet
}

Phase teamPhase(int i, time_t now) {
  const Game& g = teamState[i].game;
  if (!g.known) return PH_NONE;
  switch (g.state) {
    case GS_FINAL: return now < g.finalSeen + FINAL_HOLD_S ? PH_FINAL : PH_STALE;
    case GS_LIVE: return PH_LIVE;
    default:
      if (now < g.start) return PH_COUNTDOWN;
      return now < g.start + GAME_MAX_LENGTH_S ? PH_LIVE : PH_STALE;
  }
}

static void logGame(const TeamDef& t, const Game& g) {
  if (!g.known) { Serial.printf("[%s] no upcoming game listed\n", t.name); return; }
  char when[24];
  struct tm lt;
  localtime_r(&g.start, &lt);
  strftime(when, sizeof(when), "%a %d/%m %H:%M", &lt);
  static const char* STATES[] = {"pre", "LIVE", "FINAL"};
  Serial.printf("[%s] %s-%s  %s  %s %s-%s %s %s  (live id %s%s%s)\n", t.name,
                homeAbbr(t, g), awayAbbr(t, g), when, STATES[g.state],
                g.homeScore, g.awayScore, g.clock, g.decided, g.liveLeague,
                g.liveLeague[0] ? "/" : "", g.liveId[0] ? g.liveId : "-");
}

bool teamFetch(int i) {
  const TeamDef& t = TEAMS[i];
  TeamState& s = teamState[i];
  time_t now = time(nullptr);
  Game g = s.game;
  bool ok;
  if (s.everFetched && teamTracking(i, now)) {
    // Game on (or just ended): poll the live feed, updating in place.
    ok = t.source == SRC_ESPN ? fetchEspnLive(t, g) : fetchHaGame(t, g);
  } else {
    ok = t.source == SRC_ESPN ? fetchEspnSchedule(i, g) : fetchHaSchedule(i, g);
  }

  s.tried = true;
  s.lastOk = ok;
  if (!ok) {
    Serial.printf("[%s] fetch failed\n", t.name);
    return false;
  }
  if (g.state == GS_FINAL) {
    if (!g.finalSeen) g.finalSeen = now;
    // Anything starting before this game ended is this game: a lagging
    // fixture feed must not bring it back once the FT hold is over.
    if (g.finalSeen > s.doneThrough) s.doneThrough = g.finalSeen;
  }
  s.game = g;
  s.everFetched = true;
  logGame(t, g);
  return true;
}
