#!/usr/bin/env python3
"""Back enclosure for a Waveshare RGB-Matrix-P3-64x64 panel (192x192 mm)
with room behind it for an ESP32-S3-Zero (23.5x18 mm) and cables.

The case is printed back-down; the panel's 191 mm back frame drops into the
open top and its 192.8 mm LED face rests on the rim, outside the box. Posts
under the frame back it up. Behind it is a cavity for the ESP32, HUB75 ribbon and
power wiring. The right wall carries the DC jack, the power rocker switch and
the encoder knob, all centred on the wall's height; the ESP32 dock hangs on
the left wall so nothing on the right is in the way of its USB plug.

An optional desk stand (STAND) hangs the case between the arms of a printed
U-yoke so it can tilt forward and back in 15 degree steps: a printed stud with
a hex head drops into a pocket inside each side wall, and a printed knob pulls
the yoke arm onto a ring of teeth around it. No metal parts.

All dimensions in millimetres. Edit PARAMS and re-run:
    .venv/bin/python make_enclosure.py
Axes: X = width, Y = height when hung (+Y is the top edge), Z = depth
(Z=0 is the back that sits on the print bed / wall).
"""
import struct

import numpy as np
from manifold3d import CrossSection, FillRule, Manifold, OpType

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
# slides down onto a dovetail rail on DOCK_WALL. The board sits upside
# down in it: components and the USB-C port hang in a well against the wall,
# header pins (with jumpers) point sideways into the case, USB end faces the
# bottom edge. Tilt the USB end in first, then press the far end past the snap
# hook, which only grabs the bare middle of the board.
ESP_W, ESP_L = 18.0, 23.5
DOCK_WALL = "left"   # "left" or "right" (seen from the front); the other wall gets the connectors
ESP_Y = 55.0         # board's USB end, from inner bottom wall (plug room below)
ESP_PCB = 1.2        # board thickness
ESP_USB = 4.0        # USB-C port height under the board (measured)
ESP_WELL = ESP_USB + 0.5   # well depth under the board
ESP_STACK = 15.0     # whole board: USB-C port + PCB + pins (measured)
ESP_PINS = ESP_STACK - ESP_USB - ESP_PCB   # pins above the board
LEDGE_D = 0.6        # end ledges under the board; pin solder starts ~0.8 in
DOCK_BASE = 3.6      # dock back plate (holds the dovetail channel)
DV_NECK, DV_HEAD, DV_DEPTH, DV_CLR = 6.0, 9.0, 2.0, 0.25   # dovetail rail

# Connectors go through the SIDE walls, so the bottom edge stays flat and the
# case can stand on a desk. Every hole is centred across the wall's height
# (SIDE_Z = H / 2) so they line up from outside. Its keep-out (nut, flange or
# bezel) must still fit between the back plate and the panel frame, and must
# clear the posts and, on DOCK_WALL, the ESP32 dock and its USB plug path.
# Each entry: (name, wall, cut, keep-out, centre height from inner bottom wall)
#   cut       hole diameter, or (along wall, across depth) for a rectangle
#   keep-out  (along wall, across depth) of the biggest thing on either side
# "right" is +X seen from the front. An optional 6th value overrides the depth
# centre for parts whose keep-out is too big to sit at SIDE_Z.
SIDE_HOLES = [
    # DC jack 2.1x5.5 chassis switch (Electrokit 41019430, DC-022 type):
    # M13x1 thread, 14 mm flange, 16.8 mm hex nut, panel up to 6.3 mm.
    # (The 41015531/MJ-14SR jack is only rated 0.5 A - too little here.)
    ("dc_jack", "right", 13.3, (17.5, 17.5), 21.0),
    # Rocker switch 2-pole on-off I/O (Electrokit 41002801, Bulgin H8550VBBB):
    # 13.0 x 19.8 mm snap-in cut-out, 15 x 21 mm bezel, 4.8 mm blade terminals.
    # Long axis runs along the wall so the rocker flips up/down; the cut is
    # 0.2 mm over nominal to allow for FDM hole shrink.
    ("rocker", "right", (20.0, 13.2), (23.0, 17.0), 50.0),
    # Rotary encoder Bourns PEC11R-4220F-S0024: M7x0.75 bushing, 7 mm long,
    # body 12.5x13.4 mm inside. Knob sits near the top for easy reach.
    ("encoder", "right", 7.4, (15.0, 15.0), 150.0),
]
# Panel-mount USB-C jack, added later. Set True to cut it (22.5 mm hole). Its
# 30 mm nut is too big to centre on the wall, so it sits at the cavity centre
# instead, on the dock's wall between the pivot block / tooth ring and the top.
USB_JACK = False
USB_HOLE = ("usb_c", DOCK_WALL, 22.5, (30.0, 30.0), 130.0, FLOOR + CAVITY / 2)

KEYHOLES = True      # two wall-hanging keyholes near the top edge
VENTS = True         # vent slots in the back plate

# Tilt stand. The case hangs between the arms of a U-shaped yoke and pivots
# about its own centre, so it is balanced and the knobs only hold it against
# nudges. Per side, inside to outside: a hex-headed STUD in a pocket in the
# pivot block (the side wall's mid post, grown for the purpose), its printed
# thread out through the wall, a ring of radial TEETH on the outer wall face,
# the yoke ARM with the mating ring on its inner face, and a KNOB whose female
# thread clamps the arm onto the teeth. Loosen the knob a third of a turn to
# lift the teeth clear and re-tilt. Stud and knob print thread-up, the yoke
# prints feet-down; nothing needs support.
STAND = True
BLOCK_W, BLOCK_IN = 26.0, 8.0     # pivot block: width along the wall, depth into the case
HEX_AF, HEX_H = 14.0, 3.0         # stud head across flats, head height
HEX_POCKET, HEX_POCKET_D = 14.6, 3.5   # pocket across flats, depth from the block's inner face
SHANK_D, SHANK_HOLE = 11.4, 12.0  # stud shank and its hole through block + wall
THR_D, THR_PITCH, THR_DEPTH = 14.0, 3.0, 1.3   # printed thread: major dia, pitch, depth
THR_CLR = 0.35                    # radial clearance grown onto the female thread
THR_LEN = 20.0                    # thread length outside the wall
TEETH, TOOTH_H = 24, 1.2          # detent teeth per ring (15 degree steps), tooth height
RING_ID, RING_OD = 18.0, 32.0     # tooth ring inner / outer diameter
ARM_GAP = TOOTH_H + 0.2           # arm face to wall face when clamped: teeth 1.0 mm engaged
KNOB_D, KNOB_H, KNOB_LOBES = 34.0, 16.0, 8
KNOB_THREAD = 12.0                # female thread depth (blind)
ARM_T, ARM_W, ARM_DISC = 8.0, 20.0, 36.0   # yoke arm thickness, width at the foot, disc at the pivot
PIVOT_H = 115.0                   # pivot above the desk; case corners sweep a 101 mm circle
FOOT_L, FOOT_FWD, FOOT_W, FOOT_T = 130.0, 50.0, 20.0, 8.0   # per side; FWD = ahead of the pivot
BAR_W = 15.0                      # cross bar joining the feet under the case
TILT_CHECK = 45                   # self-check the swing this far each way

OUT = "enclosure.stl"
DOCK_OUT = "esp32_dock.stl"
YOKE_OUT, STUD_OUT, KNOB_OUT = "stand_yoke.stl", "stand_stud.stl", "stand_knob.stl"
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


def union(ms):
    return Manifold.batch_boolean(list(ms), OpType.Add)


def hex_prism(af, length):
    """Hexagonal prism along +Z from z=0, flats `af` apart."""
    return Manifold.cylinder(length, af / 2 / np.cos(np.pi / 6), circular_segments=6)


def thread(length, clearance=0.0):
    """Single-start printed thread along +Z from z=0. The cross-section is a
    polar profile (flank, crest, flank, root over one pitch) extruded with one
    full twist per pitch, which sweeps it into a helix. The female thread is
    the same profile grown by `clearance`, so male and female always match."""
    r0 = THR_D / 2 - THR_DEPTH + clearance
    r1 = THR_D / 2 + clearance
    flank, crest = 0.35, 0.15          # fractions of the pitch (root gets the rest)
    pts = []
    n = 72
    for i in range(n):
        u = i / n
        if u < flank:
            f = u / flank
        elif u < flank + crest:
            f = 1.0
        elif u < 2 * flank + crest:
            f = 1 - (u - flank - crest) / flank
        else:
            f = 0.0
        r = r0 + (r1 - r0) * f
        pts.append([r * np.cos(2 * np.pi * u), r * np.sin(2 * np.pi * u)])
    turns = length / THR_PITCH
    return Manifold.extrude(CrossSection([pts], FillRule.NonZero), length,
                            n_divisions=int(turns * 24) + 1, twist_degrees=360.0 * turns)


def tooth_ring(phase_deg=0.0):
    """TEETH radial ridges standing on z=0 with tips at z=TOOTH_H, 90 degree V
    profile of constant width, ridges at phase + k*360/TEETH. Two rings half a
    pitch out of phase, facing each other ARM_GAP apart, mesh with a 0.2 mm
    tip-to-root gap and lock every 360/TEETH degrees."""
    teeth = []
    ri, ro = RING_ID / 2, RING_OD / 2
    for k in range(TEETH):
        a = np.radians(phase_deg + k * 360.0 / TEETH)
        u, v = np.array([np.cos(a), np.sin(a)]), np.array([-np.sin(a), np.cos(a)])
        pts = []
        for r in (ri, ro):
            for side in (-TOOTH_H, TOOTH_H):
                q = r * u + side * v
                pts.append([q[0], q[1], 0.0])
            pts.append([r * u[0], r * u[1], TOOTH_H])
        teeth.append(Manifold.hull_points(pts))
    return union(teeth)


# inner coordinates start at (WALL, WALL)
def ix(v):
    return WALL + v


shell = box(0, 0, 0, OUTER, OUTER, H) - box(WALL, WALL, FLOOR, WALL + IN, WALL + IN, H + 1)
parts = [shell]
cuts = []

# --- panel support posts: 4 corners + 4 mid-sides, attached to the walls ---
# With the stand, the mid post on each side wall grows into the pivot block.
BW, BIN = (BLOCK_W, BLOCK_IN) if STAND else (POST, POST_IN)
for i, c in enumerate((0.0, IN / 2 - POST / 2, IN - POST)):
    for edge in ("bottom", "top", "left", "right"):
        w, d, c0 = POST, POST_IN, c
        if i == 1 and edge in ("left", "right"):
            w, d, c0 = BW, BIN, IN / 2 - BW / 2
        if edge == "bottom":
            p = box(ix(c), ix(0), FLOOR, ix(c + POST), ix(POST_IN), SEAT)
        elif edge == "top":
            p = box(ix(c), ix(IN - POST_IN), FLOOR, ix(c + POST), ix(IN), SEAT)
        elif edge == "left":
            p = box(ix(0), ix(c0), FLOOR, ix(d), ix(c0 + w), SEAT)
        else:
            p = box(ix(IN - d), ix(c0), FLOOR, ix(IN), ix(c0 + w), SEAT)
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

# where the dock sits on DOCK_WALL: local x -> world Z (lower edge on the
# floor), local y -> world Y, local z -> inward from the wall. `s` is the
# inward X direction; the maps also flip local x with it so the transform never
# mirrors the mesh (a reflected warp turns a manifold inside out). The dock is
# symmetric in local x, so the same printed part fits either wall.
assert DOCK_WALL in ("left", "right"), DOCK_WALL
xw, s = (WALL, 1.0) if DOCK_WALL == "left" else (OUTER - WALL, -1.0)
dy0 = ix(ESP_Y)


def dock_to_world(m):
    return m.warp_batch(lambda a: np.column_stack(
        [xw + s * a[:, 2], dy0 + a[:, 1], FLOOR + DW / 2 - s * a[:, 0]]))


assert FLOOR + DW <= SEAT, "ESP32 dock too tall for the cavity"
# dovetail rail on the wall, neck at the wall, head inward; rises from the floor
rail = CrossSection([[[0, -DV_NECK / 2], [DV_DEPTH, -DV_HEAD / 2], [DV_DEPTH, DV_HEAD / 2],
                      [0, DV_NECK / 2]]], FillRule.NonZero)
rail_world = Manifold.extrude(rail, DW).warp_batch(
    lambda a: np.column_stack([xw - s * 0.01 + s * a[:, 0], dy0 + DL / 2 + s * a[:, 1], FLOOR + a[:, 2]]))
parts.append(rail_world)

# --- connector holes through the side walls, centred on the wall's height ---
SIDE_Z = H / 2
holes = list(SIDE_HOLES) + ([USB_HOLE] if USB_JACK else [])
post_spans = [(0.0, POST), (IN / 2 - BW / 2, IN / 2 + BW / 2), (IN - POST, IN)]
dock_span = (ESP_Y - 25.0, ESP_Y + DL)          # dock + USB plug path below it
assert all(ESP_Y + DL <= a or ESP_Y >= b for a, b in post_spans), "ESP32 dock hits a panel post"
placed = {"left": [], "right": []}
for h in holes:
    name, wall, cut, (ka, kz), y = h[:5]
    z = h[5] if len(h) > 5 else SIDE_Z
    ya, yb = y - ka / 2, y + ka / 2
    assert all(yb <= a or ya >= b for a, b in post_spans), f"{name}: hits a panel post"
    if wall == DOCK_WALL:
        assert yb <= dock_span[0] or ya >= dock_span[1], f"{name}: hits the ESP32 dock or its USB plug"
    assert all(yb <= a or ya >= b for a, b in placed[wall]), f"{name}: overlaps another hole"
    assert z - kz / 2 >= FLOOR and z + kz / 2 <= SEAT, f"{name}: keep-out does not fit between back plate and panel"
    placed[wall].append((ya, yb))
    x0 = -1.0 if wall == "left" else OUTER - WALL - 1.0     # start 1 mm outside the wall
    if isinstance(cut, tuple):
        ca, cz = cut
        cuts.append(box(x0, ix(y - ca / 2), z - cz / 2, x0 + WALL + 2, ix(y + ca / 2), z + cz / 2))
    else:
        cuts.append(Manifold.cylinder(WALL + 2, cut / 2, circular_segments=64)
                    .rotate([0, 90, 0]).translate([x0, ix(y), z]))

# --- stand pivots: hex pocket + shank hole through each side wall's block,
# and the ring of detent teeth on the outer wall face ---
PIV_Y, PIV_Z = OUTER / 2, H / 2                  # case centre; same line as the connectors
if STAND:
    assert HEX_POCKET_D + 2.0 <= BLOCK_IN, "pivot block too thin for the hex pocket"
    assert HEX_POCKET / np.cos(np.pi / 6) + 2 * 4.0 <= BLOCK_W, "pivot block too narrow for the hex pocket"
    assert PIV_Z - RING_OD / 2 > 0 and PIV_Z - HEX_POCKET / np.cos(np.pi / 6) / 2 > FLOOR, "pivot too low"
    assert PIV_Z + HEX_POCKET / np.cos(np.pi / 6) / 2 < SEAT, "hex pocket breaks into the panel seat"
    wall_ring = tooth_ring(0.0).translate([0, 0, -0.01])          # sunk 0.01 into the wall
    for wall in ("left", "right"):
        if wall == "right":
            face, inner, sgn = OUTER, OUTER - WALL - BLOCK_IN, 1.0
        else:
            face, inner, sgn = 0.0, WALL + BLOCK_IN, -1.0
        rot = [0, 90 * sgn, 0]                                   # +Z -> outward X
        parts.append(wall_ring.rotate(rot).translate([face, PIV_Y, PIV_Z]))
        # pocket from the block's inner face, then the shank hole out through the wall
        cuts.append(hex_prism(HEX_POCKET, HEX_POCKET_D + 0.01).rotate(rot)
                    .translate([inner - 0.01 * sgn, PIV_Y, PIV_Z]))
        cuts.append(Manifold.cylinder(WALL + BLOCK_IN + 2, SHANK_HOLE / 2, circular_segments=64)
                    .rotate(rot).translate([inner - 1.0 * sgn, PIV_Y, PIV_Z]))
    ring_span = (PIV_Y - WALL - RING_OD / 2 - 1, PIV_Y - WALL + RING_OD / 2 + 1)   # inner-Y span, 1 mm margin
    for h in holes:
        name, wall, cut, (ka, kz), y = h[:5]
        assert y + ka / 2 <= ring_span[0] or y - ka / 2 >= ring_span[1], f"{name}: hits the stand's tooth ring"

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
        cuts.append(box(ix(vx - 2), ix(IN / 2 - 35), -1, ix(vx + 2), ix(IN / 2 + 35), FLOOR + 1))

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


# ---------------- stand parts, each in its own print orientation ----------------
WEB = BLOCK_IN + WALL - HEX_POCKET_D          # pocket floor to outer wall face


def build_stud():
    """Head on the bed, thread up."""
    head = hex_prism(HEX_AF, HEX_H)
    shank = cyl(0, 0, HEX_H - 0.01, HEX_H + WEB + 0.01, SHANK_D)   # overlaps into the thread root
    thr = thread(THR_LEN).translate([0, 0, HEX_H + WEB])           # starts at the outer wall face
    tip = HEX_H + WEB + THR_LEN
    lead = cyl(0, 0, 0, tip - 1.3, THR_D + 2) + Manifold.cylinder(
        1.3, THR_D / 2, THR_D / 2 - THR_DEPTH, circular_segments=64).translate([0, 0, tip - 1.3])
    return head + shank + (thr ^ lead)


def build_knob():
    """Clamping face on the bed (z=0), blind female thread up into the body."""
    core = cyl(0, 0, 0, KNOB_H, KNOB_D - 6)
    lobes = [cyl((KNOB_D / 2 - 4.5) * np.cos(a), (KNOB_D / 2 - 4.5) * np.sin(a), 0, KNOB_H, 9.0)
             for a in np.linspace(0, 2 * np.pi, KNOB_LOBES, endpoint=False)]
    k = union([core] + lobes)
    k -= thread(KNOB_THREAD + 1, THR_CLR).translate([0, 0, -1])
    k -= Manifold.cylinder(1.0, THR_D / 2 + THR_CLR + 1.0, THR_D / 2 - THR_DEPTH + THR_CLR,
                           circular_segments=64).translate([0, 0, -0.01])    # mouth chamfer
    return k


X_IN = OUTER / 2 + ARM_GAP                    # yoke arm inner faces, from the case centre


def build_yoke():
    """Feet on the bed. x across the case, -y towards the LED face, z up;
    the pivot axis is x at (y=0, z=PIVOT_H)."""
    ps = []
    arm_ring = tooth_ring(180.0 / TEETH).translate([0, 0, -0.01])
    for sgn in (-1.0, 1.0):
        xa = X_IN if sgn > 0 else -X_IN - ARM_T                  # arm spans [xa, xa + ARM_T]
        disc = Manifold.cylinder(ARM_T, ARM_DISC / 2, circular_segments=96).rotate([0, 90, 0]) \
            .translate([xa, 0, PIVOT_H])
        base = box(xa, -ARM_W / 2, 0, xa + ARM_T, ARM_W / 2, FOOT_T)
        arm = Manifold.batch_hull([disc, base])
        arm -= Manifold.cylinder(ARM_T + 2, (THR_D + 0.6) / 2, circular_segments=64) \
            .rotate([0, 90, 0]).translate([xa - 1, 0, PIVOT_H])
        ps.append(arm)
        ps.append(arm_ring.rotate([0, -90 * sgn, 0]).translate([sgn * X_IN, 0, PIVOT_H]))
        xc = sgn * (X_IN + FOOT_W / 2 - 2.0)                     # foot: 2 mm under the case, rest outboard
        ps.append(Manifold.batch_hull([cyl(xc, -FOOT_FWD + FOOT_W / 2, 0, FOOT_T, FOOT_W),
                                       cyl(xc, FOOT_L - FOOT_FWD - FOOT_W / 2, 0, FOOT_T, FOOT_W)]))
    ps.append(box(-(X_IN - 1.0), -BAR_W / 2, 0, X_IN - 1.0, BAR_W / 2, FOOT_T))
    return union(ps)


def case_to_yoke(m, tilt_deg=0.0):
    """Place the case in the yoke frame, hanging on the pivots, leaning back by tilt_deg."""
    return m.translate([-OUTER / 2, -OUTER / 2, -H / 2]).rotate([90 + tilt_deg, 0, 0]) \
        .translate([0, 0, PIVOT_H])


# The case is turned 90 degrees about the pivot axis between its print frame
# and the yoke frame, and a hexagon only repeats every 60, so the stud is turned
# to match (in real life you drop it in at whichever of the six angles fits).
# The knob is turned by the thread phase between its own start, 1 mm inside its
# face, and the stud thread's start at the wall face, i.e. screwed home.
STUD_TURN = 90.0
KNOB_TURN = STUD_TURN + 360.0 * (ARM_GAP + ARM_T - 1.0) / THR_PITCH


def stud_in_yoke(sgn):
    x0 = sgn * (OUTER / 2 - WEB - HEX_H)                          # head sits on the pocket floor
    return build_stud().rotate([0, 0, STUD_TURN]).rotate([0, 90 * sgn, 0]).translate([x0, 0, PIVOT_H])


def knob_in_yoke(sgn):
    return build_knob().rotate([0, 0, KNOB_TURN]).rotate([0, 90 * sgn, 0]) \
        .translate([sgn * (X_IN + ARM_T), 0, PIVOT_H])


write_stl(model, OUT, "LED panel enclosure - make_enclosure.py")
write_stl(dock, DOCK_OUT, "ESP32 dock - make_enclosure.py")
if STAND:
    yoke, stud, knob = build_yoke(), build_stud(), build_knob()
    write_stl(yoke, YOKE_OUT, "tilt stand yoke - make_enclosure.py")
    write_stl(stud, STUD_OUT, "tilt stand stud, print 2 - make_enclosure.py")
    write_stl(knob, KNOB_OUT, "tilt stand knob, print 2 - make_enclosure.py")
    # teeth: mesh with a gap when half a pitch apart, collide when in phase
    ring, mate = tooth_ring(0.0), tooth_ring(180.0 / TEETH).mirror([0, 0, 1]).translate([0, 0, ARM_GAP])
    assert (ring ^ mate).volume() < 1e-6, "tooth rings interfere when meshed"
    assert (ring ^ mate.rotate([0, 0, 180.0 / TEETH])).volume() > 10, "tooth rings do not lock"
    # thread: a male thread generated from the same origin as the female clears it
    assert (thread(KNOB_THREAD).translate([0, 0, -1]) ^ knob).volume() < 1e-6, "thread clearance"
    for sgn in (-1.0, 1.0):
        st, kn = stud_in_yoke(sgn), knob_in_yoke(sgn)
        case0 = case_to_yoke(model)
        assert (st ^ case0).volume() < 1e-6, "stud does not fit the pocket / shank hole"
        assert (st ^ yoke).volume() < 1e-6, "stud does not pass the arm hole"
        assert (kn ^ st).volume() < 1e-6, "knob thread binds on the stud"
        assert (kn ^ yoke).volume() < 1e-6, "knob hits the yoke"
        assert (kn ^ case0).volume() < 1e-6, "knob hits the case"
        tip, blind = OUTER / 2 + THR_LEN, X_IN + ARM_T + KNOB_THREAD
        assert tip + 0.5 < blind, "stud bottoms out in the knob before the arm is clamped"
    # swing: the case never touches the yoke, and stays clear of the desk and the bar
    for t in range(-TILT_CHECK, TILT_CHECK + 1, 15):
        assert (case_to_yoke(model, t) ^ yoke).volume() < 1e-3, f"case hits the yoke at {t} degrees"
    for t in range(-TILT_CHECK, TILT_CHECK + 1, 5):
        zmin = case_to_yoke(model, t).bounding_box()[2]
        assert zmin >= FOOT_T + 4.0, f"case only {zmin - FOOT_T:.1f} mm above the cross bar at {t} degrees"
    yb = yoke.bounding_box()
    print(f"stand: yoke {yb[3] - yb[0]:.1f} x {yb[4] - yb[1]:.1f} x {yb[5] - yb[2]:.1f} mm, pivot {PIVOT_H:.0f} mm "
          f"above the desk, {360 // TEETH} degree steps, checked to +/-{TILT_CHECK} degrees; "
          f"knobs stick out {KNOB_H + ARM_T + ARM_GAP:.1f} mm per side")
dock_world = dock_to_world(dock)
assert (model ^ dock_world).volume() < 1e-3, "dock collides with the case"
db, rb = dock_world.bounding_box(), rail_world.bounding_box()
assert all(rb[i] >= db[i] - 0.02 for i in range(3)) and all(rb[i] <= db[i] + 0.02 for i in range(3, 6)), \
    "dovetail rail is not inside the dock's channel"   # 0.02: the rail is sunk 0.01 into the wall
print(f"outer {OUTER:.1f} x {OUTER:.1f} x {H:.1f} mm (LED face on top), "
      f"pocket {IN:.1f} mm, rim to posts {FRAME_D} mm, cavity {CAVITY} mm")
print(f"ESP32 dock on the {DOCK_WALL} wall, {ESP_Y + WALL:.1f}..{ESP_Y + DL + WALL:.1f} mm from the outer bottom edge")
for h in holes:
    name, wall, cut, _, y = h[:5]
    z = h[5] if len(h) > 5 else SIDE_Z
    what = f"{cut[0]:.1f} x {cut[1]:.1f} mm slot" if isinstance(cut, tuple) else f"o{cut:.1f} mm hole"
    print(f"  {name:8s} {wall:5s} wall: {what}, centre {y + WALL:.1f} mm up the outer edge, {z:.1f} mm from the back")
