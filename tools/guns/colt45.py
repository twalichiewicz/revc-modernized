"""Vice City's Colt 45 (a stainless M1911 with black grips) for the first-person viewmodel.

blender -b -P tools/guns/colt45.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right; the tang's tip at y = 0 and the grip's foot
near z = 0. From the viewmodel spike (docs/research/viewmodel-spike: colt.py, v2_colt_low.py, v3_step3.py),
whose grip, anchors and hold the player approved.
"""
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(__file__))
import gamefiles
import kit
from lib import (MM, apply_all, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, profile_xz, profile_yz,
                 tris)

RAKE = math.tan(math.radians(18))  # the grip's rake back from vertical
GRIP_AXIS = (0.0, math.sin(math.radians(18)), math.cos(math.radians(18)))  # up the grip


def front_strap_y(z):
    return 73.0 - (92.0 - z) * RAKE


def back_strap_y(z):
    return -1.0 + (z - 24.5) * RAKE


# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
REGIONS = {
    "left": ((0, 0, 256, 160), (-400, 108, 69.5), (1, 0, 0), (0, 0, 1), 232 / 256),
    "top": ((0, 160, 256, 48), (0, 108, 400), (0, 0, -1), (-1, 0, 0), 232 / 256),
    "back": ((0, 208, 96, 48), (0, -400, 118), (0, 1, 0), (0, 0, 1), 44 / 48),
    "front": ((96, 208, 64, 48), (0, 600, 116), (0, -1, 0), (0, 0, 1), 34 / 48),
    "magazine": ((160, 208, 96, 48), (-400, 32.5, 50), (1, 0, 0), (0, GRIP_AXIS[2], -GRIP_AXIS[1]), 1.4),
}

# Where his right hand's fingertips go (the pad's middle, a finger's half-thickness off the surface): the index
# on the trigger's face, the others on the left grip panel behind the front strap, the thumb along the frame.
FINGER_R = 8.0
PANEL_X = -14.8
PANEL_BACK = 30.0


def on_panel(z, back=PANEL_BACK):
    return (PANEL_X - FINGER_R, front_strap_y(z) - back, z)


# The trigger finger's middle joint goes out to the right, beside the guard (out of the first-person view's sight),
# its pad on the trigger's face. Longer fingers (some
# outfits') may hold the grip nearer its front strap: up to 20 mm forward along the panel.
FORWARD = (0.0, 20.0, 20.0 * RAKE)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 93.3 + FINGER_R, 85.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_panel(60.0), None, None, FORWARD),
    ("r_ring", on_panel(43.0), None, None, FORWARD),
    ("r_pinky", on_panel(27.0, PANEL_BACK + 2.0), None, None, FORWARD),
    ("r_thumb", (-11.0 - FINGER_R, 62.0, 97.0), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("slide", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), tuple(-c for c in GRIP_AXIS)),
]
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, 217.5, 117), (0, 26, 134), (0, 203.5, 136)
# Seen from: the eye left of, behind and over the gun (real metres), as far back and as high as the Uzi's (the
# spike's closer framing read large and held high beside the SMGs), looking 12 degrees right of the barrel, level;
# aiming, 0.22 m behind the rear sight and 0.02 m over the sight line.
FRAMING = ((-0.190, -0.300, 0.200), 12.0, 0.0, 0.22, 0.02)
TOP = 135.5  # mm: the rear sight's top, the gun's highest point


def detailed(mats):
    """The photo shoot's Colt: every part the texture shows. Returns (gun, magazine body)."""
    steel, dark, grip = mats["steel"], mats["dark"], mats["grip"]
    parts = []
    slide = profile_xz("slide", fillet([(-11.5, 102), (11.5, 102), (11.5, 130), (-11.5, 130)], [0.8, 0.8, 4.5, 4.5], 3),
                       18, 216, steel)
    cutters = []
    for k in range(10):
        y = 24 + k * 2.7
        for x0, x1 in ((10.8, 13), (-13, -10.8)):
            cutters.append(box("serr", x0, x1, y, y + 1.2, 104.5, 127.5))
    cutters.append(profile_yz("port", fillet([(98, 117.5), (136, 117.5), (136, 136), (98, 136)], [3, 3, 0, 0], 2), 4.0, 14))
    cutters.append(cylinder_y("bushing_hole", 0, 117, 9.2, 212, 222, 14))
    cutters.append(cylinder_y("plug_hole", 0, 106.5, 5.4, 213, 222, 10))
    cut(slide, join("cutters", cutters))
    bevel(slide, 0.5)
    parts.append(slide)
    barrel = cylinder_y("barrel", 0, 117, 7.0, 96, 215.5, 14, steel)
    bushing = cylinder_y("bushing", 0, 117, 8.8, 211, 217.5, 14, steel)
    cut(barrel, cylinder_y("bore", 0, 117, 5.7, 200, 225, 12))
    cut(bushing, cylinder_y("bore", 0, 117, 5.7, 200, 225, 12))
    plug = cylinder_y("plug", 0, 106.5, 5.0, 212, 216.8, 10, steel)
    parts += [barrel, bushing, plug]
    front_sight = profile_yz("front_sight", [(196, 129), (207, 129), (207, 135.5), (203.5, 136), (199, 133)], -1.4, 1.4, dark)
    rear_sight = profile_yz("rear_sight", [(21, 129), (32, 129), (32, 135), (24, 135), (21, 133)], -7, 7, dark)
    cut(rear_sight, box("notch", -1.6, 1.6, 18, 36, 132.2, 137))
    parts += [front_sight, rear_sight]
    outline = [(20, 102), (197, 102), (197, 92.5), (front_strap_y(92), 92), (front_strap_y(9), 9), (back_strap_y(24.5), 24.5),
               (back_strap_y(96), 96)]
    frame = profile_yz("frame", fillet(outline, [0, 0, 4, 2, 4, 5, 3], 3), -11, 11, steel)
    bevel(frame, 0.6)
    parts.append(frame)
    guard = profile_yz("guard", fillet([(68, 92), (122, 92), (122, 79), (117, 69.5), (80, 69), (66, 74)], [0, 0, 8, 6, 9, 0], 3),
                       -4.6, 4.6, steel)
    cut(guard, profile_yz("opening", fillet([(76, 94), (116.5, 94), (116.5, 80), (113, 74), (82, 74), (76.5, 79)],
                                            [0, 0, 6, 4, 6, 0], 3), -10, 10))
    bevel(guard, 0.5)
    parts.append(guard)
    parts.append(profile_yz("trigger", [(87, 93), (93, 93), (93.3, 80.5), (91, 78.5), (88, 79), (87.4, 86)], -3.2, 3.2, dark))
    hammer = profile_yz("hammer", fillet([(20, 99), (17, 103.5), (10, 112), (5.5, 115.5), (2.5, 114), (4.5, 110), (12, 101), (14, 98)],
                                         [0, 2, 0, 2, 2, 0, 2, 0], 2), -3.4, 3.4, dark)
    tang = profile_yz("tang", fillet([(25, 93), (9, 96), (0, 100.5), (3, 104.2), (19, 104.2), (25, 101)], [0, 4, 3, 2, 0, 0], 3),
                      -9.5, 9.5, steel)
    bevel(tang, 0.5)
    parts += [hammer, tang]
    panel = fillet([(front_strap_y(89) - 4.5, 89), (front_strap_y(17) - 4.0, 17), (back_strap_y(30) + 4.5, 30.5),
                    (back_strap_y(89) + 4.5, 89)], 5, 3)
    for side, (x0, x1) in (("r", (10.5, 14.8)), ("l", (-14.8, -10.5))):
        p = profile_yz("panel_" + side, panel, x0, x1, grip)
        bevel(p, 1.2, 1, 30)
        parts.append(p)
        sx = 1 if side == "r" else -1
        for z in (78, 33):
            yc = (front_strap_y(z) + back_strap_y(z)) / 2 + (2 if z > 50 else -1)
            parts.append(cylinder_x("screw", yc, z, 2.6, sx * 14.6, sx * 15.3, 8, steel))
    parts.append(profile_yz("mag_base", [(front_strap_y(9) + 1.5, 10), (front_strap_y(9) - 0.5, 6.3),
                                         (back_strap_y(24.5) - 1.2, 21.5), (back_strap_y(24.5) - 0.8, 25)], -10, 10, dark))
    stop = profile_yz("slide_stop", fillet([(118, 97), (118, 104.5), (92, 104.5), (88, 101.5), (92, 98.5)], [3, 3, 2, 2, 0], 2),
                      -12.8, -11, dark)
    safety = profile_yz("safety", fillet([(33, 100), (52, 101), (52, 105.5), (31, 105.5)], [1, 2, 2, 1], 2), -13.2, -11, dark)
    catch = cylinder_x("catch", front_strap_y(86) - 3, 86, 4.6, -12.3, -11, 10, dark)
    parts += [stop, safety, catch]
    for p in parts:
        finish(p, 35)
    gun = join("colt45", parts)
    # The magazine's body, inside the grip: blued, its follower and the top round showing.
    mag = profile_yz("magazine", magazine_outline(1.0), -6.2, 6.2, dark)
    top = profile_yz("follower", [(front_strap_y(92) - 6, 92), (back_strap_y(92) + 4, 92), (back_strap_y(95) + 4, 95),
                                  (front_strap_y(95) - 6, 95)], -4.5, 4.5, mats["steel"])
    finish(mag, 35)
    finish(top, 35)
    return gun, join("magazine", [mag, top])


def magazine_outline(inset):
    """The magazine body's side: inside the grip from its foot to the frame, `inset` mm in from the straps."""
    return [(front_strap_y(10) - 4.0 * inset, 10), (front_strap_y(92) - 4.0 * inset, 92), (back_strap_y(92) + 4.0 * inset, 92),
            (back_strap_y(24.5) + 4.0 * inset, 24.5), (back_strap_y(24.5) + 2.0, 12)]


def low():
    """The Colt at Vice City's detail (about 300 triangles), in its moving parts: {part: [objects]}."""
    slide = [profile_xz("slide", [(-11.5, 102), (11.5, 102), (11.5, 126.5), (8.5, 130), (-8.5, 130), (-11.5, 126.5)], 18, 216),
             profile_yz("front_sight", [(196, 129), (207, 129), (207, 135.5), (199, 133)], -1.4, 1.4),
             profile_yz("rear_sight", [(21, 129), (32, 129), (32, 135), (21, 133)], -7, 7),
             cylinder_y("bushing", 0, 117, 8.8, 214, 217.5, 8)]
    outline = [(20, 102), (197, 102), (197, 92.5), (front_strap_y(92), 92), (front_strap_y(9), 9),
               (back_strap_y(24.5), 24.5), (back_strap_y(96), 96)]
    guard = profile_yz("guard", fillet([(68, 92), (122, 92), (122, 79), (117, 69.5), (80, 69), (66, 74)], [0, 0, 8, 5, 8, 0], 1),
                       -4.6, 4.6)
    cut(guard, profile_yz("opening", fillet([(76, 94), (116.5, 94), (116.5, 80), (113, 74), (82, 74), (76.5, 79)],
                                            [0, 0, 5, 3, 5, 0], 1), -10, 10), solver="EXACT")
    panel = fillet([(front_strap_y(89) - 4.5, 89), (front_strap_y(17) - 4.0, 17), (back_strap_y(30) + 4.5, 30.5),
                    (back_strap_y(89) + 4.5, 89)], 5, 1)
    body = [profile_yz("frame", fillet(outline, [0, 0, 3, 0, 3, 4, 0], 1), -11, 11), guard,
            profile_yz("hammer", [(20, 99), (10, 112), (4, 115.5), (2.5, 113), (12, 100), (14, 98)], -3.4, 3.4),
            profile_yz("tang", [(25, 93), (9, 96), (0, 100.5), (3, 104.2), (19, 104.2), (25, 101)], -9.5, 9.5),
            profile_yz("panel_r", panel, 10.5, 14.8), profile_yz("panel_l", panel, -14.8, -10.5)]
    trigger = [profile_yz("trigger", [(87, 93), (93, 93), (93.3, 80.5), (88, 79)], -3.2, 3.2)]
    magazine = [profile_yz("mag_base", [(front_strap_y(9) + 1.5, 10), (front_strap_y(9) - 0.5, 6.3),
                                        (back_strap_y(24.5) - 1.2, 21.5), (back_strap_y(24.5) - 0.8, 25)], -10, 10),
                profile_yz("mag_body", magazine_outline(1.0), -6.2, 6.2)]
    return {"body": body, "slide": slide, "trigger": trigger, "magazine": magazine}


def choose(normal, centre):
    """Which photo a face of the gun wears: the top, back or front where it faces them, the left side otherwise
    (the right mirrors it)."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return "top"
    if ax == 1 and normal.y < 0 and centre.z >= 96:
        return "back"
    if ax == 1 and normal.y > 0 and centre.z >= 99:
        return "front"
    return "left"


def hold(game_data):
    """The gun frame in his hand bone's (the game's Colt is modelled in it), scaled to the game's Colt, moved
    0.04 m down and 0.03 m forward in his hand. Returns (scale, [x axis, y axis, z axis, origin])."""
    v = gamefiles.first_geometry_vertices(gamefiles.Img(game_data).read("colt45.dff"))
    x0, x1 = min(p[0] for p in v), max(p[0] for p in v)
    z1 = max(p[2] for p in v)
    yc = (min(p[1] for p in v) + max(p[1] for p in v)) / 2
    scale = (x1 - x0) / 0.216 * 0.95
    return scale, [(0, -1, 0), (1, 0, 0), (0, 0, 1), (x0 + 0.03, yc, z1 - TOP * MM * scale - 0.04)]


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale, frame = hold(game_data)
    scene = kit.studio()
    gun, mag = detailed(kit.photo_materials())
    rgba = kit.atlas(scene, REGIONS, {"left": [gun], "top": [gun], "back": [gun], "front": [gun], "magazine": [mag]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "colt45-atlas.png")
    kit.save_png(atlas_png, flat)
    gun.hide_render = mag.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            if ob.name.startswith("mag_body"):
                kit.project(ob, REGIONS, lambda n, c: "magazine")
            else:
                kit.project(ob, REGIONS, choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING)
    print(f"colt45: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "colt45-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.32, 0.02, 0.16), (0, 0.11, 0.07), 50), ((0.30, 0.25, 0.20), (0, 0.10, 0.07), 50),
                 ((-0.15, -0.20, 0.17), (0.0, 0.25, 0.10), 35)])


main()
