#!/usr/bin/env python3
"""Reference implementation of the buss-display data fetch.

Mirrors exactly what the ESP32-S3-Zero firmware will do:
one GET request, keep 4 fields per departure, render 10 rows.

API: SL Transport (Trafiklab) - free, no API key.
Docs: https://www.trafiklab.se/api/our-apis/sl/transport/
"""

import json
import urllib.request

SITE_ID = 1810          # Norra Sköndal (from https://transport.integration.sl.se/v1/sites)
SITE_NAME = "Norra Sköndal"
FORECAST_MIN = 90       # look-ahead window; raise if fewer than 10 rows come back
NUM_ROWS = 10

# Column widths for the display (line + space + dest + right-aligned time)
LINE_W, DEST_W, TIME_W = 4, 13, 6

URL = (
    "https://transport.integration.sl.se/v1/sites/"
    f"{SITE_ID}/departures?transport=BUS&forecast={FORECAST_MIN}"
)


def fetch():
    with urllib.request.urlopen(URL, timeout=10) as resp:
        data = json.load(resp)
    rows = []
    for dep in data["departures"]:
        if dep["state"] == "CANCELLED":
            continue
        rows.append(
            {
                "line": dep["line"]["designation"],   # "802", "812C"
                "dest": dep["destination"],           # "Gullmarsplan"
                "display": dep["display"],            # "Nu", "5 min", "17:42"
            }
        )
    return rows[:NUM_ROWS]


def render(rows):
    width = LINE_W + 1 + DEST_W + TIME_W
    print(SITE_NAME)
    print("-" * width)
    for r in rows:
        line = r["line"][:LINE_W]
        dest = r["dest"][:DEST_W]
        time = r["display"].replace(" min", "m")[:TIME_W]
        print(f"{line:<{LINE_W}} {dest:<{DEST_W}}{time:>{TIME_W}}")


if __name__ == "__main__":
    render(fetch())
