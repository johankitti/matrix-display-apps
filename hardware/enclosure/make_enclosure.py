#!/usr/bin/env python3
"""Back enclosure for a Waveshare RGB-Matrix-P3-64x64 panel (192x192 mm)
with room behind it for an ESP32-S3-Zero (23.5x18 mm) and cables.

The case is printed back-down; the panel's 191 mm back frame drops into the
open top and its 192.8 mm LED face rests on the rim, outside the box. Posts
under the frame back it up. Behind it is a cavity for the ESP32, HUB75 ribbon and
power wiring.

All dimensions in millimetres. Edit PARAMS and re-run:
    .venv/bin/python make_enclosure.py
Axes: X = width, Y = height when hung (+Y is the top edge), Z = depth
(Z=0 is the back that sits on the print bed / wall).
"""
import struct

import numpy as np
from manifold3d import CrossSection, FillRule, Manifold

# ---------------- PARAMS ----------------
PANEL_FACE = 192.8   # LED face (PCB) - sits ON the rim, outside the box
PANEL = 191.0        # plastic back frame - this part goes into the pocket
FRAME_D = 12.0       # depth of the 191 mm back frame = rim to posts (measured)
CLR = 0.3            # clearance per side around the back frame
WALL = 2.4           # side wall thickness
FLOOR = 2.4          # back plate thickness
CAVITY = 32.0        # free depth behind panel (32 leaves room for a USB-C jack nut later)

POST = 12.0          # panel support posts: width along the wall
POST_IN = 6.0        # ...and how far they stick in from the wall

# M3 screw standoffs that line up with the panel's brass inserts.
# Measure insert centres from the panel's bottom-left corner (seen from
# the BACK) and list them here, e.g. [(20.0, 20.0), (172.0, 20.0)].
# Screws go in from the back (M3x8..10), heads sit in a counterbore.
MOUNT_HOLES = []

# ESP32-S3-Zero snap-in dock: a SEPARATE part (dock.stl) that prints flat and
# slides down onto a dovetail rail on the right wall. The board sits upside
# down in it: components and the USB-C port hang in a well against the wall,
# header pins (with jumpers) point sideways into the case, USB end faces the
# bottom edge. Tilt the USB end in first, then press the far end past the snap
# hook, which only grabs the bare middle of the board.
ESP_W, ESP_L = 18.0, 23.5
ESP_Y = 60.0         # board's USB end, from inner bottom wall (plug room below)
ESP_PCB = 1.2        # board thickness
ESP_USB = 4.0        # USB-C port height under the board (measured)
ESP_WELL = ESP_USB + 0.5   # well depth under the board
ESP_STACK = 15.0     # whole board: USB-C port + PCB + pins (measured)
ESP_PINS = ESP_STACK - ESP_USB - ESP_PCB   # pins above the board
LEDGE_D = 0.6        # end ledges under the board; pin solder starts ~0.8 in
DOCK_BASE = 3.6      # dock back plate (holds the dovetail channel)
DV_NECK, DV_HEAD, DV_DEPTH, DV_CLR = 6.0, 9.0, 2.0, 0.25   # dovetail rail

# All connectors go in the RIGHT side wall (+X, seen from the front), so the
# bottom edge stays flat and the case can stand on a desk.
# Each entry: (name, hole diameter, keep-out diameter for nut/flange,
#              centre height along the side from the inner bottom wall)
SIDE_HOLES = [
    # DC jack 2.1x5.5 chassis switch (Electrokit 41019430, DC-022 type):
    # M13x1 thread, 14 mm flange, 16.8 mm hex nut, panel up to 6.3 mm.
    # (The 41015531/MJ-14SR jack is only rated 0.5 A - too little here.)
    ("dc_jack", 13.3, 17.5, 21.0),
    # Rotary encoder Bourns PEC11R-4220F-S0024: M7x0.75 bushing, 7 mm long,
    # body 12.5x13.4 mm inside. Knob sits near the top for easy reach.
    ("encoder", 7.4, 15.0, 150.0),
]
# Panel-mount USB-C jack, added later. Set True to cut it (22.5 mm hole).
USB_JACK = False
USB_HOLE_D, USB_NUT_D, USB_Y = 22.5, 30.0, 60.0

KEYHOLES = True      # two wall-hanging keyholes near the top edge
VENTS = True         # vent slots in the back plate

OUT = "enclosure.stl"
DOCK_OUT = "esp32_dock.stl"
# ----------------------------------------

IN = PANEL + 2 * CLR            # inner pocket size
OUTER = IN + 2 * WALL
H = FLOOR + CAVITY + FRAME_D    # rim: the LED face rests here, outside the box
SEAT = FLOOR + CAVITY           # z where the frame's back meets the posts
assert PANEL_FACE > IN, "LED face must be wider than the pocket to rest on the rim"


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl(x, y, z0, z1, d):
    return Manifold.cylinder(z1 - z0, d / 2, circular_segments=48).translate([x, y, z0])


# inner coordinates start at (WALL, WALL)
def ix(v):
    return WALL + v


shell = box(0, 0, 0, OUTER, OUTER, H) - box(WALL, WALL, FLOOR, WALL + IN, WALL + IN, H + 1)
parts = [shell]
cuts = []

# --- panel support posts: 4 corners + 4 mid-sides, attached to the walls ---
for c in (0.0, IN / 2 - POST / 2, IN - POST):
    for edge in ("bottom", "top", "left", "right"):
        if edge == "bottom":
            p = box(ix(c), ix(0), FLOOR, ix(c + POST), ix(POST_IN), SEAT)
        elif edge == "top":
            p = box(ix(c), ix(IN - POST_IN), FLOOR, ix(c + POST), ix(IN), SEAT)
        elif edge == "left":
            p = box(ix(0), ix(c), FLOOR, ix(POST_IN), ix(c + POST), SEAT)
        else:
            p = box(ix(IN - POST_IN), ix(c), FLOOR, ix(IN), ix(c + POST), SEAT)
        parts.append(p)

# --- screw standoffs for the panel's brass inserts ---
for hx, hy in MOUNT_HOLES:
    # panel seen from the back is mirrored in X relative to this model
    x, y = ix(CLR + PANEL - hx), ix(CLR + hy)
    parts.append(cyl(x, y, FLOOR, SEAT, 9.0))
    cuts.append(cyl(x, y, -1, SEAT + 1, 3.4))          # M3 clearance
    cuts.append(cyl(x, y, -1, SEAT - 5.0, 6.5))        # head counterbore

# --- ESP32 dock (separate part), built in its own print orientation ---
# local x = across the board, y = along it (USB end at 0), z = up from the bed
DT = 1.6                                   # dock wall thickness
hw = ESP_W / 2 + 0.3                       # half pocket width
yl = ESP_L + 0.6                           # pocket length
ledge = DOCK_BASE + ESP_WELL               # underside of the board
pcb_top = ledge + ESP_PCB
DW, DL = 2 * (hw + DT), yl + DT            # dock outline


def build_dock():
    d = [box(-hw - DT, 0, 0, hw + DT, DL, DOCK_BASE)]                  # back plate
    # long side walls, flush with the board's face (header strips sit above)
    d.append(box(-hw - DT, 0, 0, -hw, DL, pcb_top))
    d.append(box(hw, 0, 0, hw + DT, DL, pcb_top))
    # USB end: open in the middle for the port and plug, corner ledges only
    d.append(box(-hw, 0, 0, -5.5, LEDGE_D, ledge))
    d.append(box(5.5, 0, 0, hw, LEDGE_D, ledge))
    # far end: ledges, fixed wall segments, and a slotted flexible hook
    d.append(box(-hw, yl - LEDGE_D, 0, -4.5, yl, ledge))
    d.append(box(4.5, yl - LEDGE_D, 0, hw, yl, ledge))
    d.append(box(-hw - DT, yl, 0, -4.5, DL, pcb_top))
    d.append(box(4.5, yl, 0, hw + DT, DL, pcb_top))
    lip = pcb_top + 0.15                   # a hair of play over the board
    d.append(box(-4.0, yl, DOCK_BASE, 4.0, yl + 1.0, lip + 2.2))       # arm
    hook = CrossSection([[[yl, lip], [yl - 0.7, lip], [yl - 0.7, lip + 0.6], [yl, lip + 2.2]]],
                        FillRule.NonZero)
    d.append(Manifold.extrude(hook, 8.0).warp_batch(lambda a: a[:, [2, 0, 1]]).translate([-4.0, 0, 0]))
    m = d[0]
    for q in d[1:]:
        m = m + q
    # dovetail channel across the back, open on the bed side (widens upward)
    n, h = DV_NECK / 2 + DV_CLR, DV_HEAD / 2 + DV_CLR
    ch = CrossSection([[[DL / 2 - n, -1], [DL / 2 + n, -1], [DL / 2 + h, DV_DEPTH + DV_CLR],
                        [DL / 2 - h, DV_DEPTH + DV_CLR]]], FillRule.NonZero)
    return m - Manifold.extrude(ch, DW + 2).warp_batch(lambda a: a[:, [2, 0, 1]]).translate([-DW / 2 - 1, 0, 0])


dock = build_dock()

# where the dock sits on the right wall: local x -> world Z (lower edge on the
# floor), local y -> world Y, local z -> inward from the wall (-X)
xw = OUTER - WALL
dy0 = ix(ESP_Y)


def dock_to_world(m):
    return m.warp_batch(lambda a: np.column_stack([xw - a[:, 2], dy0 + a[:, 1], FLOOR + DW / 2 + a[:, 0]]))


assert FLOOR + DW <= SEAT, "ESP32 dock too tall for the cavity"
# dovetail rail on the wall, neck at the wall, head inward; rises from the floor
rail = CrossSection([[[0, -DV_NECK / 2], [DV_DEPTH, -DV_HEAD / 2], [DV_DEPTH, DV_HEAD / 2],
                      [0, DV_NECK / 2]]], FillRule.NonZero)
parts.append(Manifold.extrude(rail, DW).warp_batch(
    lambda a: np.column_stack([xw + 0.01 - a[:, 0], dy0 + DL / 2 - a[:, 1], FLOOR + a[:, 2]])))

# --- connector holes through the right side wall, centred in the free depth ---
holes = list(SIDE_HOLES)
if USB_JACK:
    holes.append(("usb_c", USB_HOLE_D, USB_NUT_D, USB_Y))
side_z = FLOOR + CAVITY / 2
post_spans = [(c, c + POST) for c in (0.0, IN / 2 - POST / 2, IN - POST)]
busy = post_spans + [(ESP_Y - 25.0, ESP_Y + DL)]   # dock + USB plug path below it
assert all(ESP_Y + DL <= a or ESP_Y >= b for a, b in post_spans), "ESP32 dock hits a panel post"
for name, d, keep, y in holes:
    r = keep / 2
    assert all(y + r <= a or y - r >= b for a, b in busy[3:]), f"{name}: hits the ESP32 dock"
    assert side_z - r >= FLOOR and side_z + r <= SEAT, f"{name}: CAVITY too shallow"
    assert all(y + r <= a or y - r >= b for a, b in post_spans), f"{name}: hits a panel post"
    cuts.append(Manifold.cylinder(WALL + 2, d / 2, circular_segments=64)
                .rotate([0, 90, 0]).translate([OUTER - WALL - 1, ix(y), side_z]))

# --- keyholes near the top: enter the big hole, slide down onto the slot ---
if KEYHOLES:
    for kx in (IN * 0.25, IN * 0.75):
        ky = IN - 30.0
        cuts.append(cyl(ix(kx), ix(ky), -1, FLOOR + 1, 9.0))
        cuts.append(box(ix(kx - 2.2), ix(ky), -1, ix(kx + 2.2), ix(ky + 10.0), FLOOR + 1))
        cuts.append(cyl(ix(kx), ix(ky + 10.0), -1, FLOOR + 1, 4.4))

# --- vent slots through the back plate, clear of the ESP32 and keyholes ---
if VENTS:
    for i in range(8):
        vx = IN / 2 - 3.5 * 14 + i * 14
        cuts.append(box(ix(vx - 2), ix(60), -1, ix(vx + 2), ix(130), FLOOR + 1))

model = parts[0]
for p in parts[1:]:
    model = model + p
for c in cuts:
    model = model - c



def write_stl(m, path, title):
    mesh = m.to_mesh()
    verts = np.asarray(mesh.vert_properties)[:, :3]
    tris = np.asarray(mesh.tri_verts)
    with open(path, "wb") as f:
        f.write(title.encode().ljust(80, b"\0"))
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            a, b, c = verts[t]
            n = np.cross(b - a, c - a)
            n = n / (np.linalg.norm(n) or 1.0)
            f.write(struct.pack("<3f", *n))
            f.write(struct.pack("<9f", *a, *b, *c))
            f.write(struct.pack("<H", 0))
    print(f"wrote {path}: {len(tris)} triangles, genus {m.genus()}, "
          f"volume {m.volume() / 1000:.1f} cm^3")


write_stl(model, OUT, "LED panel enclosure - make_enclosure.py")
write_stl(dock, DOCK_OUT, "ESP32 dock - make_enclosure.py")
assert (model ^ dock_to_world(dock)).volume() < 1e-3, "dock collides with the case"
print(f"outer {OUTER:.1f} x {OUTER:.1f} x {H:.1f} mm (LED face on top), "
      f"pocket {IN:.1f} mm, rim to posts {FRAME_D} mm, cavity {CAVITY} mm")
