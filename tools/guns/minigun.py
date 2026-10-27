"""Vice City's minigun (a hand-held M134: six 7.62 mm barrels turned by an electric motor) for the first-person
viewmodel. Held at the hip in both hands: his right on the upright grip at the back of the frame (its trigger under
the top bar), his left over the left end of the handlebar across the top, as the game's model has them.

blender -b -P tools/guns/minigun.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrels, z up, x to the right; the frame's back at y = 0, the barrels' axis at
z = 0. Real dimensions (the M134 801 mm long with its 559 mm barrels, and the hand-held frame behind it), scaled to
the game's model (the game's `minigun` is the frame and handles, `minigun2` its barrels). The belt feeds from the
left as the M134's does (the game's comes in on the right, out of his sight).
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, _object, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

REAL_LENGTH = 900.0
RAKE = math.tan(math.radians(15))  # the upright grip's rake back from vertical
POST_BOTTOM, POST_TOP, POST_DEPTH, POST_W = 24.0, 140.0, 44.0, 15.0


def back_strap_y(z):
    return 26.0 + (z - POST_BOTTOM) * RAKE


def front_strap_y(z):
    return back_strap_y(z) + POST_DEPTH


HOUSING = (130.0, 330.0, 68.0)  # y from, y to, radius
DOME = (108.0, 130.0, 54.0)
PLATE = (330.0, 345.0, 60.0)  # the bearing plate the barrels turn in
BARREL_RING, BARREL_R, BARRELS_Y = 38.0, 10.0, (340.0, 900.0)
MID_CLAMP = (596.0, 618.0, 52.0)
FRONT_CLAMP = (790.0, 900.0, 56.0)
REAR_TUBE = (8.0, 112.0, 24.0)  # y from, y to, radius: the frame's bottom rail, on the axis
TOP_BAR = (back_strap_y(POST_TOP) - 10, 330.0, POST_TOP, POST_TOP + 16, 11.0)  # y from, to, z from, to, half width
STRUTS = [(190.0, 210.0), (300.0, 316.0)]  # the top bar's legs on the housing (y spans)
HANDLEBAR = (200.0, 104.0, 12.0, 160.0)  # across the rear leg: y, z, radius, half its span; gripped at its ends
FEEDER = (-72.0, -26.0, 46.0, 172.0, 322.0)  # x, z, radius, y from, y to: on the left
MOTOR = (66.0, -30.0, 34.0, 150.0, 310.0)  # on the right
TRIGGER = [(back_strap_y(133) + 67, POST_TOP), (back_strap_y(133) + 73, POST_TOP), (back_strap_y(133) + 73.5, 125),
           (back_strap_y(133) + 69, 114), (back_strap_y(133) + 65.5, 116), (back_strap_y(133) + 68, 127)]
GUARD_Y = back_strap_y(133)
GUARD = [(GUARD_Y + 40, POST_TOP), (GUARD_Y + 104, POST_TOP), (GUARD_Y + 104, 124), (GUARD_Y + 93, 110), (GUARD_Y + 34, 110)]
GUARD_OPENING = [(GUARD_Y + 46, POST_TOP + 4), (GUARD_Y + 97, POST_TOP + 4), (GUARD_Y + 97, 125), (GUARD_Y + 89, 116),
                 (GUARD_Y + 38, 116)]
# The belt: its middle line out of the feeder's foot and hanging (x, z), the rounds along y.
BELT_PATH = [(-72.0, -60.0), (-76.0, -90.0), (-78.0, -130.0), (-74.0, -170.0), (-66.0, -196.0)]
BELT_Y = (212.0, 283.0)
ROUND_R, PITCH = 6.0, 12.5


def lathe(name, axis, centre, profile, segments, mat=None, turn=0.5, closed=False, caps=True):
    """A solid turned round an axis along y or z through `centre` (x, z or x, y): profile [(along, radius)], with a
    radius of 0 closing an end. With `turn` 0.5 a face looks straight along each of the other axes. `closed`: the
    profile is a loop (a ring); `caps`: close open ends with a face."""
    bm = bmesh.new()
    rings = []
    for t, r in profile:
        ring = []
        for k in range(segments if r > 0 else 1):
            a = 2 * math.pi * (k + turn) / segments
            c, s = (r * math.cos(a), r * math.sin(a)) if r > 0 else (0.0, 0.0)
            p = (centre[0] + c, t, centre[1] + s) if axis == "y" else (centre[0] + c, centre[1] + s, t)
            ring.append(bm.verts.new(Vector(p) * MM))
        rings.append(ring)
    for a, b in zip(rings, rings[1:] + (rings[:1] if closed else [])):
        if len(a) == 1 and len(b) == 1:
            continue
        for k in range(segments):
            k1 = (k + 1) % segments
            if len(a) == 1:
                bm.faces.new((a[0], b[k], b[k1]))
            elif len(b) == 1:
                bm.faces.new((a[k], b[0], a[k1]))
            else:
                bm.faces.new((a[k], b[k], b[k1], a[k1]))
    for ring in (rings[0], rings[-1]) if caps and not closed else ():
        if len(ring) > 2:
            bm.faces.new(ring)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def tube_y(name, x, z, r, y0, y1, segments, mat=None, open_ends=False):
    """A cylinder along y; `open_ends` leaves its ends off (they're inside something)."""
    profile = [(y0, r), (y1, r)] if open_ends else [(y0, 0), (y0, r), (y1, r), (y1, 0)]
    return lathe(name, "y", (x, z), profile, segments, mat, 0.0, caps=not open_ends)


def belt_points(path, step):
    out, carry = [], 0.0
    for (ax, az), (bx, bz) in zip(path, path[1:]):
        d = Vector((bx - ax, bz - az))
        length = d.length
        t = carry
        while t <= length:
            out.append((Vector((ax, az)) + d * (t / length), d.normalized()))
            t += step
        carry = t - length
    return out


def strip(path, half):
    """A path (2D) widened `half` mm each way, as a closed outline."""
    left, right = [], []
    for i, p in enumerate(path):
        a, b = Vector(path[max(i - 1, 0)]), Vector(path[min(i + 1, len(path) - 1)])
        d = (b - a).normalized()
        n = Vector((-d.y, d.x))
        left.append(tuple(Vector(p) + n * half))
        right.append(tuple(Vector(p) - n * half))
    return left + right[::-1]


def post_outline(rounding=1):
    return fillet([(back_strap_y(POST_BOTTOM - 6), POST_BOTTOM - 6), (front_strap_y(POST_BOTTOM - 6), POST_BOTTOM - 6),
                   (front_strap_y(POST_TOP), POST_TOP), (back_strap_y(POST_TOP), POST_TOP)], [0, 0, 0, 0], rounding)


def barrel_centres():
    return [(BARREL_RING * math.cos(math.pi / 6 + k * math.pi / 3), BARREL_RING * math.sin(math.pi / 6 + k * math.pi / 3))
            for k in range(6)]


# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
REGIONS = {
    "left": ((0, 0, 128, 128), (-400, 170, -22), (1, 0, 0), (0, 0, 1), 2.85),
    "back": ((128, 0, 80, 128), (-9, -400, -22), (0, 1, 0), (0, 0, 1), 2.85),
    "front": ((208, 0, 48, 48), (0, 1400, 0), (0, -1, 0), (0, 0, 1), 2.35),
    "top": ((0, 128, 128, 80), (-9, 170, 400), (0, 0, -1), (-1, 0, 0), 2.75),
    "handle": ((128, 128, 128, 40), (0, 175, 400), (0, 0, -1), (-1, 0, 0), 2.6),
    "grip": ((128, 168, 64, 40), (-400, 75, 85), (1, 0, 0), (0, 0, 1), 3.0),
    "handlebar": ((192, 168, 64, 40), (0, HANDLEBAR[0], 400), (0, 0, -1), (0, 1, 0), 6.0),
    "barrels": ((0, 208, 256, 48), (-400, 622, 0), (1, 0, 0), (0, 0, 1), 2.35),
}

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface). His right hand round the
# upright grip as round the Colt's: the index on the trigger under the top bar, the others on the grip's left side
# 30 mm behind its front, the thumb along the bar. His left hand over the handlebar's left end, its fingers over and
# round the far side, the thumb round under it from the near side.
FINGER_R = 8.0
WEB = (0.0, back_strap_y(133.0), 133.0)
# His left hand over the handlebar's left end as over a motorbike's: along it (wrist to knuckles) forward and a
# little down, the palm down on the bar (it lies across his finger roots, 15 mm on from the palm's middle).
HB_Y, HB_Z, HB_R, HB_HALF = HANDLEBAR
HAND_X = -116.0
LEFT_AXES = ((0.0, 0.9, -0.44), (0.0, -0.44, -0.9))
PALM = (HAND_X, HB_Y - 18.5, HB_Z + 17.5)
# His left forearm out to his left and down from the handlebar's end, clear of the barrels.
LEFT_FOREARM = (-0.35, -0.55, -0.9)  # down under the carrying handle: levelled, his shoulder came into view


def hold_left(palm_mm, scale, along, palm_side):
    """His left hand's hold for any grip: the palm at `palm_mm` (gun frame), the hand along `along` from its wrist,
    its palm towards `palm_side` (kit.hold_left_for only turns it about the barrel, palm up)."""
    hx = Vector(along).normalized()
    hy = Vector(palm_side)
    hy = (hy - hx * hy.dot(hx)).normalized()
    hz = hx.cross(hy)
    palm = Vector(palm_mm) * MM * scale
    wrist = palm - hx * kit.PALM_IN_HAND[0] - hy * kit.PALM_IN_HAND[1] - hz * kit.PALM_IN_HAND[2]
    rows = (hx, hy, hz)
    return [tuple(row[i] for row in rows) for i in range(3)] + [tuple(-row.dot(wrist) for row in rows)]


def on_post(z, back=30.0):
    return (-POST_W - FINGER_R, front_strap_y(z) - back, z)


FORWARD = (0.0, 20.0, 20.0 * RAKE)
BAR_R = TOP_BAR[4]
BAR_Z = (TOP_BAR[2] + TOP_BAR[3]) / 2
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, TRIGGER[2][0] + FINGER_R - 4.0, 128.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_post(103.0), None, None, FORWARD),
    ("r_ring", on_post(86.0), None, None, FORWARD),
    ("r_pinky", on_post(70.0, 32.0), None, None, FORWARD),
    ("r_thumb", (-BAR_R - FINGER_R, back_strap_y(133) + 42, 138.0), None, None, (0.0, -12.0, 0.0)),
    # Over the bar and round under its front, the index nearest the gun (by the thumb), the thumb round under
    # from behind.
    ("l_index", (HAND_X + 26, HB_Y + 12.9, HB_Z - 15.3), None, None, (0.0, 0.0, 8.0)),
    ("l_middle", (HAND_X + 8, HB_Y + 13.5, HB_Z - 15.3), None, None, (0.0, 0.0, 8.0)),
    ("l_ring", (HAND_X - 11, HB_Y + 12.9, HB_Z - 15.3), None, None, (0.0, 0.0, 8.0)),
    ("l_pinky", (HAND_X - 28, HB_Y + 12.0, HB_Z - 14.0), None, None, (0.0, 0.0, 8.0)),
    ("l_thumb", (HAND_X + 30, HB_Y - 12.9, HB_Z - 15.3), None, (0.0, -1.0, -0.5), (6.0, 0.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("barrels", (0, (BARRELS_Y[0] + BARRELS_Y[1]) / 2, 0), (0, 1, 0)),
]
# The muzzle: the barrels' ends, at their axis. Aimed, it's looked along over the top bar.
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, BARRELS_Y[1], 0), (0, 200, TOP_BAR[3] + 6), (0, BARRELS_Y[1], TOP_BAR[3] + 6)
# Seen from: high, left of and behind the grip, the gun at his hip low on the right with the barrels running in
# beyond his left hand towards (short of) the crosshair; aiming, 0.30 m behind the top bar and 0.02 m over it.
FRAMING = ((-0.300, -0.384, 0.224), 0.0, 0.0, 0.3, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)


def weathered(name, dark, light, scale=60.0, metallic=0.7, roughness=0.5):
    """A metal finish, mottled (the photo's grain: Vice City's textures are weathered)."""
    m = material(name, dark, metallic=metallic, roughness=roughness)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 6.0
    nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].position, ramp.color_ramp.elements[0].color = 0.35, (*dark, 1)
    ramp.color_ramp.elements[1].position, ramp.color_ramp.elements[1].color = 0.65, (*light, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    return m


def materials():
    mats = kit.photo_materials()
    mats["gun"] = weathered("gunmetal", (0.06, 0.063, 0.06), (0.14, 0.145, 0.14), 60.0, 0.6, 0.5)
    mats["black"] = weathered("black_steel", (0.035, 0.037, 0.036), (0.08, 0.085, 0.08), 80.0, 0.8, 0.4)
    mats["brass"] = material("brass", (0.80, 0.56, 0.24), metallic=1.0, roughness=0.3)
    mats["copper"] = material("gilding", (0.72, 0.42, 0.24), metallic=1.0, roughness=0.3)
    return mats


def detailed(mats):
    """The photo shoot's minigun, in the groups its photos need: {name: object}."""
    gun, black, dark, steel, grip = mats["gun"], mats["black"], mats["dark"], mats["steel"], mats["grip"]
    # The housing: ringed, its rear dome, the bearing plate at its front, bolts.
    y0, y1, r = HOUSING
    housing = lathe("housing", "y", (0, 0), [(y0, 0), (y0, r - 4), (y0 + 4, r), (y1 - 4, r), (y1, r - 4), (y1, 0)], 28, gun)
    cut(housing, join("grooves", [lathe("groove", "y", (0, 0), [(y, r + 2), (y + 3, r + 2), (y + 3, r - 1.5), (y, r - 1.5)],
                                        28, closed=True) for y in (160, 228, 296)]))
    body = [housing, lathe("dome", "y", (0, 0), [(DOME[0], 0), (DOME[0], DOME[2] - 12), (DOME[0] + 10, DOME[2]),
                                                 (DOME[1] + 2, DOME[2]), (DOME[1] + 2, 0)], 24, black),
            lathe("plate", "y", (0, 0), [(PLATE[0] - 1, 0), (PLATE[0] - 1, PLATE[2]), (PLATE[1], PLATE[2]), (PLATE[1], 0)],
                  28, black)]
    for k in range(8):
        a = 2 * math.pi * (k + 0.5) / 8
        body.append(cylinder_y("bolt", 63 * math.cos(a), 63 * math.sin(a), 3.2, PLATE[1] - 1, PLATE[1] + 2.5, 8, steel))
    # The feeder (left) with its chute and the belt out of its foot; the motor (right) and its cable.
    fx, fz, fr, fy0, fy1 = FEEDER
    body += [lathe("feeder", "y", (fx, fz), [(fy0, 0), (fy0, fr - 5), (fy0 + 5, fr), (fy1 - 5, fr), (fy1, fr - 5), (fy1, 0)],
                   24, gun),
             lathe("hub", "y", (fx, fz), [(fy0 - 6, 0), (fy0 - 6, 16), (fy0, 16), (fy0, 0)], 16, black),
             box("chute", fx - 10, fx + 20, BELT_Y[0] - 6, BELT_Y[1] + 6, fz - fr - 10, fz - fr + 12, black)]
    mx, mz, mr, my0, my1 = MOTOR
    body += [lathe("motor", "y", (mx, mz), [(my0, 0), (my0, mr - 4), (my0 + 4, mr), (my1, mr), (my1, 0)], 20, black),
             lathe("motor_cap", "y", (mx, mz), [(my0 - 14, 0), (my0 - 14, mr - 8), (my0, mr - 4), (my0, 0)], 20, gun)]
    for y in range(int(my0) + 14, int(my1) - 6, 12):
        body.append(lathe("fin", "y", (mx, mz), [(y, mr - 1), (y + 4, mr - 1), (y + 4, mr + 2.5), (y, mr + 2.5)], 20, gun,
                          closed=True))
    # The frame: the rail under the axis back to the upright grip, the top bar over it on legs from the housing.
    y0, y1, r = REAR_TUBE
    rear = [lathe("rail", "y", (0, 0), [(y0, 0), (y0, r - 3), (y0 + 3, r), (y1, r), (y1, 0)], 16, black)]
    post = profile_yz("post", post_outline(3), -POST_W + 2.5, POST_W - 2.5, black)
    bevel(post, 1.0)
    rear.append(post)
    panel = fillet([(back_strap_y(POST_BOTTOM + 6) + 4, POST_BOTTOM + 6), (front_strap_y(POST_BOTTOM + 6) - 4, POST_BOTTOM + 6),
                    (front_strap_y(POST_TOP - 8) - 4, POST_TOP - 8), (back_strap_y(POST_TOP - 8) + 4, POST_TOP - 8)], 4, 2)
    for x0, x1 in ((POST_W - 2.6, POST_W), (-POST_W, -POST_W + 2.6)):
        p = profile_yz("panel", panel, x0, x1, grip)
        bevel(p, 1.0, 1, 30)
        rear.append(p)
    ty0, ty1, tz0, tz1, w = TOP_BAR
    bar = profile_xz("bar", fillet([(-w, tz0), (w, tz0), (w, tz1), (-w, tz1)], 4, 2), ty0, ty1, black)
    rear.append(bar)
    # The handlebar across it, rubber grips on its ends.
    bar_parts = [cylinder_x("handlebar", HB_Y, HB_Z, HB_R, -HB_HALF + 4, HB_HALF - 4, 16, black),
                 box("clamp", -16, 16, HB_Y - 16, HB_Y + 16, HB_Z - HB_R - 4, HB_Z + HB_R + 4, black)]
    for sx in (-1, 1):
        sleeve = cylinder_x("sleeve", HB_Y, HB_Z, HB_R + 2.5, min(sx * 74, sx * HB_HALF), max(sx * 74, sx * HB_HALF), 16, grip)
        rings = [sx * x for x in range(80, int(HB_HALF) - 6, 9)]
        cut(sleeve, join("rings", [cylinder_x("ring", HB_Y, HB_Z, HB_R + 4, x - 0.75, x + 0.75, 16) for x in rings]))
        bar_parts += [sleeve, cylinder_x("end", HB_Y, HB_Z, HB_R + 3.5, min(sx * HB_HALF, sx * (HB_HALF + 5)),
                                         max(sx * HB_HALF, sx * (HB_HALF + 5)), 16, black)]
    for o in bar_parts:
        finish(o, 35)
    handlebar = join("handlebar", bar_parts)
    for sy0, sy1 in STRUTS:
        rear.append(box("strut", -8, 8, sy0, sy1, HOUSING[2] - 6, tz0 + 2, black))
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 7, 6, 0], 3), -4.5, 4.5, black)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 6, 5, 0], 3), -10, 10))
    rear += [guard, profile_yz("trigger", TRIGGER, -3.2, 3.2, steel)]
    # The barrels: six, in their clamps, the front one a drum with the muzzles in its face.
    barrels = []
    for bx, bz in barrel_centres():
        barrels.append(tube_y("barrel", bx, bz, BARREL_R, BARRELS_Y[0], FRONT_CLAMP[0] + 4, 14, dark))
        barrels.append(tube_y("muzzle", bx, bz, BARREL_R - 1.5, FRONT_CLAMP[1] - 4, FRONT_CLAMP[1] + 2, 14, dark))
    y0, y1, r = MID_CLAMP
    barrels.append(lathe("mid_clamp", "y", (0, 0), [(y0, 0), (y0, r), (y1, r), (y1, 0)], 6, black, 0.0))
    barrels.append(tube_y("axle", 0, 0, 12, BARRELS_Y[0], FRONT_CLAMP[0] + 2, 12, steel))
    y0, y1, r = FRONT_CLAMP
    drum = lathe("front_clamp", "y", (0, 0), [(y0, 0), (y0, r - 4), (y0 + 4, r), (y1 - 3, r), (y1, r - 3), (y1, 0)], 28, gun)
    cut(drum, join("bores", [tube_y("bore", bx, bz, BARREL_R - 3, y1 - 10, y1 + 10, 12) for bx, bz in barrel_centres()]
                   + [lathe("band", "y", (0, 0), [(y, r + 2), (y + 3, r + 2), (y + 3, r - 1.2), (y, r - 1.2)], 28, closed=True)
                      for y in (y0 + 22, y1 - 22)]))
    barrels.append(drum)
    # The belt.
    belt = []
    y0, y1 = BELT_Y
    for p, d in belt_points(BELT_PATH, PITCH):
        x, z = p
        belt += [cylinder_y("case", x, z, ROUND_R, y0, y0 + 46, 12, mats["brass"]),
                 cylinder_y("bullet", x, z, 3.9, y0 + 51, y1 - 6, 10, mats["copper"]),
                 cylinder_y("tip", x, z, 2.4, y1 - 6, y1, 8, mats["copper"]),
                 cylinder_y("shoulder", x, z, ROUND_R - 1.2, y0 + 46, y0 + 51, 12, mats["brass"])]
        for ly in (y0 + 8, y0 + 34):
            belt.append(cylinder_y("link", x, z, ROUND_R + 0.9, ly, ly + 7, 12, dark))
    groups = {"body": body, "rear": rear, "barrels": barrels, "belt": belt}
    out = {"handlebar": handlebar}
    for name, obs in groups.items():
        for o in obs:
            finish(o, 35)
        out[name] = join(name, obs)
    return out


def low():
    """The minigun at Vice City's detail, in its moving parts: {part: [(object, its photos)]}."""
    y0, y1, r = HOUSING
    fx, fz, fr, fy0, fy1 = FEEDER
    mx, mz, mr, my0, my1 = MOTOR
    body = [lathe("housing", "y", (0, 0), [(DOME[0], 0), (DOME[0], DOME[2]), (y0, r), (y1, r), (y1, 0)], 8),
            lathe("plate", "y", (0, 0), [(PLATE[0], PLATE[2]), (PLATE[1], PLATE[2]), (PLATE[1], 0)], 8),
            lathe("feeder", "y", (fx, fz), [(fy0, 0), (fy0, fr), (fy1, fr), (fy1, 0)], 8),
            lathe("motor", "y", (mx, mz), [(my0 - 14, 0), (my0 - 14, mr - 8), (my0, mr), (my1, mr), (my1, 0)], 6),
            profile_xz("belt", strip(BELT_PATH, ROUND_R), BELT_Y[0], BELT_Y[1])]
    y0, y1, r = REAR_TUBE
    ty0, ty1, tz0, tz1, w = TOP_BAR
    rear = [lathe("rail", "y", (0, 0), [(y0, 0), (y0, r), (y1, r)], 6),
            profile_yz("post", post_outline(), -POST_W, POST_W),
            profile_xz("bar", [(-w, tz0), (w, tz0), (w, tz1 - 4), (w - 4, tz1), (-w + 4, tz1), (-w, tz1 - 4)], ty0, ty1)]
    for sy0, sy1 in STRUTS:
        rear.append(box("strut", -8, 8, sy0, sy1, HOUSING[2] - 6, tz0 + 1))
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 7, 6, 0], 1), -4.5, 4.5)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 6, 5, 0], 1), -10, 10), solver="EXACT")
    rear.append(guard)
    bars = [cylinder_x("handlebar", HB_Y, HB_Z, HB_R + 2, -HB_HALF - 3, HB_HALF + 3, 6)]
    barrels = [tube_y("barrel", bx, bz, BARREL_R, BARRELS_Y[0], FRONT_CLAMP[0], 6, open_ends=True)
               for bx, bz in barrel_centres()]
    y0, y1, r = MID_CLAMP
    barrels.append(lathe("mid_clamp", "y", (0, 0), [(y0, 0), (y0, r), (y1, r), (y1, 0)], 6, None, 0.0))
    y0, y1, r = FRONT_CLAMP
    barrels.append(lathe("front_clamp", "y", (0, 0), [(y0, 0), (y0, r), (y1, r), (y1, 0)], 8, None, 0.0))
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    return {"body": [(o, choose_body) for o in body] + [(o, choose_rear) for o in rear]
            + [(o, choose_handlebar) for o in bars],
            "trigger": [(o, choose_rear) for o in trigger], "barrels": [(o, choose_barrels) for o in barrels]}


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


def choose_body(normal, centre):
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return ["top", "left"]
    if ax == 1 and normal.y < 0:
        return ["back", "left"]
    return ["left"]


def choose_rear(normal, centre):
    """The grip and the top bar: their tops from the bar's own photo, their sides from the grip's."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return ["handle", "top", "left"]
    if ax == 1 and normal.y < 0:
        return ["back", "grip", "left"]
    return ["grip", "left"]


def choose_handlebar(normal, centre):
    return ["handlebar"]


def choose_barrels(normal, centre):
    if normal.y > 0.7:
        return ["front", "barrels"]
    return ["barrels"]


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "minigun", REAL_LENGTH)
    hold = kit.hold_for(WEB, scale)
    left = hold_left(PALM, scale, *LEFT_AXES)
    scene = kit.studio()
    d = detailed(materials())
    body, rear, barrels, belt, bar = d["body"], d["rear"], d["barrels"], d["belt"], d["handlebar"]
    rgba = kit.atlas(scene, REGIONS, {"left": [body, rear, belt], "back": [body, rear], "front": [barrels], "top": [body, rear],
                                      "handle": [rear], "grip": [rear], "barrels": [barrels], "handlebar": [bar]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "minigun-atlas.png")
    kit.save_png(atlas_png, flat)
    for o in d.values():
        o.hide_render = True
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in low().items():
        for ob, prefer in obs:
            project(ob, prefer)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, hold, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING, two_handed=True, hold_left=left, left_forearm=LEFT_FOREARM)
    print(f"minigun: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in hold[3]]}")
    kit.preview(os.path.join(work, "minigun-preview.png"), [o for o, _ in objects], atlas_png,
                [((-1.0, 0.45, 0.05), (0, 0.45, 0.0), 50), ((0.8, 0.9, 0.45), (0, 0.4, 0.02), 50),
                 ((-0.30, -0.30, 0.40), (0.0, 0.5, 0.0), 35)])


main()
