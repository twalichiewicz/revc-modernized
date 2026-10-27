"""Vice City's MP5 (an H&K MP5A3, its retractable stock pushed in, in the game's light blue-grey finish, with a
curved 30-round magazine) for the first-person viewmodel. Two-handed: his left hand under the slim handguard.

blender -b -P tools/guns/mp5lng.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right, drawn from the end cap's back face (y = 0) and
the bore (z = 0), then moved into the Colt's frame as the asset is written (SHIFT). A real MP5A3: 550 mm with
its stock in, a 225 mm barrel, the drum rear sight 340 mm behind the front post. As colt45.py and m4.py.
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

LENGTH = 550.0  # mm, stock pushed in
BUTT, MUZZLE_Y = -60.0, 485.0
RAKE = math.radians(16)  # the pistol grip's rake back from vertical
GRIP_UP = (math.sin(RAKE), math.cos(RAKE))  # (y, z): up the grip
GRIP_BACK_TOP, GRIP_FRONT_TOP, GRIP_TOP_Z = 72.0, 116.0, -32.0
GRIP_X = 15.0  # the grip's half width
MAG_TOP = -16.0
TUBE_Z, TUBE_R = 12.0, 12.0  # the cocking tube over the barrel
GUARD_Y = (270.0, 405.0)  # the handguard


def grip_back(t):
    """The pistol grip's back strap, t mm down it from the trigger housing: (y, z)."""
    return (GRIP_BACK_TOP - GRIP_UP[0] * t, GRIP_TOP_Z - GRIP_UP[1] * t)


def grip_front(t):
    return (GRIP_FRONT_TOP - GRIP_UP[0] * t, GRIP_TOP_Z - GRIP_UP[1] * t)


def grip_outline():
    """The grip's side: its top under the trigger housing, raked back, a palm swell at the back."""
    n = (GRIP_UP[1], -GRIP_UP[0])  # forward, square to the grip
    b = lambda t, out: (grip_back(t)[0] - n[0] * out, grip_back(t)[1] - n[1] * out)
    return [grip_back(0), (GRIP_FRONT_TOP, GRIP_TOP_Z), grip_front(40), grip_front(92), grip_back(98), b(50, 3)]


MAG_REAR = [(210, MAG_TOP), (210, -60), (215, -100), (226, -140), (244, -178), (262, -200)]
MAG_FRONT = [(300, -190), (282, -160), (266, -122), (254, -82), (248, -40), (248, MAG_TOP)]
MAG_AXIS = (0.0, 0.0, -1.0)
RECEIVER = [(-16, -22), (16, -22), (16, 18), (10, 24), (-10, 24), (-16, 18)]  # its section (x, z)
HOUSING = [(66, -22), (206, -22), (206, -30), (196, -34), (GRIP_FRONT_TOP, -34), (GRIP_BACK_TOP, -32), (62, -27)]
GUARD_OUT = [(118, -33), (198, -33), (198, -44), (190, -60), (126, -60), (118, -50)]
GUARD_IN = [(124, -24), (192, -24), (192, -44), (186, -54), (130, -54), (124, -46)]
TRIGGER = [(130, -30), (136, -30), (139, -40), (138, -49), (134, -56), (132, -54), (134, -48), (133, -39)]
HANDGUARD = [(-14, 12), (-21, 0), (-21, -14), (-16, -27), (-7, -32), (7, -32), (16, -27), (21, -14), (21, 0), (14, 12)]
BUTT_PAD = [(-44, 26), (-40, 30), (-40, -82), (-50, -92), (BUTT, -88), (-57, -36), (-54, 18)]
END_CAP = [(0, -18), (12, -22), (12, 24), (0, 21)]
COCKING = [(-11, 10), (-11, 18), (-29, 31), (-34, 26)]  # the cocking handle's section (x, z), out to the left
SIGHT_BASE = [(418, -12), (442, -12), (442, 26), (418, 26)]

SIDE = 563 / 256
REGIONS = {
    "left": ((0, 0, 256, 84), (-400, (BUTT + MUZZLE_Y) / 2 - 3, -38), (1, 0, 0), (0, 0, 1), SIDE),
    "top": ((0, 84, 256, 28), (0, (BUTT + MUZZLE_Y) / 2 - 3, 400), (0, 0, -1), (-1, 0, 0), SIDE),
    "near": ((0, 112, 128, 96), (-400, 120, -50), (1, 0, 0), (0, 0, 1), 1.6),
    "magazine": ((128, 112, 128, 96), (-400, 255, -108), (1, 0, 0), (0, 1, 0), 1.5),
    "back": ((0, 208, 64, 48), (0, -600, -22), (0, 1, 0), (0, 0, 1), 3.1),
    "front": ((64, 208, 64, 48), (0, 900, -22), (0, -1, 0), (0, 0, 1), 3.1),
    "cocking": ((128, 208, 64, 48), (-400, 409, 20), (1, 0, 0), (0, 0, 1), 0.8),
    "under": ((192, 208, 64, 48), (0, 338, -400), (0, 0, 1), (1, 0, 0), 3.0),
}
NEAR_Y, NEAR_Z = (120 - 102, 120 + 102), (-50 - 76, -50 + 76)

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface, as the Colt's).
FINGER_R = 8.0


def on_grip(t, back):
    """A fingertip on the grip's left side, t mm down it, `back` mm behind its front strap."""
    y, z = grip_front(t)
    return (-GRIP_X - FINGER_R, y - back * GRIP_UP[1], z + back * GRIP_UP[0])


def on_guard(y, z, side=1):
    """A fingertip on the handguard's side at y, z (right side, or left with side = -1)."""
    return (side * (21.0 + FINGER_R), y, z)


# His left palm under the handguard, ahead of its middle (further back, his forearm meets the magazine's curve),
# the hand rolled 10 degrees; the fingers up its far side, the thumb along the near.
PALM, ROLL = (0.0, 338.0, -35.0), 10.0
FORWARD = (0.0, 16.0, 0.0)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 136.0 + FINGER_R, -44.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(30, 20), None, None, (0.0, 8.0, -17.0)),
    ("r_ring", on_grip(50, 19), None, None, FORWARD),
    ("r_pinky", on_grip(70, 17), None, None, FORWARD),
    ("r_thumb", (-16.0 - FINGER_R, 118.0, -24.0), None, None, (0.0, -12.0, 0.0)),
    ("l_index", on_guard(368, -4), None, None, None),
    ("l_middle", on_guard(350, -2), None, None, None),
    ("l_ring", on_guard(332, -4), None, None, None),
    ("l_pinky", on_guard(315, -10), None, None, None),
    ("l_thumb", on_guard(376, -2, -1), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),  # the cocking handle, on the left of its tube
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), MAG_AXIS),
]
WEB = (0.0, *grip_back(14))  # where the web of his right hand rests: the grip's back, just under the housing
# The asset's gun frame is the Colt's: its origin moved so his web sits where the Colt's does (y 18.6, z 85).
SHIFT = Vector((0.0, 18.6 - WEB[1], 85.0 - WEB[2]))
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, 0), (0, 100, 38), (0, 430, 38)
# Seen from (the model's frame, real metres): left of and a little over the receiver, behind the end cap, looking
# 6 degrees right of the barrel, so the whole gun runs in from the lower right towards the crosshair with both
# hands in view; aiming, 0.07 m behind the drum and just over it.
FRAMING = ((-0.190, -0.180, 0.090), 6.0, 0.0, 0.07, 0.012)


def placed(p):
    return tuple(Vector(p) + SHIFT)


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
    # Vice City's MP5 is a light, blue-tinted gun-metal grey, its grip and handguard a darker blue-grey.
    m["metal"] = worn("gunmetal_photo", (0.34, 0.37, 0.42), metallic=0.6, roughness=0.36)
    m["dark"] = worn("dark_metal_photo", (0.12, 0.13, 0.15), metallic=0.65, roughness=0.4)
    m["poly"] = worn("polymer_photo", (0.17, 0.19, 0.23), roughness=0.5, spec=0.45, grain=0.35)
    m["grip"] = worn("grip_photo", (0.13, 0.145, 0.175), roughness=0.6, spec=0.4, grain=0.5, edge_mm=2.0)
    m["rubber"] = worn("rubber_photo", (0.04, 0.042, 0.046), roughness=0.75, spec=0.3, grain=0.3, edge_mm=2.5)
    m["shadow"] = material("shadow_photo", (0.01, 0.01, 0.012), roughness=0.9, spec=0.1)
    return m


def detailed(m):
    """The photo shoot's MP5: every part the texture shows. Returns (gun, magazine, cocking handle)."""
    metal, dark, poly, steel = m["metal"], m["dark"], m["poly"], m["steel"]
    parts = []
    # The receiver: a stamped tube, its strut guides and weld dimples, the ejection port on the right, the
    # claw mount's lugs on top, the magazine well and its release.
    rec = profile_xz("receiver", fillet(RECEIVER, [1, 1, 2, 2, 2, 2], 2), 12, 262, metal)
    cut(rec, box("port", 12, 20, 168, 214, 0, 15))
    for side in (-1, 1):
        cut(rec, profile_yz("flute", fillet([(24, 1), (200, 1), (200, 5), (24, 5)], 2, 2), side * 15.2, side * 20))
    bevel(rec, 0.6)
    parts.append(rec)
    parts.append(box("bolt_face", 6, 13, 166, 216, 1, 14, dark))
    for y in (34, 150, 196, 240):
        parts.append(profile_xz("claw_lug", fillet([(-7, 24), (7, 24), (6, 27), (-6, 27)], 1, 1), y, y + 8, metal))
    for side in (-1, 1):
        for y, z in ((40, -12), (60, -12), (215, 12), (240, 12), (160, 14)):
            parts.append(cylinder_x("dimple", y, z, 2.2, side * 15.5, side * 16.4, 8, metal))
    parts.append(profile_yz("magwell", fillet([(204, -22), (254, -22), (254, -62), (204, -62)], [0, 0, 2, 2], 2), -15, 15, metal))
    parts.append(profile_yz("mag_paddle", fillet([(194, -58), (206, -58), (206, -66), (192, -64)], 1, 1), -9, 9, dark))
    parts.append(cylinder_x("mag_button", 228, -40, 4, 15, 17, 12, dark))
    parts.append(profile_yz("end_cap", fillet(END_CAP, 3, 2), -15, 15, dark))
    # The trigger housing (the "Navy" group): selector, push pins, trigger guard, trigger, the grip.
    housing = profile_yz("housing", fillet(HOUSING, [2, 2, 2, 3, 3, 3, 2], 2), -15, 15, poly)
    bevel(housing, 0.8)
    parts.append(housing)
    sel = profile_yz("selector", fillet([(80, -24), (96, -30), (93, -34), (78, -28)], 1, 2), -17.5, -15, dark)
    parts += [sel, cylinder_x("selector_hub", 82, -26, 5, -17, -15, 12, dark)]
    for y in (78, 198):
        for x0, x1 in ((-16.5, -15), (15, 16.5)):
            parts.append(cylinder_x("push_pin", y, -16, 3.2, x0, x1, 12, steel))
    tg = profile_yz("trigger_guard", fillet(GUARD_OUT, [0, 0, 4, 6, 6, 3], 2), -6, 6, poly)
    cut(tg, profile_yz("tg_opening", fillet(GUARD_IN, [0, 0, 3, 4, 4, 0], 2), -10, 10))
    bevel(tg, 0.6)
    parts += [tg, profile_yz("trigger", fillet(TRIGGER, [1, 1, 3, 3, 1, 1, 3, 3], 2), -3.5, 3.5, dark)]
    g = profile_yz("grip", fillet(grip_outline(), [0, 2, 6, 8, 8, 10], 3), -GRIP_X, GRIP_X, m["grip"])
    bevel(g, 3.0, 2, 30)
    parts.append(g)
    # The drum rear sight and its base, the cocking tube, the slim handguard, the hooded front sight, the barrel.
    parts.append(profile_yz("drum_base", fillet([(84, 22), (116, 22), (112, 30), (88, 30)], 1, 2), -10, 10, metal))
    drum = cylinder_x("drum", 100, 36, 12, -9, 9, 24, dark)
    cut(drum, cylinder_y("aperture", 0, 38, 1.6, 80, 120, 10))
    parts.append(drum)
    for x0, x1 in ((-11.5, -9), (9, 11.5)):
        parts.append(profile_yz("drum_ear", fillet([(88, 24), (112, 24), (110, 44), (90, 44)], 3, 2), x0, x1, metal))
    tube = cylinder_y("cocking_tube", 0, TUBE_Z, TUBE_R, 250, 432, 20, metal)
    cut(tube, profile_yz("slot", [(330, TUBE_Z - 2.5), (418, TUBE_Z - 2.5), (418, TUBE_Z + 2.5), (330, TUBE_Z + 2.5)], -20, -6))
    parts.append(tube)
    parts.append(profile_yz("tube_slot_dark", [(330, TUBE_Z - 2.5), (418, TUBE_Z - 2.5), (418, TUBE_Z + 2.5), (330, TUBE_Z + 2.5)],
                            -10.5, -8, m["shadow"]))
    hg = profile_xz("handguard", fillet(HANDGUARD, [0, 3, 4, 6, 6, 6, 6, 4, 3, 0], 2), GUARD_Y[0], GUARD_Y[1], poly)
    bevel(hg, 0.6)
    parts.append(hg)
    ridge = [(x * 1.05, z * 1.05 + 0.6) for x, z in HANDGUARD[1:-1]]
    for k in range(9):  # its finger ridges
        y = GUARD_Y[0] + 18 + k * 12.5
        parts.append(profile_xz("ridge", fillet(ridge, [3, 4, 6, 6, 6, 6, 4, 3], 2), y, y + 4, poly))
    parts.append(profile_xz("guard_lip", fillet([(-22, 2), (22, 2), (22, -16), (17, -30), (-17, -30), (-22, -16)], 3, 2),
                            GUARD_Y[1] - 6, GUARD_Y[1], poly))
    parts.append(profile_yz("sight_base", fillet(SIGHT_BASE, 2, 2), -11, 11, metal))
    hood = cylinder_y("hood", 0, 36, 11, 422, 440, 24, metal)
    cut(hood, cylinder_y("hood_hole", 0, 36, 8.5, 410, 450, 24))
    parts += [hood, profile_yz("post", [(429, 24), (433, 24), (432.5, 38), (429.5, 38)], -1.1, 1.1, dark)]
    parts.append(cylinder_y("barrel", 0, 0, 9.0, 262, 446, 16, dark))
    lugs = cylinder_y("muzzle", 0, 0, 10.0, 446, MUZZLE_Y, 20, dark)
    cut(lugs, cylinder_y("bore", 0, 0, 4.5, 440, 495, 12))
    parts.append(lugs)
    for k in range(3):
        a = math.radians(90 + 120 * k)
        lug = box("lug", -3, 3, 452, 466, 9, 12.5, dark)
        lug.rotation_euler = (0, a - math.radians(90), 0)
        _apply_transform(lug)
        parts.append(lug)
    # The A3 stock pushed in: its struts along the receiver's sides and the curved butt pad behind the end cap.
    for side in (-1, 1):
        parts.append(profile_yz("strut", fillet([(-46, 2), (176, 2), (180, 6), (176, 10), (-46, 10)], 1, 1),
                                side * 16, side * 19, dark))
        parts.append(profile_yz("strut_catch", fillet([(160, 0), (182, 0), (182, 12), (160, 12)], 2, 2),
                                side * 16, side * 20, dark))
    parts.append(profile_yz("yoke", fillet([(-46, -6), (-38, -6), (-38, 14), (-46, 14)], 2, 2), -20, 20, dark))
    pad = profile_yz("butt_pad", fillet(BUTT_PAD, [2, 2, 3, 6, 6, 10, 6], 3), -22, 22, m["rubber"])
    bevel(pad, 2.0, 2, 30)
    parts.append(pad)
    for p in parts:
        finish(p, 35)
    gun = join("mp5", parts)
    # The magazine: curved, its ribs and witness holes, the top rounds in its lips.
    body = profile_yz("magazine", fillet(MAG_REAR + MAG_FRONT, [0, 0, 20, 20, 20, 3, 3, 20, 20, 20, 0, 0], 3), -11.5, 11.5,
                      m["dark"])
    for k in range(5):
        cut(body, cylinder_x("witness", 222 + k * 2, -60 - k * 18, 1.6, -15, 15, 8))
    bevel(body, 0.8)
    mag = [body]
    for x0, x1 in ((-12.3, -11), (11, 12.3)):
        mag.append(profile_yz("mag_rib", fillet([(230, -70), (238, -70), (264, -170), (256, -172)], 2, 2), x0, x1, m["dark"]))
    mag.append(cylinder_y("round", 0, MAG_TOP + 4.5, 4.5, 214, 236, 12, material("brass_photo", (0.75, 0.55, 0.25),
                                                                                   metallic=1.0, roughness=0.3)))
    mag.append(cylinder_y("bullet", 0, MAG_TOP + 4.5, 3.0, 236, 246, 10, m["dark"]))
    for p in mag:
        finish(p, 35)
    # The cocking handle, on its own for its own photo.
    handle = profile_xz("cocking_handle", fillet(COCKING, [1, 1, 3, 3], 2), 404, 414, metal)
    knob = cylinder_x("knob", 409, 28.5, 5.5, -36, -28, 12, metal)
    finish(handle, 35)
    finish(knob, 35)
    return gun, join("magazine", mag), join("cocking", [handle, knob])


def _apply_transform(ob):
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    ob.select_set(False)


def low():
    """The MP5 at Vice City's detail (about 440 triangles), in its moving parts: {part: [objects]}."""
    tg = profile_yz("trigger_guard", GUARD_OUT, -6, 6)
    cut(tg, profile_yz("tg_opening", GUARD_IN, -10, 10), solver="EXACT")
    hood = cylinder_y("hood", 0, 36, 11, 422, 440, 6)
    cut(hood, cylinder_y("hood_hole", 0, 36, 8.0, 410, 450, 6), solver="EXACT")
    body = [
        profile_xz("receiver", RECEIVER, 12, 262),
        profile_yz("end_cap", END_CAP, -15, 15),
        profile_yz("magwell", [(204, -22), (254, -22), (254, -62), (204, -62)], -15, 15),
        profile_yz("housing", HOUSING, -15, 15),
        tg,
        profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
        profile_yz("drum_base", [(84, 22), (116, 22), (112, 30), (88, 30)], -10, 10),
        cylinder_x("drum", 100, 36, 12, -11.5, 11.5, 6),
        cylinder_y("cocking_tube", 0, TUBE_Z, TUBE_R, 262, 432, 8),
        profile_xz("handguard", HANDGUARD, GUARD_Y[0], GUARD_Y[1]),
        profile_yz("sight_base", SIGHT_BASE, -11, 11),
        hood,
        profile_yz("post", [(429, 24), (433, 24), (432.5, 38), (429.5, 38)], -1.1, 1.1),
        cylinder_y("barrel", 0, 0, 9.5, 400, MUZZLE_Y, 8),
        box("strut_l", -19, -16, -46, 180, 2, 10),
        box("strut_r", 16, 19, -46, 180, 2, 10),
        profile_yz("butt_pad", BUTT_PAD, -22, 22),
    ]
    bolt = [profile_xz("cocking_handle", COCKING, 404, 414)]
    trigger = [profile_yz("trigger", [TRIGGER[i] for i in (0, 1, 2, 4, 5, 7)], -3.5, 3.5)]
    magazine = [profile_yz("mag_body", [MAG_REAR[i] for i in (0, 1, 3, 5)] + [MAG_FRONT[i] for i in (0, 2, 4, 5)],
                           -11.5, 11.5)]
    return {"body": body, "bolt": bolt, "trigger": trigger, "magazine": magazine}


def chooser(bvh):
    """Which photo a face of the gun wears: (normal, centre, its corners (mm), its part) -> region. The top,
    back, front or underside where it faces them and the photo shows it (not hidden behind something nearer),
    the near side's photo round the grip, the whole side's otherwise (the right mirrors the left)."""
    def visible(centre, normal, look):
        start = (centre + normal * 0.6) * MM
        hit, _, _, dist = bvh.ray_cast(start, -Vector(look).normalized(), 2.0)
        return hit is None or dist < 3.0 * MM

    def choose(normal, centre, corners, part):
        if part == "magazine":
            return "magazine"
        if part == "bolt":
            return "cocking"
        ax = max(range(3), key=lambda k: abs(normal[k]))
        if ax == 2 and normal.z > 0 and visible(centre, normal, (0, 0, -1)):
            return "top"
        if ax == 2 and normal.z < 0 and GUARD_Y[0] <= centre.y <= GUARD_Y[1] and visible(centre, normal, (0, 0, 1)):
            return "under"
        if ax == 1 and normal.y < 0 and visible(centre, normal, (0, 1, 0)):
            return "back"
        if ax == 1 and normal.y > 0 and visible(centre, normal, (0, -1, 0)):
            return "front"
        ys, zs = [c.y for c in corners], [c.z for c in corners]
        if NEAR_Y[0] <= min(ys) and max(ys) <= NEAR_Y[1] and NEAR_Z[0] <= min(zs) and max(zs) <= NEAR_Z[1]:
            return "near"
        return "left"
    return choose


def project(ob, choose, part):
    """UVs for a low-poly object: each face from the region `choose` names (kit.project, told the face's
    corners and part too)."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        corners = [me.vertices[i].co / MM for i in poly.vertices]
        centre = sum(corners, Vector()) / len(corners)
        region = REGIONS[choose(poly.normal, centre, corners, part)]
        for li in poly.loop_indices:
            uv.data[li].uv = kit.region_uv(region, me.vertices[me.loops[li].vertex_index].co / MM)


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "mp5lng", LENGTH)
    hold = kit.hold_for(placed(WEB), scale)
    hold_left = kit.hold_left_for(placed(PALM), scale, ROLL)
    scene = kit.studio()
    gun, mag, cocking = detailed(materials())
    rgba = kit.atlas(scene, REGIONS, {"left": [gun, cocking], "top": [gun, cocking], "near": [gun, cocking],
                                      "magazine": [mag], "back": [gun], "front": [gun, cocking], "cocking": [cocking],
                                      "under": [gun]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "mp5lng-atlas.png")
    kit.save_png(atlas_png, flat)
    bvh = BVHTree.FromObject(gun, bpy.context.evaluated_depsgraph_get())
    choose = chooser(bvh)
    for o in list(scene.objects):
        if o.type == "MESH":
            o.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            project(ob, choose, part)
            finish(ob, 35)
            ob.location = SHIFT * MM  # into the asset's frame (the photos' UVs are already set)
            objects.append((ob, names.index(part)))
    bpy.context.view_layer.update()
    parts = [(n, placed(pivot), axis) for n, pivot, axis in PARTS]
    anchors = [(n, placed(p), None if part is None else names.index(part), bend, slack)
               for n, p, part, bend, slack in ANCHORS]
    eye, yaw, pitch, back, above = FRAMING
    framing = (tuple(c + d * MM for c, d in zip(eye, SHIFT)), yaw, pitch, back, above)
    count = kit.write_vmg(out, scale, hold, placed(MUZZLE), placed(SIGHT_REAR), placed(SIGHT_FRONT), parts, anchors,
                          objects, palette, pixels, framing, two_handed=True, hold_left=hold_left,
                          left_forearm=kit.LEFT_FOREARM)
    print(f"mp5lng: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, "
          f"hold origin {[round(c, 4) for c in hold[3]]}")
    at = lambda x, y, z: tuple(Vector((x, y, z)) + SHIFT * MM)
    kit.preview(os.path.join(work, "mp5lng-preview.png"), [o for o, _ in objects], atlas_png,
                [(at(-0.55, 0.21, -0.02), at(0, 0.21, -0.05), 50), (at(0.42, 0.45, 0.18), at(0, 0.2, -0.04), 50),
                 (at(-0.12, -0.15, 0.10), at(0.0, 0.3, -0.02), 35)])


main()
