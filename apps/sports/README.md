# ⚽ sports

Next game and a live, ticking countdown for a handful of favourite teams, on the
64×64 HUB75 panel. One 16px block per team:

```
 ⚽  AIK-BKH       16:30      <- matchup (home first, ours in team colour) + kick-off
     06:53:55                 <- countdown; "3d 09:23:55" when days away
 ─────────────────────
 🏒  LIF-AIK       11/11
     3d 09:23:55
 ─────────────────────
 🏈  SF-LV         22:25
     12:48:55
 ─────────────────────
 ⚽  SWE-BIH       14/11
     6d 11:08:55
```

While a game is on, the countdown row becomes the live score, refreshed every
minute: a blinking red dot, the score (home–away, same order as the matchup) and
the match clock (`67'`, `HT`, `Q3 8:12`, `P2`). After the final whistle it shows
`FT 2-1` (plus `OT`/`SO`/`PEN`/`AET` where relevant) for 2 hours, then rolls on to
the next fixture.

Kick-off shows as a time (`21:25`) when it's today, otherwise as a date (`15/11`).
A red pixel in a block's top-right corner means the last fetch for that team failed
(the previous data stays on screen).

## Teams

Set in [`src/teams.cpp`](src/teams.cpp) (`TEAMS[]`, panel order = array order):

| Team | Source | Feed |
|------|--------|------|
| AIK Fotboll (herr) | ESPN | `soccer/all/teams/994` |
| AIK Hockey | TheSportsDB + hockeyallsvenskan.se | team `137307`, HA code `aik` |
| San Francisco 49ers | ESPN | `football/nfl/teams/sf` |
| Sverige (herr) | ESPN | `soccer/all/teams/466` |

Both sources are keyless. ESPN's soccer league `all` covers every competition a
team plays (Allsvenskan + cup + Europe, or Nations League + qualifiers +
friendlies for the national team). ESPN has no Swedish hockey, so AIK Hockey comes
from TheSportsDB's free tier (`eventsnext.php`, public key `123`).

**Live scores** come from different endpoints than the fixtures, because ESPN's
team endpoints (`nextEvent`, schedule) drop a game while it's in progress:

- ESPN: the single-event scoreboard `…/<sport>/<league>/scoreboard/<eventId>`
  (~10 KB). The league is the real competition (`soccer/swe.1`), since `all`
  serves a stale clock. Right after boot, the league's scoreboard is checked
  once too, so a board that starts mid-game still finds the live match.
- Hockey: TheSportsDB's free tier has no live scores, so the board uses
  HockeyAllsvenskan's own site API (`hockeyallsvenskan.se/api/game?slug=…`),
  which their site polls during games. Slugs are `<yyyymmdd>-<home>-<away>`,
  built from the TheSportsDB fixture plus the `HA_TEAMS` name→code table in
  `teams.cpp`. That table needs a touch-up when teams are promoted or relegated;
  a missing team still shows fixtures, just without a live score.

## Refresh

Each team polls on its own schedule: every 30 min when the game is far off, then
at kick-off it switches to the live feed, polled every minute until the final
whistle. The final score stays up for 2 hours. The clock comes from NTP and,
as a fallback, from the HTTP `Date` header of each fetch.

## Settings

`http://sports-display.local/` (or `192.168.4.1` on the always-on
`Sports-Settings` hotspot): upcoming games with browser-side countdowns, a
**Refresh now** button, brightness (also on the knob), and the shared night schedule
(default off 01:00–07:00).

```bash
pio run -d apps/sports -t upload -t monitor
```
