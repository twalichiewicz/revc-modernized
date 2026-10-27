"""Vice City's "Ruger" (a Ruger AC-556, the Mini-14 action in a wooden stock stained the game's grey-green, with
a pistol grip, its side-folding steel stock opened and a 30-round magazine) for the first-person viewmodel.
Two-handed: his left hand under the wooden forend.

blender -b -P tools/guns/ruger.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right, drawn from the receiver's heel (y = 0) and the
bore (z = 0), then moved into the Colt's frame as the asset is written (SHIFT). A real AC-556: 984 mm with its
stock open, an 18.5 inch (470 mm) barrel and a flash suppressor, the operating rod's handle on the right of the
receiver (it rides back with the bolt each shot and holds open on an empty magazine). As colt45.py and m4.py.
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

LENGTH = 984.0  # mm, stock open
BUTT, MUZZLE_Y = -280.0, 704.0
RAKE = math.radians(20)  # the pistol grip's rake back from vertical
GRIP_UP = (math.sin(RAKE), math.cos(RAKE))  # (y, z): up the grip
GRIP_BACK_TOP, GRIP_FRONT_TOP, GRIP_TOP_Z = 18.0, 58.0, -36.0
GRIP_X = 14.0
MAG_TOP = -12.0
FOREND_Y = (236.0, 470.0)


def grip_back(t):
    """The pistol grip's back strap, t mm down it from the stock: (y, z)."""
    return (GRIP_BACK_TOP - GRIP_UP[0] * t, GRIP_TOP_Z - GRIP_UP[1] * t)


def grip_front(t):
    return (GRIP_FRONT_TOP - GRIP_UP[0] * t, GRIP_TOP_Z - GRIP_UP[1] * t)


def grip_outline():
    """The pistol grip's side: square to the stock's belly on top, raked back, rounded at its foot."""
    return [grip_back(0), (GRIP_FRONT_TOP, GRIP_TOP_Z), grip_front(30), grip_front(94), grip_back(100)]


MAG_REAR = [(174, MAG_TOP), (174, -70), (178, -120), (186, -160), (192, -178)]
MAG_FRONT = [(254, -170), (246, -150), (240, -112), (236, -66), (236, MAG_TOP)]
MAG_AXIS = (0.0, 0.0, -1.0)
RECEIVER = [(0, -14), (204, -14), (204, 12), (196, 16), (150, 16), (140, 12), (36, 12), (30, 18), (0, 18)]
REAR_SIGHT = [(4, 16), (30, 16), (28, 30), (24, 37), (10, 37), (6, 30)]
# The wooden stock: its belly under the receiver from the folder's hinge to the magazine well, the forend
# round the barrel's lower half ahead of it.
STOCK = [(-44, -10), (-44, -40), (GRIP_BACK_TOP - 4, -40), (GRIP_FRONT_TOP + 2, -38), (172, -38), (172, -12),
         (0, -12), (0, -10)]
FOREND = [(-20, 6), (-21, -6), (-19, -22), (-12, -32), (12, -32), (19, -22), (21, -6), (20, 6)]  # its section (x, z)
HANDGUARD = [(-16, 6), (-16, 14), (-10, 21), (10, 21), (16, 14), (16, 6)]  # its section (x, z), over the barrel
GUARD_OUT = [(58, -38), (170, -38), (170, -48), (162, -64), (66, -64), (58, -54)]
GUARD_IN = [(66, -28), (154, -28), (154, -50), (148, -58), (72, -58), (66, -50)]
TRIGGER = [(84, -34), (90, -34), (93, -44), (92, -52), (88, -59), (86, -57), (88, -50), (87, -42)]
BUTT_PLATE = [(-262, -2), (-262, -128), (BUTT, -124), (BUTT, -6)]
FRONT_SIGHT = [(632, 6), (660, 6), (660, 14), (654, 30), (638, 30), (632, 14)]
OPROD = [(150, 2), (168, 2), (168, 9), (150, 9)]  # the op rod's handle, out to the right (y, z)

SIDE = 998 / 256
REGIONS = {
    "left": ((0, 0, 256, 64), (-400, (BUTT + MUZZLE_Y) / 2, -46), (1, 0, 0), (0, 0, 1), SIDE),
    "top": ((0, 64, 256, 24), (0, (BUTT + MUZZLE_Y) / 2, 400), (0, 0, -1), (-1, 0, 0), SIDE),
    "near": ((0, 88, 128, 96), (-400, 101, -45), (1, 0, 0), (0, 0, 1), 2.2),
    "magazine": ((128, 88, 128, 64), (-400, 214, -96), (1, 0, 0), (0, 1, 0), 1.5),
    "oprod": ((128, 152, 64, 32), (400, 161, 6), (-1, 0, 0), (0, 0, 1), 1.6),
    "back": ((192, 152, 64, 64), (0, -600, -37), (0, 1, 0), (0, 0, 1), 3.0),
    "front": ((0, 184, 64, 64), (0, 1100, -40), (0, -1, 0), (0, 0, 1), 2.6),
    "under": ((64, 184, 128, 48), (0, 318, -400), (0, 0, 1), (1, 0, 0), 2.5),
}
NEAR_Y, NEAR_Z = (101 - 140, 101 + 140), (-45 - 105, -45 + 105)

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface, as the Colt's).
FINGER_R = 8.0


def on_grip(t, back):
    """A fingertip on the grip's left side, t mm down it, `back` mm behind its front strap."""
    y, z = grip_front(t)
    return (-GRIP_X - FINGER_R, y - back * GRIP_UP[1], z + back * GRIP_UP[0])


def on_forend(y, z, side=1):
    """A fingertip on the forend's side at y, z (right side, or left with side = -1)."""
    return (side * (21.0 + FINGER_R), y, z)


# His left palm under the forend a little behind its middle (further forward, his upper arm comes into view;
# further back, his forearm meets the magazine), the hand rolled 10 degrees; the fingers up its far side, the
# thumb along the near.
PALM, ROLL = (0.0, 305.0, -35.0), 10.0
FORWARD = (0.0, 18.0, 0.0)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 90.0 + FINGER_R, -48.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(38, 20), None, None, (0.0, 8.0, -17.0)),
    ("r_ring", on_grip(58, 19), None, None, FORWARD),
    ("r_pinky", on_grip(78, 17), None, None, FORWARD),
    ("r_thumb", (-18.0 - FINGER_R, 62.0, -26.0), None, None, (0.0, -12.0, 0.0)),
    ("l_index", on_forend(335, -2), None, None, None),
    ("l_middle", on_forend(317, 0), None, None, None),
    ("l_ring", on_forend(299, -2), None, None, None),
    ("l_pinky", on_forend(282, -8), None, None, None),
    ("l_thumb", on_forend(343, 0, -1), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("bolt", (0, 0, 0), (0, -1, 0)),  # the operating rod and its handle, on the right
    ("trigger", (0, 0, 0), (0, -1, 0)),
    ("magazine", (0, 0, 0), MAG_AXIS),
]
# Where the web of his right hand rests: on the grip's back strap 25 mm down it, pressed 3 mm into it (higher,
# his thumb's root meets the side of the stock's belly, which is wider than the grip).
WEB = (0.0, grip_back(25)[0] + 3.0, grip_back(25)[1])
# The asset's gun frame is the Colt's: its origin moved so his web sits where the Colt's does (y 18.6, z 85).
SHIFT = Vector((0.0, 18.6 - WEB[1], 85.0 - WEB[2]))
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, 0), (0, 17, 33), (0, 646, 33)
# Seen from (the model's frame, real metres): left of and over the receiver, behind it by most of the stock,
# looking 5 degrees right of the barrel and 1 down, so the gun runs in from the lower right towards the crosshair
# with both hands in view; aiming, 0.08 m behind the rear sight and just over it.
FRAMING = ((-0.190, -0.270, 0.110), 5.0, -1.0, 0.08, 0.006)


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


def wood(name, colour, roughness=0.45):
    """Oiled wood: grain running along the stock (y), in bands of `colour` darker and lighter."""
    m = material(name, colour, 0.0, roughness, 0.45)
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1.0, 0.12, 1.0)  # long grain along y
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.wave_type = "BANDS"
    wave.bands_direction = "Z"
    wave.inputs["Scale"].default_value = 45.0
    wave.inputs["Distortion"].default_value = 10.0
    wave.inputs["Detail"].default_value = 3.0
    nt.links.new(mp.outputs["Vector"], wave.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    lo, hi = ramp.color_ramp.elements
    lo.position, lo.color = 0.15, (*(c * 0.55 for c in colour), 1)
    hi.position, hi.color = 0.85, (*(c * 1.35 for c in colour), 1)
    nt.links.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    edge = nt.nodes.new("ShaderNodeBevel")
    edge.inputs["Radius"].default_value = 2.0 * MM
    nt.links.new(edge.outputs["Normal"], b.inputs["Normal"])
    return m


def materials():
    m = kit.photo_materials()
    # Vice City's Ruger: mid-grey blued steel, the stock a grey-green, the grip and magazine near black.
    m["blued"] = worn("blued_photo", (0.11, 0.115, 0.12), metallic=0.7, roughness=0.36)
    m["dark"] = worn("dark_photo", (0.035, 0.037, 0.04), metallic=0.7, roughness=0.42)
    m["wood"] = wood("wood_photo", (0.09, 0.115, 0.095))
    m["poly"] = worn("polymer_photo", (0.03, 0.032, 0.034), roughness=0.55, spec=0.4, grain=0.35)
    m["shadow"] = material("shadow_photo", (0.01, 0.01, 0.01), roughness=0.9, spec=0.1)
    return m


def detailed(m):
    """The photo shoot's Ruger: every part the texture shows. Returns (gun, magazine, operating rod)."""
    blued, dark, wd, poly, steel = m["blued"], m["dark"], m["wood"], m["poly"], m["steel"]
    parts = []
    # The receiver: its heel and aperture sight, the bolt in the open top, the ejection port, the gas piston's
    # guide forward of it.
    rec = profile_yz("receiver", fillet(RECEIVER, [0, 0, 2, 3, 3, 3, 2, 2, 0], 2), -13, 13, blued)
    cut(rec, box("port", -6, 20, 66, 140, 4, 22))
    bevel(rec, 0.6)
    parts.append(rec)
    parts.append(profile_yz("bolt", fillet([(66, 2), (140, 2), (140, 11), (66, 11)], 2, 2), -6, 6, blued))
    parts.append(cylinder_y("bolt_lug", 0, 7, 4.5, 140, 148, 10, blued))
    sight = profile_yz("rear_sight", fillet(REAR_SIGHT, 2, 2), -7, 7, blued)
    cut(sight, cylinder_y("aperture", 0, 33, 1.8, 0, 40, 10))
    for x0, x1 in ((-9, -6), (6, 9)):
        parts.append(profile_yz("sight_ear", fillet([(6, 16), (28, 16), (24, 40), (12, 40)], 2, 2), x0, x1, blued))
    parts.append(sight)
    parts.append(cylinder_x("windage", 17, 22, 3.5, 9, 12, 10, steel))
    parts.append(cylinder_x("bolt_stop", 46, 4, 3.5, -15, -13, 10, steel))
    # The wooden stock: its belly and hinge, the magazine well, the forend with its ventilated handguard over
    # the barrel; the steel gas block, the barrel, the front sight between its wings, the flash suppressor.
    st = profile_yz("stock", fillet(STOCK, [3, 4, 6, 6, 0, 0, 0, 0], 3), -18, 18, wd)
    cut(st, profile_yz("wood_panel", fillet([(-30, -18), (-10, -18), (-10, -32), (-30, -32)], 3, 2), -30, -16.8))
    bevel(st, 1.5, 1, 30)
    parts.append(st)
    fe = profile_xz("forend", fillet(FOREND, [0, 3, 6, 6, 6, 6, 3, 0], 3), FOREND_Y[0] - 70, FOREND_Y[1], wd)
    bevel(fe, 1.5, 1, 30)
    parts.append(fe)
    parts.append(profile_yz("forend_tip", fillet([(FOREND_Y[1] - 4, 6), (FOREND_Y[1] + 2, 6), (FOREND_Y[1] + 2, -26),
                                                 (FOREND_Y[1] - 4, -30)], 2, 2), -19, 19, wd))
    hg = profile_xz("handguard", fillet(HANDGUARD, [0, 3, 5, 5, 3, 0], 2), 208, 452, poly)
    vents = []
    for k in range(8):
        y = 226 + k * 27
        vents.append(cylinder_x("vent", y + 6, 15, 3.6, -30, 30, 12))
        vents.append(cylinder_x("vent", y + 12, 15, 3.6, -30, 30, 12))
    cut(hg, join("vents", vents))
    parts.append(hg)
    parts.append(cylinder_y("under_vents", 0, 6, 12, 206, 452, 16, m["shadow"]))
    parts.append(profile_yz("hinge", fillet([(-60, -12), (-44, -12), (-44, -42), (-60, -42)], 3, 2), -10, 10, blued))
    parts.append(cylinder_x("hinge_pin", -52, -27, 4, -12, 12, 12, steel))
    parts.append(profile_yz("swivel", fillet([(410, -32), (424, -32), (424, -38), (410, -38)], 2, 2), -4, 4, steel))
    gb = profile_yz("gas_block", fillet([(452, 22), (476, 22), (476, -14), (452, -14)], 2, 2), -12, 12, blued)
    bevel(gb, 0.6)
    parts.append(gb)
    parts.append(profile_yz("bayonet_lug", fillet([(456, -14), (476, -14), (476, -26), (462, -26)], 1.5, 2), -4, 4, blued))
    parts.append(cylinder_y("gas_pipe", 0, 14, 4.5, 440, 456, 10, steel))
    parts.append(cylinder_y("barrel", 0, 0, 9.0, 200, 476, 16, dark))
    parts.append(cylinder_y("barrel_front", 0, 0, 8.0, 476, 662, 16, dark))
    fs = profile_yz("front_sight", fillet(FRONT_SIGHT, [0, 0, 2, 2, 2, 2], 2), -9, 9, blued)
    cut(fs, box("sight_gap", -6, 6, 630, 662, 18, 40))
    parts.append(fs)
    parts.append(profile_yz("blade", [(642, 18), (650, 18), (649, 33), (643, 33)], -1.2, 1.2, dark))
    hider = cylinder_y("flash_hider", 0, 0, 11, 662, MUZZLE_Y, 20, dark)
    for k in range(4):
        cut(hider, _rotated_slot(math.radians(90 * k + 45)))
    cut(hider, cylinder_y("bore", 0, 0, 5.0, 650, 720, 12))
    parts.append(hider)
    # The trigger guard (it locks the action into the stock) with its safety, the trigger, the pistol grip.
    tg = profile_yz("trigger_guard", fillet(GUARD_OUT, [0, 0, 4, 6, 6, 3], 2), -6, 6, blued)
    cut(tg, profile_yz("tg_opening", fillet(GUARD_IN, [0, 0, 3, 4, 4, 0], 2), -10, 10))
    bevel(tg, 0.6)
    parts.append(tg)
    parts.append(profile_yz("safety", fillet([(146, -36), (156, -36), (156, -52), (148, -50)], 1, 2), -3, 3, steel))
    parts.append(profile_yz("trigger", fillet(TRIGGER, [1, 1, 3, 3, 1, 1, 3, 3], 2), -3.5, 3.5, steel))
    parts.append(profile_yz("mag_latch", fillet([(164, -36), (172, -36), (172, -50), (166, -50)], 1, 1), -5, 5, steel))
    g = profile_yz("grip", fillet(grip_outline(), [0, 2, 6, 8, 8], 3), -GRIP_X, GRIP_X, m["grip"])
    bevel(g, 3.0, 2, 30)
    parts.append(g)
    # The folding stock, open: its strut back from the hinge and the butt plate.
    parts.append(cylinder_y("strut", 0, -30, 7, -262, -58, 16, blued))
    parts.append(profile_yz("brace", fillet([(-262, -112), (-250, -112), (-120, -48), (-60, -36), (-60, -30),
                                             (-124, -40), (-262, -102)], 2, 2), -4, 4, blued))
    plate = profile_yz("butt_plate", fillet(BUTT_PLATE, 4, 2), -20, 20, blued)
    bevel(plate, 1.0)
    parts.append(plate)
    parts.append(profile_yz("butt_pad", fillet([(BUTT - 4, -6), (BUTT, -6), (BUTT, -124), (BUTT - 4, -124)], 1.5, 2),
                            -19, 19, m["grip"]))
    for p in parts:
        finish(p, 35)
    gun = join("ruger", parts)
    # The magazine: blued, curved a little, ribbed, the top round in its lips.
    body = profile_yz("magazine", fillet(MAG_REAR + MAG_FRONT, [0, 0, 20, 20, 3, 3, 20, 20, 0, 0], 3), -11, 11, dark)
    bevel(body, 0.8)
    mag = [body]
    for x0, x1 in ((-11.8, -10.5), (10.5, 11.8)):
        mag.append(profile_yz("mag_rib", fillet([(186, -40), (226, -40), (232, -150), (196, -156)], 3, 2), x0, x1, dark))
    mag.append(profile_yz("baseplate", fillet([(190, -174), (256, -166), (257, -174), (191, -183)], 2, 2), -12.5, 12.5, dark))
    mag.append(cylinder_y("round", 0, MAG_TOP + 4.5, 4.6, 180, 222, 12,
                          material("brass_photo", (0.75, 0.55, 0.25), metallic=1.0, roughness=0.3)))
    mag.append(cylinder_y("bullet", 0, MAG_TOP + 4.5, 2.9, 222, 234, 10,
                          material("copper_photo", (0.7, 0.35, 0.2), metallic=1.0, roughness=0.3)))
    for p in mag:
        finish(p, 35)
    # The operating rod: its handle out to the right and the rod forward along the receiver's side.
    rod = profile_yz("op_rod", fillet([(140, -2), (204, -2), (204, 6), (140, 6)], 1, 2), 13, 16, blued)
    handle = profile_xz("op_handle", fillet([(13, 1), (24, 1), (30, 4), (30, 10), (24, 11), (13, 11)], 1.5, 2),
                        OPROD[0][0], OPROD[1][0], blued)
    finish(rod, 35)
    finish(handle, 35)
    return gun, join("magazine", mag), join("op_rod", [rod, handle])


def _rotated_slot(angle):
    """A cutter for one of the flash suppressor's slots at `angle` round the bore."""
    s = box("slot", -2, 2, 672, 700, 6, 16)
    s.rotation_euler = (0, angle, 0)
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action="DESELECT")
    s.select_set(True)
    bpy.context.view_layer.objects.active = s
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    s.select_set(False)
    return s


def low():
    """The Ruger at Vice City's detail (about 420 triangles), in its moving parts: {part: [objects]}."""
    tg = profile_yz("trigger_guard", GUARD_OUT, -6, 6)
    cut(tg, profile_yz("tg_opening", GUARD_IN, -10, 10), solver="EXACT")
    body = [
        profile_yz("receiver", RECEIVER, -13, 13),
        profile_yz("rear_sight", REAR_SIGHT, -8, 8),
        profile_yz("stock", STOCK, -18, 18),
        profile_xz("forend", FOREND, FOREND_Y[0] - 70, FOREND_Y[1]),
        profile_xz("handguard", HANDGUARD, 208, 452),
        profile_yz("gas_block", [(452, 22), (476, 22), (476, -26), (452, -14)], -12, 12),
        cylinder_y("barrel", 0, 0, 8.5, 476, 662, 8),
        profile_yz("front_sight", FRONT_SIGHT, -9, 9),
        cylinder_y("flash_hider", 0, 0, 11, 662, MUZZLE_Y, 8),
        tg,
        profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
        profile_yz("hinge", [(-60, -12), (-44, -12), (-44, -42), (-60, -42)], -10, 10),
        cylinder_y("strut", 0, -30, 7, -262, -58, 6),
        profile_yz("brace", [(-262, -112), (-250, -112), (-60, -36), (-60, -30), (-262, -102)], -4, 4),
        profile_yz("butt_plate", BUTT_PLATE, -20, 20),
    ]
    bolt = [box("op_rod", 13, 16, 140, 204, -2, 6), profile_xz("op_handle", [(13, 1), (30, 4), (30, 10), (13, 11)],
                                                                 OPROD[0][0], OPROD[1][0])]
    trigger = [profile_yz("trigger", [TRIGGER[i] for i in (0, 1, 2, 4, 5, 7)], -3.5, 3.5)]
    magazine = [profile_yz("mag_body", MAG_REAR + MAG_FRONT, -11, 11)]
    return {"body": body, "bolt": bolt, "trigger": trigger, "magazine": magazine}


def chooser(bvh):
    """Which photo a face of the gun wears: (normal, centre, its corners (mm), its part) -> region. The top,
    back, front or underside where it faces them and the photo shows it (not hidden behind something nearer),
    the near side's photo round the receiver and grip, the whole side's otherwise (the right mirrors the left)."""
    def visible(centre, normal, look):
        start = (centre + normal * 0.6) * MM
        hit, _, _, dist = bvh.ray_cast(start, -Vector(look).normalized(), 2.0)
        return hit is None or dist < 3.0 * MM

    def choose(normal, centre, corners, part):
        if part == "magazine":
            return "magazine"
        if part == "bolt":
            return "oprod"
        ax = max(range(3), key=lambda k: abs(normal[k]))
        if ax == 2 and normal.z > 0 and visible(centre, normal, (0, 0, -1)):
            return "top"
        if ax == 2 and normal.z < 0 and FOREND_Y[0] - 70 <= centre.y <= FOREND_Y[1] and visible(centre, normal, (0, 0, 1)):
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
    scale = kit.scale_for(game_data, "ruger", LENGTH)
    hold = kit.hold_for(placed(WEB), scale)
    hold_left = kit.hold_left_for(placed(PALM), scale, ROLL)
    scene = kit.studio()
    gun, mag, rod = detailed(materials())
    rgba = kit.atlas(scene, REGIONS, {"left": [gun], "top": [gun, rod], "near": [gun], "magazine": [mag],
                                      "oprod": [gun, rod], "back": [gun, rod], "front": [gun, rod], "under": [gun]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "ruger-atlas.png")
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
    print(f"ruger: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, "
          f"hold origin {[round(c, 4) for c in hold[3]]}")
    at = lambda x, y, z: tuple(Vector((x, y, z)) + SHIFT * MM)
    kit.preview(os.path.join(work, "ruger-preview.png"), [o for o, _ in objects], atlas_png,
                [(at(-0.85, 0.21, -0.03), at(0, 0.21, -0.05), 50), (at(0.6, 0.6, 0.25), at(0, 0.2, -0.04), 50),
                 (at(-0.15, -0.22, 0.09), at(0.0, 0.4, -0.02), 35)])


main()
