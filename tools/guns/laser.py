"""Vice City's laser-scope sniper rifle (a Heckler & Koch PSG-1: a G3-pattern roller-delayed receiver, a heavy
barrel, a slab-sided handguard, an adjustable stock with a cheek piece, a palm-shelf grip, a five-round magazine,
a 6x42 scope on a claw mount and a laser on the scope) in the game's grey, its grip brown, for the first-person
viewmodel. Scoped: the game draws its own scope view when aiming, so only the hip view matters.

blender -b -P tools/guns/laser.py -- <GameData> <out.vmg> <work dir>

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
from lib import (MM, _object, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz,
                 profile_yz)

LENGTH = 1208.0  # overall, butt plate to muzzle
SPLIT = 650.0  # the side and top photos are in two halves, behind and ahead of this (y): the receiver's front
SCOPE_Z = 70.0  # the scope's axis over the bore, on its claw mount
SCOPE = [(285, 21.5), (330, 21.5), (345, 16.0), (360, 15.0), (510, 15.0), (545, 25.0), (610, 25.0)]  # (y, radius)
LASER_Z = SCOPE_Z + 15.0 + 9.5  # the laser's axis, on the scope's top
TUBE_Z = 16.0  # the cocking tube's axis, over the barrel
TRIGGER_FACE = 390.0


def grip_front_y(z):
    """The grip's front edge, behind the guard, raked back about 18 degrees."""
    return 348.0 + (z + 56.0) * 26.0 / 82.0


def grip_back_y(z):
    return 270.0 + (z + 138.0) * 30.0 / 104.0


# Outlines (y, z). The stock: its top straight back from the receiver, its bottom rising to it; the cheek piece on
# top. The receiver's cross-section (x, z), stamped and round-topped. The grip with its palm shelf, the trigger
# housing under the receiver, the guard, the trigger, the magazine, the handguard.
STOCK = [(18, 24), (300, 22), (316, 18), (316, -12), (294, -14), (262, -42), (18, -112)]
CHEEK = [(70, 18), (250, 18), (250, 38), (234, 44), (70, 44)]
RECEIVER = [(-16.5, -30), (16.5, -30), (16.5, 14), (11, 25), (-11, 25), (-16.5, 14)]
REC_Y0, REC_Y1 = 300.0, SPLIT
GRIP = [(300, -34), (352, -44), (grip_front_y(-56), -56), (grip_front_y(-138), -138), (334, -144), (334, -152),
        (258, -152), (258, -144), (grip_back_y(-138), -138)]
HOUSING = [(318, -28), (452, -28), (452, -40), (440, -46), (330, -46)]
GUARD = [(352, -44), (352, -70), (360, -79), (430, -79), (440, -70), (442, -44), (435, -44), (433, -66), (426, -72),
         (366, -72), (359, -66), (359, -44)]
TRIGGER = [(382, -40), (389, -40), (TRIGGER_FACE + 1.0, -56), (TRIGGER_FACE - 2.0, -64), (383, -63), (385, -52)]
MAG_AXIS = (0.0, 0.04, -1.0)  # down, a touch forward: the way it drops out
MAG = [(456, -6), (516, -6), (520, -110), (461, -110)]
HANDGUARD = [(SPLIT, 4), (905, 4), (905, -30), (895, -46), (SPLIT, -48)]
HALF = {"stock": 21.0, "cheek": 19.0, "grip": 16.0, "housing": 15.0, "mag": 12.0, "handguard": 26.0}
STOCK_TAPER = (40.0, 300.0, 17.0)  # the stock narrows to the receiver

# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# The side and top in two halves each, then the back (eyepiece, receiver), the front (muzzle, objective, laser,
# sight hood) and the magazine on its own (it's seen whole as it drops).
SIDE_MM = 2.61
FRONT_MM = 2.25
REGIONS = {
    "left": ((0, 0, 256, 108), (-400, 326, -26.5), (1, 0, 0), (0, 0, 1), SIDE_MM),
    "left_front": ((0, 108, 256, 50), (-400, 927.5, -1.5), (1, 0, 0), (0, 0, 1), FRONT_MM),
    "top": ((0, 158, 256, 22), (0, 326, 400), (0, 0, -1), (-1, 0, 0), SIDE_MM),
    "top_front": ((0, 180, 256, 30), (-7, 927.5, 400), (0, 0, -1), (-1, 0, 0), FRONT_MM),
    "back": ((0, 210, 20, 46), (0, -400, 30), (0, 1, 0), (0, 0, 1), 3.0),
    "front": ((20, 210, 24, 46), (0, 1500, 22.5), (0, -1, 0), (0, 0, 1), 3.6),
    "magazine": ((44, 210, 32, 46), (-400, 488, -62.5), (1, 0, 0), (0, 0, 1), 2.5),
}

# Where his right hand's fingertips go (a pad's middle, a finger's half-thickness off the surface): the index on
# the trigger's face, the others on the grip's left side behind its front edge, the thumb along the receiver's left.
# His left hand's under the handguard: fingers up its right (far) side, thumb along its left.
FINGER_R = 8.0
GRIP_BACK = 34.0  # mm behind the grip's front edge


def on_grip(z, back=GRIP_BACK):
    return (-HALF["grip"] - FINGER_R, grip_front_y(z) - back, z)


WEB = (0.0, 296.0, -40.0)  # the web of his right hand: the top of the grip's back, under the receiver's end
HAND_YAW, HAND_PITCH = 18.0, 0.0
PALM = (0.0, 690.0, -60.0)  # his left palm, under the handguard
LEFT_ROLL = 0.0
GUARD_SIDE = HALF["handguard"] + FINGER_R
AROUND = (1.0, 0.0, -0.8)  # a left finger's middle joint: out round the handguard's lower right corner
DOWN_SIDE = (0.0, 0.0, -14.0)  # slack: down the handguard's side (longer fingers)
ALONG_GRIP = (0.0, 18.0, 0.0)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, TRIGGER_FACE + FINGER_R - 3.0, -57.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(-68.0), None, None, ALONG_GRIP),
    ("r_ring", on_grip(-88.0), None, None, ALONG_GRIP),
    ("r_pinky", on_grip(-108.0, GRIP_BACK + 2.0), None, None, ALONG_GRIP),
    ("r_thumb", (-HALF["housing"] - FINGER_R - 4.0, 348.0, -20.0), None, None, (0.0, -14.0, 0.0)),
    ("l_index", (GUARD_SIDE, PALM[1] + 33.0, -18.0), None, AROUND, DOWN_SIDE),
    ("l_middle", (GUARD_SIDE, PALM[1] + 11.0, -16.0), None, AROUND, DOWN_SIDE),
    ("l_ring", (GUARD_SIDE, PALM[1] - 11.0, -17.0), None, AROUND, DOWN_SIDE),
    ("l_pinky", (GUARD_SIDE, PALM[1] - 31.0, -20.0), None, AROUND, DOWN_SIDE),
    ("l_thumb", (-GUARD_SIDE, PALM[1] + 52.0, -20.0), None, None, (0.0, -15.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), MAG_AXIS),
]
MUZZLE = (0, LENGTH, 0)
SIGHT_REAR, SIGHT_FRONT = (0, SCOPE[0][0], SCOPE_Z), (0, SCOPE[-1][0], SCOPE_Z)  # the scope's axis
# Seen from: the eye left of and over the grip (real metres), looking along the barrel (levelled: it points where he
# aims); the aim values are the Colt's (the game draws its own scope view).
FRAMING = ((-0.175, -0.004, 0.193), 0.0, 0.0, 0.22, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
LEFT_FOREARM = (-0.548, -0.778, -0.306)  # kit's, turned with the levelled framing: runs where it did in the view


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


def narrow(ob, half, taper):
    """Narrows an object (built `half` mm either side of the middle) to taper's half-width at its y1, from its y0."""
    y0, y1, h1 = taper
    for v in ob.data.vertices:
        t = min(1.0, max(0.0, (v.co.y / MM - y0) / (y1 - y0)))
        v.co.x *= 1.0 + t * (h1 / half - 1.0)
    return ob


def piece(name, outline, half, mat=None, chamfer=0.0, segments=3, taper=None):
    """A detailed slab: its side outline `half` mm either side of the middle, its edges rounded."""
    ob = profile_yz(name, outline, -half, half, mat)
    if chamfer:
        bevel(ob, chamfer, segments, 30)
    return narrow(ob, half, taper) if taper else ob


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
    """A low-poly slab: its side outline either side of the middle, its long edges chamfered (`chamfer` mm)."""
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
    return narrow(ob, half, taper) if taper else ob


def hold_turned(web_mm, scale, yaw_deg, pitch_deg):
    """kit.hold_for with his hand turned about the web point: `yaw_deg` its knuckles towards the gun's left (his
    wrist out to the right of the stock), `pitch_deg` its knuckles up."""
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    turn = Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(pitch, 3, "X")
    hx, hy, hz = turn @ Vector((0, 1, 0)), turn @ Vector((-1, 0, 0)), turn @ Vector((0, 0, 1))
    axes = [Vector((hx[i], hy[i], hz[i])) for i in range(3)]  # gun x, y, z in the hand's frame
    w = Vector(web_mm) * MM * scale
    origin = Vector(kit.WEB_IN_HAND) - (axes[0] * w.x + axes[1] * w.y + axes[2] * w.z)
    return [tuple(a) for a in axes] + [tuple(origin)]


def grain(m, scale, dark, light, stretch=(90.0, 6.0, 90.0)):
    """Streaks along y through a material's colour (wood grain, or a moulding's mottling)."""
    nt = m.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = stretch
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 6.0
    noise.inputs["Distortion"].default_value = 1.5
    nt.links.new(mp.outputs["Vector"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.35, (*dark, 1)
    els[1].position, els[1].color = 0.65, (*light, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])
    return m


def materials():
    """Vice City's PSG-1 is grey all over (its metal a little blue), its grip brown, the scope's eyepiece tan."""
    m = kit.photo_materials()
    m["grey"] = material("grey_photo", (0.17, 0.18, 0.21), metallic=0.7, roughness=0.42)
    m["polymer"] = grain(material("polymer_photo", (0.15, 0.155, 0.17), roughness=0.7, spec=0.3), 4.0,
                         (0.12, 0.125, 0.14), (0.17, 0.175, 0.19), (30.0, 30.0, 30.0))
    m["grip"] = grain(material("grip_brown_photo", (0.2, 0.09, 0.045), roughness=0.6, spec=0.3), 1.0,
                      (0.09, 0.04, 0.02), (0.26, 0.13, 0.065))
    m["rubber"] = material("tan_photo", (0.36, 0.29, 0.17), roughness=0.75, spec=0.2)
    m["glass"] = material("glass_photo", (0.01, 0.015, 0.04), metallic=0.2, roughness=0.05, spec=1.0)
    m["red"] = material("laser_lens_photo", (0.6, 0.02, 0.02), metallic=0.1, roughness=0.1, spec=1.0)
    m["pad"] = material("pad_photo", (0.02, 0.02, 0.022), roughness=0.85, spec=0.2)
    return m


def scope_parts(mats, segments, detail=True):
    """The scope, its turret, the claw mount and rings, the laser on top."""
    grey = mats["grey"] if mats else None
    parts = [lathe_y("scope", SCOPE, segments, grey, z=SCOPE_Z)]
    parts.append(box("turret", -11, 11, 425, 455, SCOPE_Z + 8, SCOPE_Z + 27, grey))
    parts.append(box("claw_mount", -12, 12, 384, 520, 22, SCOPE_Z - 14, grey))
    for y0 in (392.0, 494.0):
        parts.append(lathe_y("ring", [(y0, 17.5), (y0 + 16, 17.5)], segments if detail else 6, grey, z=SCOPE_Z))
    parts.append(lathe_y("laser", [(470, 8.5), (548, 8.5)], segments if detail else 6, mats["dark"] if mats else None,
                         z=LASER_Z))
    parts.append(box("laser_mount", -6, 6, 486, 532, SCOPE_Z + 12, LASER_Z - 5, mats["dark"] if mats else None))
    if detail:
        parts.append(lathe_y("eyecup", [(284, 22.0), (318, 22.0), (322, 21.7)], segments, mats["rubber"], z=SCOPE_Z))
        parts.append(cylinder_y("eyepiece", 0, SCOPE_Z, 17.0, 283.0, 284.5, segments, mats["glass"]))
        parts.append(cylinder_y("objective", 0, SCOPE_Z, 21.5, 609.0, 610.6, segments, mats["glass"]))
        parts.append(cylinder_y("laser_lens", 0, LASER_Z, 5.5, 547.5, 549.0, segments, mats["red"]))
        parts.append(lathe_z("windage", 440, SCOPE_Z + 27, SCOPE_Z + 31, 9.0, segments, grey))
        for y in (400, 502):  # the claws' clamp levers on the left
            parts.append(box("claw_lever", -15, -12, y - 6, y + 6, 30, SCOPE_Z - 18, mats["dark"]))
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
    """The photo shoot's PSG-1. Returns (gun, magazine)."""
    grey, poly, dark = mats["grey"], mats["polymer"], mats["dark"]
    parts = [piece("stock", fillet(STOCK, [4, 4, 0, 0, 6, 10, 6], 3), HALF["stock"], poly, 5.0, 3, STOCK_TAPER),
             piece("cheek", fillet(CHEEK, [0, 0, 3, 6, 4], 3), HALF["cheek"], poly, 4.0, 3),
             piece("butt_plate", fillet([(0, 28), (18, 28), (18, -114), (0, -114)], [4, 0, 0, 4], 2), 21.5, mats["pad"],
                   3.0, 2)]
    for z in range(-100, 20, 9):  # the butt plate's ribbing
        parts.append(box("rib", -21.8, 21.8, 0.5, 2.0, z, z + 3, mats["pad"]))
    rec = profile_xz("receiver", fillet(RECEIVER, [1, 1, 3, 6, 6, 3], 3), REC_Y0, REC_Y1, grey)
    bevel(rec, 1.0)
    parts.append(rec)
    parts.append(box("end_cap", -15, 15, 296, 312, -32, 22, grey))
    for sx in (-1, 1):  # the receiver's pressed rib, and the pins through it
        parts.append(box("rib", sx * 16.4, sx * 17.2, 330, 640, 4, 8, dark))
        for y, z in ((322, -14), (342, -38), (430, -38)):
            parts.append(cylinder_x("pin", y, z, 3.2, sx * 14.8, sx * 17.4 if z > -30 else sx * 16.0, 10, mats["steel"]))
    parts.append(box("mag_well", -17.5, 17.5, 446, 526, -42, -26, grey))
    parts.append(cylinder_x("mag_release", 534, -36, 4.0, -18.5, 18.5, 12, dark))
    parts.append(piece("housing", HOUSING, HALF["housing"], poly, 2.0, 2))
    parts.append(piece("grip", fillet(GRIP, [0, 6, 4, 6, 3, 3, 3, 3, 6], 3), HALF["grip"], mats["grip"], 5.0, 3))
    guard = profile_yz("guard", fillet(GUARD, [0, 6, 6, 6, 6, 0, 0, 5, 5, 5, 4, 0], 3), -5.5, 5.5, dark)
    bevel(guard, 0.6)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(TRIGGER, [0, 0, 2, 2, 0, 0], 2), -3.5, 3.5, mats["steel"]))
    hg = piece("handguard", fillet(HANDGUARD, [0, 6, 6, 6, 0], 3), HALF["handguard"], poly, 6.0, 3)
    parts.append(hg)
    for y in range(690, 880, 24):  # the handguard's vents
        for sx in (-1, 1):
            parts.append(box("vent", sx * (HALF["handguard"] - 0.6), sx * (HALF["handguard"] + 0.4), y, y + 12, -32, -8,
                             mats["pad"]))
    parts.append(box("rail", -9, 9, 690, 880, -57, -46, grey))
    parts.append(lathe_y("cocking_tube", [(SPLIT - 10, 11.5), (925, 11.5)], 24, grey, z=TUBE_Z))
    parts.append(box("front_cap", -13, 13, 918, 940, -13, TUBE_Z + 12, grey))
    hood = lathe_y("hood", [(922, 11.0), (938, 11.0)], 24, grey, z=TUBE_Z + 30)
    cut(hood, lathe_y("hole", [(915, 8.0), (945, 8.0)], 24, z=TUBE_Z + 30))
    parts.append(hood)
    parts.append(box("hood_post", -4, 4, 922, 938, TUBE_Z + 8, TUBE_Z + 22, grey))
    parts.append(box("front_post", -1.2, 1.2, 928, 932, TUBE_Z + 20, TUBE_Z + 30, dark))
    parts.append(rod("cocking_handle", (-11, 700, TUBE_Z), (-34, 694, TUBE_Z + 2), 3.5, 10, dark))
    parts.append(lathe_y("cocking_knob", [(686, 5.5), (700, 5.5)], 12, dark, x=-36, z=TUBE_Z + 2))
    parts.append(lathe_y("barrel", [(900, 12.0), (1125, 11.0)], 24, dark))
    brake = lathe_y("brake", [(1122, 14.5), (1206, 14.5), (1208, 13.5)], 24, dark)
    parts.append(brake)
    for y in (1140, 1160, 1180):  # its ports
        for sx in (-1, 1):
            parts.append(box("port", sx * 13.5, sx * 15.0, y, y + 10, -5, 5, mats["pad"]))
    parts.append(cylinder_y("bore", 0, 0, 5.0, 1207.5, 1208.6, 16, mats["pad"]))
    parts += scope_parts(mats, 32)
    for p in parts:
        finish(p, 35)
    gun = join("laser", parts)
    mag = piece("magazine", MAG, HALF["mag"], grey, 1.5, 2)
    ribs = [box("mag_rib", sx * (HALF["mag"] - 0.5), sx * (HALF["mag"] + 0.6), 470, 508, z, z + 3, dark)
            for sx in (-1, 1) for z in (-90, -60, -30)]
    floor = box("mag_floor", -13.5, 13.5, 458, 523, -116, -108, dark)
    mag_parts = [mag, floor] + ribs
    for p in mag_parts:
        finish(p, 35)
    return gun, join("magazine", mag_parts)


def low():
    """The PSG-1 at Vice City's detail, in its moving parts: {part: [objects]}."""
    body = [slab("stock", STOCK, HALF["stock"], 5.0, STOCK_TAPER),
            slab("cheek", CHEEK, HALF["cheek"], 4.0),
            box("butt_plate", -21.5, 21.5, 0, 18, -114, 28),
            profile_xz("receiver", RECEIVER, REC_Y0, REC_Y1 + 4),
            box("end_cap", -15, 15, 296, 312, -32, 22),
            box("mag_well", -17.5, 17.5, 446, 526, -42, -26),
            profile_yz("housing", HOUSING, -HALF["housing"], HALF["housing"]),
            slab("grip", GRIP, HALF["grip"], 4.0),
            profile_yz("guard", GUARD, -5.5, 5.5),
            slab("handguard", [(SPLIT - 4, 4), (905, 4), (905, -30), (895, -46), (SPLIT - 4, -48)], HALF["handguard"], 5.0),
            box("rail", -9, 9, 690, 880, -57, -46),
            lathe_y("cocking_tube", [(SPLIT - 2, 11.5), (925, 11.5)], 6, z=TUBE_Z),
            box("front_cap", -13, 13, 918, 940, -13, TUBE_Z + 12),
            box("hood", -11, 11, 922, 938, TUBE_Z + 8, TUBE_Z + 41),
            rod("cocking_handle", (-11, 700, TUBE_Z), (-40, 694, TUBE_Z + 2), 4.5, 4),
            lathe_y("barrel", [(900, 12.0), (1125, 11.0)], 8),
            lathe_y("brake", [(1122, 14.5), (1208, 14.5)], 8)]
    body += scope_parts(None, 8, detail=False)
    trigger = [profile_yz("trigger", TRIGGER, -3.5, 3.5)]
    magazine = [profile_yz("mag_body", MAG, -HALF["mag"], HALF["mag"]), box("mag_floor", -13.5, 13.5, 458, 523, -116, -108)]
    return {"body": body, "trigger": trigger, "magazine": magazine}


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
    scale = kit.scale_for(game_data, "laser", LENGTH)
    frame = hold_turned(WEB, scale, HAND_YAW, HAND_PITCH)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    gun, mag = detailed(materials())
    every = [gun, mag]
    rgba = kit.atlas(scene, REGIONS, {"left": every, "left_front": every, "top": every, "top_front": every,
                                      "back": every, "front": every, "magazine": [mag]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "laser-atlas.png")
    kit.save_png(atlas_png, flat)
    choose = chooser(seen_by([gun, mag]))
    gun.hide_render = mag.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            project(ob, (lambda n, c: "magazine") if ob.name.startswith("mag_body") else choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING, two_handed=True, hold_left=hold_left, left_forearm=LEFT_FOREARM)
    print(f"laser: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "laser-preview.png"), [o for o, _ in objects], atlas_png,
                [((-1.8, 0.6, 0.0), (0, 0.6, -0.02), 50), ((0.9, 1.2, 0.45), (0, 0.5, 0.0), 35),
                 ((-0.17, 0.05, 0.12), (0.0, 0.65, 0.0), 30)])


main()
