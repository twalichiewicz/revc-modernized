"""Vice City's Stubby Shotgun ("buddyshot"): a sawn-off over-and-under 12-bore, its barrels cut to 13 inches and
its stock to a raked pistol grip, for the first-person viewmodel.

blender -b -P tools/guns/buddyshot.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrels, z up, x to the right (as colt45.py): the action's rear at y = 0, the top
bore 100 mm up. Real dimensions: 560 mm overall, the barrels 330 mm, the action 112 mm. As the game's model (its
texture is gun_tubbs_shotgun_b): one barrel over the other with a rib on top, a box action, a forend under the
barrels and a grip raked back 40 degrees, all in a dark, mottled gunmetal grey.

Held in both hands: his right on the grip, his left under the forend (no moving parts but the trigger). The grip
is raked, so the right hand's hold is the Colt's turned down about its web point (`hold_pitched`, as the
chromegun's; the game holds its own pitched in his hand too).
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

REAL_LENGTH = 560.0
BORE, BORE_LOW = 100.0, 78.5  # the top and bottom barrels' axes
BARREL_R, MUZZLE_Y = 10.8, 440.0
ACTION = [(0, 56), (100, 56), (112, 64), (112, 117), (14, 118), (0, 114)]
ACTION_HALF = 19.0
FORE_Y0, FORE_Y1 = 114.0, 290.0
# The grip: the wrist's top from the action down into the grip's back (the web's curve), its back raked 40 degrees,
# its foot, its front up to the trigger guard.
GRIP = [(0.5, 114), (-22, 108), (-38, 97), (-75.6, 56.5), (-107, 11), (-65, -24), (-30, 16), (-1, 53), (0.5, 56)]
GRIP_HALF = 16.0
GRIP_DOWN = (Vector((-65.0, -24.0)) - Vector((-1.0, 53.0))).normalized()  # down its front edge
GRIP_IN = Vector((-GRIP_DOWN.y, GRIP_DOWN.x))  # from its front edge back into the grip
if GRIP_IN.x > 0:
    GRIP_IN = -GRIP_IN
GUARD = [(2, 56), (82, 56), (80, 49), (68, 32), (40, 27), (14, 29), (4, 38), (0, 48)]
GUARD_OPENING = [(10, 58), (74, 58), (72, 50), (62, 37), (40, 33), (18, 35), (11, 42), (10, 50)]
TRIGGER = [(34, 56), (41, 56), (41.5, 48), (44, 40), (48, 35), (44, 34), (38, 40), (35, 48)]


def fore_section(inset=0.0):
    """The forend's cross-section (x, z) round the bottom barrel, `inset` mm in."""
    pts = [(-16, 58), (16, 58), (21, 66), (21.5, 86), (17, 92), (-17, 92), (-21.5, 86), (-21, 66)]
    out = []
    for x, z in pts:
        d = Vector((x, z - 76.0))
        k = max(0.0, 1.0 - inset / d.length)
        out.append((d.x * k, 76.0 + d.y * k))
    return out


# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# Side and top each in two: the grip, action and forend; the barrels' front half.
SPLIT = 270.0  # where the low-poly barrels and ribs are cut between the two
REGIONS = {
    "side_rear": ((0, 0, 256, 100), (-400, 89, 48), (1, 0, 0), (0, 0, 1), 1.6),
    "side_front": ((0, 100, 128, 48), (-400, 347.5, 86.5), (1, 0, 0), (0, 0, 1), 1.6),
    "front": ((128, 100, 48, 48), (0, 900, 86.5), (0, -1, 0), (0, 0, 1), 1.6),
    "top_rear": ((0, 148, 256, 28), (0, 89, 400), (0, 0, -1), (-1, 0, 0), 1.6),
    "top_front": ((0, 176, 128, 28), (0, 347.5, 400), (0, 0, -1), (-1, 0, 0), 1.6),
}

FINGER_R = 8.0
# The right hand: its web in the curve of the grip's back under the tang, the hand pitched down 25 degrees along it.
WEB = (0.0, -43.0, 90.5)
PITCH = 25.0


def on_grip(t, back=30.0):
    """A fingertip on the grip's left side: `t` mm down its front edge, `back` mm in from it."""
    p = Vector((-1.0, 53.0)) + GRIP_DOWN * t + GRIP_IN * back
    return (-GRIP_HALF - FINGER_R, p.x, p.y)


ALONG_GRIP = (0.0, -GRIP_IN.x * 24.0, -GRIP_IN.y * 24.0)  # slack: towards the grip's front edge (longer fingers)
PALM_Y = 200.0
PALM = (0.0, PALM_Y, 56.0)
LEFT_ROLL = 15.0
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 44.5, 49.5), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(14.0, 22.0), None, None, ALONG_GRIP),
    ("r_ring", on_grip(27.0), None, None, ALONG_GRIP),
    ("r_pinky", on_grip(45.0, 32.0), None, None, ALONG_GRIP),
    ("r_thumb", (-ACTION_HALF - FINGER_R, 20.0, 92.0), None, None, (0.0, -14.0, 0.0)),
    # (the forend's narrow for his hand: his fingers wrap up its right side onto the barrels')
    ("l_index", (24.0, PALM_Y + 24, 94.0), None, None, None),
    ("l_middle", (24.0, PALM_Y + 6, 96.0), None, None, None),
    ("l_ring", (24.0, PALM_Y - 12, 94.0), None, None, None),
    ("l_pinky", (24.0, PALM_Y - 28, 90.0), None, None, None),
    ("l_thumb", (-21.5 - FINGER_R, PALM_Y + 40, 80.0), None, None, None),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
# The top barrel's muzzle; sighted along the rib to the bead.
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, BORE), (0, 20, 118), (0, MUZZLE_Y - 8, 116.5)
FRAMING = ((-0.181, -0.216, 0.180), 3.0, 0.0, 0.22, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
LEFT_FOREARM = (-0.478, -0.758, -0.444)  # kit's, turned with the levelled framing: runs where it did in the view


def hold_pitched(web_mm, scale, pitch_deg):
    """kit.hold_for with his hand turned `pitch_deg` down about the web point (the barrels up in his hand): a raked
    grip is held along its rake, not upright as a pistol's."""
    t = math.radians(pitch_deg)
    axes = [Vector((0, -1, 0)), Vector((math.cos(t), 0, math.sin(t))), Vector((-math.sin(t), 0, math.cos(t)))]
    w = Vector(web_mm) * MM * scale
    origin = Vector(kit.WEB_IN_HAND) - (axes[0] * w.x + axes[1] * w.y + axes[2] * w.z)
    return [tuple(a) for a in axes] + [tuple(origin)]


def speckle(m, scale, strength, dark, light):
    """Breaks a material's colour and surface up with fine noise: the mottled look of Vice City's photo textures
    (worn metal; grain on wood when `scale` is stretched along y)."""
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = scale
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Detail"].default_value = 6.0
    noise.inputs["Roughness"].default_value = 0.6
    nt.links.new(mp.outputs["Vector"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.35, (*dark, 1)
    els[1].position, els[1].color = 0.65, (*light, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    if strength:
        bump = nt.nodes.new("ShaderNodeBump")
        bump.inputs["Strength"].default_value = strength
        nt.links.new(noise.outputs["Fac"], bump.inputs["Height"])
        nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    return m


def materials():
    """The game's stubby is a dark, mottled gunmetal all over (its texture averages (63, 64, 63)); the grip and
    forend a near-black walnut."""
    mats = kit.photo_materials()
    mats["gunmetal"] = speckle(material("gunmetal", (0.12, 0.12, 0.12), metallic=0.7, roughness=0.4),
                               (200.0, 200.0, 200.0), 0.2, (0.07, 0.07, 0.07), (0.20, 0.20, 0.19))
    mats["wood"] = speckle(material("dark_walnut", (0.04, 0.035, 0.03), roughness=0.55, spec=0.35),
                           (90.0, 6.0, 90.0), 0.0, (0.022, 0.019, 0.016), (0.065, 0.055, 0.046))
    return mats


def detailed(mats):
    """The photo shoot's sawn-off: every part the texture shows. Returns the gun."""
    metal, wood, dark = mats["gunmetal"], mats["wood"], mats["dark"]
    parts = []
    action = profile_yz("action", fillet(ACTION, [1, 3, 3, 2, 6, 2], 3), -ACTION_HALF, ACTION_HALF, metal)
    bevel(action, 1.5)
    parts.append(action)
    # The action's side: the hinge pin, a scroll panel's edge; the top tang and its opening lever.
    for s in (-1, 1):
        parts.append(cylinder_x("hinge_pin", 102, 64, 6.0, s * ACTION_HALF, s * (ACTION_HALF + 0.8), 16, dark))
        parts.append(profile_yz("panel", fillet([(18, 66), (88, 66), (96, 74), (96, 108), (18, 108)], 3, 2),
                                s * (ACTION_HALF - 0.2), s * (ACTION_HALF + 0.4),
                                speckle(material("scroll", (0.16, 0.16, 0.16), metallic=0.7, roughness=0.35),
                                        (900.0, 900.0, 900.0), 0.5, (0.05, 0.05, 0.05), (0.26, 0.26, 0.25))))
    parts.append(profile_yz("tang", fillet([(-34, 104), (4, 112), (4, 118), (-30, 108)], [3, 0, 0, 3], 2), -8, 8, metal))
    lever = profile_xz("lever", fillet([(-6, 118), (6, 118), (6, 123), (-6, 123)], 1.5, 2), -14, 14, metal)
    parts.append(lever)
    parts.append(profile_xz("lever_thumb", fillet([(4, 118), (16, 118), (16, 124), (4, 124)], 2, 2), -16, -6, metal))
    parts.append(cylinder_x("safety", -6, 118, 3.0, -4, 4, 12, dark))
    # The barrels, the ribs between and on top, the bead.
    for z in (BORE, BORE_LOW):
        b = cylinder_y("barrel", 0, z, BARREL_R, 110, MUZZLE_Y, 24, metal)
        cut(b, cylinder_y("bore", 0, z, 9.3, MUZZLE_Y - 40, MUZZLE_Y + 5, 20))
        parts.append(b)
    parts.append(box("side_rib", -4.5, 4.5, 112, MUZZLE_Y - 1, BORE_LOW, BORE, dark))
    rib = box("top_rib", -4.5, 4.5, 112, MUZZLE_Y - 2, BORE + 9.5, BORE + 13.5, metal)
    cut(rib, join("vents", [box("vent", -6, 6, 130 + 26 * k, 144 + 26 * k, BORE + 9.4, BORE + 12.2) for k in range(11)]))
    parts.append(rib)
    parts.append(cylinder_x("bead", MUZZLE_Y - 8, BORE + 14.6, 1.6, -1.6, 1.6, 12, mats["steel"]))
    # The forend: dark walnut, chequered panels (grooves), its iron at the front.
    fore = profile_xz("forend", fore_section(), FORE_Y0, FORE_Y1 - 6, wood)
    bevel(fore, 1.5)
    parts.append(fore)
    for s in (-1, 1):  # chequered panels on its sides
        parts.append(profile_yz("chequer", fillet([(FORE_Y0 + 22, 68), (FORE_Y1 - 26, 68), (FORE_Y1 - 26, 84),
                                                   (FORE_Y0 + 22, 84)], 4, 2), s * 21.0, s * 21.9, mats["grip"]))
    parts.append(profile_xz("fore_tip", fore_section(1.5), FORE_Y1 - 6, FORE_Y1, metal))
    # The grip (the stock cut down), its cap; the trigger guard and trigger.
    grip = profile_yz("grip", fillet(GRIP, [3, 12, 10, 30, 8, 8, 20, 6, 2], 4), -GRIP_HALF, GRIP_HALF, wood)
    bevel(grip, 3.5, 3)
    parts.append(grip)
    cap = profile_yz("grip_cap", [(-107, 11), (-65, -24), (-67, -27), (-110, 9)], -GRIP_HALF + 1, GRIP_HALF - 1, dark)
    parts.append(cap)
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 4, 8, 6, 6, 4, 2], 3), -5, 5, metal)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 4, 7, 5, 5, 3, 2], 3), -10, 10))
    bevel(guard, 0.6)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(TRIGGER, [0, 0, 2, 3, 2, 2, 3, 2], 2), -3.2, 3.2, mats["steel"]))
    for p in parts:
        finish(p, 35)
    return join("buddyshot", parts)


def low():
    """The sawn-off at Vice City's detail, in its moving parts: {part: [objects]}."""
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 3, 6, 4, 4, 2, 0], 1), -5, 5)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 3, 5, 3, 3, 2, 0], 1), -10, 10), solver="EXACT")
    body = [profile_yz("action", ACTION, -ACTION_HALF, ACTION_HALF),
            profile_yz("tang", [(-34, 104), (4, 112), (4, 118), (-30, 108)], -8, 8),
            profile_xz("lever", [(-6, 118), (6, 118), (6, 123), (-6, 123)], -14, 14),
            cylinder_y("barrel", 0, BORE, BARREL_R, 112, SPLIT, 10),
            cylinder_y("barrel", 0, BORE, BARREL_R, SPLIT, MUZZLE_Y, 10),
            cylinder_y("barrel_low", 0, BORE_LOW, BARREL_R, 112, SPLIT, 10),
            cylinder_y("barrel_low", 0, BORE_LOW, BARREL_R, SPLIT, MUZZLE_Y, 10),
            box("side_rib", -4.5, 4.5, 112, MUZZLE_Y - 1, BORE_LOW, BORE),
            box("top_rib", -4.5, 4.5, 112, SPLIT, BORE + 9.5, BORE + 13.5),
            box("top_rib", -4.5, 4.5, SPLIT, MUZZLE_Y - 2, BORE + 9.5, BORE + 13.5),
            box("bead", -1.6, 1.6, MUZZLE_Y - 10, MUZZLE_Y - 6, BORE + 12, BORE + 16.2),
            profile_xz("forend", fore_section(), FORE_Y0, FORE_Y1),
            profile_yz("grip", fillet(GRIP, [0, 8, 6, 0, 4, 4, 0, 4, 0], 1), -GRIP_HALF, GRIP_HALF),
            guard]
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    return {"body": body, "trigger": trigger}


def choose(normal, centre):
    """Which photo a face wears: the top where it faces up, the front at the muzzles, the side (the left: the right
    mirrors it) otherwise; each in its rear or front half."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    rear = centre.y < SPLIT
    if ax == 2 and normal.z > 0:
        return "top_rear" if rear else "top_front"
    if ax == 1 and normal.y > 0 and centre.y > MUZZLE_Y - 12:
        return "front"
    return "side_rear" if rear else "side_front"


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "buddyshot", REAL_LENGTH)
    frame = hold_pitched(WEB, scale, PITCH)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    gun = detailed(materials())
    rgba = kit.atlas(scene, REGIONS, {k: [gun] for k in REGIONS})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "buddyshot-atlas.png")
    kit.save_png(atlas_png, flat)
    gun.hide_render = True
    names = [p[0] for p in PARTS]
    objects = []
    for part, obs in low().items():
        for ob in obs:
            kit.project(ob, REGIONS, choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING, two_handed=True, hold_left=hold_left, left_forearm=LEFT_FOREARM)
    print(f"buddyshot: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin "
          f"{[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "buddyshot-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.6, 0.18, 0.1), (0, 0.17, 0.05), 35), ((0.45, 0.45, 0.25), (0, 0.15, 0.05), 35),
                 ((-0.15, -0.15, 0.2), (0.0, 0.2, 0.08), 35)])


main()
