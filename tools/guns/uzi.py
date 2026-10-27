"""Vice City's Uzi: the full-size IMI Uzi (470 mm with its metal stock folded under, the 260 mm barrel), in the
game's light satin finish, its 32-round magazine in the grip, held one-handed, for the first-person viewmodel.

blender -b -P tools/guns/uzi.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right; y = 0 where the folded stock's butt is (the
receiver's back at 25), the grip's foot at z = 0, the bore 143 up. Built as tools/guns/colt45.py (the worked example) is. The top cover's cocking knob is
the bolt.
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

B = 143.0  # the bore's height
LENGTH = 470.0
MUZZLE_Y = 470.0
REC_Y0, REC_Y1 = 25.0, 395.0  # the receiver's back and front
REC_BOTTOM, REC_TOP, REC_HALF = 105.0, 165.0, 20.0
RAKE = math.tan(math.radians(6.5))
GRIP_AXIS = (0.0, math.sin(math.radians(6.5)), math.cos(math.radians(6.5)))  # up the grip
GRIP_X = 15.5
KNOB = (248.0, 270.0)  # the cocking knob, forward


def back_y(z):
    """The back of the grip at height z."""
    return 138.0 + z * RAKE


def front_y(z):
    """The grip's front at height z."""
    return 190.0 + z * RAKE


MAG_TOP, MAG_FOOT, MAG_HALF = 125.0, -100.0, 11.0


def mag_outline(inset=0.0):
    """The magazine's side (y, z): along the grip, 32 deep."""
    return [(back_y(MAG_FOOT) + 12.0 + inset, MAG_FOOT), (back_y(MAG_FOOT) + 44.0 - inset, MAG_FOOT),
            (back_y(MAG_TOP) + 44.0 - inset, MAG_TOP), (back_y(MAG_TOP) + 12.0 + inset, MAG_TOP)]


SIDE_MMPP = 480 / 256
_MAG_MID = (0.0, back_y(10.5) + 28.0, 10.5)
REGIONS = {
    "left": ((0, 0, 256, 106), (-400, 236, 96), (1, 0, 0), (0, 0, 1), SIDE_MMPP),
    "top": ((0, 106, 256, 28), (0, 236, 400), (0, 0, -1), (-1, 0, 0), SIDE_MMPP),
    # The magazine lying along the atlas, its foot to the left.
    "magazine": ((0, 134, 256, 36), (-400, _MAG_MID[1], _MAG_MID[2]), (1, 0, 0), (0, GRIP_AXIS[2], -GRIP_AXIS[1]), 1.05),
    "back": ((0, 170, 64, 86), (0, -400, 147), (0, 1, 0), (0, 0, 1), 1.0),
    "front": ((64, 170, 64, 86), (0, 700, 147), (0, -1, 0), (0, 0, 1), 1.0),
}

FINGER_R = 8.0
GRIP_BACK = 20.0


def on_grip(z, back=GRIP_BACK):
    return (-GRIP_X - FINGER_R, front_y(z) - back, z)


FORWARD = (0.0, 20.0, 20.0 * math.tan(math.radians(18)))
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 222.3 + FINGER_R, 94.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(60.0), None, None, FORWARD),
    ("r_ring", on_grip(42.0), None, None, FORWARD),
    ("r_pinky", on_grip(24.0, GRIP_BACK + 2.0), None, None, FORWARD),
    ("r_thumb", (-REC_HALF - 2.0 - FINGER_R, 172.0, 108.0), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), tuple(-c for c in GRIP_AXIS)),
]
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, B), (0, 45, 174.5), (0, 362, 176.0)
# The back of the grip (its safety), under the receiver; the gun 2.5 mm to his left of the Colt's line, so the wide
# receiver clears his knuckles.
WEB = (2.5, back_y(95.0) - 2.5, 95.0)
# Seen from further left than the pistols, so his hand shows under the long receiver.
FRAMING = ((-0.203, -0.312, 0.199), 12.0, 0.0, 0.26, 0.02)  # pulled back: the whole gun and his hand in view; levelled: its barrel where he aims (vcmm-viewmodel --level)


def photo_materials():
    mats = kit.photo_materials()
    # The game's Uzi is a light, slightly blue satin steel with dark grips and handguard.
    mats["receiver"] = material("uzi_steel", (0.70, 0.73, 0.78), metallic=0.85, roughness=0.36)
    mats["polymer"] = material("uzi_polymer", (0.12, 0.13, 0.15), roughness=0.6, spec=0.35)
    mats["magazine"] = material("uzi_mag", (0.55, 0.58, 0.63), metallic=0.8, roughness=0.42)
    return mats


def receiver_section(detail):
    """The receiver's cross-section (x, z): a box, its top cover's edges rounded."""
    h = REC_HALF
    if detail:
        right = [(h - 1.5, REC_BOTTOM), (h, REC_BOTTOM + 1.5), (h, REC_TOP - 7.0), (h - 1.0, REC_TOP - 3.0),
                 (h - 3.0, REC_TOP - 1.0), (h - 7.0, REC_TOP)]
    else:
        right = [(h - 1.5, REC_BOTTOM), (h, REC_BOTTOM + 1.5), (h, REC_TOP - 6.0), (h - 6.0, REC_TOP)]
    return right + [(-x, z) for x, z in reversed(right)]


def sight_section(top, inner, outer, mid_top, mid_half):
    """A sight's guard ears and blade between them, as a cross-section (x, z) from the receiver's top."""
    z0 = REC_TOP - 1.0
    return [(-outer, z0), (outer, z0), (outer, top), (inner, top), (inner, mid_top - 4.0), (mid_half, mid_top - 4.0),
            (mid_half, mid_top), (-mid_half, mid_top), (-mid_half, mid_top - 4.0), (-inner, mid_top - 4.0), (-inner, top),
            (-outer, top)]


def handguard_section():
    return [(-REC_HALF - 1.5, REC_BOTTOM - 2.0), (REC_HALF + 1.5, REC_BOTTOM - 2.0), (REC_HALF + 1.5, 140.0),
            (REC_HALF - 1.0, 142.0), (-REC_HALF + 1.0, 142.0), (-REC_HALF - 1.5, 140.0)]


def grip_outline():
    return [(front_y(REC_BOTTOM) + 2.0, REC_BOTTOM + 2.0), (front_y(6.0), 6.0), (front_y(0.0) - 3.0, 0.0),
            (back_y(0.0) + 3.0, 0.0), (back_y(4.0), 4.0), (back_y(REC_BOTTOM + 2.0), REC_BOTTOM + 2.0)]


def guard_outline():
    return [(front_y(REC_BOTTOM) - 2.0, REC_BOTTOM + 2.0), (268.0, REC_BOTTOM + 2.0), (268.0, 80.0), (262.0, 72.0),
            (front_y(72.0) + 6.0, 72.0), (front_y(80.0) - 1.0, 80.0)]


def guard_opening():
    return [(front_y(REC_BOTTOM) + 1.0, REC_BOTTOM + 4.0), (263.0, REC_BOTTOM + 4.0), (263.0, 82.0), (259.0, 77.0),
            (front_y(77.0) + 6.0, 77.0), (front_y(82.0) + 1.0, 82.0)]


def trigger_outline():
    return [(214.0, REC_BOTTOM + 1.0), (220.0, REC_BOTTOM + 1.0), (222.4, 96.0), (221.8, 88.0), (219.0, 83.0),
            (217.5, 84.0), (218.6, 91.0), (214.6, 99.0)]


def detailed(mats):
    """The photo shoot's Uzi. Returns (gun, magazine, knob)."""
    rec, dark, poly, steel = mats["receiver"], mats["dark"], mats["polymer"], mats["steel"]
    parts = []
    # The receiver: pressed ribs along its sides, the top cover's seam, the cocking slot on top, the ejection port.
    receiver = profile_xz("receiver", receiver_section(True), REC_Y0, REC_Y1, rec)
    cutters = []
    for sx in (-1, 1):
        cutters.append(box("seam", sx * REC_HALF - 0.6, sx * REC_HALF + 0.6, REC_Y0 - 1, 300.0, 152.0, 152.8))
    cutters.append(box("slot", -3.2, 3.2, 120.0, KNOB[1] + 2.0, REC_TOP - 2.0, REC_TOP + 2))
    cutters.append(box("port", REC_HALF - 2.0, REC_HALF + 2, 228.0, 276.0, 126.0, 150.0))
    cut(receiver, join("cutters", cutters))
    bevel(receiver, 0.6, 1, 40)
    parts.append(receiver)
    parts.append(box("slot_dark", -3.4, 3.4, 119.0, KNOB[1] + 3.0, REC_TOP - 2.6, REC_TOP - 1.8, dark))
    parts.append(box("port_bolt", REC_HALF - 2.6, REC_HALF - 1.8, 227.0, 277.0, 125.0, 151.0, dark))
    # Its pressed ribs, standing out along each side.
    for sx in (-1, 1):
        for z in (121.0, 134.0):
            rib = profile_yz("rib", fillet([(60.0, z), (272.0, z), (272.0, z + 4.0), (60.0, z + 4.0)], 2.0, 2),
                             sx * (REC_HALF - 1.0), sx * (REC_HALF + 1.0), rec)
            bevel(rib, 0.7, 2, 30)
            parts.append(rib)
    parts.append(profile_xz("end_plate", [(x * 0.97, z) for x, z in receiver_section(True)], REC_Y0 - 3.0, REC_Y0 + 0.5, rec))
    # The sights: the rear's flip aperture between its ears, the front post between tall ears.
    rear = profile_xz("rear_sight", sight_section(182.0, 12.5, 16.5, 178.0, 4.0), 36.0, 54.0, rec)
    cut(rear, cylinder_y("aperture", 0, 174.5, 1.4, 30, 60, 10))
    bevel(rear, 0.5)
    front = profile_xz("front_sight", sight_section(190.0, 11.0, 14.5, 176.0, 1.4), 352.0, 372.0, rec)
    bevel(front, 0.5)
    parts += [rear, front]
    # The barrel nut (knurled) and the barrel.
    nut = cylinder_y("nut", 0, B, 18.0, REC_Y1 - 2.0, 412.0, 48, steel)
    knurl = []
    for k in range(24):
        a = math.radians(k * 15)
        g = box("knurl", -0.9, 0.9, 400.0, 420.0, 17.0, 20.0)
        g.location = (0, 0, B * MM)
        g.rotation_euler = (0, a, 0)
        knurl.append(g)
    kj = join("knurls", knurl)
    bpy.context.view_layer.objects.active = kj
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    cut(nut, kj)
    barrel = cylinder_y("barrel", 0, B, 9.5, 405.0, MUZZLE_Y, 32, steel)
    cut(barrel, cylinder_y("bore", 0, B, 4.6, MUZZLE_Y - 20, MUZZLE_Y + 5, 16))
    bevel(barrel, 0.5)
    parts += [nut, barrel]
    # The handguard (dark, grooved) round the receiver's lower front.
    hg = profile_xz("handguard", handguard_section(), 282.0, 388.0, poly)
    cut(hg, join("hg_grooves", [box("g", sx * (REC_HALF + 0.3), sx * (REC_HALF + 3), 296.0 + k * 12.0, 299.0 + k * 12.0,
                                    110.0, 136.0) for k in range(7) for sx in (-1, 1)]))
    bevel(hg, 0.6)
    parts.append(hg)
    # The grip: dark panels with their grooves, the grip safety at its back, the selector over it.
    grip = profile_yz("grip", fillet(grip_outline(), [3, 3, 3, 3, 3, 0], 3), -GRIP_X, GRIP_X, poly)
    cut(grip, join("grip_grooves", [box("g", sx * (GRIP_X - 1.0), sx * (GRIP_X + 2), 120, 230, z, z + 1.6)
                                    for z in range(14, 100, 9) for sx in (-1, 1)]))
    bevel(grip, 1.6, 1, 30)
    parts.append(grip)
    parts.append(profile_yz("grip_safety", fillet([(back_y(40.0) - 3.5, 40.0), (back_y(40.0) + 2, 40.0),
                                                   (back_y(100.0) + 2, 100.0), (back_y(100.0) - 2.5, 100.0)],
                                                  1.5, 2), -8, 8, dark))
    parts.append(profile_yz("selector", fillet([(196, 98), (214, 100), (214, 106), (196, 106)], 1.5, 2), -REC_HALF - 1.6,
                            -REC_HALF + 1, dark))
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 4, 4, 3, 0], 3), -5.0, 5.0, rec)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 3, 3, 3, 0], 3), -10, 10))
    bevel(guard, 0.6)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(trigger_outline(), [0, 0, 3, 3, 1, 1, 3, 0], 2), -3.0, 3.0, steel))
    # The stock's hinge at the receiver's back. (The folded stock itself, hanging under the back as the game's
    # does, is left off: it's never in the first-person view, and his forearm runs where it would be.)
    parts.append(cylinder_x("hinge", REC_Y0 + 4.0, REC_BOTTOM + 4.0, 5.0, -REC_HALF - 1, REC_HALF + 1, 16, rec))
    for p in parts:
        finish(p, 35)
    gun = join("uzi", parts)
    mag = profile_yz("magazine", mag_outline(), -MAG_HALF, MAG_HALF, mats["magazine"])
    bevel(mag, 1.0)
    floor = profile_yz("floor", [(back_y(MAG_FOOT) + 10.0, MAG_FOOT - 4.0), (back_y(MAG_FOOT) + 47.0, MAG_FOOT - 4.0),
                                 (back_y(MAG_FOOT) + 47.0, MAG_FOOT + 1.0), (back_y(MAG_FOOT) + 10.0, MAG_FOOT + 1.0)],
                       -MAG_HALF - 1.2, MAG_HALF + 1.2, dark)
    bevel(floor, 0.8)
    for p in (mag, floor):
        finish(p, 35)
    knob = profile_xz("knob", fillet([(-8, REC_TOP - 1), (8, REC_TOP - 1), (8, 172), (5, 176), (-5, 176), (-8, 172)],
                                     [0, 0, 1, 2, 2, 1], 2), KNOB[0], KNOB[1], rec)
    cut(knob, join("knob_grooves", [box("g", -9, 9, KNOB[0] + 3 + k * 3.2, KNOB[0] + 4.4 + k * 3.2, 172.5, 180)
                                    for k in range(5)]))
    finish(knob, 35)
    return gun, join("magazine", [mag, floor]), knob


def low():
    """The Uzi at Vice City's detail, in its moving parts: {part: [objects]}."""
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 3, 3, 2, 0], 1), -5.0, 5.0)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 2, 2, 2, 0], 1), -10, 10), solver="EXACT")
    body = [profile_xz("receiver", receiver_section(False), REC_Y0 - 3.0, REC_Y1),
            profile_xz("rear_sight", sight_section(182.0, 12.5, 16.5, 178.0, 4.0), 36.0, 54.0),
            profile_xz("front_sight", sight_section(190.0, 11.0, 14.5, 176.0, 1.4), 352.0, 372.0),
            cylinder_y("nut", 0, B, 18.0, REC_Y1 - 2.0, 412.0, 10),
            cylinder_y("barrel", 0, B, 9.5, 410.0, MUZZLE_Y, 8),
            profile_xz("handguard", handguard_section(), 282.0, 388.0),
            profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
            profile_yz("grip_safety", [(back_y(40.0) - 3.5, 40.0), (back_y(40.0) + 2, 40.0), (back_y(100.0) + 2, 100.0),
                                       (back_y(100.0) - 2.5, 100.0)], -8, 8),
            guard]
    bolt = [profile_xz("knob", [(-8, REC_TOP - 1), (8, REC_TOP - 1), (8, 172), (5, 176), (-5, 176), (-8, 172)],
                       KNOB[0], KNOB[1])]
    trigger = [profile_yz("trigger", trigger_outline(), -3.0, 3.0)]
    magazine = [profile_yz("mag_body", mag_outline(), -MAG_HALF, MAG_HALF),
                profile_yz("mag_floor", [(back_y(MAG_FOOT) + 10.0, MAG_FOOT - 4.0), (back_y(MAG_FOOT) + 47.0, MAG_FOOT - 4.0),
                                         (back_y(MAG_FOOT) + 47.0, MAG_FOOT + 1.0), (back_y(MAG_FOOT) + 10.0, MAG_FOOT + 1.0)],
                           -MAG_HALF - 1.2, MAG_HALF + 1.2)]
    return {"body": body, "bolt": bolt, "trigger": trigger, "magazine": magazine}


def choose(normal, centre):
    """Which photo a face wears: the top, back or front where it faces them, the left side otherwise (the right
    mirrors it)."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return "top"
    if ax == 1 and normal.y < 0 and centre.z >= 106:
        return "back"
    if ax == 1 and normal.y > 0 and centre.z >= 106:
        return "front"
    return "left"


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "uzi", LENGTH)
    frame = kit.hold_for(WEB, scale)
    scene = kit.studio()
    gun, mag, knob = detailed(photo_materials())
    whole = [gun, mag, knob]
    rgba = kit.atlas(scene, REGIONS, {"left": whole, "top": whole, "back": whole, "front": whole, "magazine": [mag]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "uzi-atlas.png")
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
    print(f"uzi: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "uzi-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.60, 0.22, 0.12), (0, 0.24, 0.10), 50), ((0.50, 0.55, 0.30), (0, 0.24, 0.10), 50),
                 ((-0.17, -0.08, 0.21), (0.0, 0.45, 0.13), 35)])


main()
