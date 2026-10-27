"""Vice City's sniper rifle (a bolt-action hunting rifle: a Remington 700 with a 24 inch barrel, its walnut
sporter stock in the game's red-brown, blued barrel and action, a black 3-9x40 hunting scope in two rings) for
the first-person viewmodel. Scoped: the game draws its own scope view when aiming, so only the hip view matters.

blender -b -P tools/guns/sniper.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the bore, z up, x to the right; the butt plate's back at y = 0, the bore at z = 0.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, _object, bevel, box, cylinder_x, cylinder_y, fillet, finish, join, material, profile_yz

LENGTH = 1120.0  # overall, butt plate to muzzle
SPLIT = 605.0  # the side and top photos are in two halves, behind and ahead of this (y)
REC_Y0, REC_Y1, REC_R = 322.0, 535.0, 17.3  # the receiver: a round action
SCOPE_Z = 44.0  # the scope's axis over the bore
SCOPE = [(262, 19.5), (300, 19.5), (316, 15.5), (328, 12.7), (512, 12.7), (546, 23.5), (598, 23.5)]  # (y, radius)
BARREL = [(535, 15.0), (600, 14.2), (1100, 9.3), (1120, 9.0)]
TRIGGER_FACE = 341.0
KNOB = (44.0, 326.0, -15.0)  # the bolt knob's middle


def grip_front_y(z):
    """The pistol grip's front edge, behind the guard, raked back about 26 degrees."""
    return 313.0 + (z + 47.0) * 31.0 / 63.0


def grip_back_y(z):
    """Its back edge, from the cap up to where it meets the butt's belly."""
    return 230.0 + (z + 117.0) * 22.0 / 43.0


# The stock's outlines (y, z): the butt and comb; the wrist and pistol grip; the forend along the action and barrel
# (one piece in the photos, two either side of SPLIT in the low-poly model).
BUTT = [(8, -24), (222, -14), (240, -24), (258, -30), (258, -70), (242, -82), (8, -160)]
GRIP = [(236, -26), (256, -28), (300, -9), (306, -46), (grip_front_y(-47), -47), (grip_front_y(-110), -110),
        (grip_front_y(-110) - 6, -115), (grip_back_y(-117), -117), (grip_back_y(-74), -74)]
FORE_REAR = [(300, -3), (SPLIT, -3), (SPLIT, -42.6), (470, -46), (318, -46), (300, -24)]
FORE_FRONT = [(SPLIT - 0.01, -3), (826, -3), (838, -8), (842, -16), (838, -27), (826, -32), (SPLIT - 0.01, -42.6)]
STOCK_HALF = {"butt": 21.0, "grip": 17.0, "fore_rear": 21.0, "fore_front": 21.0}
BUTT_TAPER = (120.0, 250.0, 16.5)  # the butt narrows to the wrist
GUARD = [(312, -45), (312, -66), (320, -76), (372, -76), (382, -66), (386, -45), (379, -45), (376, -62), (368, -70),
         (325, -70), (319, -63), (319, -45)]
TRIGGER = [(334, -40), (340, -40), (TRIGGER_FACE + 1.0, -56), (TRIGGER_FACE - 1.0, -64), (336, -64), (338, -54)]

# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# The side and top in two halves each (the rifle's too long for one 256 px row at a useful size), then the back
# (butt plate, eyepiece), the front (muzzle, objective, forend tip) and the bolt seen from the right.
SIDE_MM = 2.42
FRONT_MM = 2.12
REGIONS = {
    "left": ((0, 0, 256, 100), (-400, 302, -46.5), (1, 0, 0), (0, 0, 1), SIDE_MM),
    "left_front": ((0, 100, 256, 32), (-400, 866, -14), (1, 0, 0), (0, 0, 1), FRONT_MM),
    "top": ((0, 132, 256, 24), (0, 302, 400), (0, 0, -1), (-1, 0, 0), SIDE_MM),
    "top_front": ((0, 156, 256, 24), (0, 866, 400), (0, 0, -1), (-1, 0, 0), FRONT_MM),
    "back": ((0, 180, 24, 76), (0, -400, -45), (0, 1, 0), (0, 0, 1), 3.2),
    "front": ((24, 180, 40, 76), (0, 1400, 6), (0, -1, 0), (0, 0, 1), 1.65),
    "bolt": ((64, 180, 72, 56), (400, 312, -12), (-1, 0, 0), (0, 0, 1), 1.0),
}

# Where his right hand's fingertips go (a pad's middle, a finger's half-thickness off the surface): the index on
# the trigger's face, the others on the grip's left side behind its front edge, the thumb over the wrist along the
# stock's left side. His left hand's under the forend: fingers up its right (far) side, thumb along its left.
FINGER_R = 8.0
GRIP_BACK = 34.0  # mm behind the grip's front edge


def on_grip(z, back=GRIP_BACK):
    return (-STOCK_HALF["grip"] - FINGER_R, grip_front_y(z) - back, z)


WEB = (0.0, 252.0, -30.0)  # the web of his right hand: the top of the wrist, behind the tang
HAND_YAW, HAND_PITCH = 18.0, 0.0
PALM = (0.0, 545.0, -50.0)  # his left palm, under the forend
LEFT_ROLL = 0.0
FORE_SIDE = STOCK_HALF["fore_rear"] + FINGER_R
AROUND = (1.0, 0.0, -0.8)  # a left finger's middle joint: out round the forend's lower right corner
DOWN_SIDE = (0.0, 0.0, -14.0)  # slack: down the forend's side (longer fingers)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, TRIGGER_FACE + FINGER_R, -57.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(-62.0), None, None, (0.0, 18.0, 0.0)),
    ("r_ring", on_grip(-80.0), None, None, (0.0, 18.0, 0.0)),
    ("r_pinky", on_grip(-97.0, GRIP_BACK + 2.0), None, None, (0.0, 18.0, 0.0)),
    ("r_thumb", (-STOCK_HALF["grip"] - FINGER_R - 2.0, 296.0, -12.0), None, None, (0.0, -14.0, 0.0)),
    ("l_index", (FORE_SIDE, PALM[1] + 33.0, -20.0), None, AROUND, DOWN_SIDE),
    ("l_middle", (FORE_SIDE, PALM[1] + 11.0, -18.0), None, AROUND, DOWN_SIDE),
    ("l_ring", (FORE_SIDE, PALM[1] - 11.0, -19.0), None, AROUND, DOWN_SIDE),
    ("l_pinky", (FORE_SIDE, PALM[1] - 31.0, -22.0), None, AROUND, DOWN_SIDE),
    ("l_thumb", (-FORE_SIDE, PALM[1] + 52.0, -22.0), None, None, (0.0, -15.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
MUZZLE = (0, LENGTH, 0)
SIGHT_REAR, SIGHT_FRONT = (0, SCOPE[0][0], SCOPE_Z), (0, SCOPE[-1][0], SCOPE_Z)  # the scope's axis
# Seen from: the eye left of and over the wrist (real metres), looking along the barrel (levelled: it points where he
# aims); the aim values are the Colt's (the game draws its own scope view).
FRAMING = ((-0.155, -0.009, 0.115), 0.0, 0.0, 0.22, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
LEFT_FOREARM = (-0.535, -0.777, -0.333)  # kit's, turned with the levelled framing: runs where it did in the view


def lathe_y(name, stations, segments, mat=None, x=0.0, z=0.0, cap=(True, True)):
    """A surface of revolution about the line through (x, z) along y: stations [(y, radius)], mm."""
    bm = bmesh.new()
    rings = []
    for y, r in stations:
        rings.append([bm.verts.new(((x + r * math.cos(2 * math.pi * k / segments)) * MM, y * MM,
                                    (z + r * math.sin(2 * math.pi * k / segments)) * MM)) for k in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segments):
            j = (k + 1) % segments
            bm.faces.new((a[k], a[j], b[j], b[k]))
    if cap[0]:
        bm.faces.new(list(reversed(rings[0])))
    if cap[1]:
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def rod(name, p0, p1, r, segments, mat=None):
    """A cylinder from p0 to p1 (mm)."""
    a, b = Vector(p0), Vector(p1)
    d = (b - a).normalized()
    u = d.orthogonal().normalized()
    v = d.cross(u)
    bm = bmesh.new()
    rings = [[bm.verts.new((c + (u * math.cos(2 * math.pi * k / segments) + v * math.sin(2 * math.pi * k / segments))
                            * r) * MM) for k in range(segments)] for c in (a, b)]
    for k in range(segments):
        j = (k + 1) % segments
        bm.faces.new((rings[0][k], rings[0][j], rings[1][j], rings[1][k]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def stock_piece(name, outline, half, mat=None, chamfer=0.0, segments=1, taper=None):
    """A piece of the stock: its side outline `half` mm either side of the middle, its edges chamfered; `taper`
    (y0, y1, half at y1): narrowed from y0 to y1."""
    ob = profile_yz(name, outline, -half, half, mat)
    if chamfer:
        bevel(ob, chamfer, segments, 30)
    if taper:
        y0, y1, h1 = taper
        for v in ob.data.vertices:
            t = min(1.0, max(0.0, (v.co.y / MM - y0) / (y1 - y0)))
            v.co.x *= 1.0 + t * (h1 / half - 1.0)
    return ob


def inset(outline, d):
    """A closed outline (y, z) moved `d` mm inwards, its corners mitred."""
    n = len(outline)
    area = sum(outline[i][0] * outline[(i + 1) % n][1] - outline[(i + 1) % n][0] * outline[i][1] for i in range(n))
    side = 1.0 if area > 0 else -1.0  # counter-clockwise: inwards is to the left of each edge
    out = []
    for i in range(n):
        p, a, b = Vector(outline[i]), Vector(outline[i - 1]), Vector(outline[(i + 1) % n])
        e0, e1 = (p - a).normalized(), (b - p).normalized()
        n0, n1 = Vector((-e0.y, e0.x)) * side, Vector((-e1.y, e1.x)) * side
        m = n0 + n1
        k = d / max(0.35, m.length / 2) if m.length > 1e-6 else d
        out.append(tuple(p + m.normalized() * k * (1.0 if m.length > 1e-6 else 0.0)))
    return out


def slab(name, outline, half, chamfer, taper=None):
    """A low-poly stock piece: its side outline either side of the middle, its long edges chamfered (`chamfer`
    mm), narrowed as stock_piece's `taper` says."""
    rings = [(inset(outline, chamfer), -half), (outline, -half + chamfer), (outline, half - chamfer),
             (inset(outline, chamfer), half)]
    bm = bmesh.new()
    vs = [[bm.verts.new((x * MM, y * MM, z * MM)) for y, z in ring] for ring, x in rings]
    n = len(outline)
    for a, b in zip(vs, vs[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(vs[0])
    bm.faces.new(list(reversed(vs[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    ob = _object(name, bm)
    if taper:
        y0, y1, h1 = taper
        for v in ob.data.vertices:
            t = min(1.0, max(0.0, (v.co.y / MM - y0) / (y1 - y0)))
            v.co.x *= 1.0 + t * (h1 / half - 1.0)
    return ob


def hold_turned(web_mm, scale, yaw_deg, pitch_deg):
    """kit.hold_for with his hand turned about the web point: `yaw_deg` its knuckles towards the gun's left (his
    wrist out to the right of the stock's wrist), `pitch_deg` its knuckles up. A rifle's stock is gripped from
    the side and below, its butt behind the grip where a pistol's hand has room."""
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    turn = Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(pitch, 3, "X")
    hx, hy, hz = turn @ Vector((0, 1, 0)), turn @ Vector((-1, 0, 0)), turn @ Vector((0, 0, 1))
    axes = [Vector((hx[i], hy[i], hz[i])) for i in range(3)]  # gun x, y, z in the hand's frame
    w = Vector(web_mm) * MM * scale
    origin = Vector(kit.WEB_IN_HAND) - (axes[0] * w.x + axes[1] * w.y + axes[2] * w.z)
    return [tuple(a) for a in axes] + [tuple(origin)]


def materials():
    m = kit.photo_materials()
    # Walnut as Vice City paints it: red-brown, with darker streaks along the grain.
    wood = material("walnut_photo", (0.28, 0.08, 0.032), roughness=0.45, spec=0.35)
    nt = wood.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (90.0, 6.0, 90.0)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 1.0
    noise.inputs["Detail"].default_value = 6.0
    noise.inputs["Distortion"].default_value = 1.5
    nt.links.new(mp.outputs["Vector"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.35, (0.13, 0.034, 0.014, 1)
    els[1].position, els[1].color = 0.65, (0.38, 0.11, 0.045, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    m["wood"] = wood
    m["scope"] = material("scope_photo", (0.035, 0.038, 0.048), metallic=0.5, roughness=0.35)
    m["glass"] = material("glass_photo", (0.01, 0.015, 0.04), metallic=0.2, roughness=0.05, spec=1.0)
    m["pad"] = material("pad_photo", (0.012, 0.012, 0.012), roughness=0.8, spec=0.2)
    return m


def scope(mats, segments):
    """The scope, its turrets, rings and bases."""
    parts = [lathe_y("scope", SCOPE, segments, mats["scope"], z=SCOPE_Z)]
    parts.append(cylinder_y("turret_base", 0, SCOPE_Z, 15.5, 404, 436, segments, mats["scope"]))
    parts.append(lathe_z("elevation", 420, SCOPE_Z + 12, SCOPE_Z + 27, 9.5, segments, mats["scope"]))
    parts.append(cylinder_x("windage", 420, SCOPE_Z, 9.5, 12, 27, segments, mats["scope"]))
    for y0 in (352.0, 470.0):
        parts.append(cylinder_y("ring", 0, SCOPE_Z, 15.5, y0, y0 + 14, segments, mats["dark"]))
        parts.append(box("ring_base", -9, 9, y0 - 3, y0 + 17, REC_R - 3, SCOPE_Z - 12, mats["dark"]))
    return parts


def lathe_z(name, y, z0, z1, r, segments, mat=None):
    """An upright cylinder at (0, y) from z0 to z1 (mm)."""
    outline = [(r * math.cos(2 * math.pi * k / segments), y + r * math.sin(2 * math.pi * k / segments)) for k in range(segments)]
    bm = bmesh.new()
    lo = [bm.verts.new((px * MM, py * MM, z0 * MM)) for px, py in outline]
    hi = [bm.verts.new((px * MM, py * MM, z1 * MM)) for px, py in outline]
    for k in range(segments):
        j = (k + 1) % segments
        bm.faces.new((lo[k], lo[j], hi[j], hi[k]))
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def detailed(mats):
    """The photo shoot's rifle. Returns (gun, bolt)."""
    wood, steel, dark = mats["wood"], mats["steel"], mats["dark"]
    parts = []
    butt = stock_piece("butt", fillet(BUTT, [3, 20, 8, 0, 0, 12, 4], 3), STOCK_HALF["butt"], wood, 7.0, 3, BUTT_TAPER)
    grip = stock_piece("grip", fillet(GRIP, [0, 8, 6, 0, 4, 6, 4, 6, 10], 3), STOCK_HALF["grip"], wood, 6.0, 3)
    fore = stock_piece("forend", fillet(FORE_REAR[:1] + FORE_FRONT[1:-1] + FORE_REAR[2:], [0, 6, 6, 6, 6, 6, 0, 0, 0, 0], 3),
                       STOCK_HALF["fore_rear"], wood, 6.0, 3)
    parts += [butt, grip, fore]
    # Chequering panels on the grip and forend, a little darker than the wood.
    chq = kit.photo_materials()["grip"]
    chq.diffuse_color = (0.1, 0.03, 0.01, 1)
    nt = chq.node_tree
    nt.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.12, 0.035, 0.015, 1)
    for sx in (-1, 1):
        parts.append(profile_yz("chequer_grip", fillet([(grip_front_y(-56) - 6, -56), (grip_front_y(-100) - 6, -100),
                                                         (grip_back_y(-100) + 8, -100), (grip_back_y(-60) + 6, -56)], 4, 2),
                                sx * (STOCK_HALF["grip"] - 2.5), sx * (STOCK_HALF["grip"] + 0.3), chq))
        parts.append(profile_yz("chequer_fore", fillet([(560, -12), (700, -12), (700, -36), (560, -38)], 5, 2),
                                sx * (STOCK_HALF["fore_front"] + 0.3), sx * (STOCK_HALF["fore_front"] - 3.0), chq))
    parts.append(profile_yz("butt_plate", fillet([(0, -24), (8, -24), (8, -160), (0, -160)], [2, 0, 0, 2], 2),
                            -STOCK_HALF["butt"] + 0.5, STOCK_HALF["butt"] - 0.5, mats["pad"]))
    parts.append(profile_yz("grip_cap", [(grip_front_y(-110) - 4, -112), (grip_back_y(-117) + 2, -115),
                                         (grip_back_y(-117) + 2.5, -119), (grip_front_y(-110) - 4, -116)], -15, 15, dark))
    parts.append(cylinder_x("swivel_rear", 80, -137, 4.0, -2, 2, 10, steel))
    parts.append(cylinder_x("swivel_front", 790, -33, 4.0, -2, 2, 10, steel))
    rec = cylinder_y("receiver", 0, 0, REC_R, REC_Y0, REC_Y1, 32, dark)
    bevel(rec, 1.0)
    parts.append(rec)
    parts.append(box("recoil_lug", -REC_R + 2, REC_R - 2, 520, 530, -REC_R - 3, -8, dark))
    parts.append(profile_yz("tang", [(298, -6), (322, -6), (322, 8), (306, 6)], -6.5, 6.5, dark))
    parts.append(box("port", 6, 18.5, 410, 486, -4, 11, mats["pad"]))  # the ejection port's shadow, on the right
    barrel = lathe_y("barrel", BARREL, 32, dark)
    parts.append(barrel)
    parts.append(cylinder_y("crown", 0, 0, 4.0, 1119.5, 1120.5, 16, mats["pad"]))
    parts += scope(mats, 32)
    parts.append(cylinder_y("objective", 0, SCOPE_Z, 20.5, 597, 598.6, 32, mats["glass"]))
    parts.append(cylinder_y("eyepiece", 0, SCOPE_Z, 16.5, 261.4, 263, 32, mats["glass"]))
    guard = profile_yz("guard", fillet(GUARD, [0, 6, 6, 6, 6, 0, 0, 5, 5, 5, 4, 0], 3), -4.5, 4.5, dark)
    bevel(guard, 0.6)
    parts += [guard, box("floor_plate", -15, 15, 312, 472, -48.5, -44, dark)]
    parts.append(profile_yz("trigger", fillet(TRIGGER, [0, 0, 2, 2, 0, 0], 2), -3.0, 3.0, steel))
    for p in parts:
        finish(p, 35)
    gun = join("sniper", parts)
    bolt = bolt_parts(mats, 24)
    for p in bolt:
        finish(p, 35)
    return gun, join("bolt", bolt)


def bolt_parts(mats, segments):
    """The bolt's shroud behind the receiver, its handle and knob out to the right."""
    dark = mats["dark"]
    shroud = lathe_y("shroud", [(292, 8.0), (296, 10.5), (322, 10.5)], segments, dark)
    cock = box("cocking_piece", -4, 4, 286, 300, -12, -3, dark)
    handle = rod("handle", (10, 336, 3), (40, 328, -12), 4.2, max(6, segments // 3), dark)
    knob = lathe_knob(mats, segments)
    return [shroud, cock, handle, knob]


def lathe_knob(mats, segments):
    """The bolt knob: a ball on the handle's end."""
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=segments, v_segments=max(4, segments // 2), radius=8.5 * MM)
    for v in bm.verts:
        v.co += Vector(KNOB) * MM
    return _object("knob", bm, mats["dark"])


def low():
    """The rifle at Vice City's detail, in its moving parts: {part: [objects]}."""
    body = [slab("butt", BUTT, STOCK_HALF["butt"], 5.0, BUTT_TAPER),
            slab("grip", GRIP, STOCK_HALF["grip"], 4.0),
            # The forend in two, either side of SPLIT, overlapping by their chamfer so they meet without a groove.
            slab("fore_rear", [(300, -3), (SPLIT + 4, -3), (SPLIT + 4, -42.4)] + FORE_REAR[3:], STOCK_HALF["fore_rear"], 4.0),
            slab("fore_front", [(SPLIT - 4, -3), FORE_FRONT[1], FORE_FRONT[3], FORE_FRONT[5], (SPLIT - 4, -42.7)],
                 STOCK_HALF["fore_front"], 4.0),
            lathe_y("receiver", [(REC_Y0, REC_R), (REC_Y1, REC_R)], 8),
            profile_yz("tang", [(298, -6), (322, -6), (322, 8), (306, 6)], -6.5, 6.5),
            lathe_y("barrel_rear", [(BARREL[0][0], BARREL[0][1]), (SPLIT, 14.2)], 8),
            lathe_y("barrel_front", [(SPLIT, 14.2), (BARREL[-1][0], BARREL[-1][1])], 8),
            lathe_y("scope", SCOPE, 10, z=SCOPE_Z),
            box("turret", -9, 9, 410, 430, SCOPE_Z, SCOPE_Z + 27),
            box("windage", 0, 27, 412, 428, SCOPE_Z - 8, SCOPE_Z + 8),
            profile_yz("guard", GUARD, -4.5, 4.5),
            box("floor_plate", -15, 15, 312, 472, -48.5, -44),
            profile_yz("butt_plate", [(0, -24), (8, -24), (8, -160), (0, -160)], -STOCK_HALF["butt"] + 0.5,
                       STOCK_HALF["butt"] - 0.5)]
    for y0 in (352.0, 470.0):
        body.append(lathe_y("ring", [(y0, 15.5), (y0 + 14, 15.5)], 6, z=SCOPE_Z))
        body.append(box("ring_base", -9, 9, y0 - 3, y0 + 17, REC_R - 3, SCOPE_Z - 12))
    bolt = [lathe_y("shroud", [(292, 8.0), (296, 10.5), (322, 10.5)], 6, cap=(True, False)),
            rod("bolt_handle", (10, 336, 3), (40, 328, -12), 4.2, 4),
            lathe_knob_low()]
    trigger = [profile_yz("trigger", TRIGGER, -3.0, 3.0)]
    return {"body": body, "bolt": bolt, "trigger": trigger}


def lathe_knob_low():
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=6, v_segments=3, radius=8.5 * MM)
    for v in bm.verts:
        v.co += Vector(KNOB) * MM
    return _object("bolt_knob", bm)


def seen_by(objects):
    """Whether a low-poly face shows in a region's photo of `objects` (the photographed model): nothing of them
    between it and the camera but their own surface where it is (within 6 mm)."""
    dg = bpy.context.evaluated_depsgraph_get()
    trees = [BVHTree.FromObject(o, dg) for o in objects]

    def seen(region, normal, centre):
        d = -Vector(region[2]).normalized()
        start = centre * MM + Vector(normal) * 0.0005
        for tree in trees:
            o, gone = Vector(start), 0.0
            for _ in range(8):
                hit = tree.ray_cast(o, d, 2.0)
                if hit[0] is None:
                    break
                gone += hit[3]
                if gone > 0.006:
                    return False
                o = hit[0] + d * 0.0002
                gone += 0.0002
        return True
    return seen


def covers(region, points_mm):
    """Whether a region's photo takes in all these points (mm)."""
    (x0, y0, w, h) = region[0]
    for p in points_mm:
        u, v = kit.region_uv(region, p)
        px, py = u * kit.SIZE, (1 - v) * kit.SIZE
        if not (x0 - 0.5 <= px <= x0 + w + 0.5 and y0 - 0.5 <= py <= y0 + h + 0.5):
            return False
    return True


def project(ob, choose):
    """kit.project, with each face's photo chosen knowing its corners: one that doesn't take them all in gives way
    to the side photo."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        corners = [me.vertices[i].co / MM for i in poly.vertices]
        centre = sum(corners, Vector()) / len(corners)
        name = choose(poly.normal, centre)
        if not covers(REGIONS[name], corners):
            name = "left_front" if centre.y > SPLIT else "left"
        for li in poly.loop_indices:
            uv.data[li].uv = kit.region_uv(REGIONS[name], me.vertices[me.loops[li].vertex_index].co / MM)


def chooser(seen):
    """Which photo a face wears: the top, back or front where it faces them and they see it, the left side otherwise
    (the right mirrors it); the front half's own photos ahead of SPLIT."""
    def choose(normal, centre):
        ax = max(range(3), key=lambda k: abs(normal[k]))
        side = "left_front" if centre.y > SPLIT else "left"
        if ax == 2 and normal.z > 0:
            pick = "top_front" if centre.y > SPLIT else "top"
        elif ax == 1 and normal.y < 0:
            pick = "back"
        elif ax == 1 and normal.y > 0:
            pick = "front"
        else:
            return side
        return pick if seen(REGIONS[pick], normal, centre) else side
    return choose


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "sniper", LENGTH)
    frame = hold_turned(WEB, scale, HAND_YAW, HAND_PITCH)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    gun, bolt = detailed(materials())
    every = [gun, bolt]
    rgba = kit.atlas(scene, REGIONS, {"left": every, "left_front": every, "top": every, "top_front": every,
                                      "back": every, "front": every, "bolt": [bolt]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "sniper-atlas.png")
    kit.save_png(atlas_png, flat)
    choose = chooser(seen_by([gun, bolt]))
    gun.hide_render = bolt.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            project(ob, (lambda n, c: "bolt") if ob.name.startswith(("bolt_handle", "bolt_knob")) else choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING, two_handed=True, hold_left=hold_left, left_forearm=LEFT_FOREARM)
    print(f"sniper: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "sniper-preview.png"), [o for o, _ in objects], atlas_png,
                [((-1.7, 0.56, 0.0), (0, 0.56, -0.04), 50), ((0.9, 1.1, 0.45), (0, 0.45, -0.03), 35),
                 ((-0.17, 0.0, 0.08), (0.0, 0.6, -0.04), 30)])


main()
