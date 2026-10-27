"""Vice City's flamethrower for the first-person viewmodel: a self-contained unit, its fuel tank and propane bottle
slung under the receiver, a heat shield over the hands, a slotted shroud round the fuel tube and the nozzle at the
front. Held at the hip in both hands: his right on the upright handle at the back of the stock (the trigger under
its cap), his left palm up under the shroud's plain rear half.

blender -b -P tools/guns/flame.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the fuel tube, z up, x to the right; the stock's back at y = 0, the tube's axis at
z = 0. It's the game's own design (no real weapon), its parts at the sizes the game's model has them, scaled to the
game's model like the other guns (its length over the game model's x extent).
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

REAL_LENGTH = 1174.0
RAKE = math.tan(math.radians(15))  # the handle's rake back from vertical
POST_BOTTOM, POST_TOP, POST_DEPTH, POST_W = -9.0, 95.0, 46.0, 15.0


def back_strap_y(z):
    return 40.0 + (z - POST_BOTTOM) * RAKE


def front_strap_y(z):
    return back_strap_y(z) + POST_DEPTH


STOCK = (0.0, 200.0, -64.0, -9.0, 13.0)  # y from, y to, z from, z to, half width
CAP = (55.0, 172.0, POST_TOP, 112.0, 15.0)
RECEIVER = (200.0, 440.0, -88.0, 34.0, 30.0)
RAISED = (287.0, 430.0, 34.0, 60.0, 22.0)
SHROUD = (425.0, 830.0, 27.5)
SPLIT_Y = 590.0  # the shroud's plain rear half ends; the slots, and the front photo, begin
TUBE = (830.0, 1072.0, 11.0)
NOZZLE = [(1066, 0), (1068, 24), (1174, 26), (1174, 14), (1164, 0)]  # (y, radius), round the tube's axis
TANK = (-8.0, 413.0, [(-86, 0), (-86, 40), (-100, 57), (-112, 60), (-358, 60), (-374, 48), (-386, 0)])  # x, y, (z, r)
BOTTLE = (-30.0, 317.0, [(-86, 0), (-86, 16), (-98, 29), (-106, 31), (-276, 31), (-288, 22), (-292, 0)])
ROD_PATH = [(512.0, -20.0), (512.0, -92.0), (512.0, -104.0), (497.0, -134.0), (472.0, -148.0)]  # the front handle (y, z)
ROD_R = 7.0
TRIGGER = [(133, 95), (139, 95), (139.5, 80), (135, 69), (131.5, 71), (134, 82)]
GUARD = [(112, 95), (170, 95), (170, 79), (159, 65), (100, 65)]
GUARD_OPENING = [(118, 99), (163, 99), (163, 80), (155, 71), (104, 71)]
REAR_HANDLE = [(296, 60), (316, 60), (316, 86), (300, 93), (276, 93), (276, 88), (296, 81)]
# The heat shield: across (x, y) its middle and the wings bent back, standing from the receiver's top and leaning
# forward.
SHIELD_PLAN = [(-72.0, -22.0), (-56.0, 0.0), (56.0, 0.0), (72.0, -22.0)]
SHIELD = (446.0, 55.0, 118.0, 24.0, 3.0)  # y at its foot, z foot, z top, how far forward its top leans, thickness


def lathe(name, axis, centre, profile, segments, mat=None):
    """A solid turned round an axis along y or z through `centre` (x, z or x, y): profile [(along, radius)], with a
    radius of 0 closing an end. A face looks straight along each of the other axes."""
    bm = bmesh.new()
    rings = []
    for t, r in profile:
        ring = []
        for k in range(segments if r > 0 else 1):
            a = 2 * math.pi * (k + 0.5) / segments
            c, s = (r * math.cos(a), r * math.sin(a)) if r > 0 else (0.0, 0.0)
            p = (centre[0] + c, t, centre[1] + s) if axis == "y" else (centre[0] + c, centre[1] + s, t)
            ring.append(bm.verts.new(Vector(p) * MM))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
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
    for ring in (rings[0], rings[-1]):
        if len(ring) > 2:
            bm.faces.new(ring)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def shield(name, mat=None):
    """The heat shield: its plan (x, y) given a thickness, stood up from its foot to its top, leaning forward."""
    y0, z0, z1, lean, th = SHIELD
    bm = bmesh.new()
    plan = [(x, y) for x, y in SHIELD_PLAN] + [(x, y - th) for x, y in reversed(SHIELD_PLAN)]
    lo = [bm.verts.new(Vector((x, y0 + y, z0)) * MM) for x, y in plan]
    hi = [bm.verts.new(Vector((x, y0 + y + lean, z1)) * MM) for x, y in plan]
    bm.faces.new(lo)
    bm.faces.new(hi)
    n = len(plan)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def strip(path, half):
    """A path (y, z) widened `half` mm each way, as a closed outline."""
    left, right = [], []
    for i, p in enumerate(path):
        a, b = Vector(path[max(i - 1, 0)]), Vector(path[min(i + 1, len(path) - 1)])
        d = (b - a).normalized()
        n = Vector((-d.y, d.x))
        left.append(tuple(Vector(p) + n * half))
        right.append(tuple(Vector(p) - n * half))
    return left + right[::-1]


def post_outline(rounding=1):
    return fillet([(back_strap_y(POST_BOTTOM - 2), POST_BOTTOM - 2), (front_strap_y(POST_BOTTOM - 2), POST_BOTTOM - 2),
                   (front_strap_y(POST_TOP), POST_TOP), (back_strap_y(POST_TOP), POST_TOP)], [0, 0, 0, 0], rounding)


def receiver_section():
    y0, y1, z0, z1, w = RECEIVER
    return [(-w + 4, z0), (w - 4, z0), (w, z0 + 4), (w, z1 - 3), (w - 3, z1), (-w + 3, z1), (-w, z1 - 3), (-w, z0 + 4)]


# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
REGIONS = {
    "left_rear": ((0, 0, 256, 96), (-400, 300, 15), (1, 0, 0), (0, 0, 1), 2.4),
    "left_front": ((0, 96, 256, 34), (-400, 884, -6), (1, 0, 0), (0, 0, 1), 592 / 256),
    "top_rear": ((0, 130, 256, 22), (0, 300, 400), (0, 0, -1), (-1, 0, 0), 2.4),
    "top_front": ((0, 152, 256, 22), (0, 884, 400), (0, 0, -1), (-1, 0, 0), 592 / 256),
    "tanks": ((0, 174, 104, 82), (-400, 392, -240), (1, 0, 0), (0, 1, 0), 3.2),
    "back_grip": ((104, 174, 24, 82), (0, -400, 24), (0, 1, 0), (0, 0, 1), 2.4),
    "back_body": ((128, 174, 32, 82), (0, -400, 7.5), (0, 1, 0), (0, 0, 1), 2.5),
    "shield": ((160, 174, 96, 82), (0, -400, 100), (0, 1, 0), (0, 0, 1), 1.9),
}

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface). His right hand round the
# upright handle as round the Colt's grip: the index on the trigger under the cap, the others on the handle's left
# side 30 mm behind its front, the thumb along the cap. His left palm up under the shroud, its fingers round the far
# side, the thumb up the near side.
FINGER_R = 8.0
WEB = (0.0, back_strap_y(89.0), 89.0)
PALM = (-4.0, 560.0, -SHROUD[2] - 3.0)
LEFT_ROLL = 0.0
LEFT_FOREARM = (-0.431, -0.633, -0.643)  # turned with the levelled framing: runs where it did in the view


def on_post(z, back=30.0):
    return (-POST_W - FINGER_R, front_strap_y(z) - back, z)


FORWARD = (0.0, 20.0, 20.0 * RAKE)
SIDE_R = SHROUD[2] + FINGER_R
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, TRIGGER[2][0] + FINGER_R - 4.0, 84.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_post(59.0), None, None, FORWARD),
    ("r_ring", on_post(42.0), None, None, FORWARD),
    ("r_pinky", on_post(26.0, 32.0), None, None, FORWARD),
    ("r_thumb", (-CAP[4] - FINGER_R, 108.0, 97.0), None, None, (0.0, -12.0, 0.0)),
    ("l_index", (SIDE_R, PALM[1] + 26, -2.0), None, None, (0.0, 0.0, 10.0)),
    ("l_middle", (SIDE_R, PALM[1] + 7, 0.0), None, None, (0.0, 0.0, 10.0)),
    ("l_ring", (SIDE_R, PALM[1] - 12, -2.0), None, None, (0.0, 0.0, 10.0)),
    ("l_pinky", (SIDE_R, PALM[1] - 29, -6.0), None, None, (0.0, 0.0, 10.0)),
    ("l_thumb", (-SIDE_R - 1.0, PALM[1] + 40, 6.0), None, (-1.0, 0.0, -0.3), (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
# The muzzle is the nozzle's end. It has no sights: aimed, it's looked along just over the shield's top edge.
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, NOZZLE[2][0], 0), (0, 300, 123), (0, NOZZLE[2][0], 123)
# Seen from: up and to the left of the handle, the gun at his hip low on the right (its tanks under the receiver at
# the bottom of the view, his right hand on the handle at the edge), the nozzle short of the crosshair; aiming, 0.30 m
# behind the line over the shield and 0.02 m above it.
FRAMING = ((-0.261, -0.144, 0.109), 1.0, 0.0, 0.3, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)


def weathered(name, dark, light, scale=60.0, metallic=0.7, roughness=0.5):
    """A painted or bare metal, mottled (the photo's grain: Vice City's textures are weathered)."""
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
    mats["alu"] = weathered("alu", (0.36, 0.40, 0.38), (0.60, 0.64, 0.61))
    mats["tank"] = weathered("tank_paint", (0.55, 0.30, 0.23), (0.74, 0.47, 0.37), 25.0, 0.0, 0.45)
    mats["red"] = weathered("bottle_paint", (0.50, 0.07, 0.02), (0.68, 0.14, 0.04), 25.0, 0.0, 0.35)
    mats["green"] = material("label_green", (0.06, 0.32, 0.16), roughness=0.6)
    mats["white"] = material("label_white", (0.85, 0.85, 0.82), roughness=0.6)
    return mats


def detailed(mats):
    """The photo shoot's flamethrower, in the groups its photos need: {name: object}."""
    alu, dark, grip, steel = mats["alu"], mats["dark"], mats["grip"], mats["steel"]
    # The stock bar and the upright handle at its back: chequered grips on the handle, its cap, guard and trigger.
    y0, y1, z0, z1, w = STOCK
    stock = profile_xz("stock", fillet([(-w, z0), (w, z0), (w, z1), (-w, z1)], 3, 2), y0 + 6, y1 + 2, alu)
    bevel(stock, 0.8)
    rear = [stock, profile_xz("end", fillet([(-w - 1.5, z0 - 1.5), (w + 1.5, z0 - 1.5), (w + 1.5, z1 + 1.5),
                                             (-w - 1.5, z1 + 1.5)], 4, 2), y0, y0 + 8, dark)]
    for y in (30, 170):
        rear.append(cylinder_x("bolt", y, (z0 + z1) / 2, 3.2, -w - 1, w + 1, 10, steel))
    post = profile_yz("post", post_outline(3), -POST_W + 2.5, POST_W - 2.5, dark)
    bevel(post, 1.0)
    rear.append(post)
    panel = fillet([(back_strap_y(2) + 4, 2), (front_strap_y(2) - 4, 2), (front_strap_y(POST_TOP - 6) - 4, POST_TOP - 6),
                    (back_strap_y(POST_TOP - 6) + 4, POST_TOP - 6)], 4, 2)
    for x0, x1 in ((POST_W - 2.6, POST_W), (-POST_W, -POST_W + 2.6)):
        p = profile_yz("panel", panel, x0, x1, grip)
        bevel(p, 1.0, 1, 30)
        rear.append(p)
    y0, y1, z0, z1, w = CAP
    cap = profile_xz("cap", fillet([(-w, z0), (w, z0), (w, z1 - 4), (w - 4, z1), (-w + 4, z1), (-w, z1 - 4)], 2, 2),
                     y0, y1, alu)
    bevel(cap, 1.0)
    rear.append(cap)
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 7, 6, 0], 3), -4.5, 4.5, alu)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 6, 5, 0], 3), -10, 10))
    rear.append(guard)
    rear.append(profile_yz("trigger", TRIGGER, -3.2, 3.2, dark))
    # The receiver with its raised top, the folding handle on it, the regulator and the front handle under it.
    y0, y1, z0, z1, w = RECEIVER
    rec = profile_xz("receiver", fillet(receiver_section(), 1.5, 2), y0, y1, alu)
    cutters = [box("seam", -w - 2, w + 2, y, y + 1.5, z0 - 2, z1 + 2) for y in (262, 380)]
    cutters.append(box("vent", -w - 2, -w + 1.2, 300, 360, -40, -20))
    cut(rec, join("cutters", cutters))
    bevel(rec, 0.8)
    body = [rec]
    for y in (214, 426):
        for z in (-74, 20):
            for sx in (-1, 1):
                body.append(cylinder_x("screw", y, z, 3.0, min(sx * w, sx * (w + 1.2)), max(sx * w, sx * (w + 1.2)), 8, steel))
    y0, y1, z0, z1, w = RAISED
    top = profile_xz("raised", fillet([(-w, z0 - 2), (w, z0 - 2), (w, z1 - 5), (w - 5, z1), (-w + 5, z1), (-w, z1 - 5)], 2, 2),
                     y0, y1, alu)
    bevel(top, 0.8)
    body += [top, profile_yz("handle", fillet(REAR_HANDLE, [0, 0, 4, 3, 0, 0, 3], 2), -9, 9, steel)]
    for x in (-12, 0, 12):
        body.append(box("rib", x - 1.2, x + 1.2, y0 + 12, y1 - 12, z1 - 1, z1 + 1, steel))
    body.append(box("regulator", 4, 24, 330, 356, -190, RECEIVER[2] + 2, dark))
    body.append(cylinder_y("gauge", 14, -150, 9, 356, 362, 14, steel))
    rod = strip(ROD_PATH, ROD_R)
    body.append(profile_yz("rod", fillet(rod, [0, 0, 4, 4, 0, 0, 4, 4, 0, 0], 3), -ROD_R, ROD_R, steel))
    # The shield, its top edge rolled, riveted to its brackets.
    sy, sz0, sz1, lean, th = SHIELD
    parts = [shield("shield", alu), cylinder_x("lip", sy + lean - th / 2, sz1, 3.2, -56, 56, 10, alu)]
    for z in (64, 108):
        back = sy - th + lean * (z - sz0) / (sz1 - sz0)
        for x in (-42, -14, 14, 42):
            parts.append(cylinder_y("rivet", x, z, 2.6, back - 1.2, back + 1, 8, steel))
    for o in parts:
        finish(o, 35)
    shield_ob = join("shield", parts)
    # The shroud (plain behind, slotted in front), the fuel tube, the igniter line and the nozzle.
    y0, y1, r = SHROUD
    shroud = lathe("shroud", "y", (0, 0), [(y0, 0), (y0, r - 3), (y0 + 3, r), (y1 - 3, r), (y1, r - 3), (y1, 0)], 20, steel)
    slots = []
    for k in range(9):
        y = SPLIT_Y + 14 + k * 24
        for a in (0, 1, 2, 3, 4, 5, 7):  # all round but underneath
            u, t = Vector((math.cos(a * math.pi / 4), math.sin(a * math.pi / 4))), Vector((0, 0))
            t = Vector((-u.y, u.x))
            ring = [u * (r - 4) - t * 5, u * (r - 4) + t * 5, u * (r + 4) + t * 5, u * (r + 4) - t * 5]
            slots.append(profile_xz("slot", [tuple(p) for p in ring], y, y + 12))
    cut(shroud, join("slots", slots))
    front = [shroud, cylinder_y("band", 0, 0, r + 1.2, y0 + 4, y0 + 14, 20, dark),
             cylinder_y("band", 0, 0, r + 1.2, SPLIT_Y - 6, SPLIT_Y + 4, 20, dark),
             cylinder_y("tube", 0, 0, TUBE[2], TUBE[0] - 4, TUBE[1] + 4, 16, steel),
             profile_yz("igniter", [(832, -31), (1078, -23), (1078, -17), (832, -25)], -3, 3, dark)]
    nozzle = lathe("nozzle", "y", (0, 0), NOZZLE, 16, alu)
    cut(nozzle, cylinder_y("cup", 0, 0, 14, 1150, 1180, 14))
    front += [nozzle, cylinder_y("lip", 0, 0, 27, 1150, 1156, 16, dark)]
    # The tanks: the fuel tank, painted, labelled on the left; the propane bottle, red, with its valve.
    tank = lathe("tank", "z", TANK[:2], TANK[2], 24, mats["tank"])
    tx, ty = TANK[:2]
    tanks = [tank, box("label", tx - 63, tx - 57, ty - 20, ty + 20, -150, -114, mats["green"]),
             box("mark", tx - 62.5, tx - 57, ty - 24, ty + 24, -180, -160, mats["white"]),
             box("stripe", tx - 62.2, tx - 57, ty - 5, ty + 5, -340, -190, mats["white"]),
             lathe("band", "z", TANK[:2], [(-118, 0), (-118, 61), (-126, 61), (-126, 0)], 24, dark),
             lathe("band", "z", TANK[:2], [(-344, 0), (-344, 61), (-352, 61), (-352, 0)], 24, dark)]
    bx, by = BOTTLE[:2]
    tanks += [lathe("bottle", "z", BOTTLE[:2], BOTTLE[2], 20, mats["red"]),
              lathe("collar", "z", BOTTLE[:2], [(-262, 0), (-262, 32), (-270, 32), (-270, 0)], 20, dark),
              box("mark", bx - 33, bx - 28, by - 10, by + 10, -240, -200, mats["white"])]
    groups = {"rear": rear, "body": body, "shield": [shield_ob], "front": front, "tanks": tanks}
    out = {}
    for name, obs in groups.items():
        for o in obs:
            finish(o, 35)
        out[name] = join(name, obs) if len(obs) > 1 else obs[0]
    return out


def low():
    """The flamethrower at Vice City's detail, in its moving parts: {part: [(object, its photos)]}."""
    y0, y1, z0, z1, w = STOCK
    rear = [box("stock", -w, w, y0, y1 + 2, z0, z1), profile_yz("post", post_outline(), -POST_W, POST_W)]
    y0, y1, z0, z1, w = CAP
    rear.append(profile_xz("cap", [(-w, z0), (w, z0), (w, z1 - 4), (w - 4, z1), (-w + 4, z1), (-w, z1 - 4)], y0, y1))
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 7, 6, 0], 1), -4.5, 4.5)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 6, 5, 0], 1), -10, 10), solver="EXACT")
    rear.append(guard)
    y0, y1, z0, z1, w = RECEIVER
    body = [profile_xz("receiver", receiver_section(), y0, y1)]
    y0, y1, z0, z1, w = RAISED
    body += [profile_xz("raised", [(-w, z0 - 2), (w, z0 - 2), (w, z1 - 5), (w - 5, z1), (-w + 5, z1), (-w, z1 - 5)], y0, y1),
             profile_yz("handle", REAR_HANDLE, -9, 9),
             profile_yz("rod_top", strip(ROD_PATH[:2], ROD_R), -ROD_R, ROD_R),
             lathe("shroud_rear", "y", (0, 0), [(SHROUD[0], 0), (SHROUD[0], SHROUD[2]), (SPLIT_Y, SHROUD[2]), (SPLIT_Y, 0)], 8)]
    front = [lathe("shroud_front", "y", (0, 0), [(SPLIT_Y, 0), (SPLIT_Y, SHROUD[2]), (SHROUD[1], SHROUD[2]),
                                                  (SHROUD[1], 0)], 8),
             lathe("tube", "y", (0, 0), [(TUBE[0], 0), (TUBE[0], TUBE[2]), (TUBE[1], TUBE[2]), (TUBE[1], 0)], 6),
             lathe("nozzle", "y", (0, 0), NOZZLE, 8),
             profile_yz("igniter", [(832, -31), (1078, -23), (1078, -17), (832, -25)], -3, 3)]
    tanks = [lathe("tank", "z", TANK[:2], [(-86, 0), (-86, 52), (-104, 60), (-360, 60), (-386, 0)], 8),
             lathe("bottle", "z", BOTTLE[:2], [BOTTLE[2][0], (-86, 26), (-100, 31), (-278, 31), (-292, 0)], 8),
             box("regulator", 4, 24, 330, 356, -190, RECEIVER[2] + 2),
             profile_yz("rod_low", strip(ROD_PATH[1:], ROD_R), -ROD_R, ROD_R)]
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    return {"body": [(o, choose_rear) for o in rear] + [(o, choose_body) for o in body] + [(o, choose_front) for o in front]
            + [(o, choose_tanks) for o in tanks] + [(shield("shield"), choose_shield)],
            "trigger": [(o, choose_rear) for o in trigger]}


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


def _choose(normal, centre, back):
    half = "rear" if centre.y < SPLIT_Y else "front"
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return ["top_" + half, "left_" + half]
    if ax == 1 and normal.y < 0:
        return [back, "left_" + half]
    return ["left_" + half]


def choose_rear(normal, centre):
    """The stock and the handle: their backs from the photo of the handle from behind."""
    return _choose(normal, centre, "back_grip")


def choose_body(normal, centre):
    return _choose(normal, centre, "back_body")


def choose_front(normal, centre):
    return _choose(normal, centre, "back_body")


def choose_tanks(normal, centre):
    return ["tanks"]


def choose_shield(normal, centre):
    return ["shield"]


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "flame", REAL_LENGTH)
    hold = kit.hold_for(WEB, scale)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    d = detailed(materials())
    rear, body, shield_ob, front, tanks = d["rear"], d["body"], d["shield"], d["front"], d["tanks"]
    rgba = kit.atlas(scene, REGIONS, {"left_rear": [rear, body, front], "left_front": [front],
                                      "top_rear": [rear, body, front], "top_front": [front],
                                      "tanks": [body, tanks], "back_grip": [rear],
                                      "back_body": [body, front], "shield": [shield_ob]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "flame-atlas.png")
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
                          FRAMING, two_handed=True, hold_left=hold_left, left_forearm=LEFT_FOREARM)
    print(f"flame: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in hold[3]]}")
    kit.preview(os.path.join(work, "flame-preview.png"), [o for o, _ in objects], atlas_png,
                [((-1.2, 0.55, -0.05), (0, 0.55, -0.12), 50), ((0.9, 1.1, 0.4), (0, 0.5, -0.1), 50),
                 ((-0.30, -0.20, 0.30), (0.0, 0.7, -0.10), 35)])


main()
