# 🚌 buss-display

An ESP32-S3-Zero + 64×64 HUB75 LED matrix departure board showing the next
buses from a Stockholm bus stop — live, straight from SL's realtime API.
Station and direction are picked from a settings page the device serves at
`http://buss-display.local/`.

```
Norra Sköndal
------------------------
172  Hallunda cent    Nu
807  Brandbergens     Nu
802  Tyresö centru  2min
181  Farsta strand  5min
875  Tyresö kyrka   9min
```

## How it works

```
┌───────────────┐    GET every ~30 s        ┌─────────────────────┐
│ ESP32-S3-Zero │ ────────────────────────► │  SL Transport API   │
│ + HUB75 panel │ ◄──────────────────────── │  (free, no API key) │
└───────┬───────┘    JSON: next departures  └──────────▲──────────┘
        │ serves settings page                         │ station search +
        ▼                                              │ live preview (CORS *)
┌─────────────────────────┐                            │
│ your phone / browser    │ ───────────────────────────┘
│ buss-display.local      │
└─────────────────────────┘
```

The board polls the [SL Transport API](https://www.trafiklab.se/api/our-apis/sl/transport/)
(Trafiklab open data — **no API key required**) and renders the ten next
bus departures with SL's own pre-computed countdown strings, the same
ones shown on the physical signs: `Nu`, `5 min`, or a clock time like `17:42`.

### The one request that powers everything

```
GET https://transport.integration.sl.se/v1/sites/1810/departures?transport=BUS&forecast=90
```

| Piece | Meaning |
|---|---|
| `1810` | Site ID for Norra Sköndal (from `/v1/sites`) |
| `transport=BUS` | Buses only |
| `forecast=90` | Look-ahead window in minutes |

Only three fields per departure are kept:

| Field | Example | Used for |
|---|---|---|
| `line.designation` | `802`, `812C` | Line column |
| `destination` | `Gullmarsplan` | Destination column |
| `display` | `Nu`, `5 min`, `17:42` | Time-left column |

## Settings page

The device serves `http://buss-display.local/` on your Wi-Fi. Because the SL
API sends `Access-Control-Allow-Origin: *`, the page does the heavy lifting
**in the browser**: typeahead search over all 6,510 SL sites (the 1.3 MB list
never touches the ESP32), a live preview of upcoming departures, and
direction options labeled with sampled destinations (since `direction_code`
is per-line, not geographic). Saving POSTs a tiny `{siteId, name, direction}`
to the device, which persists it in NVS flash — settings survive power cuts.

## Repo contents

| Path | Purpose |
|---|---|
| [`src/main.cpp`](src/main.cpp) | Firmware: Wi-Fi, SL fetch (ArduinoJson filtered parse), HUB75 rendering |
| [`src/web.cpp`](src/web.cpp) | Settings page + `/api/settings` endpoint |
| [`src/settings.cpp`](src/settings.cpp) | NVS-persisted settings (station, direction) |
| [`include/config.h`](include/config.h) | Pin map, panel constants, layout, colors, defaults |
| [`docs/hardware-reference.md`](docs/hardware-reference.md) | Board, wiring, power — everything hardware |
| [`fetch_departures.py`](fetch_departures.py) | Desktop reference implementation of the fetch + render loop |

## Build & flash

```bash
pio run -e s3mini -t upload -t monitor
```

No Wi-Fi credentials in the code: the device uses whatever network is saved
in its NVS flash (WiFiManager pattern). On a fresh board — or after a network
change — it opens a **Buss-Setup** access point; join it from a phone and pick
your network. It alternates between retrying the saved network (3 min) and
portal windows (3 min), so it can never get permanently stuck headless.

Or try the fetch logic without hardware:

```bash
python3 fetch_departures.py
```

## Field notes from the API investigation

- **No auth, no secrets** — SL Transport is open data under fair use.
  The deprecated `api.sl.se` APIs (dead since 2024) needed keys; most
  old tutorials point there. Anything not on
  `transport.integration.sl.se` is outdated.
- **Cancelled buses are filtered before truncating to 10 rows**, so
  dropped departures are backfilled by later ones.
- **`stop_point.designation` is optional** — Norra Sköndal has no
  platform letters, so the parser must not assume the field exists.
- **`direction_code` is per line, not geographic** — city-bound buses
  appear under both codes depending on the line. Filter by destination
  whitelist if you only want one direction.
- **Line names aren't all digits** (`812C`), and destination names can
  be 20+ chars (`Brandbergens centrum`) — columns truncate accordingly.

## Display layout

TomThumb 3×5 font on the 64×64 panel: station header, separator, then 9
departure rows (line number in SL red — or blue for Blåbuss trunk lines —
destination in white, time right-aligned in green). Set `SHOW_HEADER 0` in
`config.h` to fit 10 rows instead. The onboard WS2812 doubles as a status
LED: blue connecting, green ok, yellow fetching, red fetch failed (the panel
then keeps showing the last good data plus a red corner pixel).

## Roadmap

- [x] Investigate & verify the data source
- [x] Reference fetch + render implementation
- [x] ESP32-S3-Zero firmware (Wi-Fi + HTTP + ArduinoJson filtered parse)
- [x] Drive the 64×64 HUB75 panel
- [x] Web settings page (station picker + direction)
- [ ] Flash & verify on the real hardware
- [ ] Case & mounting
