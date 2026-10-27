"""Vice City's silenced Ingram: an Ingram MAC-10 (269 mm with its stock collapsed) in the game's grey parkerized
finish, its 30-round magazine in the grip, the suppressor on (548 mm overall with it), held one-handed, for the
first-person viewmodel.

blender -b -P tools/guns/ingramsl.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right; the stock's butt plate at y = 0, the grip's foot at
z = 0, the bore 119 up. Built as tools/guns/colt45.py (the worked example) is. The cocking knob on top is the bolt.

The game's own model of it has no suppressor: it's measured (for the scale) as the bare MAC-10 it is, 269 mm.
SUPPRESSOR = False builds it that way too.
"""
import math
import os
import sys

import bmesh

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, _object, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

SUPPRESSOR = True
B = 119.0  # the bore's height
GAME_LENGTH = 269.0  # the bare gun, butt to muzzle, as the game's model has it
REC_Y0, REC_Y1 = 9.0, 236.0
REC_BOTTOM, REC_TOP, REC_HALF = 86.0, 139.0, 19.0
# The grip: its back, front, half width. Upright, as the MAC-10's is, and no more than a sleeve round the magazine.
GRIP_Y0, GRIP_Y1, GRIP_X = 106.0, 146.0, 14.0
THREAD_Y1 = 262.0
SUP_Y0, SUP_Y1, SUP_R = 257.0, 548.0, 25.0  # the suppressor
MUZZLE_Y = SUP_Y1 if SUPPRESSOR else 269.0
MAG = (108.5, 143.5, 12.5, 126.0, -110.0)  # back, front, half width, top, body's foot
KNOB = (175.0, 7.0)  # the cocking knob, forward: its middle (y), radius


SIDE_MMPP = 1.22
REGIONS = {
    "left": ((0, 0, 256, 128), (-400, 152, 76), (1, 0, 0), (0, 0, 1), SIDE_MMPP),
    "top": ((0, 128, 256, 40), (0, 152, 400), (0, 0, -1), (-1, 0, 0), SIDE_MMPP),
    "suppressor": ((0, 168, 256, 40), (-400, 404.5, B), (1, 0, 0), (0, 0, 1), 1.3),
    # The magazine lying along the atlas, its foot to the left.
    "magazine": ((0, 208, 192, 40), (-400, 126.0, 6.0), (1, 0, 0), (0, 1, 0), 1.25),
    "back": ((192, 208, 64, 48), (0, -400, 118), (0, 1, 0), (0, 0, 1), 1.4),
}

FINGER_R = 8.0
GRIP_BACK = 14.0


def on_grip(z, back=GRIP_BACK):
    return (-GRIP_X - FINGER_R, GRIP_Y1 - back, z)


FORWARD = (0.0, 20.0, 0.0)  # towards the grip's front edge
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 163.3 + FINGER_R, 76.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(48.0), None, None, FORWARD),
    ("r_ring", on_grip(31.0), None, None, FORWARD),
    ("r_pinky", on_grip(14.0, GRIP_BACK + 2.0), None, None, FORWARD),
    ("r_thumb", (-REC_HALF - 2.0 - FINGER_R, 128.0, 90.0), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), (0, 0, -1)),
]
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, B), (0, 20, 145.0), (0, 219, 147.0)
# The back of the grip, under the receiver; the gun 2 mm to his left of the Colt's line, so the wide receiver clears
# his knuckles.
WEB = (2.0, GRIP_Y0, REC_BOTTOM - 12.0)
FRAMING = ((-0.232, -0.312, 0.177), 10.0, 0.0, 0.24, 0.02)  # pulled back: the whole gun and his hand in view; levelled: its barrel where he aims (vcmm-viewmodel --level)


def photo_materials():
    mats = kit.photo_materials()
    # The game's Ingram: grey parkerizing, a little blue, matte; a darker magazine; the suppressor matte black.
    mats["receiver"] = material("mac_grey", (0.36, 0.38, 0.42), metallic=0.6, roughness=0.55)
    mats["magazine"] = material("mac_mag", (0.22, 0.23, 0.26), metallic=0.6, roughness=0.5)
    sup = material("mac_suppressor", (0.035, 0.035, 0.04), roughness=0.8, spec=0.3)
    nt = sup.node_tree
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 1400.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.3
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])
    mats["suppressor"] = sup
    return mats


def prism_z(name, outline, z0, z1, mat=None):
    """A footprint (x, y, mm) extruded up z0..z1 (mm)."""
    bm = bmesh.new()
    v0 = [bm.verts.new((x * MM, y * MM, z0 * MM)) for x, y in outline]
    v1 = [bm.verts.new((x * MM, y * MM, z1 * MM)) for x, y in outline]
    bm.faces.new(v0)
    bm.faces.new(list(reversed(v1)))
    n = len(outline)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((v0[i], v1[i], v1[j], v0[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def knob_footprint(segments):
    y, r = KNOB
    return [(r * math.cos(2 * math.pi * (k + 0.5) / segments), y + r * math.sin(2 * math.pi * (k + 0.5) / segments))
            for k in range(segments)]


def receiver_section():
    h = REC_HALF
    right = [(h - 1.5, REC_BOTTOM), (h, REC_BOTTOM + 1.5), (h, REC_TOP - 1.5), (h - 1.5, REC_TOP)]
    return right + [(-x, z) for x, z in reversed(right)]


def sight_section(top, inner, outer, mid_top, mid_half):
    """A sight's guard ears and blade between them, as a cross-section (x, z) from the receiver's top."""
    z0 = REC_TOP - 1.0
    return [(-outer, z0), (outer, z0), (outer, top), (inner, top), (inner, mid_top - 4.0), (mid_half, mid_top - 4.0),
            (mid_half, mid_top), (-mid_half, mid_top), (-mid_half, mid_top - 4.0), (-inner, mid_top - 4.0), (-inner, top),
            (-outer, top)]


def grip_outline():
    return [(GRIP_Y1, REC_BOTTOM + 2.0), (GRIP_Y1, 3.0), (GRIP_Y1 - 3.0, 0.0), (GRIP_Y0 + 3.0, 0.0), (GRIP_Y0, 3.0),
            (GRIP_Y0, REC_BOTTOM + 2.0)]


def guard_outline():
    return [(GRIP_Y1 - 4.0, REC_BOTTOM + 2.0), (204.0, REC_BOTTOM + 2.0), (204.0, 61.0), (199.0, 56.0),
            (GRIP_Y1 - 4.0, 56.0)]


def guard_opening():
    return [(GRIP_Y1 + 0.5, REC_BOTTOM + 4.0), (200.0, REC_BOTTOM + 4.0), (200.0, 62.5), (196.5, 60.0),
            (GRIP_Y1 + 0.5, 60.0)]


def trigger_outline():
    return [(156.0, REC_BOTTOM + 1.0), (162.0, REC_BOTTOM + 1.0), (163.5, 78.0), (162.8, 71.0), (160.0, 66.0),
            (158.5, 67.0), (159.6, 74.0), (156.5, 81.0)]


def butt_outline():
    """The collapsed stock's butt plate, upright behind the receiver."""
    return [(0.0, 72.0), (7.0, 72.0), (7.0, 134.0), (0.0, 134.0)]


def magazine_body(name, mat=None):
    back, front, half, top, foot = MAG
    return box(name, -half, half, back, front, foot, top, mat)


def floor_plate(name, mat=None):
    back, front, half, top, foot = MAG
    return box(name, -half - 1.5, half + 1.5, back - 2.0, front + 3.0, foot - 4.5, foot + 0.5, mat)


def detailed(mats):
    """The photo shoot's Ingram. Returns (gun, magazine, knob, suppressor or None)."""
    rec, dark, steel = mats["receiver"], mats["dark"], mats["steel"]
    parts = []
    receiver = profile_xz("receiver", receiver_section(), REC_Y0, REC_Y1, rec)
    cutters = [box("slot", -3.4, 3.4, 96.0, KNOB[0] + KNOB[1] + 2.0, REC_TOP - 2.5, REC_TOP + 2),
               box("port", REC_HALF - 2.0, REC_HALF + 2, 150.0, 190.0, 112.0, 128.0)]
    for sx in (-1, 1):
        for z in (124.0, 129.0, 134.0):  # the grooves along its sides, as the game's has them
            cutters.append(box("groove", sx * REC_HALF - 0.9, sx * REC_HALF + 0.9, 92.0, 206.0, z, z + 1.6))
        # The channels the stock's struts slide in, along the bottom.
        cutters.append(box("channel", sx * REC_HALF - 1.4, sx * REC_HALF + 1.4, REC_Y0 - 1, 98.0, 89.0, 95.0))
    cut(receiver, join("cutters", cutters))
    bevel(receiver, 0.5, 1, 40)
    parts.append(receiver)
    parts.append(box("slot_dark", -3.6, 3.6, 95.0, KNOB[0] + KNOB[1] + 3.0, REC_TOP - 3.0, REC_TOP - 2.2, dark))
    parts.append(box("port_bolt", REC_HALF - 2.6, REC_HALF - 1.8, 149.0, 191.0, 111.0, 129.0, dark))
    for sx in (-1, 1):
        parts.append(box("strut", sx * REC_HALF - 1.0, sx * REC_HALF + 1.0, 6.0, 96.0, 90.0, 94.0, steel))
    for y, z in ((30.0, 96.0), (224.0, 96.0), (60.0, 130.0)):
        parts.append(cylinder_x("pin", y, z, 2.0, -REC_HALF - 0.4, REC_HALF + 0.4, 10, steel))
    rear = profile_xz("rear_sight", sight_section(150.0, 6.5, 10.0, 147.0, 3.5), 14.0, 26.0, rec)
    cut(rear, cylinder_y("aperture", 0, 145.0, 1.2, 10, 30, 10))
    bevel(rear, 0.5)
    front = profile_xz("front_sight", sight_section(152.0, 6.0, 9.0, 147.0, 1.2), 210.0, 228.0, rec)
    bevel(front, 0.5)
    parts += [rear, front]
    thread = cylinder_y("thread", 0, B, 7.5, REC_Y1 - 2.0, THREAD_Y1 if SUPPRESSOR else 269.0, 24, steel)
    cut(thread, cylinder_y("bore", 0, B, 4.6, 240.0, 280.0, 16))
    parts.append(thread)
    # The grip (upright, part of the lower), its magazine well; the guard, the trigger; the stock's butt plate.
    grip = profile_yz("grip", fillet(grip_outline(), [0, 3, 3, 3, 3, 0], 3), -GRIP_X, GRIP_X, rec)
    cut(grip, join("grip_grooves", [box("g", sx * (GRIP_X - 0.8), sx * (GRIP_X + 2), GRIP_Y0 + 6, GRIP_Y1 - 6, z, z + 1.4)
                                    for z in range(10, 78, 7) for sx in (-1, 1)]))
    bevel(grip, 1.2, 1, 30)
    parts.append(grip)
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 3, 4, 0], 3), -4.5, 4.5, rec)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 2, 3, 0], 3), -10, 10))
    bevel(guard, 0.5)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(trigger_outline(), [0, 0, 3, 3, 1, 1, 3, 0], 2), -3.0, 3.0, steel))
    parts.append(profile_yz("catch", fillet([(GRIP_Y0 - 3, 2), (GRIP_Y0 + 4, 2), (GRIP_Y0 + 4, -5), (GRIP_Y0 - 3, -5)],
                                            1, 2), -6, 6, dark))
    butt = profile_yz("butt", fillet(butt_outline(), [2, 2, 2, 2], 2), -REC_HALF + 1, REC_HALF - 1, dark)
    bevel(butt, 0.8)
    parts.append(butt)
    for p in parts:
        finish(p, 35)
    gun = join("ingram", parts)
    mag = magazine_body("magazine", mats["magazine"])
    bevel(mag, 1.2)
    floor = floor_plate("floor", dark)
    bevel(floor, 0.8)
    for p in (mag, floor):
        finish(p, 35)
    knob = prism_z("knob", knob_footprint(24), REC_TOP - 2.0, 152.0, rec)
    cut(knob, box("notch", -1.6, 1.6, KNOB[0] - 10, KNOB[0] + 10, 148.0, 160.0))
    bevel(knob, 0.6)
    finish(knob, 35)
    sup = None
    if SUPPRESSOR:
        tube = cylinder_y("suppressor", 0, B, SUP_R, SUP_Y0 + 8.0, SUP_Y1 - 6.0, 48, mats["suppressor"])
        rear_cap = cylinder_y("sup_rear", 0, B, SUP_R + 0.6, SUP_Y0, SUP_Y0 + 9.0, 48, dark)
        front_cap = cylinder_y("sup_front", 0, B, SUP_R + 0.4, SUP_Y1 - 7.0, SUP_Y1, 48, dark)
        cut(front_cap, cylinder_y("sup_bore", 0, B, 5.5, SUP_Y1 - 3, SUP_Y1 + 5, 16))
        for o in (rear_cap, front_cap):
            bevel(o, 1.0)
        for o in (tube, rear_cap, front_cap):
            finish(o, 35)
        sup = join("suppressor", [tube, rear_cap, front_cap])
    return gun, join("magazine", [mag, floor]), knob, sup


def low():
    """The Ingram at Vice City's detail, in its moving parts: {part: [objects]}."""
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 2, 3, 0], 1), -4.5, 4.5)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 2, 2, 0], 1), -10, 10), solver="EXACT")
    body = [profile_xz("receiver", receiver_section(), REC_Y0, REC_Y1),
            profile_xz("rear_sight", sight_section(150.0, 6.5, 10.0, 147.0, 3.5), 14.0, 26.0),
            profile_xz("front_sight", sight_section(152.0, 6.0, 9.0, 147.0, 1.2), 210.0, 228.0),
            cylinder_y("thread", 0, B, 7.5, REC_Y1 - 2.0, THREAD_Y1 if SUPPRESSOR else 269.0, 6),
            profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
            guard,
            profile_yz("butt", butt_outline(), -REC_HALF + 1, REC_HALF - 1)]
    if SUPPRESSOR:
        body.append(cylinder_y("suppressor", 0, B, SUP_R, SUP_Y0, SUP_Y1, 12))
    bolt = [prism_z("knob", knob_footprint(8), REC_TOP - 2.0, 152.0)]
    trigger = [profile_yz("trigger", trigger_outline(), -3.0, 3.0)]
    magazine = [magazine_body("mag_body"), floor_plate("mag_floor")]
    return {"body": body, "bolt": bolt, "trigger": trigger, "magazine": magazine}


def choose(normal, centre):
    """Which photo a face wears: the suppressor's own (its ends from the back), the top or back where a face looks
    that way, the left side otherwise (the right mirrors it)."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if centre.y > SUP_Y0 + 1.0 and SUPPRESSOR:
        return "back" if ax == 1 and normal.y < 0 else "suppressor"
    if ax == 2 and normal.z > 0:
        return "top"
    if ax == 1 and normal.y < 0 and centre.z >= 85:
        return "back"
    return "left"


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "ingramsl", GAME_LENGTH)
    frame = kit.hold_for(WEB, scale)
    scene = kit.studio()
    gun, mag, knob, sup = detailed(photo_materials())
    whole = [o for o in (gun, mag, knob, sup) if o]
    subjects = {"left": whole, "top": whole, "back": whole, "magazine": [mag], "suppressor": [sup] if sup else []}
    regions = dict(REGIONS)
    if not sup:
        del regions["suppressor"]
    rgba = kit.atlas(scene, regions, subjects)
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "ingramsl-atlas.png")
    kit.save_png(atlas_png, flat)
    for o in whole:
        o.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            if part == "magazine":
                kit.project(ob, REGIONS, lambda n, c: "magazine")
            else:
                kit.project(ob, REGIONS, choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING)
    print(f"ingramsl: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "ingramsl-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.75, 0.38, 0.10), (0, 0.38, 0.10), 40), ((0.55, 0.60, 0.30), (0, 0.30, 0.10), 50),
                 ((-0.27, -0.14, 0.27), (0.0, 0.45, 0.13), 35)])


main()
