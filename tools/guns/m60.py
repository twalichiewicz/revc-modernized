"""Vice City's M60 (the US Army's 7.62 mm general-purpose machine gun, belt fed from the left) for the first-person
viewmodel. Held at the hip in both hands: his right on the pistol grip, his left palm up under the forearm.

blender -b -P tools/guns/m60.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right; the butt's back at y = 0, the bore at z = 0. Real
dimensions (1105 mm long, the barrel 560 mm), in Vice City's blue-grey finish, scaled to the game's M60.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import (MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz)

REAL_LENGTH = 1105.0
RAKE = math.tan(math.radians(18))  # the pistol grip's rake back from vertical
GRIP_TOP = -55.0  # the trigger housing's underside, where the grip meets it


def front_strap_y(z):
    return 395.0 + (z - GRIP_TOP) * RAKE


def back_strap_y(z):
    return 347.0 + (z - GRIP_TOP) * RAKE


# The receiver, its feed cover, the barrel socket and the forearm (handguard): y from, y to, z from, z to, half width.
RECEIVER = (270.0, 545.0, -40.0, 28.0, 25.0)
COVER = (415.0, 545.0, 28.0, 50.0, 27.0)
SOCKET = (545.0, 566.0, -30.0, 26.0, 22.0)
HOUSING = (330.0, 480.0, GRIP_TOP, -40.0, 16.0)
FOREARM = (566.0, 790.0, -58.0, 4.0, 31.0)
BARREL_R, FLASH = 12.0, (1040.0, 1105.0, 13.0)
GRIP_W = 15.0  # the grip's half width over its panels

# The belt: its middle line where it leaves the feed tray on his left and hangs (x, z), the rounds along y.
BELT_PATH = [(-18.0, 20.0), (-34.0, 18.0), (-44.0, 8.0), (-48.0, -14.0), (-48.0, -54.0), (-44.0, -88.0), (-38.0, -108.0)]
BELT_Y = (470.0, 541.0)
ROUND_R, PITCH = 6.0, 12.5


def forearm_section(inset=0.0):
    z0, z1, w = FOREARM[2] + inset, FOREARM[3] - inset, FOREARM[4] - inset
    return [(-w, z0 + 12), (-w + 7, z0), (w - 7, z0), (w, z0 + 12), (w, z1 - 8), (w - 5, z1), (-w + 5, z1), (-w, z1 - 8)]


def receiver_section():
    """Across the receiver (x, z): its bottom edges chamfered."""
    y0, y1, z0, z1, w = RECEIVER
    return [(-w + 4, z0), (w - 4, z0), (w, z0 + 4), (w, z1 - 3), (w - 3, z1), (-w + 3, z1), (-w, z1 - 3), (-w, z0 + 4)]


def stock_outline():
    return [(0, -122), (0, 24), (3, 26), (RECEIVER[0] + 4, 28), (RECEIVER[0] + 4, -40), (150, -78), (14, -122)]


def grip_outline(rounding=1):
    z0 = -165.0
    return fillet([(front_strap_y(GRIP_TOP), GRIP_TOP), (front_strap_y(z0 + 6), z0 + 6), (front_strap_y(z0) - 6, z0),
                   (back_strap_y(z0) + 4, z0), (back_strap_y(z0 + 8), z0 + 8), (back_strap_y(GRIP_TOP), GRIP_TOP)],
                  [0, 4, 3, 3, 4, 0], rounding)


GUARD = [(400, GRIP_TOP), (458, GRIP_TOP), (458, -80), (447, -96), (385, -96)]
GUARD_OPENING = [(405, -50), (451, -50), (451, -80), (442, -89), (388, -89)]
TRIGGER = [(410, -55), (418, -55), (419.5, -68), (415, -83), (411, -81), (413, -68)]
HANDLE = (25.0, 47.0, 500.0, 515.0, -16.0, -4.0)  # the cocking handle on the right: x, y, z spans


def belt_points(path, step):
    """Points every `step` mm along a polyline, with the direction there."""
    out, carry = [], 0.0
    for (ax, az), (bx, bz) in zip(path, path[1:]):
        d = Vector((bx - ax, bz - az))
        length = d.length
        t = carry
        while t <= length:
            p = Vector((ax, az)) + d * (t / length)
            out.append((p, d.normalized()))
            t += step
        carry = t - length
    return out


def belt_strip(half):
    """The belt's outline across (x, z): the path offset `half` mm each way."""
    left, right = [], []
    for i, p in enumerate(BELT_PATH):
        a = Vector(BELT_PATH[max(i - 1, 0)])
        b = Vector(BELT_PATH[min(i + 1, len(BELT_PATH) - 1)])
        d = (b - a).normalized()
        n = Vector((-d.y, d.x))
        left.append(tuple(Vector(p) + n * half))
        right.append(tuple(Vector(p) - n * half))
    return left + right[::-1]


# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# The side is shot in two halves (the butt to the barrel socket, the socket to the muzzle) for the detail.
REAR_MMPP, FRONT_MMPP = 576 / 256, 570 / 256
REGIONS = {
    "left_rear": ((0, 0, 256, 104), (-400, 284, -57), (1, 0, 0), (0, 0, 1), REAR_MMPP),
    "left_front": ((0, 104, 256, 48), (-400, 825, -10.5), (1, 0, 0), (0, 0, 1), FRONT_MMPP),
    "top_rear": ((0, 152, 256, 36), (0, 284, 400), (0, 0, -1), (-1, 0, 0), REAR_MMPP),
    "top_front": ((0, 188, 256, 30), (0, 825, 400), (0, 0, -1), (-1, 0, 0), FRONT_MMPP),
    "back": ((0, 218, 100, 38), (-15, -400, -52), (0, 1, 0), (1, 0, 0), 2.4),
    "front": ((100, 218, 60, 38), (0, 1400, -10), (0, -1, 0), (1, 0, 0), 1.8),
    "belt": ((160, 218, 96, 38), (-400, 505.5, -55), (1, 0, 0), (0, 1, 0), 2.0),
}
SPLIT_Y = 560.0  # faces behind this wear the rear photos, in front the front ones

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface). His right hand as on the Colt:
# the index on the trigger's face, the others on the grip's left side 30 mm behind its front strap, the thumb along
# the trigger housing. His left palm up under the forearm, its fingers round the far (right) side, the thumb along
# the near side pointing forward.
FINGER_R = 8.0
WEB = (0.0, back_strap_y(-60.0), -60.0)  # where the web of his right hand rests, under the housing
PALM = (-6.0, 680.0, FOREARM[2] - 3.0)  # his left palm, under the forearm
LEFT_ROLL = 0.0


def on_grip(z, back=30.0):
    return (-GRIP_W - FINGER_R, front_strap_y(z) - back, z)


FORWARD = (0.0, 20.0, 20.0 * RAKE)  # slack: towards the front strap, as the Colt's
SIDE_R = FOREARM[4] + FINGER_R
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, TRIGGER[2][0] + FINGER_R - 6.0, -68.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(-90.0), None, None, FORWARD),
    ("r_ring", on_grip(-107.0), None, None, FORWARD),
    ("r_pinky", on_grip(-123.0, 32.0), None, None, FORWARD),
    ("r_thumb", (-HOUSING[4] - FINGER_R, 387.0, -49.0), None, None, (0.0, -12.0, 0.0)),
    ("l_index", (SIDE_R, PALM[1] + 26, -24.0), None, None, (0.0, 0.0, 12.0)),
    ("l_middle", (SIDE_R, PALM[1] + 7, -22.0), None, None, (0.0, 0.0, 12.0)),
    ("l_ring", (SIDE_R, PALM[1] - 12, -24.0), None, None, (0.0, 0.0, 12.0)),
    ("l_pinky", (SIDE_R, PALM[1] - 29, -30.0), None, None, (0.0, 0.0, 12.0)),
    ("l_thumb", (-SIDE_R - 1.0, PALM[1] + 40, -18.0), None, (-1.0, 0.0, -0.3), (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, FLASH[1], 0), (0, 556, 52), (0, 1024, 36)
# Seen from: the eye up and to the left of the receiver, the gun at his hip low on the right, its muzzle short of
# the crosshair; aiming, 0.30 m behind the rear sight (the stock's long) and 0.02 m over the sight line.
FRAMING = ((-0.230, -0.005, 0.113), 1.0, 0.0, 0.3, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
# His left arm runs back from the handguard out to his left (as kit.LEFT_FOREARM) and steeper down, as at the hip:
# into the view from the lower left, its upper arm out of sight.
LEFT_FOREARM = (-0.431, -0.650, -0.626)  # turned with the levelled framing: runs where it did in the view


def detailed(mats):
    """The photo shoot's M60: every part the texture shows. Returns (gun, belt)."""
    park, poly, dark, grip, steel = mats["park"], mats["poly"], mats["dark"], mats["grip"], mats["steel"]
    parts = []
    # The butt and its plate, the stock with a sling swivel and the shoulder rest folded on top.
    stock = profile_yz("stock", fillet(stock_outline(), [0, 0, 3, 0, 0, 30, 6], 3), -21, 21, poly)
    bevel(stock, 2.5, 2, 30)
    parts.append(stock)
    plate = profile_yz("buttplate", fillet([(-0.1, -124), (-0.1, 27), (7, 27), (7, -124)], [3, 3, 0, 0], 2), -23, 23, dark)
    cut(plate, join("grooves", [box("groove", -30, 30, -2, 1.2, z, z + 2.2) for z in range(-118, 22, 7)]))
    parts.append(plate)
    parts.append(profile_yz("rest", fillet([(8, 26), (82, 27), (82, 30.5), (8, 30.5)], [0, 2, 2, 0], 2), -15, 15, park))
    parts.append(cylinder_x("swivel", 60, -108, 4.5, -4, 4, 10, steel))
    # The receiver: sheet steel, riveted, a rail along its foot, the feed tray's mouth on the left.
    y0, y1, z0, z1, w = RECEIVER
    rec = profile_xz("receiver", fillet(receiver_section(), [1, 1, 1, 1, 0, 0, 1, 1], 2), y0, y1, park)
    cut(rec, box("tray", -w - 2, -w + 4, 466, y1 + 1, 6, z1 - 2))
    bevel(rec, 0.6)
    parts.append(rec)
    for sx in (-1, 1):
        parts.append(box("rail", sx * w - 1.2, sx * w + 1.2, y0 + 8, y1 - 4, z0 + 6, z0 + 11, park))
        for y in range(int(y0) + 22, int(y1) - 10, 34):
            for z in (z0 + 16, z1 - 9):
                parts.append(cylinder_x("rivet", y, z, 2.2, sx * w - 0.8 * (sx < 0), sx * w + 0.8 * (sx > 0), 8, steel))
    # The feed cover with its ribs and latch; the barrel socket with the rear sight, its knob and the barrel lock.
    y0, y1, z0, z1, w = COVER
    cover = profile_xz("cover", fillet([(-w, z0), (w, z0), (w, z1 - 8), (w - 8, z1), (-w + 8, z1), (-w, z1 - 8)],
                                       [0, 0, 2, 3, 3, 2], 3), y0, y1, park)
    bevel(cover, 0.6)
    parts.append(cover)
    for x in (-12, 0, 12):
        parts.append(box("rib", x - 1.5, x + 1.5, y0 + 10, y1 - 8, z1 - 1.5, z1 + 1.2, park))
    parts.append(profile_yz("latch", fillet([(y0 - 12, z0), (y0 + 2, z0), (y0 + 2, z0 + 19), (y0 - 8, z0 + 17)],
                                            [0, 0, 2, 2], 2), -11, 11, dark))
    y0, y1, z0, z1, w = SOCKET
    sock = profile_xz("socket", fillet([(-w, z0), (w, z0), (w, z1), (-w, z1)], [3, 3, 2, 2], 2), y0, y1, park)
    bevel(sock, 0.6)
    parts.append(sock)
    leaf = profile_yz("rear_sight", [(y0 + 3, z1), (y1 - 2, z1), (y1 - 4, 54), (y0 + 6, 54)], -9, 9, dark)
    cut(leaf, cylinder_y("aperture", 0, 48, 2.0, y0, y1, 8))
    parts.append(leaf)
    parts.append(cylinder_x("knob", 556, 34, 7, -16, -9.5, 12, dark))
    parts.append(profile_yz("lock", fillet([(y0 + 2, -26), (y1 - 2, -26), (y1 - 4, -6), (y0 + 4, -6)], 2, 2),
                            -w - 3, -w, dark))
    # The trigger housing, its guard and trigger, the safety; the pistol grip with chequered panels.
    y0, y1, z0, z1, w = HOUSING
    house = box("housing", -w, w, y0, y1, z0, z1, park)
    bevel(house, 1.0)
    parts.append(house)
    parts.append(cylinder_x("safety", 452, -47, 4, -w - 2.5, -w, 10, dark))
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 8, 6, 0], 3), -4.5, 4.5, park)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 6, 5, 0], 3), -10, 10))
    bevel(guard, 0.5)
    parts.append(guard)
    trigger = profile_yz("trigger", TRIGGER, -3.2, 3.2, dark)
    core = profile_yz("grip", grip_outline(3), -GRIP_W + 2.2, GRIP_W - 2.2, park)
    bevel(core, 1.0)
    parts.append(core)
    panel = fillet([(front_strap_y(GRIP_TOP - 4) - 4, GRIP_TOP - 4), (front_strap_y(-158) - 4, -158),
                    (back_strap_y(-158) + 4, -158), (back_strap_y(GRIP_TOP - 4) + 4, GRIP_TOP - 4)], 4, 2)
    for x0, x1 in ((GRIP_W - 2.4, GRIP_W), (-GRIP_W, -GRIP_W + 2.4)):
        p = profile_yz("panel", panel, x0, x1, grip)
        bevel(p, 1.0, 1, 30)
        parts.append(p)
    # The forearm: ribbed, a row of vents high on each side, steel bands at its ends.
    y0, y1 = FOREARM[0], FOREARM[1]
    fore = profile_xz("forearm", fillet(forearm_section(), [6, 4, 4, 6, 3, 3, 3, 3], 3), y0, y1, poly)
    cutters = []
    for k in range(7):
        y = y0 + 28 + k * 27
        for x0, x1 in ((-40, -FOREARM[4] + 3.5), (FOREARM[4] - 3.5, 40)):
            cutters.append(profile_yz("vent", fillet([(y, -18), (y + 15, -18), (y + 15, -8), (y, -8)], 4.5, 3), x0, x1))
    for z in (-34, -44):
        for sx in (-1, 1):
            cutters.append(box("groove", sx * FOREARM[4] - 1.2, sx * FOREARM[4] + 1.2, y0 + 14, y1 - 14, z, z + 2.5))
    cut(fore, join("cutters", cutters))
    bevel(fore, 0.8)
    parts.append(fore)
    for y in (y0, y1 - 8):
        parts.append(profile_xz("band", forearm_section(-1.0), y, y + 8, steel))
    # The barrel and its gas cylinder, the bipod folded forward under it, the front sight, the flash hider.
    parts.append(cylinder_y("barrel", 0, 0, BARREL_R, SOCKET[0], FLASH[0], 20, dark))
    parts.append(cylinder_y("gas", 0, -26, 9, FOREARM[1] - 10, 905, 14, park))
    yoke = box("yoke", -16, 16, 895, 915, -40, -12, park)
    bevel(yoke, 2.0, 2)
    parts.append(yoke)
    for sx in (-1, 1):
        leg = box("leg", min(sx * 7, sx * 14), max(sx * 7, sx * 14), 915, 1028, -45, -36, park)
        cut(leg, join("notches", [box("notch", -20, 20, y, y + 2.5, -47, -44) for y in range(930, 1015, 9)]))
        parts.append(leg)
        parts.append(box("foot", min(sx * 6, sx * 15), max(sx * 6, sx * 15), 1022, 1034, -48, -34, dark))
    parts.append(cylinder_y("sight_band", 0, 0, BARREL_R + 2.5, 1010, 1030, 20, dark))
    parts.append(profile_yz("front_sight", [(1014, 8), (1030, 8), (1028, 36), (1020, 36)], -1.6, 1.6, dark))
    for sx in (-1, 1):
        parts.append(profile_yz("ear", [(1012, 8), (1030, 8), (1029, 33), (1016, 30)], sx * 7, sx * 9.5, dark))
    flash = cylinder_y("flash", 0, 0, FLASH[2], FLASH[0], FLASH[1], 20, dark)
    slots = [profile_xz("slot", [(-1.8, 4), (1.8, 4), (1.8, 20), (-1.8, 20)], FLASH[0] + 18, FLASH[1] + 2),
             profile_xz("slot", [(-1.8, -20), (1.8, -20), (1.8, -4), (-1.8, -4)], FLASH[0] + 18, FLASH[1] + 2),
             profile_xz("slot", [(-20, -1.8), (-4, -1.8), (-4, 1.8), (-20, 1.8)], FLASH[0] + 18, FLASH[1] + 2),
             profile_xz("slot", [(4, -1.8), (20, -1.8), (20, 1.8), (4, 1.8)], FLASH[0] + 18, FLASH[1] + 2)]
    cut(flash, join("slots", slots + [cylinder_y("bore", 0, 0, 9.0, FLASH[0] + 6, FLASH[1] + 2, 14)]))
    parts.append(flash)
    # The cocking handle (it rides on the right).
    hx0, hx1, hy0, hy1, hz0, hz1 = HANDLE
    handle = join("handle", [box("stem", hx0 - 1, hx1 - 8, hy0 + 3, hy1 - 3, hz0 + 3, hz1 - 3),
                             cylinder_x("knob", (hy0 + hy1) / 2, (hz0 + hz1) / 2, 7, hx1 - 9, hx1, 10)])
    handle.data.materials.clear()
    handle.data.materials.append(dark)
    for p in parts + [trigger, handle]:
        finish(p, 35)
    gun = join("m60", parts + [trigger, handle])
    return gun, belt(mats)


def belt(mats):
    """The belt: 7.62 mm rounds (brass cases, gilded bullets) in black steel links, along BELT_PATH."""
    brass, copper, dark = mats["brass"], mats["copper"], mats["dark"]
    pieces = []
    y0, y1 = BELT_Y
    for p, d in belt_points(BELT_PATH, PITCH):
        x, z = p
        pieces.append(cylinder_y("case", x, z, ROUND_R, y0, y0 + 46, 12, brass))
        pieces.append(cylinder_y("shoulder", x, z, ROUND_R - 1.2, y0 + 46, y0 + 51, 12, brass))
        pieces.append(cylinder_y("bullet", x, z, 3.9, y0 + 51, y1 - 6, 10, copper))
        pieces.append(cylinder_y("tip", x, z, 2.4, y1 - 6, y1, 8, copper))
        for ly in (y0 + 8, y0 + 34):
            pieces.append(cylinder_y("link", x, z, ROUND_R + 0.9, ly, ly + 7, 12, dark))
        n = Vector((-d.y, d.x))
        c = p + n * (ROUND_R + 0.5)
        pieces.append(box("tab", c.x - 1.2, c.x + 1.2, y0 + 8, y0 + 41, c.y - 1.2, c.y + 1.2, dark))
    for p in pieces:
        finish(p, 35)
    return join("belt", pieces)


def low():
    """The M60 at Vice City's detail, in its moving parts: {part: [objects]}."""
    y0, y1, z0, z1, w = RECEIVER
    body = [profile_yz("stock", stock_outline(), -21, 21),
            profile_yz("buttplate", [(-0.1, -124), (-0.1, 27), (7, 27), (7, -124)], -23, 23),
            profile_xz("receiver", receiver_section(), y0, y1)]
    y0, y1, z0, z1, w = COVER
    body += [profile_xz("cover", [(-w, z0), (w, z0), (w, z1 - 8), (w - 8, z1), (-w + 8, z1), (-w, z1 - 8)], y0, y1),
             profile_yz("latch", [(y0 - 12, z0), (y0 + 2, z0), (y0 + 2, z0 + 19), (y0 - 8, z0 + 17)], -11, 11)]
    y0, y1, z0, z1, w = SOCKET
    body += [box("socket", -w, w, y0, y1, z0, z1),
             profile_yz("rear_sight", [(y0 + 3, z1), (y1 - 2, z1), (y1 - 4, 54), (y0 + 6, 54)], -9, 9)]
    y0, y1, z0, z1, w = HOUSING
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 8, 6, 0], 1), -4.5, 4.5)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 6, 5, 0], 1), -10, 10), solver="EXACT")
    body += [box("housing", -w, w, y0, y1, z0, z1), guard, profile_yz("grip", grip_outline(1), -GRIP_W, GRIP_W),
             profile_xz("forearm", forearm_section(), FOREARM[0], FOREARM[1]),
             cylinder_y("barrel", 0, 0, BARREL_R, SPLIT_Y, FLASH[0], 8),
             cylinder_y("flash", 0, 0, FLASH[2], FLASH[0], FLASH[1], 8),
             cylinder_y("gas", 0, -26, 9, FOREARM[1], 895, 6),
             box("yoke", -16, 16, 895, 915, -40, -12),
             box("leg_l", -14, -7, 915, 1034, -45, -36), box("leg_r", 7, 14, 915, 1034, -45, -36),
             profile_yz("front_sight", [(1012, 8), (1030, 8), (1029, 36), (1018, 36)], -8, 8),
             profile_xz("belt", belt_strip(ROUND_R), BELT_Y[0], BELT_Y[1])]
    hx0, hx1, hy0, hy1, hz0, hz1 = HANDLE
    bolt = [box("handle", hx0 - 1, hx1, hy0, hy1, hz0, hz1)]
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    return {"body": body, "bolt": bolt, "trigger": trigger}


def fits(region, verts, margin=0.6):
    (x0, y0, w, h) = region[0]
    for v in verts:
        u, t = kit.region_uv(region, v)
        px, py = u * kit.SIZE, (1 - t) * kit.SIZE
        if not (x0 - margin <= px <= x0 + w + margin and y0 - margin <= py <= y0 + h + margin):
            return False
    return True


def project(ob, prefer):
    """UVs for a low-poly object (kit.project, but each face takes the first region it fits whole of those
    `prefer(normal, centre)` lists, the last if none)."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        verts = [me.vertices[i].co / MM for i in poly.vertices]
        centre = sum(verts, Vector()) / len(verts)
        names = prefer(poly.normal, centre)
        name = next((n for n in names if fits(REGIONS[n], verts)), names[-1])
        for li in poly.loop_indices:
            uv.data[li].uv = kit.region_uv(REGIONS[name], me.vertices[me.loops[li].vertex_index].co / MM)


def choose(normal, centre):
    """Which photos a face of the gun may wear, best first: the top, back or front where it faces them, the left side
    otherwise (the right mirrors it); the rear photos behind the barrel socket, the front ones ahead."""
    half = "rear" if centre.y < SPLIT_Y else "front"
    side = "left_" + half
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return ["top_" + half, side]
    if ax == 1 and normal.y < 0:
        return ["back", side]
    if ax == 1 and normal.y > 0:
        return ["front", side]
    return [side]


def choose_belt(normal, centre):
    return ["belt"]


def materials():
    mats = kit.photo_materials()
    mats["park"] = material("parkerized", (0.30, 0.33, 0.38), metallic=0.6, roughness=0.45)
    mats["poly"] = material("polymer", (0.14, 0.15, 0.165), roughness=0.55, spec=0.4)
    mats["brass"] = material("brass", (0.80, 0.56, 0.24), metallic=1.0, roughness=0.3)
    mats["copper"] = material("gilding", (0.72, 0.42, 0.24), metallic=1.0, roughness=0.3)
    return mats


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "m60", REAL_LENGTH)
    hold = kit.hold_for(WEB, scale)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    gun, belt_ob = detailed(materials())
    both = [gun, belt_ob]
    rgba = kit.atlas(scene, REGIONS, {"left_rear": [gun], "left_front": [gun], "top_rear": [gun], "top_front": [gun],
                                      "back": both, "front": both, "belt": [belt_ob]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "m60-atlas.png")
    kit.save_png(atlas_png, flat)
    gun.hide_render = belt_ob.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            project(ob, choose_belt if ob.name.startswith("belt") else choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, hold, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING, two_handed=True, hold_left=hold_left, left_forearm=LEFT_FOREARM)
    print(f"m60: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in hold[3]]}")
    kit.preview(os.path.join(work, "m60-preview.png"), [o for o, _ in objects], atlas_png,
                [((-1.1, 0.55, 0.12), (0, 0.55, -0.04), 50), ((0.9, 1.1, 0.5), (0, 0.5, -0.02), 50),
                 ((-0.30, 0.0, 0.26), (0.0, 0.7, -0.05), 35)])


main()
