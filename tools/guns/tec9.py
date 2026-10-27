"""Vice City's Tec-9: an Intratec TEC-9 (317 mm overall, its 5-inch barrel in the perforated shroud), grey all over
as the game's is, the 32-round magazine forward of the trigger guard, held one-handed, for the first-person
viewmodel.

blender -b -P tools/guns/tec9.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right; the receiver's back at y = 0, the grip's foot at
z = 0, the bore 130 up. Built as tools/guns/colt45.py (the worked example) is. The cocking knob on the left is the
bolt.
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

B = 130.0  # the bore's height
LENGTH = 317.0
MUZZLE_Y = 317.0
UP_BOTTOM, UP_HALF = 112.0, 13.5  # the upper receiver: its underside, half its width; its top a half round
UP_TOP = 133.5 + UP_HALF
UP_FRONT = 205.0
SHROUD_R, SHROUD_Y1 = 14.0, 300.0
RAKE = math.tan(math.radians(17.8))
GRIP_X = 15.0
MAG = (134.0, 164.0, 10.5, 110.0, -116.0)  # the magazine: back, front, half width, top, body's foot
KNOB = (166.0, 136.0)  # the cocking knob, forward: y, z


def back_y(z):
    """The back of the grip at height z."""
    return -2.0 + z * RAKE


def front_y(z):
    """The grip's front edge at height z."""
    return 78.0 - (108.0 - z) * RAKE


SIDE_MMPP = 330 / 256
REGIONS = {
    "left": ((0, 0, 256, 124), (-400, 158, 72), (1, 0, 0), (0, 0, 1), SIDE_MMPP),
    "top": ((0, 124, 256, 40), (0, 158, 400), (0, 0, -1), (-1, 0, 0), SIDE_MMPP),
    # The magazine lying along the atlas (its foot to the left).
    "magazine": ((0, 164, 256, 36), (-400, 149.5, -4.5), (1, 0, 0), (0, 1, 0), 0.98),
    "back": ((0, 200, 128, 56), (0, -400, 131), (0, 1, 0), (0, 0, 1), 0.76),
    "front": ((128, 200, 128, 56), (0, 600, 131), (0, -1, 0), (0, 0, 1), 0.76),
}

FINGER_R = 8.0
GRIP_BACK = 24.0  # the fingers' pads behind the grip's front edge


def on_grip(z, back=GRIP_BACK):
    return (-GRIP_X - FINGER_R, front_y(z) - back, z)


FORWARD = (0.0, 20.0, 20.0 * RAKE)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 101.0 + FINGER_R, 86.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(62.0), None, None, FORWARD),
    ("r_ring", on_grip(45.0), None, None, FORWARD),
    ("r_pinky", on_grip(28.0, GRIP_BACK + 2.0), None, None, FORWARD),
    ("r_thumb", (-14.5 - FINGER_R, 66.0, 114.0), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), (0, 0, -1)),
]
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, B), (0, 16, 150.5), (0, 198, 151.0)
WEB = (0.0, back_y(104.0), 104.0)  # the back of the grip, under the receiver's overhang
FRAMING = ((-0.200, -0.310, 0.200), 12.0, 0.0, 0.24, 0.02)  # as far back and high as the Uzi's (closer, it read large), level


def photo_materials():
    mats = kit.photo_materials()
    # The game's Tec-9 is grey all over: a satin grey receiver, a grey polymer frame, a dark shroud.
    mats["receiver"] = material("tec_grey", (0.50, 0.51, 0.53), metallic=0.85, roughness=0.42)
    poly = material("tec_polymer", (0.30, 0.31, 0.33), roughness=0.72, spec=0.35)
    nt = poly.node_tree
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 900.0
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.35
    nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])
    mats["polymer"] = poly
    mats["magazine"] = material("tec_mag", (0.44, 0.45, 0.47), metallic=0.8, roughness=0.45)
    return mats


def upper_section(detail):
    """The upper receiver's cross-section (x, z): flat sides, a half-round top."""
    h, zc = UP_HALF, UP_TOP - UP_HALF
    steps = (0, 20, 40, 60, 80) if detail else (0, 35, 70)
    arc = [(h * math.cos(math.radians(a)), zc + h * math.sin(math.radians(a))) for a in steps]
    right = [(h - 1.0, UP_BOTTOM), (h, UP_BOTTOM + 1.0)] + arc
    return right + [(-x, z) for x, z in reversed(right)]


def lower_outline():
    """The polymer lower: under the receiver from the grip to the magazine well, the well's housing."""
    return [(back_y(114.0), 114.0), (176.0, 114.0), (176.0, 80.0), (172.0, 74.0), (130.0, 74.0), (128.0, 94.0),
            (60.0, 94.0), (40.0, 100.0)]


def grip_outline():
    return [(front_y(108.0) + 2.0, 108.0), (front_y(8.0), 8.0), (front_y(8.0) + 1.2, 3.0), (front_y(0.0) - 1.0, 0.0),
            (back_y(0.0) + 3.0, 0.0), (back_y(4.0), 4.0), (back_y(30.0) - 2.0, 30.0), (back_y(108.0), 108.0),
            (back_y(112.0), 112.0), (70.0, 112.0)]


def guard_outline():
    return [(front_y(96.0) - 2.0, 96.0), (130.0, 96.0), (130.0, 74.0), (125.0, 69.5), (80.0, 69.5), (front_y(78.0) - 1.0, 78.0)]


def guard_opening():
    return [(front_y(98.0) + 2.0, 98.0), (125.0, 98.0), (125.0, 77.0), (122.0, 74.0), (80.0, 74.0), (front_y(80.0) + 2.0, 80.0)]


def trigger_outline():
    return [(94.0, 95.0), (100.0, 95.0), (101.2, 88.0), (100.2, 81.0), (98.0, 78.0), (96.6, 79.0), (97.6, 86.0), (94.4, 91.0)]


def magazine_body(name, mat=None, inset=0.0):
    back, front, half, top, foot = MAG
    return box(name, -half + inset, half - inset, back + inset, front - inset, foot, top, mat)


def detailed(mats):
    """The photo shoot's Tec-9. Returns (gun, magazine)."""
    grey, dark, poly, steel = mats["receiver"], mats["dark"], mats["polymer"], mats["steel"]
    parts = []
    # The upper receiver: its cocking slot along the left (dark inside), the ejection port on the right, the end
    # cap, the sights.
    upper = profile_xz("upper", upper_section(True), 0.0, UP_FRONT, grey)
    cutters = [box("slot", -UP_HALF - 2, -UP_HALF + 2.0, 98.0, 178.0, KNOB[1] - 2.2, KNOB[1] + 2.2),
               box("port", UP_HALF - 2.0, UP_HALF + 2, 112.0, 152.0, 124.0, 140.0)]
    cut(upper, join("cutters", cutters))
    bevel(upper, 0.6, 1, 40)
    parts.append(upper)
    parts.append(box("slot_dark", -UP_HALF + 1.6, -UP_HALF + 2.4, 97.0, 179.0, KNOB[1] - 2.6, KNOB[1] + 2.6, dark))
    parts.append(box("port_bolt", UP_HALF - 2.4, UP_HALF - 1.6, 111.0, 153.0, 123.0, 141.0, dark))
    cap = profile_xz("end_cap", [(x * 0.96, z) for x, z in upper_section(True)], -3.0, 0.5, grey)
    bevel(cap, 0.6)
    parts.append(cap)
    parts.append(profile_yz("sling_loop", fillet([(2, UP_TOP - 6), (12, UP_TOP - 6), (12, UP_TOP + 1), (2, UP_TOP + 1)],
                                                 2, 2), -UP_HALF - 2.5, -UP_HALF + 1, dark))
    rsight = box("rear_sight", -5.5, 5.5, 10.0, 22.0, UP_TOP - 2.0, 152.5, grey)
    cut(rsight, box("notch", -1.4, 1.4, 5, 30, 150.0, 160))
    bevel(rsight, 0.6)
    parts.append(rsight)
    parts.append(profile_yz("front_sight", fillet([(192, UP_TOP - 2), (202, UP_TOP - 2), (201, 151.5), (196, 151.5)],
                                                  [0, 0, 1, 1], 2), -1.5, 1.5, grey))
    for y in (6.0, 186.0):
        parts.append(cylinder_x("pin", y, UP_BOTTOM + 6.0, 2.0, -UP_HALF - 0.4, UP_HALF + 0.4, 10, steel))
    # The shroud: a dark tube, holed in rows, the barrel inside and its threaded muzzle.
    shroud = cylinder_y("shroud", 0, B, SHROUD_R, UP_FRONT - 2.0, SHROUD_Y1, 40, dark)
    holes = []
    for k in range(6):
        y = 214.0 + k * 13.5
        for a in (0, 60, 120):
            h = cylinder_x("hole", 0, 0, 3.6, -30, 30, 14)
            h.location = (0, y * MM, B * MM)
            h.rotation_euler = (0, math.radians(a), 0)
            holes.append(h)
    hj = join("holes", holes)
    bpy.context.view_layer.objects.active = hj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    cut(shroud, cylinder_y("bore", 0, B, SHROUD_R - 1.4, UP_FRONT - 10, SHROUD_Y1 + 10, 32))
    cut(shroud, hj)
    parts.append(shroud)
    parts.append(cylinder_y("shroud_cap", 0, B, SHROUD_R + 0.3, SHROUD_Y1 - 4.0, SHROUD_Y1, 40, dark))
    barrel = cylinder_y("barrel", 0, B, 7.0, UP_FRONT - 4, MUZZLE_Y - 14.0, 24, steel)
    thread = cylinder_y("thread", 0, B, 7.6, MUZZLE_Y - 16.0, MUZZLE_Y, 24, dark)
    cut(thread, cylinder_y("muzzle_bore", 0, B, 4.6, MUZZLE_Y - 10, MUZZLE_Y + 5, 16))
    bevel(thread, 0.5)
    parts += [barrel, thread]
    # The polymer lower and grip, the guard, the trigger, the magazine catch.
    lower = profile_yz("lower", fillet(lower_outline(), [0, 0, 3, 3, 2, 2, 4, 4], 3), -12.5, 12.5, poly)
    bevel(lower, 1.0)
    parts.append(lower)
    grip = profile_yz("grip", fillet(grip_outline(), [3, 4, 3, 3, 4, 3, 10, 3, 0, 0], 3), -GRIP_X, GRIP_X, poly)
    bevel(grip, 3.0, 2, 30)
    parts.append(grip)
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 3, 4, 6, 3], 3), -5.0, 5.0, poly)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 3, 3, 6, 3], 3), -10, 10))
    bevel(guard, 0.8)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(trigger_outline(), [0, 0, 3, 3, 1, 1, 3, 0], 2), -3.0, 3.0, steel))
    parts.append(cylinder_x("frame_pin", 150.0, 104.0, 2.2, -12.9, 12.9, 10, steel))
    for p in parts:
        finish(p, 35)
    gun = join("tec9", parts)
    mag = magazine_body("magazine", mats["magazine"])
    bevel(mag, 1.2)
    back, front, half, top, foot = MAG
    floor = box("floor", -half - 1.5, half + 1.5, back - 2.0, front + 3.0, foot - 5.0, foot + 0.5, dark)
    bevel(floor, 1.0)
    ribs = [box("rib", -half - 0.4, half + 0.4, back + 3.0, front - 3.0, z, z + 1.0, mats["magazine"])
            for z in (foot + 20.0, foot + 60.0, foot + 100.0)]
    follower = box("lips", -half + 1.5, half - 1.5, back + 2.0, front - 6.0, top, top + 3.0, steel)
    for p in [mag, floor, follower] + ribs:
        finish(p, 35)
    return gun, join("magazine", [mag, floor, follower] + ribs)


def low():
    """The Tec-9 at Vice City's detail, in its moving parts: {part: [objects]}."""
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 2, 3, 4, 0], 1), -5.0, 5.0)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 2, 2, 4, 0], 1), -10, 10), solver="EXACT")
    body = [profile_xz("upper", upper_section(False), -2.5, UP_FRONT),
            box("rear_sight", -5.5, 5.5, 10.0, 22.0, UP_TOP - 2.0, 152.5),
            profile_yz("front_sight", [(192, UP_TOP - 2), (202, UP_TOP - 2), (201, 151.5), (196, 151.5)], -1.5, 1.5),
            cylinder_y("shroud", 0, B, SHROUD_R, UP_FRONT - 2.0, SHROUD_Y1, 10),
            cylinder_y("thread", 0, B, 7.6, SHROUD_Y1 - 1.0, MUZZLE_Y, 6),
            profile_yz("lower", lower_outline(), -12.5, 12.5),
            profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
            guard]
    bolt = [cylinder_x("knob", KNOB[0], KNOB[1], 5.0, -UP_HALF - 10.5, -UP_HALF + 1.0, 6)]
    trigger = [profile_yz("trigger", trigger_outline(), -3.0, 3.0)]
    back, front, half, top, foot = MAG
    magazine = [magazine_body("mag_body"), box("mag_floor", -half - 1.5, half + 1.5, back - 2.0, front + 3.0, foot - 5.0, foot + 0.5)]
    return {"body": body, "bolt": bolt, "trigger": trigger, "magazine": magazine}


def detailed_knob(mats):
    knob = cylinder_x("knob", KNOB[0], KNOB[1], 5.0, -UP_HALF - 10.5, -UP_HALF + 1.0, 20, mats["receiver"])
    bevel(knob, 1.0)
    finish(knob, 35)
    return knob


def choose(normal, centre):
    """Which photo a face wears: the top, back or front where it faces them, the left side otherwise (the right
    mirrors it)."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return "top"
    if ax == 1 and normal.y < 0 and centre.z >= 108:
        return "back"
    if ax == 1 and normal.y > 0 and centre.z >= 108:
        return "front"
    return "left"


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "tec9", LENGTH)
    frame = kit.hold_for(WEB, scale)
    scene = kit.studio()
    mats = photo_materials()
    gun, mag = detailed(mats)
    knob = detailed_knob(mats)
    whole = [gun, mag, knob]
    rgba = kit.atlas(scene, REGIONS, {"left": whole, "top": whole, "back": whole, "front": whole, "magazine": [mag]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "tec9-atlas.png")
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
    print(f"tec9: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "tec9-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.45, 0.12, 0.10), (0, 0.16, 0.05), 50), ((0.40, 0.40, 0.25), (0, 0.15, 0.06), 50),
                 ((-0.16, -0.18, 0.20), (0.0, 0.32, 0.12), 35)])


main()
