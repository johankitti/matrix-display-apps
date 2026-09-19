#include "leaderboard.h"
#include "settings.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <sys/time.h>
#include <time.h>

// ---------------------------------------------------------------------------
// PSRAM allocator for ArduinoJson: the filtered document (~40-80 KB for a
// 150-player field) lands in external PSRAM, keeping internal SRAM free for
// the TLS buffers and the panel's DMA framebuffer.
// ---------------------------------------------------------------------------
struct SpiRamAllocator : ArduinoJson::Allocator {
  void* allocate(size_t size) override {
    if (psramFound()) return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    return malloc(size);
  }
  void deallocate(void* ptr) override { heap_caps_free(ptr); }
  void* reallocate(void* ptr, size_t newSize) override {
    if (psramFound()) return heap_caps_realloc(ptr, newSize, MALLOC_CAP_SPIRAM);
    return realloc(ptr, newSize);
  }
};

// ---------------------------------------------------------------------------
// Tours. ESPN serves the PGA Tour and the DP World Tour from the same endpoints
// with the same JSON shape; only the league slug in the URL differs. Every
// fetch below goes through `g_tour`, set by fetchTour() for the duration of one
// tour's fetch, so the parsing code never has to know which tour it's on.
// ---------------------------------------------------------------------------

struct TourInfo {
  const char* slug;     // ESPN league slug in the URLs
  const char* label;    // uppercase display name (fallback event name, web page)
  const char* bbcSlug;  // BBC Sport tournament slug for live in-round scoring,
                        // or nullptr to rely on ESPN alone (see fetchBbcBoard)
};
static const TourInfo TOURS[] = {
    {"pga", "PGA TOUR", nullptr},          // TOUR_PGA: ESPN is live + complete
    {"eur", "DP WORLD TOUR", "european-tour"},  // TOUR_EUR: ESPN lags -> BBC
};
static const TourInfo* g_tour = &TOURS[TOUR_PGA];

const char* tourLabel(uint8_t tour) {
  if (tour == TOUR_AUTO) return "AUTO";
  return tour < TOUR_AUTO ? TOURS[tour].label : "?";
}

// ESPN scoreboard URL for the current tour, with an optional query string
// (e.g. "?dates=20260917") appended.
static void scoreboardUrl(char* buf, size_t n, const char* query = "") {
  int len = snprintf(buf, n, ESPN_SCOREBOARD_URL_FMT, g_tour->slug);
  if (len > 0 && (size_t)len < n) strlcpy(buf + len, query, n - len);
}

static void headerUrl(char* buf, size_t n) {
  snprintf(buf, n, ESPN_HEADER_URL_FMT, g_tour->slug);
}

// ---------------------------------------------------------------------------
// Text helpers: the LED font is ASCII-only, so "Ludvig Åberg" must become
// "ABERG" before it can be drawn.
// ---------------------------------------------------------------------------

// Latin Extended-A (U+0100-U+017F: Š, Ł, Ř, Ő, Ć, Ž ...) -> base letter.
// Common in DP World Tour fields (Czech, Polish, Hungarian, Croatian names).
// Uppercase only: every consumer uppercases or case-folds the result anyway.
static char foldLatinExtA(uint16_t cp) {
  static const struct { uint16_t lo, hi; char c; } R[] = {
      {0x100, 0x105, 'A'}, {0x106, 0x10D, 'C'}, {0x10E, 0x111, 'D'},
      {0x112, 0x11B, 'E'}, {0x11C, 0x123, 'G'}, {0x124, 0x127, 'H'},
      {0x128, 0x133, 'I'}, {0x134, 0x135, 'J'}, {0x136, 0x138, 'K'},
      {0x139, 0x142, 'L'}, {0x143, 0x14B, 'N'}, {0x14C, 0x153, 'O'},
      {0x154, 0x159, 'R'}, {0x15A, 0x161, 'S'}, {0x162, 0x167, 'T'},
      {0x168, 0x173, 'U'}, {0x174, 0x175, 'W'}, {0x176, 0x178, 'Y'},
      {0x179, 0x17E, 'Z'},
  };
  for (const auto& r : R)
    if (cp >= r.lo && cp <= r.hi) return r.c;
  return 0;
}

// Folds UTF-8 accents to plain ASCII (Å->A, é->e, ø->o, ß->s, Š->S ...):
// Latin-1 Supplement plus Latin Extended-A. Anything else is dropped.
static void asciiFold(const char* src, char* dst, size_t dstSize) {
  size_t o = 0;
  const uint8_t* s = (const uint8_t*)src;
  while (*s && o < dstSize - 1) {
    uint8_t b = *s;
    if (b < 0x80) {
      dst[o++] = (char)b;
      s++;
    } else if ((b == 0xC4 || b == 0xC5) && s[1]) {
      char r = foldLatinExtA((uint16_t)((b & 0x1F) << 6) | (s[1] & 0x3F));
      if (r) dst[o++] = r;
      s += 2;
    } else if (b == 0xC3 && s[1]) {
      uint8_t c = s[1];
      char r = 0;
      if (c >= 0x80 && c <= 0x86)      r = 'A';
      else if (c == 0x87)              r = 'C';
      else if (c >= 0x88 && c <= 0x8B) r = 'E';
      else if (c >= 0x8C && c <= 0x8F) r = 'I';
      else if (c == 0x90)              r = 'D';
      else if (c == 0x91)              r = 'N';
      else if ((c >= 0x92 && c <= 0x96) || c == 0x98) r = 'O';
      else if (c >= 0x99 && c <= 0x9C) r = 'U';
      else if (c == 0x9D)              r = 'Y';
      else if (c == 0x9F)              r = 's';
      else if (c >= 0xA0 && c <= 0xA6) r = 'a';
      else if (c == 0xA7)              r = 'c';
      else if (c >= 0xA8 && c <= 0xAB) r = 'e';
      else if (c >= 0xAC && c <= 0xAF) r = 'i';
      else if (c == 0xB0)              r = 'd';
      else if (c == 0xB1)              r = 'n';
      else if ((c >= 0xB2 && c <= 0xB6) || c == 0xB8) r = 'o';
      else if (c >= 0xB9 && c <= 0xBC) r = 'u';
      else if (c == 0xBD || c == 0xBF) r = 'y';
      if (r) dst[o++] = r;
      s += 2;
    } else {
      s++;
      while ((*s & 0xC0) == 0x80) s++;  // skip continuation bytes
    }
  }
  dst[o] = 0;
}

static void toUpperInPlace(char* s) {
  for (; *s; s++) *s = toupper((unsigned char)*s);
}

// "Ludvig Åberg" -> "ABERG": last name only, folded and uppercased.
static void surnameOf(const char* fullName, char* dst, size_t dstSize) {
  char folded[32];
  asciiFold(fullName, folded, sizeof(folded));
  const char* last = strrchr(folded, ' ');
  strlcpy(dst, last ? last + 1 : folded, dstSize);
  toUpperInPlace(dst);
}

// Matching key for a name: ASCII-folded, uppercased, and with the digraphs the
// BBC feed uses for Nordic/German letters collapsed (OE->O, AE->A, AA->A,
// UE->U). ESPN writes "Ludvig Åberg" / "Rasmus Højgaard"; BBC writes "Ludvig
// Aaberg" / "Rasmus Hoejgaard"; both key to "LUDVIG ABERG" / "RASMUS HOJGARD",
// as does a config pattern typed either way. Matching only — never displayed.
static void nameKey(const char* name, char* dst, size_t dstSize) {
  char folded[48];
  asciiFold(name, folded, sizeof(folded));
  toUpperInPlace(folded);
  size_t o = 0;
  for (const char* p = folded; *p && o < dstSize - 1;) {
    bool digraph = (p[0] == 'O' && p[1] == 'E') || (p[0] == 'U' && p[1] == 'E') ||
                   (p[0] == 'A' && (p[1] == 'E' || p[1] == 'A'));
    dst[o++] = *p;
    p += digraph ? 2 : 1;
  }
  dst[o] = 0;
}

// Substring match of one config pattern against a full name, both reduced to
// nameKey form: "aberg" matches "Ludvig Åberg" and "Ludvig Aaberg" alike.
static bool nameMatchesPattern(const char* fullName, const char* pattern) {
  char key[48], pat[32];
  nameKey(fullName, key, sizeof(key));
  nameKey(pattern, pat, sizeof(pat));
  return strstr(key, pat) != nullptr;
}

// Same player under either feed's spelling (see nameKey).
static bool sameNameKey(const char* a, const char* b) {
  char ka[48], kb[48];
  nameKey(a, ka, sizeof(ka));
  nameKey(b, kb, sizeof(kb));
  return strcmp(ka, kb) == 0;
}

static bool matchesAnyPinned(const char* fullName) {
  for (uint8_t i = 0; i < settings.pinnedCount; i++) {
    if (nameMatchesPattern(fullName, settings.pinned[i])) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Date helpers. "Now" comes from the HTTP Date header of each response;
// it also sets the system clock, which the night schedule relies on.
//
// All parsing converts to epoch WITHOUT mktime(): mktime interprets struct
// tm in the device's local timezone, and main.cpp sets TZ to Sweden for the
// night schedule — these timestamps are UTC and must stay that way.
// ---------------------------------------------------------------------------

// Civil date -> UTC epoch (Howard Hinnant's days-from-civil algorithm).
static time_t utcEpoch(int y, int m, int d, int hh, int mm, int ss) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153u * (unsigned)(m + (m > 2 ? -3 : 9)) + 2u) / 5u + (unsigned)d - 1u;
  unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
  long days = (long)era * 146097L + (long)doe - 719468L;
  return (time_t)days * 86400 + hh * 3600 + mm * 60 + ss;
}

static const char* const MONTHS_UP[12] = {"JAN", "FEB", "MAR", "APR",
                                          "MAY", "JUN", "JUL", "AUG",
                                          "SEP", "OCT", "NOV", "DEC"};

static int monthIndex(const char* mon3) {
  static const char* names = "janfebmaraprmayjunjulaugsepoctnovdec";
  for (int i = 0; i < 12; i++) {
    if (strncasecmp(mon3, names + i * 3, 3) == 0) return i;
  }
  return -1;
}

// "Sun, 10 Aug 2026 15:49:37 GMT" -> UTC epoch, 0 on failure.
static time_t parseHttpDate(const char* s) {
  char mon[4] = {0};
  int d, y, hh, mm, ss;
  if (sscanf(s, "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) != 6)
    return 0;
  int m = monthIndex(mon);
  if (m < 0) return 0;
  return utcEpoch(y, m + 1, d, hh, mm, ss);
}

// "2026-08-13T07:00Z" -> UTC epoch at ~noon that day (day resolution is all
// the calendar logic needs), 0 on failure.
static time_t parseIsoDate(const char* s) {
  int y, m, d;
  if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return 0;
  return utcEpoch(y, m, d, 12, 0, 0);
}

// "2026-08-27T15:00Z" / "2026-08-27T15:00:00Z" -> UTC epoch, keeping the time
// of day (the header feed's tee times are always UTC 'Z'). 0 on failure.
static time_t parseIsoDateTime(const char* s) {
  int y, m, d, hh, mm, ss = 0;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) < 5)
    return 0;
  return utcEpoch(y, m, d, hh, mm, ss);
}

// UTC epoch -> board-local "HH:MM" (TZ is set in main.cpp).
static void epochToLocalHM(time_t utc, char* out, size_t outSize) {
  struct tm lt;
  localtime_r(&utc, &lt);
  snprintf(out, outSize, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

// ("2026-08-13...", "2026-08-16...") -> "AUG 13-16"; across a month
// boundary -> "AUG 30-SEP 2".
static void formatDateRange(const char* startIso, const char* endIso,
                            char* dst, size_t dstSize) {
  int y1, m1, d1, y2, m2, d2;
  if (sscanf(startIso, "%d-%d-%d", &y1, &m1, &d1) != 3) {
    strlcpy(dst, "", dstSize);
    return;
  }
  if (sscanf(endIso, "%d-%d-%d", &y2, &m2, &d2) != 3) {
    snprintf(dst, dstSize, "%s %d", MONTHS_UP[m1 - 1], d1);
    return;
  }
  if (m1 == m2) {
    snprintf(dst, dstSize, "%s %d-%d", MONTHS_UP[m1 - 1], d1, d2);
  } else {
    snprintf(dst, dstSize, "%s %d-%s %d", MONTHS_UP[m1 - 1], d1,
             MONTHS_UP[m2 - 1], d2);
  }
}

// Tee times on the scoreboard feed arrive as a Java Date.toString() in the
// tournament's US timezone, e.g. "Thu Aug 20 14:55:00 PDT 2026". Look up the
// zone's UTC offset so the time can be re-expressed in the board's local zone.
static bool zoneOffsetSeconds(const char* z, long* off) {
  static const struct { const char* abbr; long off; } ZONES[] = {
      {"UTC", 0},         {"GMT", 0},
      {"EST", -5 * 3600}, {"EDT", -4 * 3600},
      {"CST", -6 * 3600}, {"CDT", -5 * 3600},
      {"MST", -7 * 3600}, {"MDT", -6 * 3600},
      {"PST", -8 * 3600}, {"PDT", -7 * 3600},
  };
  for (const auto& e : ZONES) {
    if (strcmp(z, e.abbr) == 0) { *off = e.off; return true; }
  }
  return false;  // unknown zone (e.g. an overseas event) -> caller falls back
}

// "Thu Aug 20 14:55:00 PDT 2026" -> local "HH:MM" (TZ is set in main.cpp).
// Returns false if the format or timezone can't be parsed.
static bool parseTeeTime(const char* s, char* out, size_t outSize) {
  if (!s || !*s) return false;
  char mon[4] = {0}, zone[6] = {0};
  int d, hh, mm, ss, y;
  if (sscanf(s, "%*s %3s %d %d:%d:%d %5s %d", mon, &d, &hh, &mm, &ss, zone,
             &y) != 7)
    return false;
  int m = monthIndex(mon);
  long off;
  if (m < 0 || !zoneOffsetSeconds(zone, &off)) return false;
  time_t utc = utcEpoch(y, m + 1, d, hh, mm, ss) - off;  // local -> UTC
  struct tm lt;
  localtime_r(&utc, &lt);  // UTC -> board-local wall clock
  snprintf(out, outSize, "%02d:%02d", lt.tm_hour, lt.tm_min);
  return true;
}

// ---------------------------------------------------------------------------
// HTTP: fetch a URL and filter-parse it. Optionally reports the server's
// clock (from the Date header) via nowUtc.
// ---------------------------------------------------------------------------

static bool getJson(const char* url, JsonDocument& doc,
                    const JsonDocument& filter, time_t* nowUtc) {
  WiFiClientSecure client;
  client.setInsecure();  // hobby display: skip cert validation so the board
                         // keeps working when ESPN rotates certificates

  HTTPClient http;
  // HTTP/1.0 disables chunked transfer encoding — required so ArduinoJson
  // can parse the response stream directly.
  http.useHTTP10(true);
  http.setTimeout(20000);
  http.setConnectTimeout(10000);
  if (!http.begin(client, url)) return false;

  static const char* headerKeys[] = {"Date"};
  if (nowUtc) http.collectHeaders(headerKeys, 1);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[espn] HTTP %d\n", code);
    http.end();
    return false;
  }
  if (nowUtc) {
    *nowUtc = parseHttpDate(http.header("Date").c_str());
    // Keep the system clock in sync — the night schedule depends on it,
    // and it survives deep sleep on the ESP32's internal RTC.
    if (*nowUtc > 1700000000) {
      struct timeval tv = {.tv_sec = *nowUtc, .tv_usec = 0};
      settimeofday(&tv, nullptr);
    }
  }

  // Buffer the whole (~0.5 MB) body into PSRAM, then parse from RAM. Parsing
  // straight off the TLS stream is unreliable at this size: a slow filtered
  // parse lets the connection drop mid-body (JSON error: IncompleteInput). A
  // tight read loop can't be outrun by the parser.
  WiFiClient* stream = http.getStreamPtr();
  size_t cap = 128 * 1024, len = 0;
  char* body = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
  if (!body) { http.end(); return false; }
  uint32_t lastRx = millis();
  while (http.connected() || stream->available()) {
    size_t avail = stream->available();
    if (avail) {
      if (len + avail > cap) {
        while (len + avail > cap) cap += 128 * 1024;
        char* nb = (char*)heap_caps_realloc(body, cap, MALLOC_CAP_SPIRAM);
        if (!nb) { heap_caps_free(body); http.end(); return false; }
        body = nb;
      }
      len += stream->readBytes(body + len, avail);
      lastRx = millis();
    } else if (millis() - lastRx > 20000) {
      break;  // stalled: give up, retry later
    } else {
      delay(2);
    }
  }
  http.end();

  // ESPN nests deeper than ArduinoJson's default limit of 10 (events >
  // competitions > competitors > ...); raise the ceiling (costs stack only for
  // the actual depth, ~15).
  DeserializationError err =
      deserializeJson(doc, body, len, DeserializationOption::Filter(filter),
                      DeserializationOption::NestingLimit(30));
  heap_caps_free(body);
  if (err) {
    Serial.printf("[espn] JSON error: %s\n", err.c_str());
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// MODE_LIVE: leaderboard rows
// ---------------------------------------------------------------------------

// This feed carries no competitor.status/position, so leaderboard positions
// (with ties: T1, T1, T3 ...) are derived from total score to par.
static const int SCORE_NONE = 100000;  // "-"/no score: ranks last, ties nobody

static int scoreStrToPar(const char* s) {
  if (!s || !*s || strcmp(s, "-") == 0) return SCORE_NONE;
  if (strcmp(s, "E") == 0) return 0;
  return atoi(s);  // handles "+2", "-14"
}

static int scoreToPar(JsonVariantConst sv) {
  if (sv.is<const char*>()) return scoreStrToPar(sv.as<const char*>());
  if (sv.isNull()) return SCORE_NONE;
  return sv.as<int>();
}

// Position = 1 + (players strictly better), "T"-prefixed when the score is
// shared. `scores` is every competitor's parsed total, in leaderboard order.
static void assignPosition(GolferRow& r, const int* scores, int n) {
  int mine = scoreStrToPar(r.score);
  if (mine == SCORE_NONE) return;  // keep fillRow's sort-order fallback
  int better = 0, equal = 0;
  for (int i = 0; i < n; i++) {
    if (scores[i] < mine) better++;
    else if (scores[i] == mine) equal++;
  }
  if (equal > 1) snprintf(r.pos, sizeof(r.pos), "T%d", better + 1);
  else snprintf(r.pos, sizeof(r.pos), "%d", better + 1);
}

// True if the competitor has a linescore entry for `period`. Players still in
// the field get one (even a bare placeholder) as soon as that round is on the
// schedule; a cut/withdrawn player has none for rounds they won't play.
static bool hasLinescoreForPeriod(JsonObjectConst c, int period) {
  for (JsonObjectConst ls : c["linescores"].as<JsonArrayConst>())
    if ((int)(ls["period"] | 0) == period) return true;
  return false;
}

// Detects a player who is out of the tournament and, if so, writes a short
// badge ("MC"/"WD"/"DQ") for the position column.
//
// Tier 1 — ESPN's explicit label. Only present while a round is actively in
// progress (competitor.status is null between rounds and on Final), but it's
// authoritative when it's there.
//
// Tier 2 — a structural fallback for the null-status feed. `round` is the round
// the board is showing. Once the cut has been applied (round 3+), a player with
// no linescore for that round isn't in it -> cut (or, less commonly, withdrawn;
// Tier 1 catches an explicitly-labelled WD). Deliberately not attempted before
// round 3, where a made-cut player who simply hasn't teed off looks identical.
static bool classifyOut(JsonObjectConst c, int round, char* badge, size_t n) {
  const char* posDisp = c["status"]["position"]["displayName"] | "";
  const char* typeName = c["status"]["type"]["name"] | "";
  if (strstr(typeName, "WITHDRAW") || strcasecmp(posDisp, "WD") == 0) {
    strlcpy(badge, "WD", n);
    return true;
  }
  if (strstr(typeName, "DISQUALIF") || strcasecmp(posDisp, "DQ") == 0) {
    strlcpy(badge, "DQ", n);
    return true;
  }
  if (strstr(typeName, "CUT") || strcasecmp(posDisp, "CUT") == 0 ||
      strcasecmp(posDisp, "MC") == 0) {
    strlcpy(badge, "MC", n);
    return true;
  }
  if (round >= 3 && !hasLinescoreForPeriod(c, round)) {
    strlcpy(badge, "MC", n);  // structural: past the cut, not in the field
    return true;
  }
  return false;
}

// Case- and accent-insensitive full-name match, for lining a scoreboard athlete
// up with the header feed's tee-time list (both carry ESPN display names).
static bool sameDisplayName(const char* a, const char* b) {
  char fa[48], fb[48];
  asciiFold(a, fa, sizeof(fa));
  asciiFold(b, fb, sizeof(fb));
  return strcasecmp(fa, fb) == 0;
}

// The scoreboard omits tee times until a round is imminent; the header feed
// (passed to fillLive as `teeSource`) carries them days ahead. Writes the local
// "HH:MM" for `fullName` into `out`, or leaves `out` untouched if not found.
static void teeTimeFromHeader(JsonArrayConst teeSource, const char* fullName,
                              char* out, size_t outSize) {
  for (JsonObjectConst c : teeSource) {
    if (sameDisplayName(c["displayName"] | "", fullName)) {
      time_t tee = parseIsoDateTime(c["status"]["teeTime"] | "");
      if (tee > 0) epochToLocalHM(tee, out, outSize);
      return;
    }
  }
}

static void fillRow(GolferRow& row, JsonObjectConst c, const char* compState,
                    int period, JsonArrayConst teeSource = JsonArrayConst()) {
  surnameOf(c["athlete"]["displayName"] | "?", row.name, sizeof(row.name));

  // Total score to par: ESPN sends a display string ("-14", "E") but be
  // defensive about it arriving as a number.
  JsonVariantConst sv = c["score"];
  if (sv.is<const char*>()) {
    strlcpy(row.score, sv.as<const char*>(), sizeof(row.score));
  } else if (!sv.isNull()) {
    int v = sv.as<int>();
    if (v == 0) strlcpy(row.score, "E", sizeof(row.score));
    else snprintf(row.score, sizeof(row.score), "%+d", v);
  } else {
    strlcpy(row.score, "-", sizeof(row.score));
  }

  // Out of the tournament (cut/withdrawn/DQ): show name + total with a badge in
  // the rank column and no per-round detail (no tee time for someone who's out).
  if (classifyOut(c, period, row.pos, sizeof(row.pos))) {
    row.out = true;
    strlcpy(row.today, "-", sizeof(row.today));
    strlcpy(row.thru, "-", sizeof(row.thru));
    row.tee[0] = 0;
    return;
  }

  // Position: live events carry "T5"-style strings; finished events don't,
  // so fall back to the competitor's sort order.
  const char* posDisp = c["status"]["position"]["displayName"];
  if (posDisp && *posDisp) {
    strlcpy(row.pos, posDisp, sizeof(row.pos));
  } else {
    snprintf(row.pos, sizeof(row.pos), "%d", (int)(c["order"] | 0));
  }

  // This round's score to par + holes played come from the current round's
  // linescore. competitor.status is null on this feed, so the per-hole array
  // length is the reliable "thru".
  strlcpy(row.today, "-", sizeof(row.today));
  strlcpy(row.thru, "-", sizeof(row.thru));
  row.tee[0] = 0;
  for (JsonObjectConst ls : c["linescores"].as<JsonArrayConst>()) {
    if ((int)(ls["period"] | 0) != period) continue;
    const char* tv = ls["displayValue"];
    if (tv && *tv) strlcpy(row.today, tv, sizeof(row.today));
    int holes = ls["linescores"].as<JsonArrayConst>().size();
    // The DP World Tour feed carries no per-hole array at all; fall back to
    // the competitor's status.thru (populated while a round is in progress).
    if (holes == 0) holes = c["status"]["thru"] | 0;
    if (strcmp(compState, "post") == 0 || holes >= 18) {
      strlcpy(row.thru, "F", sizeof(row.thru));
    } else if (holes > 0) {
      snprintf(row.thru, sizeof(row.thru), "%d", holes);
    } else {
      // Yet to tee off today: surface the round's tee time (a positional,
      // unlabelled stat, so accept the first stat that parses as one).
      for (JsonObjectConst st :
           ls["statistics"]["categories"][0]["stats"].as<JsonArrayConst>()) {
        if (parseTeeTime(st["displayValue"] | "", row.tee, sizeof(row.tee)))
          break;
      }
    }
    break;
  }

  // Before a round is imminent the scoreboard carries no tee time (the loop
  // above found nothing); fall back to the header feed's draw when one was
  // supplied — this is what fills the within-24h pre-tournament leaderboard.
  if (row.tee[0] == 0 && !teeSource.isNull())
    teeTimeFromHeader(teeSource, c["athlete"]["displayName"] | "", row.tee,
                      sizeof(row.tee));
}

// Between rounds ESPN publishes the next round's tee times into that round's
// linescore statistics only once the draw is set (a few hours after the prior
// round ends); until then the round is a bare {"period":N} placeholder. Returns
// true as soon as any competitor carries a parseable tee time for `period`, so
// the board only switches to the tee-time view when there's something to show.
static bool roundHasTeeTimes(JsonArrayConst competitors, int period) {
  char buf[6];
  for (JsonObjectConst c : competitors) {
    for (JsonObjectConst ls : c["linescores"].as<JsonArrayConst>()) {
      if ((int)(ls["period"] | 0) != period) continue;
      for (JsonObjectConst st :
           ls["statistics"]["categories"][0]["stats"].as<JsonArrayConst>()) {
        if (parseTeeTime(st["displayValue"] | "", buf, sizeof(buf))) return true;
      }
      break;
    }
  }
  return false;
}

static void fillLive(Leaderboard& lb, JsonObjectConst event,
                     JsonArrayConst teeSource = JsonArrayConst()) {
  lb.mode = MODE_LIVE;

  const char* rawName =
      event["shortName"] | (const char*)(event["name"] | g_tour->label);
  asciiFold(rawName, lb.eventName, sizeof(lb.eventName));
  toUpperInPlace(lb.eventName);

  JsonObjectConst comp = event["competitions"][0];
  const char* compState = comp["status"]["type"]["state"] | "in";
  const char* statusName = comp["status"]["type"]["name"] | "";
  int period = comp["status"]["period"] | 1;
  if (period < 1) period = 1;

  // Between rounds the current round is complete (state "post") but the event
  // isn't Final. Once the next round's draw is published, switch to it: the
  // leaderboard totals are unchanged, but each row surfaces that player's next
  // tee time instead of a flat "F". Until the tee times are out, keep showing
  // the completed round's results (score + "F") rather than blank columns.
  // Cap at 4 rounds; a fully-live round always shows itself.
  int displayPeriod = period;
  const char* rowState = compState;
  bool betweenRounds = strcmp(compState, "post") == 0 &&
                       strcmp(statusName, "STATUS_FINAL") != 0;
  // Advance to the next round's tee times when its draw is known — either from
  // the scoreboard (roundHasTeeTimes) or, before the scoreboard publishes them,
  // from the header feed handed in as `teeSource` (see fetchLeaderboard).
  if (betweenRounds && period < 4 &&
      (!teeSource.isNull() ||
       roundHasTeeTimes(comp["competitors"].as<JsonArrayConst>(), period + 1))) {
    displayPeriod = period + 1;
    rowState = "pre";  // upcoming round: nobody's teed off -> tee times, not "F"
  }
  period = displayPeriod;
  // Tournament over: the whole event is Final (not just a round complete). Show
  // "F" in the header slot instead of "R4" so the board reads as a settled
  // result, not a round still to be played.
  if (strcmp(statusName, "STATUS_FINAL") == 0) {
    strlcpy(lb.roundLabel, "F", sizeof(lb.roundLabel));
  } else {
    snprintf(lb.roundLabel, sizeof(lb.roundLabel), "R%d", period);
  }

  // In round 1 a player who hasn't teed off has no score at all, so ESPN's
  // placeholder rank ("T1") and total ("E") are noise -> flag the board as R1 so
  // drawRow drops those two columns for any row that still carries a tee time
  // (mixed board: started players show full stats, the rest show name + tee).
  // Guarded to R1: from R2 on, a yet-to-tee player already has a real prior-round
  // total that must keep showing (the between-rounds "pre" state is period+1).
  lb.firstRound = period == 1;

  // Competitors arrive sorted by leaderboard order. The board holds BOARD_ROWS
  // golfers: the top of the leaderboard, plus any tracked golfers who sit
  // outside it pinned to the bottom rows (so a pick is always visible). A pick
  // already inside the leaders is shown there, highlighted, not duplicated.
  //
  // The leader block grows to fill whatever rows the pinned picks don't use, so
  // the board is always full. That count is circular (how many leaders fit
  // depends on how many picks are pinned, which depends on where the leader
  // block ends), so settle it with a short fixed-point pass: with L leaders,
  // a pick is "pinned" only if it ranks at index >= L.
  JsonArrayConst competitors = comp["competitors"].as<JsonArrayConst>();

  // Parse the whole field's totals once, for tie-aware positions (see below),
  // and note which tracked golfers are in this field (for TOUR_AUTO).
  static int scores[168];  // >= the largest field either tour plays (156)
  int nScores = 0;
  for (JsonObjectConst c : competitors) {
    const char* fullName = c["athlete"]["displayName"] | "";
    for (uint8_t i = 0; i < settings.pinnedCount && i < MAX_PINNED_GOLFERS; i++)
      if (nameMatchesPattern(fullName, settings.pinned[i])) lb.pickInField[i] = true;
    if (nScores >= (int)(sizeof(scores) / sizeof(scores[0]))) break;
    scores[nScores++] = scoreToPar(c["score"]);
  }

  uint8_t leaderSlots = BOARD_ROWS, pinnedSlots = 0;
  for (uint8_t iter = 0; iter <= MAX_PINNED_ROWS; iter++) {
    uint8_t deep = 0;
    int idx = 0;
    for (JsonObjectConst c : competitors) {
      if (idx >= leaderSlots &&
          matchesAnyPinned(c["athlete"]["displayName"] | "")) {
        if (deep < MAX_PINNED_ROWS) deep++;
      }
      idx++;
    }
    if (deep == pinnedSlots) break;
    pinnedSlots = deep;
    leaderSlots = BOARD_ROWS - deep;
  }

  for (JsonObjectConst c : competitors) {
    bool isPinned = matchesAnyPinned(c["athlete"]["displayName"] | "");
    if (lb.leaderCount < leaderSlots) {
      GolferRow& r = lb.leaders[lb.leaderCount++];
      fillRow(r, c, rowState, period, teeSource);
      if (!r.out) assignPosition(r, scores, nScores);
      r.selected = isPinned;  // a pick among the top rows stays highlighted
    } else if (isPinned && lb.pinnedCount < pinnedSlots) {
      GolferRow& r = lb.pinned[lb.pinnedCount++];
      fillRow(r, c, rowState, period, teeSource);
      if (!r.out) assignPosition(r, scores, nScores);
      r.selected = true;
    }
    if (lb.leaderCount >= leaderSlots && lb.pinnedCount >= pinnedSlots) break;
  }
}

// ---------------------------------------------------------------------------
// MODE_NEXT: upcoming tournament + per-golfer field status.
//
// ESPN has no public "athlete schedule" API, so "which event does golfer X
// play next" is answered as: is X in the field of the tour's next event?
// The field is usually published Mon/Tue of tournament week — until then
// every pinned golfer shows TBD.
// ---------------------------------------------------------------------------

// The scoreboard endpoint only carries tee times close to the round, but the
// keyless "header" feed publishes the upcoming event's draw days ahead. Fetch
// it to (1) seed the first-tee countdown with the earliest real tee time and
// (2) fill each pinned golfer's tee time for the within-24h start list. Applied
// only when the header's event is the same tournament we're showing (matched by
// start day), so tee times never land on the wrong event. Best-effort: on any
// failure nextStart stays 0 (no countdown) and tees stay blank.
// Fetches the header feed (which carries per-player tee times days ahead) into
// `doc`, but only when its event's start day matches `matchDateIso` (a
// "YYYY-MM-DD..." string) — so tee times never come from the wrong tournament.
// One retry: this host's TLS handshake fails intermittently, as the scoreboard
// host's does. Returns true with `doc` holding the header event; false (and
// `doc` unusable) on any network error, date mismatch, or a stale draw: the
// header feed has been seen relabelling the just-played round's tee times as
// the next round's, so a draw whose latest tee time is already behind the
// server clock is rejected (every caller wants times still to come). `doc` must
// have been constructed with the caller's allocator, since its parsed body
// outlives this.
static bool fetchHeaderEvent(JsonDocument& doc, const char* matchDateIso) {
  int wy, wm, wd;
  if (sscanf(matchDateIso, "%d-%d-%d", &wy, &wm, &wd) != 3) return false;

  JsonDocument filter;
  JsonObject fEv = filter["sports"].add<JsonObject>()["leagues"]
                       .add<JsonObject>()["events"].add<JsonObject>();
  fEv["date"] = true;
  JsonObject fc = fEv["competitors"].add<JsonObject>();
  fc["displayName"] = true;
  fc["status"]["teeTime"] = true;

  char url[160];
  headerUrl(url, sizeof(url));
  bool ok = false;
  time_t now = 0;
  for (int attempt = 0; attempt < 2 && !ok; attempt++)
    ok = getJson(url, doc, filter, &now);
  if (!ok) return false;

  JsonObjectConst ev = doc["sports"][0]["leagues"][0]["events"][0];
  int ey, em, ed;
  if (sscanf(ev["date"] | "", "%d-%d-%d", &ey, &em, &ed) != 3) return false;
  if (!(ey == wy && em == wm && ed == wd)) return false;  // another event

  time_t latest = 0;
  for (JsonObjectConst c : ev["competitors"].as<JsonArrayConst>()) {
    time_t tee = parseIsoDateTime(c["status"]["teeTime"] | "");
    if (tee > latest) latest = tee;
  }
  if (latest > 0 && now > 100000 && latest < now - 3600) {
    Serial.println("[espn] header draw is stale (all tee times past), ignoring");
    return false;
  }
  return true;
}

static void fillNextTeeTimes(Leaderboard& lb, const char* startIso,
                             ArduinoJson::Allocator* allocator) {
  JsonDocument doc(allocator);
  if (!fetchHeaderEvent(doc, startIso)) return;  // countdown off, tees blank

  JsonObjectConst ev = doc["sports"][0]["leagues"][0]["events"][0];
  time_t earliest = 0;
  for (JsonObjectConst c : ev["competitors"].as<JsonArrayConst>()) {
    time_t tee = parseIsoDateTime(c["status"]["teeTime"] | "");
    if (tee == 0) continue;
    if (earliest == 0 || tee < earliest) earliest = tee;
    const char* fullName = c["displayName"] | "";
    for (uint8_t i = 0; i < lb.nextGolferCount; i++) {
      if (nameMatchesPattern(fullName, settings.pinned[i]))
        epochToLocalHM(tee, lb.nextGolfers[i].tee, sizeof(lb.nextGolfers[i].tee));
    }
  }
  lb.nextStart = earliest;  // 0 until the draw is published
}

static void fillNext(Leaderboard& lb, const JsonDocument& doc, time_t now,
                     time_t completedStart,
                     ArduinoJson::Allocator* allocator) {
  lb.mode = MODE_NEXT;

  // The scoreboard's Date header should always parse; if it somehow didn't,
  // approximate "now" with the currently listed event's date.
  if (now == 0) now = parseIsoDate(doc["events"][0]["date"] | "");

  // Pick the current-or-next tournament from the season calendar.
  //
  // `completedStart` is the start date of the event ESPN currently reports as
  // finished ("post"), or 0 if none. A tournament's calendar endDate is a UTC
  // timestamp that rolls a day past the final round, and parseIsoDate keeps
  // only the date part — so a purely date-based "has it ended?" test lingers a
  // day or two after the trophy is lifted. We instead trust ESPN's own state
  // and skip the just-finished event (and anything before it) outright. The
  // endDate test stays as a fallback for when the feed has no completed event.
  const char* startIso = nullptr;
  for (JsonObjectConst c : doc["leagues"][0]["calendar"].as<JsonArrayConst>()) {
    time_t start = parseIsoDate(c["startDate"] | "");
    if (completedStart != 0 && start != 0 && start <= completedStart) continue;
    time_t end = parseIsoDate(c["endDate"] | "");
    if (end == 0 || end + 86400 <= now) continue;
    asciiFold(c["label"] | g_tour->label, lb.nextName, sizeof(lb.nextName));
    toUpperInPlace(lb.nextName);
    startIso = c["startDate"] | "";
    formatDateRange(startIso, c["endDate"] | "", lb.nextDates,
                    sizeof(lb.nextDates));
    break;
  }
  if (!startIso || !*startIso) {
    lb.mode = MODE_NONE;  // calendar exhausted: season is over
    return;
  }

  // Pinned golfers start as TBD, named after their config pattern.
  lb.nextGolferCount = min((size_t)MAX_PINNED_ROWS, (size_t)settings.pinnedCount);
  for (uint8_t i = 0; i < lb.nextGolferCount; i++) {
    strlcpy(lb.nextGolfers[i].name, settings.pinned[i],
            sizeof(lb.nextGolfers[i].name));
    toUpperInPlace(lb.nextGolfers[i].name);
    strlcpy(lb.nextGolfers[i].status, "TBD", sizeof(lb.nextGolfers[i].status));
    lb.nextGolfers[i].tee[0] = 0;
  }

  // Seed the countdown (and pinned tee times) from the header feed. Done before
  // the no-pinned early return so the countdown works even with nothing pinned.
  fillNextTeeTimes(lb, startIso, allocator);

  if (lb.nextGolferCount == 0) return;

  // Ask the scoreboard for the next event's start date — once entries are
  // published its competitor list is the tournament field.
  int y, m, d;
  if (sscanf(startIso, "%d-%d-%d", &y, &m, &d) != 3) return;
  char query[24], url[160];
  snprintf(query, sizeof(query), "?dates=%04d%02d%02d", y, m, d);
  scoreboardUrl(url, sizeof(url), query);

  JsonDocument filter;
  JsonObject fEvent = filter["events"].add<JsonObject>();
  JsonObject fc = fEvent["competitions"].add<JsonObject>()["competitors"]
                      .add<JsonObject>();
  fc["athlete"]["displayName"] = true;

  JsonDocument fieldDoc(allocator);
  if (!getJson(url, fieldDoc, filter, nullptr)) return;  // stay TBD

  JsonArrayConst field =
      fieldDoc["events"][0]["competitions"][0]["competitors"].as<JsonArrayConst>();
  if (field.size() == 0) return;  // field not published yet: stay TBD

  // Every tracked golfer is checked against the field (TOUR_AUTO needs all of
  // them); only the first nextGolferCount get a row on the panel.
  for (uint8_t i = 0; i < settings.pinnedCount && i < MAX_PINNED_GOLFERS; i++) {
    bool shown = i < lb.nextGolferCount;
    if (shown)
      strlcpy(lb.nextGolfers[i].status, "OUT", sizeof(lb.nextGolfers[i].status));
    for (JsonObjectConst c : field) {
      const char* fullName = c["athlete"]["displayName"] | "";
      if (nameMatchesPattern(fullName, settings.pinned[i])) {
        lb.pickInField[i] = true;
        if (shown) {
          surnameOf(fullName, lb.nextGolfers[i].name, sizeof(lb.nextGolfers[i].name));
          strlcpy(lb.nextGolfers[i].status, "IN", sizeof(lb.nextGolfers[i].status));
        }
        break;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// BBC Sport live board (DP World Tour). ESPN's EUR scoreboard is hours behind
// during play — no holes-played, no in-round scores, the previous round's
// totals — so while a round is in progress the live rows come from BBC's
// keyless leaderboard feed instead. ESPN still supplies everything else for the
// tour: the calendar, the upcoming event's field, and tee sheets between rounds.
//
// Feed shape (leaderboard.participants[], in rank order):
//   rank         "1", "2", "2" (ties repeat the number; "-" = no rank)
//   name.fullName  BBC's transliteration: "Ludvig Aaberg", "Rasmus Hoejgaard"
//   totalScore.value  "-5", "+2", "E", "-" (no score)
//   thru.value   holes played "12"; "F"/"18" done; a UK-local tee time "13:20"
//                for players yet to start; "WD" when withdrawn
//   roundScores[].value  strokes for each COMPLETED round ("67")
//   status       "Withdrawn" etc., present only for players out of the event
// leaderboard.status is "MidEvent" while a round is being played and
// "Intermission" between rounds; currentRound is the round in play/just done.
// ---------------------------------------------------------------------------

// Player names for the panel come from ESPN's spelling when the same player is
// found in ESPN's field (so BBC's "Aaberg" shows as ABERG, matching the PGA
// board), else from BBC's. Only called for the handful of rows drawn.
static void bbcDisplayName(const char* bbcName, JsonArrayConst espnField,
                           char* dst, size_t dstSize) {
  for (JsonObjectConst c : espnField) {
    const char* n = c["athlete"]["displayName"] | "";
    if (sameNameKey(n, bbcName)) {
      surnameOf(n, dst, dstSize);
      return;
    }
  }
  surnameOf(bbcName, dst, dstSize);
}

// Fills one row from a BBC participant. `rankCount[r]` is how many players
// share rank r (for the "T" prefix). `ukOffset` is the feed's UTC offset in
// seconds (its tee times are UK wall clock) and `now` the current UTC epoch,
// for turning "13:20" into the board's local time.
static void fillBbcRow(GolferRow& row, JsonObjectConst p, int round, int par,
                       const uint8_t* rankCount, long ukOffset, time_t now,
                       JsonArrayConst espnField) {
  bbcDisplayName(p["name"]["fullName"] | "?", espnField, row.name, sizeof(row.name));

  const char* total = p["totalScore"]["value"] | "-";
  strlcpy(row.score, *total ? total : "-", sizeof(row.score));
  strlcpy(row.today, "-", sizeof(row.today));
  strlcpy(row.thru, "-", sizeof(row.thru));
  row.tee[0] = 0;

  // Out of the event: badge in the rank column, no per-round detail.
  const char* status = p["status"] | "";
  if (*status) {
    row.out = true;
    if (strncasecmp(status, "Disq", 4) == 0) strlcpy(row.pos, "DQ", sizeof(row.pos));
    else if (strcasestr(status, "cut")) strlcpy(row.pos, "MC", sizeof(row.pos));
    else strlcpy(row.pos, "WD", sizeof(row.pos));  // Withdrawn / Retired
    return;
  }

  int rank = atoi(p["rank"] | "0");
  if (rank <= 0) strlcpy(row.pos, "-", sizeof(row.pos));
  else if (rankCount[rank] > 1) snprintf(row.pos, sizeof(row.pos), "T%d", rank);
  else snprintf(row.pos, sizeof(row.pos), "%d", rank);

  // Holes played, finished, or a tee time still to come.
  const char* thru = p["thru"]["value"] | "";
  int hh, mm;
  bool isTee = strchr(thru, ':') != nullptr;  // "13:20" (test first: atoi("18:00") == 18)
  if (!isTee && (strcmp(thru, "F") == 0 || atoi(thru) >= 18)) {
    strlcpy(row.thru, "F", sizeof(row.thru));
  } else if (!isTee && atoi(thru) > 0) {
    strlcpy(row.thru, thru, sizeof(row.thru));
  } else if (isTee && sscanf(thru, "%d:%d", &hh, &mm) == 2 && now > 100000) {
    // UK wall clock today -> UTC -> board-local. Today's date is taken from the
    // current UK time so the conversion is right on either side of midnight.
    time_t ukNow = now + ukOffset;
    struct tm uk;
    gmtime_r(&ukNow, &uk);
    time_t tee = utcEpoch(uk.tm_year + 1900, uk.tm_mon + 1, uk.tm_mday, hh, mm, 0) -
                 ukOffset;
    epochToLocalHM(tee, row.tee, sizeof(row.tee));
    return;  // yet to start today: no round score
  }

  // Today's score to par isn't in the feed: total minus the completed prior
  // rounds (roundScores holds completed rounds only). Once today's round is in
  // roundScores too, read it straight.
  int totalPar = scoreStrToPar(row.score);
  if (totalPar == SCORE_NONE) return;
  JsonArrayConst rounds = p["roundScores"].as<JsonArrayConst>();
  int today, prior = 0, i = 0;
  for (JsonObjectConst r : rounds) {
    if (i >= round - 1) break;
    prior += atoi(r["value"] | "0") - par;
    i++;
  }
  if ((int)rounds.size() >= round) today = atoi(rounds[round - 1]["value"] | "0") - par;
  else today = totalPar - prior;
  if (today == 0) strlcpy(row.today, "E", sizeof(row.today));
  else snprintf(row.today, sizeof(row.today), "%+d", today);
}

// Builds a MODE_LIVE board from BBC's feed. Returns false — leaving `lb`
// untouched — when the fetch fails or BBC isn't the better source right now.
// It's used while a round is in progress ("MidEvent"), and at "Intermission"
// once the next round's draw is in the feed (every player's `thru` becomes
// "Round N tee time HH:MM" — ESPN's header feed only lists ~25 players and has
// been seen serving the previous round's times relabelled) or when ESPN still
// hasn't caught up to the round BBC says is complete (`espnPeriod` <
// currentRound). Otherwise ESPN's pipeline (the Final board, the countdown) is
// the richer view.
static bool fetchBbcBoard(Leaderboard& lb, JsonArrayConst espnField, int espnPeriod,
                          ArduinoJson::Allocator* allocator) {
  if (!g_tour->bbcSlug) return false;

  JsonDocument filter;
  JsonObject fl = filter["leaderboard"].to<JsonObject>();
  fl["displayName"] = true;
  fl["status"] = true;
  fl["currentRound"] = true;
  fl["par"] = true;
  fl["startDateTime"] = true;
  JsonObject fp = fl["participants"].add<JsonObject>();
  fp["rank"] = true;
  fp["status"] = true;
  fp["name"]["fullName"] = true;
  fp["totalScore"]["value"] = true;
  fp["thru"]["value"] = true;
  fp["thru"]["accessible"] = true;  // "Round 4 tee time 12:05" names the round
  fp["roundScores"].add<JsonObject>()["value"] = true;

  char url[200];
  snprintf(url, sizeof(url), BBC_LEADERBOARD_URL_FMT, g_tour->bbcSlug);
  JsonDocument doc(allocator);
  time_t now = 0;
  if (!getJson(url, doc, filter, &now)) return false;

  JsonObjectConst board = doc["leaderboard"];
  const char* status = board["status"] | "";
  int round = board["currentRound"] | 0;
  JsonArrayConst players = board["participants"].as<JsonArrayConst>();
  bool inPlay = strcmp(status, "MidEvent") == 0;
  bool between = strcmp(status, "Intermission") == 0;
  // Between rounds: once the draw is out, every active player's thru reads
  // "Round N tee time HH:MM" — that N is the round the board should show.
  int teeRound = 0;
  if (between) {
    for (JsonObjectConst p : players) {
      int r;
      if (sscanf(p["thru"]["accessible"] | "", "Round %d tee time", &r) == 1 &&
          r > round) {
        teeRound = r;
        break;
      }
    }
  }
  bool espnBehind = between && espnPeriod < round;
  Serial.printf("[bbc] %s round %d, %u players (espn period %d, next draw R%d)\n",
                status, round, (unsigned)players.size(), espnPeriod, teeRound);
  if (round < 1 || round > 4 || players.size() == 0 ||
      !(inPlay || espnBehind || teeRound))
    return false;
  if (teeRound) round = teeRound;  // rows carry the upcoming round's tee times

  lb.mode = MODE_LIVE;
  asciiFold(board["displayName"] | g_tour->label, lb.eventName, sizeof(lb.eventName));
  toUpperInPlace(lb.eventName);
  snprintf(lb.roundLabel, sizeof(lb.roundLabel), "R%d", round);
  lb.firstRound = round == 1;
  int par = atoi(board["par"] | "72");

  // "2026-09-17T07:10:00.000+01:00": the trailing offset is the feed's clock
  // (UK time, whatever the event's own zone), which its tee times are in.
  long ukOffset = 0;
  const char* sdt = board["startDateTime"] | "";
  const char* tz = strlen(sdt) >= 6 ? sdt + strlen(sdt) - 6 : "";
  int oh, om;
  if ((tz[0] == '+' || tz[0] == '-') && sscanf(tz + 1, "%d:%d", &oh, &om) == 2)
    ukOffset = (tz[0] == '-' ? -1 : 1) * (oh * 3600L + om * 60L);

  // Pass 1: who's a pick, who shares a rank, and which picks are in the field.
  static bool isPick[168];
  static uint8_t rankCount[170];
  memset(rankCount, 0, sizeof(rankCount));
  int n = 0;
  for (JsonObjectConst p : players) {
    if (n >= (int)(sizeof(isPick) / sizeof(isPick[0]))) break;
    const char* fullName = p["name"]["fullName"] | "";
    isPick[n] = false;
    for (uint8_t i = 0; i < settings.pinnedCount && i < MAX_PINNED_GOLFERS; i++) {
      if (nameMatchesPattern(fullName, settings.pinned[i])) {
        isPick[n] = true;
        lb.pickInField[i] = true;
      }
    }
    int rank = atoi(p["rank"] | "0");
    if (rank > 0 && rank < (int)sizeof(rankCount)) rankCount[rank]++;
    n++;
  }

  // Leader block vs pinned rows: same fixed point as fillLive — with L leaders,
  // a pick is pinned below only if it sits at index >= L.
  uint8_t leaderSlots = BOARD_ROWS, pinnedSlots = 0;
  for (uint8_t iter = 0; iter <= MAX_PINNED_ROWS; iter++) {
    uint8_t deep = 0;
    for (int i = leaderSlots; i < n; i++)
      if (isPick[i] && deep < MAX_PINNED_ROWS) deep++;
    if (deep == pinnedSlots) break;
    pinnedSlots = deep;
    leaderSlots = BOARD_ROWS - deep;
  }

  // Pass 2: fill the rows that will be drawn.
  int idx = 0;
  for (JsonObjectConst p : players) {
    if (idx >= n) break;
    if (lb.leaderCount < leaderSlots) {
      GolferRow& r = lb.leaders[lb.leaderCount++];
      fillBbcRow(r, p, round, par, rankCount, ukOffset, now, espnField);
      r.selected = isPick[idx];
    } else if (isPick[idx] && lb.pinnedCount < pinnedSlots) {
      GolferRow& r = lb.pinned[lb.pinnedCount++];
      fillBbcRow(r, p, round, par, rankCount, ukOffset, now, espnField);
      r.selected = true;
    }
    idx++;
    if (lb.leaderCount >= leaderSlots && lb.pinnedCount >= pinnedSlots) break;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

// The end of a tournament's window: the calendar endDate (a UTC timestamp that
// rolls a day past the final round) for the calendar entry whose start matches
// `ev`. Returns 0 if no match. Used to keep a just-finished event's leaderboard
// up for a grace period instead of immediately flipping to the next countdown.
static time_t eventEndFromCalendar(const JsonDocument& doc, JsonObjectConst ev) {
  time_t evStart = parseIsoDate(ev["date"] | "");
  if (evStart == 0) return 0;
  for (JsonObjectConst c : doc["leagues"][0]["calendar"].as<JsonArrayConst>()) {
    if (parseIsoDate(c["startDate"] | "") == evStart)
      return parseIsoDate(c["endDate"] | "");
  }
  return 0;
}

// Fetches one tour's scoreboard and builds its board into `out`. This is the
// whole per-tour pipeline; fetchLeaderboard (below) runs it once, or once per
// tour in TOUR_AUTO.
static bool fetchTour(Tour tour, Leaderboard& out) {
  g_tour = &TOURS[tour];
  Serial.printf("[espn] %s: fetching scoreboard\n", g_tour->slug);

  // Filter: of the ~1.3 MB response, keep only these fields (~50 KB).
  JsonDocument filter;
  JsonObject fEvent = filter["events"].add<JsonObject>();
  fEvent["shortName"] = true;
  fEvent["name"] = true;
  fEvent["date"] = true;
  JsonObject fComp = fEvent["competitions"].add<JsonObject>();
  fComp["status"]["period"] = true;
  fComp["status"]["type"]["state"] = true;
  fComp["status"]["type"]["name"] = true;  // STATUS_FINAL vs STATUS_PLAY_COMPLETE
  JsonObject fc = fComp["competitors"].add<JsonObject>();
  fc["order"] = true;
  fc["score"] = true;
  fc["athlete"]["displayName"] = true;
  fc["status"]["thru"] = true;
  fc["status"]["type"]["state"] = true;
  fc["status"]["type"]["name"] = true;  // STATUS_CUT / STATUS_WITHDRAWN (when live)
  fc["status"]["position"]["displayName"] = true;
  // Per-round linescores: today's score to par (displayValue), and holes
  // played via the nested per-hole array (competitor.status is null on this
  // feed, so the array length is the only reliable "thru"). The whole body is
  // buffered before parsing (see getJson), so this heavier parse is safe.
  JsonObject fLine = fc["linescores"].add<JsonObject>();
  fLine["period"] = true;
  fLine["displayValue"] = true;
  fLine["linescores"].add<JsonObject>()["period"] = true;  // per-hole, for counting thru
  // Round statistics carry the tee time (unlabelled) for players yet to start.
  fLine["statistics"]["categories"].add<JsonObject>()["stats"].add<JsonObject>()
      ["displayValue"] = true;
  // Season calendar, for the "next tournament" screen.
  JsonObject fCal = filter["leagues"].add<JsonObject>()["calendar"].add<JsonObject>();
  fCal["label"] = true;
  fCal["startDate"] = true;
  fCal["endDate"] = true;

  SpiRamAllocator allocator;
  JsonDocument doc(&allocator);
  time_t now = 0;
  char url[160];
  scoreboardUrl(url, sizeof(url));
  // One retry: the ~0.3-1 MB body occasionally arrives truncated (TLS drop ->
  // "IncompleteInput"), and in TOUR_AUTO a failed tour is silently skipped.
  bool ok = false;
  for (int attempt = 0; attempt < 2 && !ok; attempt++)
    ok = getJson(url, doc, filter, &now);
  if (!ok) {
    // ESPN down but this tour has BBC live scoring: a round in progress can
    // still be shown from BBC alone (no ESPN field -> BBC's own name spellings).
    if (g_tour->bbcSlug) {
      Leaderboard bbc;
      bbc.tour = tour;
      if (fetchBbcBoard(bbc, JsonArrayConst(), 0, &allocator)) {
        out = bbc;
        return true;
      }
    }
    return false;
  }

  // A tournament that's underway wins; otherwise show what's next. ESPN models
  // a whole tournament as one event, so "underway" is broader than a round
  // actively being played ("in"): between rounds the event reports state "post"
  // with type STATUS_PLAY_COMPLETE ("Round 1 - Play Complete"), which is still a
  // live tournament — only STATUS_FINAL means the trophy's been lifted. So an
  // event is live while a round is in progress OR a round is done but the event
  // isn't Final. While scanning, note the latest genuinely-finished (Final)
  // event so the "next" screen can skip a tournament that just ended (fillNext).
  JsonObjectConst liveEvent;
  JsonObjectConst finalEvent;  // most recent genuinely-finished event this feed
  JsonObjectConst preEvent;    // the upcoming (not-yet-started) event, if any
  time_t completedStart = 0;
  for (JsonObjectConst ev : doc["events"].as<JsonArrayConst>()) {
    JsonObjectConst type = ev["competitions"][0]["status"]["type"];
    const char* st = type["state"] | "";
    bool isFinal = strcmp(type["name"] | "", "STATUS_FINAL") == 0;
    if (strcmp(st, "in") == 0 || (strcmp(st, "post") == 0 && !isFinal)) {
      liveEvent = ev;
      break;
    }
    if (strcmp(st, "post") == 0) {  // STATUS_FINAL: note it so fillNext skips it
      time_t s = parseIsoDate(ev["date"] | "");
      if (s > completedStart) {
        completedStart = s;
        finalEvent = ev;
      }
    } else if (strcmp(st, "pre") == 0 && preEvent.isNull()) {
      preEvent = ev;  // its competitor list is the field once the draw is out
    }
  }

  // Treat a tournament as "leaderboard-worthy" for a day either side of play, so
  // the board tracks the event rather than a countdown to a single pick:
  //   * within 24h AFTER the trophy's lifted -> keep the final leaderboard;
  //   * within 24h BEFORE the first tee      -> show the field (par + tee times).
  // A live event still wins outright.
  // Tours whose ESPN scoring lags during play (DP World Tour) take their live
  // rows from BBC while a round is on. ESPN's field for the same event lends
  // its name spellings; the ESPN round number tells BBC when ESPN is behind.
  if (g_tour->bbcSlug) {
    JsonObjectConst xrefEvent = !liveEvent.isNull()    ? liveEvent
                                : !preEvent.isNull()   ? preEvent
                                : finalEvent;
    JsonArrayConst espnField =
        xrefEvent["competitions"][0]["competitors"].as<JsonArrayConst>();
    int espnPeriod =
        liveEvent.isNull() ? 0 : (int)(liveEvent["competitions"][0]["status"]["period"] | 0);
    Leaderboard bbc;
    bbc.tour = tour;
    if (fetchBbcBoard(bbc, espnField, espnPeriod, &allocator)) {
      out = bbc;
      return true;
    }
  }

  Leaderboard lb;  // build into a temp so `out` stays intact on failure
  lb.tour = tour;
  if (!liveEvent.isNull()) {
    // Between rounds the scoreboard withholds the next round's tee times until a
    // few hours out, but the header feed carries them days ahead. When the
    // current round is done (not Final) and the scoreboard has no tee times for
    // the next round yet, pull the header feed's draw and hand it to fillLive so
    // the board shows the upcoming round's start list instead of a flat "F".
    // hdrDoc must outlive fillLive (it reads teeSource), so keep it in scope.
    JsonDocument hdrDoc(&allocator);
    JsonArrayConst teeSource;
    JsonObjectConst liveComp = liveEvent["competitions"][0];
    JsonObjectConst liveType = liveComp["status"]["type"];
    int livePeriod = liveComp["status"]["period"] | 1;
    bool liveBetween = strcmp(liveType["state"] | "", "post") == 0 &&
                       strcmp(liveType["name"] | "", "STATUS_FINAL") != 0;
    if (liveBetween && livePeriod >= 1 && livePeriod < 4 &&
        !roundHasTeeTimes(liveComp["competitors"].as<JsonArrayConst>(),
                          livePeriod + 1) &&
        fetchHeaderEvent(hdrDoc, liveEvent["date"] | ""))
      teeSource = hdrDoc["sports"][0]["leagues"][0]["events"][0]
                      ["competitors"].as<JsonArrayConst>();
    fillLive(lb, liveEvent, teeSource);
  } else if (!finalEvent.isNull() && now > 100000 &&
             now < eventEndFromCalendar(doc, finalEvent) + 86400) {
    // Just finished: hold the final standings up rather than flip to "next up".
    fillLive(lb, finalEvent);
  } else {
    fillNext(lb, doc, now, completedStart, &allocator);
    // fillNext seeds lb.nextStart with the real earliest tee time (header feed).
    // Once that's within 24h the event is imminent enough to render as a live
    // leaderboard: everyone at par with their tee times, including any pick's.
    if (!preEvent.isNull() && lb.nextStart > 0 && now > 100000 &&
        lb.nextStart - now <= 86400) {
      // The scoreboard has no tee times yet this far out, so pull the draw from
      // the header feed and hand it to fillLive as each row's tee-time source.
      // hdrDoc must outlive fillLive (it reads teeSource), so keep it in scope.
      JsonDocument hdrDoc(&allocator);
      JsonArrayConst teeSource;
      if (fetchHeaderEvent(hdrDoc, preEvent["date"] | ""))
        teeSource = hdrDoc["sports"][0]["leagues"][0]["events"][0]
                        ["competitors"].as<JsonArrayConst>();
      Leaderboard live;
      live.tour = tour;
      fillLive(live, preEvent, teeSource);
      if (live.leaderCount > 0) lb = live;  // only if the field is populated
    }
  }

  out = lb;
  return true;
}

// TOUR_AUTO: which of the two boards to show. Walk the tracked golfers in
// order; the first one found in exactly one tour's field decides. A golfer in
// both fields (one tour's event just finished, the other's is next) goes to
// whichever board is live, else PGA. No tracked golfer in either field (fields
// not yet published, or nobody playing this week) -> PGA.
static Tour chooseTour(const Leaderboard& pga, const Leaderboard& eur) {
  for (uint8_t i = 0; i < settings.pinnedCount && i < MAX_PINNED_GOLFERS; i++) {
    bool inP = pga.pickInField[i], inE = eur.pickInField[i];
    if (inP && !inE) return TOUR_PGA;
    if (inE && !inP) return TOUR_EUR;
    if (inP && inE)
      return (eur.mode == MODE_LIVE && pga.mode != MODE_LIVE) ? TOUR_EUR : TOUR_PGA;
  }
  return TOUR_PGA;
}

bool fetchLeaderboard(Leaderboard& out) {
  if (settings.tour != TOUR_AUTO)
    return fetchTour(settings.tour == TOUR_EUR ? TOUR_EUR : TOUR_PGA, out);

  // Auto: the two tours' boards are built independently, then one is picked
  // by where the tracked golfers are playing. One tour failing to fetch isn't
  // fatal — show the other; both failing is.
  Leaderboard pga, eur;
  bool okPga = fetchTour(TOUR_PGA, pga);
  bool okEur = fetchTour(TOUR_EUR, eur);
  if (!okPga && !okEur) return false;
  if (okPga != okEur) {
    out = okPga ? pga : eur;
    Serial.printf("[espn] auto: %s fetch failed, showing %s\n",
                  okPga ? "eur" : "pga", TOURS[out.tour].slug);
    return true;
  }
  Tour pick = chooseTour(pga, eur);
  out = pick == TOUR_EUR ? eur : pga;
  Serial.printf("[espn] auto: showing %s\n", TOURS[pick].slug);
  return true;
}

// ---------------------------------------------------------------------------
// Debug fixture: a synthetic live leaderboard for iterating on the display
// without waiting for a real tournament. Enabled via DEBUG_FAKE_LIVE in
// config.h; main.cpp loads this instead of fetching. Edit the rows below to
// try different names, scores and "thru" states against renderLive/drawRow.
// ---------------------------------------------------------------------------

static void setRow(GolferRow& r, const char* pos, const char* name,
                   const char* today, const char* score, const char* thru) {
  strlcpy(r.pos, pos, sizeof(r.pos));
  strlcpy(r.name, name, sizeof(r.name));
  strlcpy(r.today, today, sizeof(r.today));
  strlcpy(r.score, score, sizeof(r.score));
  strlcpy(r.thru, thru, sizeof(r.thru));
  r.tee[0] = 0;
}

void loadDebugLeaderboard(Leaderboard& out) {
  Leaderboard lb;
  lb.mode = MODE_LIVE;
  strlcpy(lb.eventName, "BMW CHAMPIONSHIP", sizeof(lb.eventName));
  strlcpy(lb.roundLabel, "R2", sizeof(lb.roundLabel));

  // Top 7 — mixes solo/tied ranks, under/over/even scores, and every "thru"
  // state (finished, mid-round, not yet started), plus a highlighted pick. The
  // leader block grew to 7 because only one pick sits below (see MAX_PINNED_ROWS).
  //          pos    name         today  total  thru
  setRow(lb.leaders[0], "1",   "SCHEFFLER", "-5", "-15", "F");
  setRow(lb.leaders[1], "T2",  "MCILROY",   "-3", "-12", "F");
  setRow(lb.leaders[2], "T2",  "HENLEY",    "-4", "-12", "16");
  setRow(lb.leaders[3], "4",   "MORIKAWA",  "-4", "-10", "12");
  setRow(lb.leaders[4], "5",   "ABERG",     "-3", "-9",  "13");
  lb.leaders[4].selected = true;  // Åberg is a pick -> stays highlighted up here
  setRow(lb.leaders[5], "6",   "FLEETWOOD", "-1", "-8",  "7");
  setRow(lb.leaders[6], "T7",  "THOMAS",    "-",  "-7",  "-");  // yet to tee off
  strlcpy(lb.leaders[6].tee, "13:20", sizeof(lb.leaders[6].tee));
  lb.leaderCount = 7;

  // Pinned golfers below the divider. Åberg isn't repeated here: he's already
  // among the leaders above, the same de-dup the live feed does when a pinned
  // golfer sits inside the top rows.
  //
  // Norén demonstrates an out-of-tournament pick: the "MC" badge (orange) sits
  // in the rank column, the total stays, and the per-round columns are blank.
  // Swap "MC" for "WD"/"DQ" to preview those, or clear `out` for the normal row.
  setRow(lb.pinned[0], "MC", "NOREN", "-", "-1", "-");
  lb.pinned[0].out = true;
  lb.pinned[0].selected = true;
  lb.pinnedCount = 1;

  out = lb;
}
