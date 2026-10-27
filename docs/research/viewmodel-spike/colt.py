"""Throwaway spike: Vice City's Colt 45 (a stainless M1911 with black grips) as a first-person model.

Millimetres, y forward along the barrel, z up, x to the right. The tang's tip is at y = 0.
"""
import math
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from lib import (MM, apply_all, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, modifier,
                 profile_xz, profile_yz, tris)

RAKE = math.tan(math.radians(18))  # the grip's rake back from vertical


def front_strap_y(z):
    return 73.0 - (92.0 - z) * RAKE


def back_strap_y(z):
    return -1.0 + (z - 24.5) * RAKE


def build(mats):
    steel, dark, grip = mats["steel"], mats["dark"], mats["grip"]
    parts = []

    # The slide: a rounded-top section along the barrel, serrated at the back,
    # the ejection port on the right, bored at the front.
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

    # The barrel (seen through the port), its bushing and the recoil spring plug, bored.
    barrel = cylinder_y("barrel", 0, 117, 7.0, 96, 215.5, 14, steel)
    bushing = cylinder_y("bushing", 0, 117, 8.8, 211, 217.5, 14, steel)
    cut(barrel, cylinder_y("bore", 0, 117, 5.7, 200, 225, 12))
    cut(bushing, cylinder_y("bore", 0, 117, 5.7, 200, 225, 12))
    plug = cylinder_y("plug", 0, 106.5, 5.0, 212, 216.8, 10, steel)
    parts += [barrel, bushing, plug]

    # Sights.
    front_sight = profile_yz("front_sight", [(196, 129), (207, 129), (207, 135.5), (203.5, 136), (199, 133)], -1.4, 1.4, dark)
    rear_sight = profile_yz("rear_sight", [(21, 129), (32, 129), (32, 135), (24, 135), (21, 133)], -7, 7, dark)
    cut(rear_sight, box("notch", -1.6, 1.6, 18, 36, 132.2, 137))
    parts += [front_sight, rear_sight]

    # The frame: dust cover, the grip raked back, rounded at the bottom.
    outline = [(20, 102), (197, 102), (197, 92.5), (front_strap_y(92), 92), (front_strap_y(9), 9), (back_strap_y(24.5), 24.5),
               (back_strap_y(96), 96)]
    frame = profile_yz("frame", fillet(outline, [0, 0, 4, 2, 4, 5, 3], 3), -11, 11, steel)
    bevel(frame, 0.6)
    parts.append(frame)

    # The trigger guard, its opening cut out.
    guard = profile_yz("guard", fillet([(68, 92), (122, 92), (122, 79), (117, 69.5), (80, 69), (66, 74)], [0, 0, 8, 6, 9, 0], 3),
                       -4.6, 4.6, steel)
    cut(guard, profile_yz("opening", fillet([(76, 94), (116.5, 94), (116.5, 80), (113, 74), (82, 74), (76.5, 79)],
                                            [0, 0, 6, 4, 6, 0], 3), -10, 10))
    bevel(guard, 0.5)
    parts.append(guard)

    trigger = profile_yz("trigger", [(87, 93), (93, 93), (93.3, 80.5), (91, 78.5), (88, 79), (87.4, 86)], -3.2, 3.2, dark)
    parts.append(trigger)

    # The hammer, cocked, and the grip safety's tang behind it.
    hammer = profile_yz("hammer", fillet([(20, 99), (17, 103.5), (10, 112), (5.5, 115.5), (2.5, 114), (4.5, 110), (12, 101), (14, 98)],
                                         [0, 2, 0, 2, 2, 0, 2, 0], 2), -3.4, 3.4, dark)
    tang = profile_yz("tang", fillet([(25, 93), (9, 96), (0, 100.5), (3, 104.2), (19, 104.2), (25, 101)], [0, 4, 3, 2, 0, 0], 3),
                      -9.5, 9.5, steel)
    bevel(tang, 0.5)
    parts += [hammer, tang]

    # Grip panels, with their screws, and the magazine's base.
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
    mag = profile_yz("mag_base", [(front_strap_y(9) + 1.5, 10), (front_strap_y(9) - 0.5, 6.3), (back_strap_y(24.5) - 1.2, 21.5),
                                  (back_strap_y(24.5) - 0.8, 25)], -10, 10, dark)
    parts.append(mag)

    # Left side: the slide stop, the thumb safety, the magazine catch.
    stop = profile_yz("slide_stop", fillet([(118, 97), (118, 104.5), (92, 104.5), (88, 101.5), (92, 98.5)], [3, 3, 2, 2, 0], 2),
                      -12.8, -11, dark)
    safety = profile_yz("safety", fillet([(33, 100), (52, 101), (52, 105.5), (31, 105.5)], [1, 2, 2, 1], 2), -13.2, -11, dark)
    catch = cylinder_x("catch", front_strap_y(86) - 3, 86, 4.6, -12.3, -11, 10, dark)
    parts += [stop, safety, catch]

    for p in parts:
        finish(p, 35)
    total = sum(tris(p) for p in parts)
    gun = join("colt45", parts)
    return gun, total


def materials():
    return {
        "steel": material("stainless", (0.58, 0.58, 0.56), metallic=1.0, roughness=0.34),
        "dark": material("dark_steel", (0.08, 0.08, 0.085), metallic=0.85, roughness=0.42),
        "grip": material("grip", (0.035, 0.032, 0.03), roughness=0.62, spec=0.35),
    }
