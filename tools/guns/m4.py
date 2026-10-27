"""Vice City's M4 (a Colt M4 carbine with its fixed carry handle, black, with a 30-round magazine) for the
first-person viewmodel. Two-handed: his left hand under the handguard.

blender -b -P tools/guns/m4.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right, drawn from the upper receiver's back face (y = 0)
and the bore (z = 0), then moved into the Colt's frame as the asset is written (SHIFT). A real M4: 838 mm with its stock out, a 14.5 inch (368 mm) barrel, a carbine handguard, the
front sight 14.5 inches ahead of the rear. As colt45.py (the worked example), on kit.py.
"""
import math
import os
import sys

import bpy
from mathutils import Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

LENGTH = 838.0  # mm, stock extended
BUTT, MUZZLE_Y = -258.0, 580.0
TUBE_Z, TUBE_R = -2.0, 14.5  # the buffer tube (receiver extension)
GUARD_R = 26.0  # the handguard's radius
GUARD_Y = (196.0, 364.0)
RAKE = math.radians(25)  # the pistol grip's rake back from vertical
GRIP_UP = (math.sin(RAKE), math.cos(RAKE))  # (y, z): up the grip
GRIP_BACK_TOP, GRIP_FRONT_TOP, GRIP_TOP_Z = -4.0, 44.0, -30.0
MAG_TOP = -14.0


def grip_back(t):
    """The pistol grip's back strap, t mm down it from the receiver: (y, z)."""
    return (GRIP_BACK_TOP - GRIP_UP[0] * t, GRIP_TOP_Z - GRIP_UP[1] * t)


def grip_front(t):
    return (GRIP_FRONT_TOP - GRIP_UP[0] * t, GRIP_TOP_Z - GRIP_UP[1] * t)


def grip_outline(swell=4.0):
    """The A2 grip's side: its top under the receiver, raked back, a finger swell on its front strap."""
    n = (GRIP_UP[1], -GRIP_UP[0])  # forward, square to the grip
    f = lambda t, out=0.0: (grip_front(t)[0] + n[0] * out, grip_front(t)[1] + n[1] * out)
    return [grip_back(0), (GRIP_FRONT_TOP, GRIP_TOP_Z), f(30), f(46, swell), f(62), f(96, -2), grip_back(104)]


# The 30-round magazine's side: straight in the magazine well, curving forward below it.
MAGAZINE = [(124, MAG_TOP), (124, -72), (128, -112), (135, -148), (143, -176),
            (206, -168), (199, -138), (192, -104), (188, -70), (188, MAG_TOP)]
MAG_AXIS = (0.0, 0.0, -1.0)
UPPER = [(0, -12), (178, -12), (182, -6), (182, 12), (178, 18), (0, 18)]
LOWER = [(0, -12), (192, -12), (192, -30), (190, -40), (190, -66), (116, -66), (116, -30), (GRIP_FRONT_TOP, GRIP_TOP_Z),
         (-12, -30), (-24, -24), (-24, -12)]
HANDLE = [(2, 18), (2, 60), (6, 68), (26, 68), (30, 62), (122, 62), (130, 56), (134, 18)]
WINDOW = [(40, 26), (112, 26), (116, 52), (36, 52)]
GUARD_OUT = [(46, -30), (118, -30), (118, -44), (112, -56), (54, -56), (46, -48)]
GUARD_IN = [(52, -20), (112, -20), (112, -44), (108, -50), (58, -50), (52, -44)]
TRIGGER = [(63, -28), (69, -28), (72, -37), (71, -46), (67, -53), (65, -51), (67, -44), (66, -36)]
STOCK = [(BUTT, 22), (-128, 22), (-120, 14), (-120, -18), (-138, -24), (-236, -92), (BUTT, -98)]
FSB = [(374, 8), (402, 8), (398, 30), (396, 50), (386, 50), (378, 30)]

# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
SIDE = 852 / 256
REGIONS = {
    "left": ((0, 0, 256, 88), (-400, (BUTT + MUZZLE_Y) / 2, -56), (1, 0, 0), (0, 0, 1), SIDE),
    "top": ((0, 88, 256, 32), (0, (BUTT + MUZZLE_Y) / 2, 400), (0, 0, -1), (-1, 0, 0), SIDE),
    "near": ((0, 120, 128, 104), (-400, 83, -26), (1, 0, 0), (0, 0, 1), 2.0),
    "magazine": ((128, 120, 128, 72), (-400, 160, -96), (1, 0, 0), (0, 1, 0), 1.5),
    "back": ((128, 192, 64, 64), (0, -600, -28), (0, 1, 0), (0, 0, 1), 3.1),
    "front": ((192, 192, 64, 64), (0, 900, -28), (0, -1, 0), (0, 0, 1), 3.1),
    "port": ((0, 224, 64, 32), (400, 106, 0), (-1, 0, 0), (0, 0, 1), 1.0),
    "handle": ((64, 224, 64, 32), (0, 68, 400), (0, 0, -1), (-1, 0, 0), 2.2),
}
NEAR_Y, NEAR_Z = (83 - 128, 83 + 128), (-26 - 104, -26 + 104)

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface, as the Colt's).
FINGER_R = 8.0
GRIP_X = 14.0  # the grip's half width


def on_grip(t, back):
    """A fingertip on the grip's left side, t mm down it, `back` mm behind its front strap."""
    y, z = grip_front(t)
    return (-GRIP_X - FINGER_R, y - back * GRIP_UP[1], z + back * GRIP_UP[0])


def on_guard(y, deg, r=GUARD_R + FINGER_R):
    """A fingertip on the handguard at y, `deg` round from its right side (up positive)."""
    a = math.radians(deg)
    return (r * math.cos(a), y, r * math.sin(a))


# His left palm under the handguard a little ahead of its middle, the hand rolled 10 degrees so the palm
# leans on the near side; the fingers up the far side, the thumb along the near side.
PALM, ROLL = (0.0, 300.0, -GUARD_R), 10.0
# The middle, ring and little fingers below the trigger guard, the finger swell between the middle and ring.
# Longer fingers (some outfits') may go further round the grip: forward along it, the middle down past the guard.
FORWARD = (0.0, 18.0, 0.0)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 69.0 + FINGER_R, -42.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(37, 22), None, None, (0.0, 8.0, -17.0)),
    ("r_ring", on_grip(56, 20), None, None, FORWARD),
    ("r_pinky", on_grip(75, 18), None, None, FORWARD),
    ("r_thumb", (-12.5 - FINGER_R, 40.0, -22.0), None, None, (0.0, -12.0, 0.0)),
    ("l_index", on_guard(330, 5), None, None, None),
    ("l_middle", on_guard(311, 7), None, None, None),
    ("l_ring", on_guard(293, 5), None, None, None),
    ("l_pinky", on_guard(276, -1), None, None, None),
    ("l_thumb", on_guard(338, 160), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),  # the bolt carrier in the ejection port (the charging handle stays put)
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), MAG_AXIS),
]
WEB = (0.0, *grip_back(14))  # where the web of his right hand rests: the grip's back strap, under the receiver
# The asset's gun frame is the Colt's: its origin moved so his web sits where the Colt's does (y 18.6, z 85), the
# grip's foot near z = 0 and the trigger by the Colt's. (The model above is drawn from the receiver's back face and
# the bore; the shift is applied as the asset is written.)
SHIFT = Vector((0.0, 18.6 - WEB[1], 85.0 - WEB[2]))


def placed(p):
    return tuple(Vector(p) + SHIFT)


MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, 0), (0, 14, 63), (0, 390, 63)
# Seen from (in the model's frame, real metres): left of and over the receiver, behind it by the stock's length,
# looking 5 degrees right of the barrel and 1 down, so the gun runs in from the lower right towards the crosshair
# with both hands in view; aiming, 0.075 m behind the rear sight and just over the carry handle.
FRAMING = ((-0.190, -0.270, 0.115), 5.0, -1.0, 0.075, 0.009)
LEFT_FOREARM = (-0.45, -0.7, -0.6)  # kit's, steeper: his left shoulder's sleeve showed at the bottom left


def worn(name, colour, metallic=0.0, roughness=0.5, spec=0.5, grain=0.2, edge_mm=1.2):
    """A photographed finish: `colour` mottled by +-`grain` (a few millimetres' noise), its edges rounded by a
    bevel shader so they catch the light the way a real gun's do (Cycles)."""
    m = material(name, colour, metallic, roughness, spec)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 350.0
    noise.inputs["Detail"].default_value = 6.0
    nt.links.new(tc.outputs["Object"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    lo, hi = ramp.color_ramp.elements
    lo.position, lo.color = 0.3, (*(c * (1 - grain) for c in colour), 1)
    hi.position, hi.color = 0.7, (*(c * (1 + grain) for c in colour), 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    edge = nt.nodes.new("ShaderNodeBevel")
    edge.inputs["Radius"].default_value = edge_mm * MM
    nt.links.new(edge.outputs["Normal"], b.inputs["Normal"])
    return m


def materials():
    m = kit.photo_materials()
    # Vice City's M4 is a dark grey, a little cool, with lighter edges.
    m["anod"] = worn("anodized_photo", (0.07, 0.075, 0.08), metallic=0.5, roughness=0.36)
    m["park"] = worn("parkerized_photo", (0.055, 0.058, 0.06), metallic=0.7, roughness=0.45)
    m["poly"] = worn("polymer_photo", (0.045, 0.048, 0.05), roughness=0.5, spec=0.45, grain=0.3)
    m["grip"] = worn("grip_photo", (0.035, 0.037, 0.04), roughness=0.6, spec=0.4, grain=0.5, edge_mm=2.0)
    m["shadow"] = material("shadow_photo", (0.01, 0.01, 0.01), roughness=0.9, spec=0.1)
    m["brass"] = material("brass_photo", (0.75, 0.55, 0.25), metallic=1.0, roughness=0.3)
    return m


def detailed(m):
    """The photo shoot's M4: every part the texture shows. Returns (gun, carry handle, magazine)."""
    anod, park, poly, grip, steel = m["anod"], m["park"], m["poly"], m["grip"], m["steel"]
    parts = []
    # The upper: the ejection port (right) with the bolt carrier in it, the forward assist and the deflector.
    upper = profile_yz("upper", fillet(UPPER, [0, 0, 2, 2, 2, 0], 2), -11.5, 11.5, anod)
    cut(upper, box("port", 9.5, 14, 82, 130, -7.5, 7.5))
    bevel(upper, 0.6)
    parts.append(upper)
    parts.append(box("carrier", 7.5, 10.2, 80, 132, -7.5, 7.5, park))
    parts.append(box("carrier_key", 7.0, 10.6, 96, 104, -1.0, 6.0, steel))
    parts.append(profile_yz("dust_cover", [(82, -8.5), (130, -8.5), (130, -12), (82, -12)], 11.5, 13.0, anod))
    parts.append(profile_yz("deflector", fillet([(64, 4), (78, 4), (78, 16), (70, 18), (64, 12)], 2, 2), 11, 16, anod))
    fa = cylinder_y("forward_assist", 15.0, 6.0, 6.5, 22, 56, 12, anod)
    parts += [fa, cylinder_y("fa_button", 15.0, 6.0, 5.0, 14, 22, 12, steel)]
    # The charging handle at the back, its latch on the left.
    parts.append(profile_yz("charging", [(-10, 13), (0, 13), (0, 21), (-10, 21)], -9, 9, anod))
    parts.append(profile_xz("ch_latch", [(-18, 13.5), (-9, 13.5), (-9, 20.5), (-16, 20.5)], -12, -1, anod))
    # The carry handle (its own photo from above), with the rear sight's aperture and windage drum.
    handle = profile_yz("handle", fillet(HANDLE, [0, 2, 2, 2, 2, 3, 3, 0], 2), -9.5, 9.5, anod)
    cut(handle, profile_yz("window", fillet(WINDOW, 4, 2), -20, 20))
    cut(handle, cylinder_x("aperture", 14, 63, 1.6, -20, 20, 10))
    cut(handle, box("sight_slot", -12, 12, 9, 19, 58, 69))
    bevel(handle, 0.6)
    aperture = profile_yz("aperture_leaf", fillet([(10, 56), (18, 56), (18, 69), (10, 69)], 1.5, 2), -6, 6, park)
    cut(aperture, cylinder_x("hole", 14, 63, 1.7, -10, 10, 10))
    drum = cylinder_x("windage", 14, 58, 6.5, 9.5, 14, 16, steel)
    elevation = cylinder_x("elevation", 22, 40, 7.0, -11, 11, 16, park)
    hparts = [handle, aperture, drum]
    parts.append(elevation)
    # The lower: magazine well, bolt catch, selector, pins, magazine release, trigger guard and trigger.
    lower = profile_yz("lower", fillet(LOWER, [0, 2, 0, 2, 2, 0, 0, 0, 4, 0, 0], 2), -12.5, 12.5, anod)
    cut(lower, profile_yz("magwell", [(119, -40), (187, -40), (187, -70), (119, -70)], -10, 10))
    for x0, x1 in ((-14, -12), (12, 14)):  # the magazine well's side panels, where the markings go
        cut(lower, profile_yz("panel", fillet([(126, -30), (182, -30), (182, -58), (126, -58)], 3, 2), x0, x1))
    bevel(lower, 0.6)
    parts.append(lower)
    parts.append(profile_yz("flare", [(114, -60), (192, -60), (193, -67), (113, -67)], -14, 14, anod))
    parts.append(profile_yz("bolt_catch", fillet([(106, -14), (120, -14), (122, -30), (110, -30)], 2, 2), -14.5, -12.5, anod))
    parts.append(cylinder_x("mag_release", 112, -36, 4.5, 12.5, 15, 12, steel))
    parts.append(profile_yz("mag_fence", [(104, -30), (120, -30), (120, -42), (108, -42)], 12.5, 14.5, anod))
    selector = profile_yz("selector", fillet([(22, -19), (44, -21), (44, -25), (22, -25)], [3, 1, 1, 3], 2), -15, -12.5, park)
    parts += [selector, cylinder_x("selector_hub", 22, -22, 5, -14.5, -12.5, 12, park)]
    for y, z in ((186, -18), (4, -18), (62, -22), (84, -22)):
        for x0, x1 in ((-13.5, -12.5), (12.5, 13.5)):
            parts.append(cylinder_x("pin", y, z, 3.0 if y in (4, 186) else 2.2, x0, x1, 10, steel))
    tg = profile_yz("trigger_guard", fillet(GUARD_OUT, [0, 0, 4, 6, 6, 3], 2), -5, 5, anod)
    cut(tg, profile_yz("tg_opening", fillet(GUARD_IN, [0, 0, 3, 4, 4, 0], 2), -10, 10))
    bevel(tg, 0.5)
    parts += [tg, profile_yz("trigger", fillet(TRIGGER, [1, 1, 3, 3, 1, 1, 3, 3], 2), -3.2, 3.2, park)]
    g = profile_yz("grip", fillet(grip_outline(), [0, 2, 3, 6, 4, 6, 6], 3), -GRIP_X, GRIP_X, grip)
    bevel(g, 2.5, 2, 30)
    parts.append(g)
    # The buffer tube, its castle nut and end plate, and the four-position stock run out.
    parts.append(profile_yz("tower", fillet([(-24, -24), (0, -24), (0, 14), (-24, 14)], 4, 2), -15.5, 15.5, anod))
    parts.append(cylinder_y("tube", 0, TUBE_Z, TUBE_R, -190, -24, 24, anod))
    nut = cylinder_y("castle_nut", 0, TUBE_Z, 17.5, -34, -26, 24, park)
    for k in range(6):
        a = 2 * math.pi * k / 6
        cut(nut, _rotated_notch(a))
    parts += [nut, cylinder_y("end_plate", 0, TUBE_Z, 17.0, -26, -24, 24, park)]
    stock = profile_yz("stock", fillet(STOCK, [2, 3, 3, 4, 4, 8, 2], 3), -20, 20, poly)
    cut(stock, profile_yz("sling_slot", fillet([(-250, -40), (-238, -40), (-238, -76), (-250, -76)], 2, 2), -30, 30))
    bevel(stock, 1.5, 1, 30)
    parts.append(stock)
    parts.append(profile_yz("butt_pad", fillet([(BUTT - 6, 22), (BUTT, 22), (BUTT, -98), (BUTT - 6, -98)], 1.5, 2),
                            -20.5, 20.5, m["grip"]))
    parts.append(profile_yz("stock_lever", fillet([(-170, -38), (-140, -26), (-136, -30), (-166, -42)], 1, 2), -6, 6, poly))
    parts.append(profile_xz("stock_ridge", [(-20.5, -4), (20.5, -4), (20.5, 4), (-20.5, 4)], -232, -136, poly))
    # The delta ring, the ribbed carbine handguard with its vents, the cap, the front sight and the barrel.
    parts.append(cylinder_y("delta_ring", 0, 0, 29.5, 182, 196, 28, park))
    parts.append(cylinder_y("barrel_nut", 0, 0, 20.0, 178, 184, 20, park))
    ribs = [(r * math.cos(2 * math.pi * k / 64), r * math.sin(2 * math.pi * k / 64))
            for k, r in ((k, GUARD_R + (0.7 if k % 2 else 0.0)) for k in range(64))]
    hg = profile_xz("handguard", ribs, GUARD_Y[0], GUARD_Y[1], poly)
    vents = []
    for k in range(6):
        y = 216 + k * 24
        for side in (-1, 1):
            vents.append(cylinder_x("vent", y, 8, 4.5, side * 10, side * 40, 12))
            vents.append(cylinder_x("vent", y + 6, 8, 4.5, side * 10, side * 40, 12))
    cut(hg, join("vents", vents))
    cut(hg, cylinder_y("bore", 0, 0, GUARD_R - 3, GUARD_Y[0] - 5, GUARD_Y[1] + 5, 24))
    parts.append(hg)
    for y0, y1 in ((GUARD_Y[0], GUARD_Y[0] + 6), (GUARD_Y[1] - 6, GUARD_Y[1])):
        parts.append(cylinder_y("seam", 0, 0, GUARD_R + 1.0, y0, y1, 28, poly))
    parts.append(cylinder_y("heat_shield", 0, 0, GUARD_R - 3, GUARD_Y[0], GUARD_Y[1], 20, m["shadow"]))
    parts.append(cylinder_y("gas_tube", 0, 12, 2.5, 190, 380, 8, steel))
    parts.append(cylinder_y("cap", 0, 0, 23.5, 364, 372, 24, park))
    parts.append(cylinder_y("barrel", 0, 0, 9.0, 170, 402, 16, park))
    barrel = cylinder_y("muzzle_barrel", 0, 0, 7.8, 402, 540, 16, park)
    cut(barrel, _groove())
    parts.append(barrel)
    parts.append(cylinder_y("fsb_collar", 0, 0, 12.5, 372, 402, 16, park))
    parts.append(profile_yz("fsb", fillet(FSB, [0, 0, 3, 2, 2, 3], 2), -7, 7, park))
    for x0, x1 in ((-8, -5.2), (5.2, 8)):
        parts.append(profile_yz("ear", fillet([(384, 48), (397, 48), (395, 66), (387, 66)], [0, 0, 1.5, 1.5], 2), x0, x1, park))
    parts.append(profile_yz("post", [(389, 48), (393, 48), (392.5, 63), (389.5, 63)], -1.3, 1.3, park))
    parts.append(profile_yz("bayonet_lug", fillet([(388, -10), (402, -10), (402, -22), (392, -22)], 1.5, 2), -4, 4, park))
    swivel = cylinder_x("swivel", 380, -26, 7, -2, 2, 16, steel)
    cut(swivel, cylinder_x("swivel_hole", 380, -26, 4.5, -5, 5, 16))
    parts.append(swivel)
    hider = cylinder_y("flash_hider", 0, 0, 11, 536, MUZZLE_Y, 20, park)
    slots = [box("slot", -2, 2, 548, 574, 6, 16)]
    for a in (60, 120, -60, -120):
        s = box("slot", -2, 2, 548, 574, 6, 16)
        s.rotation_euler = (0, math.radians(a), 0)
        slots.append(s)
    for s in slots[1:]:
        bpy.context.view_layer.update()
        _apply_transform(s)
    cut(hider, join("slots", slots))
    cut(hider, cylinder_y("bore", 0, 0, 5.0, 530, 590, 12))
    parts.append(hider)
    for p in parts + hparts:
        finish(p, 35)
    gun = join("m4", parts)
    handle_ob = join("carry_handle", hparts)
    # The magazine: ribbed body, its baseplate, and the top round in its lips.
    body = profile_yz("magazine", fillet(MAGAZINE, [0, 0, 8, 8, 2, 2, 8, 8, 0, 0], 3), -11, 11, m["anod"])
    bevel(body, 0.8)
    mag = [body]
    for x0, x1 in ((-11.8, -10.5), (10.5, 11.8)):
        mag.append(profile_yz("mag_rib", fillet([(132, -28), (180, -28), (184, -96), (138, -100)], 3, 2), x0, x1, m["anod"]))
    mag.append(profile_yz("baseplate", fillet([(141, -172), (208, -164), (210, -172), (142, -182)], 2, 2), -12.5, 12.5, poly))
    mag.append(cylinder_y("round", 0, MAG_TOP + 4.5, 4.7, 130, 172, 12, m["brass"]))
    mag.append(cylinder_y("bullet", 0, MAG_TOP + 4.5, 2.9, 172, 184, 10, m["brass"]))
    for p in mag:
        finish(p, 35)
    return gun, handle_ob, join("magazine", mag)


def _apply_transform(ob):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.select_set(False)


def _rotated_notch(angle):
    """A castle-nut notch cutter at `angle` round the tube."""
    c = box("notch", -2.2, 2.2, -36, -30.5, 0, 20)
    c.location = (0, 0, TUBE_Z * MM)
    c.rotation_euler = (0, angle, 0)
    bpy.context.view_layer.update()
    _apply_transform(c)
    return c


def _groove():
    """The M4's cut ahead of the front sight (where a grenade launcher clamps): a ring round the barrel."""
    outer = cylinder_y("groove", 0, 0, 12, 405, 409, 16)
    inner = cylinder_y("groove_in", 0, 0, 7.0, 400, 414, 16)
    cut(outer, inner)
    return outer


def low():
    """The M4 at Vice City's detail (about 610 triangles), in its moving parts: {part: [objects]}."""
    handle = profile_yz("handle", HANDLE, -9.5, 9.5)
    cut(handle, profile_yz("window", WINDOW, -20, 20), solver="EXACT")
    tg = profile_yz("trigger_guard", GUARD_OUT, -5, 5)
    cut(tg, profile_yz("tg_opening", GUARD_IN, -10, 10), solver="EXACT")
    body = [
        profile_yz("upper", UPPER, -11.5, 11.5),
        handle,
        box("charging", -9, 9, -10, 0, 13, 21),
        profile_yz("lower", LOWER, -12.5, 12.5),
        tg,
        profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
        profile_yz("tower", [(-24, -24), (0, -24), (0, 14), (-24, 14)], -15.5, 15.5),
        cylinder_y("tube", 0, TUBE_Z, TUBE_R, -190, -24, 8),
        profile_yz("stock", STOCK, -20, 20),
        cylinder_y("delta_ring", 0, 0, 29.5, 182, 196, 10),
        cylinder_y("handguard", 0, 0, GUARD_R, GUARD_Y[0], GUARD_Y[1], 12),
        cylinder_y("cap", 0, 0, 23.5, 364, 372, 10),
        cylinder_y("fsb_collar", 0, 0, 12.5, 372, 402, 8),
        profile_yz("fsb", FSB, -7, 7),
        profile_yz("ear_l", [(384, 48), (397, 48), (395, 66), (387, 66)], -8, -5.2),
        profile_yz("ear_r", [(384, 48), (397, 48), (395, 66), (387, 66)], 5.2, 8),
        profile_yz("post", [(389, 48), (393, 48), (392.5, 63), (389.5, 63)], -1.3, 1.3),
        profile_yz("bayonet_lug", [(388, -10), (402, -10), (402, -22), (392, -22)], -4, 4),
        cylinder_y("barrel", 0, 0, 7.8, 402, 536, 8),
        cylinder_y("flash_hider", 0, 0, 11, 536, MUZZLE_Y, 8),
    ]
    bolt = [box("carrier", 11.0, 12.0, 82, 130, -7.5, 7.5)]
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    magazine = [profile_yz("mag_body", MAGAZINE, -11, 11),
                profile_yz("baseplate", [(141, -172), (208, -164), (210, -172), (142, -182)], -12.5, 12.5)]
    return {"body": body, "bolt": bolt, "trigger": trigger, "magazine": magazine}


def chooser(bvh):
    """Which photo a face of the gun wears: (normal, centre, its corners (mm), its part) -> region. The top,
    back or front where it faces them and the photo shows it (not hidden behind something nearer), the near
    side's photo round the receiver, the whole side's otherwise (the right mirrors the left)."""
    def visible(centre, normal, look):
        d = -Vector(look).normalized()
        start = (centre + normal * 0.6) * MM
        hit, _, _, dist = bvh.ray_cast(start, d, 2.0)
        return hit is None or dist < 3.0 * MM

    def choose(normal, centre, corners, part, name):
        if part == "magazine":
            return "magazine"
        if part == "bolt":
            return "port"
        ax = max(range(3), key=lambda k: abs(normal[k]))
        if name.startswith("handle") and ax == 2 and normal.z > 0:
            return "handle"
        if ax == 2 and normal.z > 0 and not name.startswith("handle") and visible(centre, normal, (0, 0, -1)):
            return "top"
        if ax == 1 and normal.y < 0 and visible(centre, normal, (0, 1, 0)):
            return "back"
        if ax == 1 and normal.y > 0 and visible(centre, normal, (0, -1, 0)):
            return "front"
        ys, zs = [c.y for c in corners], [c.z for c in corners]
        if NEAR_Y[0] <= min(ys) and max(ys) <= NEAR_Y[1] and NEAR_Z[0] <= min(zs) and max(zs) <= NEAR_Z[1]:
            return "near"
        return "left"
    return choose


def project(ob, regions, choose, part):
    """UVs for a low-poly object: each face from the region `choose` names (kit.project, told the face's
    corners and part too)."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        corners = [me.vertices[i].co / MM for i in poly.vertices]
        centre = sum(corners, Vector()) / len(corners)
        region = regions[choose(poly.normal, centre, corners, part, ob.name)]
        for li in poly.loop_indices:
            uv.data[li].uv = kit.region_uv(region, me.vertices[me.loops[li].vertex_index].co / MM)


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "m4", LENGTH)
    hold = kit.hold_for(placed(WEB), scale)
    hold_left = kit.hold_left_for(placed(PALM), scale, ROLL)
    scene = kit.studio()
    gun, handle, mag = detailed(materials())
    rgba = kit.atlas(scene, REGIONS, {"left": [gun, handle], "top": [gun], "near": [gun, handle], "magazine": [mag],
                                      "back": [gun, handle], "front": [gun, handle], "port": [gun],
                                      "handle": [handle]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "m4-atlas.png")
    kit.save_png(atlas_png, flat)
    dg = bpy.context.evaluated_depsgraph_get()
    bvh = BVHTree.FromObject(join("occluders", [gun, handle]), dg)
    choose = chooser(bvh)
    for o in list(scene.objects):
        if o.type == "MESH":
            o.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            project(ob, REGIONS, choose, part)
            finish(ob, 35)
            ob.location = SHIFT * MM  # into the asset's frame (the photos' UVs are already set)
            objects.append((ob, names.index(part)))
    bpy.context.view_layer.update()
    parts = [(n, placed(pivot), axis) for n, pivot, axis in PARTS]
    anchors = [(n, placed(p), None if part is None else names.index(part), bend, slack)
               for n, p, part, bend, slack in ANCHORS]
    (ex, ey, ez), yaw, pitch, back, above = FRAMING
    framing = (tuple(c + d * MM for c, d in zip((ex, ey, ez), SHIFT)), yaw, pitch, back, above)
    count = kit.write_vmg(out, scale, hold, placed(MUZZLE), placed(SIGHT_REAR), placed(SIGHT_FRONT), parts, anchors,
                          objects, palette, pixels, framing, two_handed=True, hold_left=hold_left,
                          left_forearm=LEFT_FOREARM)
    print(f"m4: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in hold[3]]}")
    at = lambda x, y, z: tuple(Vector((x, y, z)) + SHIFT * MM)
    kit.preview(os.path.join(work, "m4-preview.png"), [o for o, _ in objects], atlas_png,
                [(at(-0.75, 0.16, 0.0), at(0, 0.16, -0.04), 50), (at(0.55, 0.55, 0.25), at(0, 0.16, -0.03), 50),
                 (at(-0.14, -0.20, 0.10), at(0.0, 0.35, -0.02), 35)])


main()
