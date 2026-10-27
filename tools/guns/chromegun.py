"""Vice City's Shotgun ("chromegun"): a chromed Remington 870 pump with an 18-inch barrel, a walnut stock with a
semi-pistol grip and a ribbed ("corncob") walnut forend, for the first-person viewmodel.

blender -b -P tools/guns/chromegun.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right (as colt45.py): the receiver's rear at y = 0, the
bore 100 mm up. Real dimensions: 978 mm overall, the barrel 457 mm, the receiver 182 mm, the forend 220 mm. The
game's model has the same proportions (its barrel is 0.47 of its length, as an 18-inch 870's), its receiver
chrome, its stock and forend brown wood, its trigger guard black.

Held in both hands: his right on the wrist of the stock, his left under the forend (the pump: his left hand's
anchors move with it as it racks). A stock's wrist is held with the hand pitched steeply down along the grip,
its web on top of the wrist, so the right hand's hold is the Colt's turned 50 degrees down about its web point
(`hold_pitched`, which kit.hold_for can't do; the game holds its own chromegun pitched in his hand too). That
lines his knuckles up with the semi-pistol grip's front and puts his index level with the trigger.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

REAL_LENGTH = 978.0
BORE = 100.0  # z of the bore's axis
RECV = (0.0, 182.0, 62.0, 114.0, 15.5)  # the receiver: y0, y1, z0, z1, half width
BARREL_R, MUZZLE_Y = 10.2, 641.0
TUBE_Z, TUBE_R, TUBE_END = 76.0, 11.0, 598.0  # the magazine tube
FORE_Y0, FORE_Y1 = 262.0, 482.0  # the forend (pump), closed; it racks back 70 mm, to just short of the receiver
BUTT_Y = -337.0


def fore_section(inset=0.0):
    """The forend's cross-section (x, z), `inset` mm in all round."""
    pts = [(-16, 54), (16, 54), (24, 61), (26, 76), (22.5, 92), (-22.5, 92), (-26, 76), (-24, 61)]
    cx, cz = 0.0, 74.0
    out = []
    for x, z in pts:
        d = Vector((x - cx, z - cz))
        k = max(0.0, 1.0 - inset / d.length)
        out.append((cx + d.x * k, cz + d.y * k))
    return out


# The stock's side: the top along the receiver's line, dropping to the wrist and the comb; the butt; the belly up to
# a semi-pistol grip whose front runs up at 54 degrees from vertical to the trigger guard.
STOCK = [(0.5, 104), (-30, 89), (-90, 86), (BUTT_Y + 13, 86), (BUTT_Y + 29, -28), (-150, -8), (-92, -4), (-70, -4),
         (-4, 44), (0.5, 50)]
STOCK_FILLET = [3, 12, 20, 2, 2, 20, 10, 12, 8, 3]
PAD = [(BUTT_Y, 87), (BUTT_Y + 13, 86), (BUTT_Y + 29, -28), (BUTT_Y + 16, -29)]


def stock_half(y):
    """The stock's half width at y: the receiver's at its front, 17.5 at the wrist, 21 at the butt."""
    if y >= 0:
        return RECV[4]
    if y >= -60:
        return RECV[4] + (17.5 - RECV[4]) * (-y / 60)
    return 17.5 + (21.0 - 17.5) * min(1.0, (-60 - y) / (-60 - BUTT_Y))


def taper(ob, half):
    """An object made 2 mm wide (x -1..1) given its half width `half(y)` (mm) along its length."""
    for v in ob.data.vertices:
        v.co.x = math.copysign(half(v.co.y / MM) * MM, v.co.x)
    ob.data.update()


GUARD = [(0, 62), (96, 62), (94, 55), (80, 40), (56, 34), (22, 34), (6, 40), (0, 50)]
GUARD_OPENING = [(10, 64), (86, 64), (84, 56), (72, 45), (54, 40), (24, 40), (14, 45), (10, 53)]
TRIGGER = [(38, 62), (45, 62), (45, 55), (47, 48), (51, 43), (47, 42), (42, 47), (39, 55)]

# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# The gun's long, so its side and top are each photographed in two halves: the stock and receiver, and the barrel
# and forend.
REAR_C, FRONT_C = (-340 + 200) / 2, (140 + 652) / 2
REGIONS = {
    "side_rear": ((0, 0, 256, 72), (-400, REAR_C, 40), (1, 0, 0), (0, 0, 1), 540 / 256),
    "side_front": ((0, 72, 256, 40), (-400, FRONT_C, 88), (1, 0, 0), (0, 0, 1), 2.0),
    "top_rear": ((0, 112, 256, 28), (0, REAR_C, 400), (0, 0, -1), (-1, 0, 0), 540 / 256),
    "top_front": ((0, 140, 256, 28), (0, FRONT_C, 400), (0, 0, -1), (-1, 0, 0), 2.0),
    "front": ((0, 168, 88, 88), (0, 900, 82), (0, -1, 0), (0, 0, 1), 0.75),
    "pump_back": ((88, 168, 88, 88), (0, -400, 82), (0, 1, 0), (0, 0, 1), 0.75),
}

# Where his fingertips go (the pad's middle, a finger's half-thickness off the surface).
FINGER_R = 8.0
# The right hand: its web on the top of the wrist just behind the receiver, the hand pitched down along the grip.
WEB = (0.0, -24.0, 91.0)
PITCH = 50.0
GRIP_TOP = Vector((-4.0, 44.0))  # the grip's front edge, from just behind the guard down-back to its foot
GRIP_DOWN = (Vector((-70.0, -4.0)) - GRIP_TOP).normalized()
GRIP_IN = Vector((GRIP_DOWN.y, -GRIP_DOWN.x))  # from its front edge into the grip, towards the web
if GRIP_IN.y < 0:
    GRIP_IN = -GRIP_IN


def on_grip(t, back=30.0):
    """A fingertip on the grip's left side: `t` mm down its front edge, `back` mm in from it."""
    p = GRIP_TOP + GRIP_DOWN * t + GRIP_IN * back
    return (-stock_half(p.x) - FINGER_R, p.x, p.y)


ALONG_GRIP = (0.0, -GRIP_IN.x * 28.0, -GRIP_IN.y * 28.0)  # slack: towards the grip's front edge (longer fingers)
# The left hand: palm up under the forend's middle, fingers across it and up its right side, thumb along its left.
PALM_Y = FORE_Y0 + 85.0  # the rear half of the forend
PALM = (0.0, PALM_Y, 49.0)
LEFT_ROLL = 15.0
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 48.5, 55.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(4.0), None, None, ALONG_GRIP),
    ("r_ring", on_grip(21.0), None, None, ALONG_GRIP),
    ("r_pinky", on_grip(37.0, 32.0), None, None, ALONG_GRIP),
    ("r_thumb", (-RECV[4] - FINGER_R, 20.0, 86.0), None, None, (0.0, -12.0, 0.0)),
    ("l_index", (26 + FINGER_R, PALM_Y + 24, 73.0), "pump", None, None),
    ("l_middle", (26 + FINGER_R, PALM_Y + 6, 75.0), "pump", None, None),
    ("l_ring", (26 + FINGER_R, PALM_Y - 12, 73.0), "pump", None, None),
    ("l_pinky", (25 + FINGER_R, PALM_Y - 28, 70.0), "pump", None, None),
    ("l_thumb", (-26 - FINGER_R, PALM_Y + 40, 80.0), "pump", None, None),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("pump", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
# A bead shotgun: sighted along the receiver's top to the bead.
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, BORE), (0, 20, RECV[3]), (0, MUZZLE_Y - 9, BORE + BARREL_R + 2.2)
FRAMING = ((-0.181, -0.221, 0.195), 3.0, 0.0, 0.25, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
LEFT_FOREARM = (-0.477, -0.750, -0.458)  # kit's, turned with the levelled framing: runs where it did in the view


def hold_pitched(web_mm, scale, pitch_deg):
    """kit.hold_for with his hand turned `pitch_deg` down about the web point (the barrel up in his hand): a stock's
    wrist is gripped along a raked grip, not a pistol's upright one."""
    t = math.radians(pitch_deg)
    axes = [Vector((0, -1, 0)), Vector((math.cos(t), 0, math.sin(t))), Vector((-math.sin(t), 0, math.cos(t)))]
    w = Vector(web_mm) * MM * scale
    origin = Vector(kit.WEB_IN_HAND) - (axes[0] * w.x + axes[1] * w.y + axes[2] * w.z)
    return [tuple(a) for a in axes] + [tuple(origin)]


def speckle(m, scale, strength, dark, light):
    """Breaks a material's colour and surface up with fine noise: the mottled look of Vice City's photo textures
    (engraving and wear on metal, grain on wood when `scale` is stretched along y)."""
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
    mats = kit.photo_materials()
    # Bright nickel-chrome, as the game's (its receiver is a light, mottled grey).
    mats["chrome"] = material("chrome", (0.88, 0.88, 0.87), metallic=0.75, roughness=0.28)
    mats["engraved"] = speckle(material("engraved", (0.88, 0.88, 0.87), metallic=0.75, roughness=0.3),
                               (160.0, 160.0, 160.0), 0.4, (0.42, 0.42, 0.41), (0.95, 0.95, 0.94))
    # Walnut, light and grey as the game's (its stock is about (127, 109, 96)).
    wood = speckle(material("walnut", (0.4, 0.3, 0.22), roughness=0.5, spec=0.35), (90.0, 6.0, 90.0), 0.0,
                   (0.30, 0.21, 0.16), (0.56, 0.41, 0.31))
    mats["wood"] = wood
    mats["rubber"] = material("rubber", (0.02, 0.02, 0.02), roughness=0.8, spec=0.2)
    return mats


def detailed(mats):
    """The photo shoot's 870: every part the texture shows. Returns (gun, pump)."""
    chrome, dark, wood = mats["chrome"], mats["dark"], mats["wood"]
    y0, y1, z0, z1, hw = RECV
    parts = []
    recv = profile_yz("receiver", fillet([(y0, z0), (y1, z0), (y1, z1), (y0 + 16, z1), (y0, z1 - 10)], [1, 1, 1.5, 8, 2], 3),
                      -hw, hw, mats["engraved"])
    cutters = [box("port", hw - 3, hw + 3, 70, 150, 86, 110)]  # the ejection port (right side)
    cutters.append(box("loading_port", -11, 11, 104, 176, z0 - 2, z0 + 6))  # under it
    cut(recv, join("cutters", cutters))
    bevel(recv, 1.2)
    parts.append(recv)
    # The left side: the trigger plate's two pins, the action bar lock, a line along the receiver.
    for y in (22, 98):
        parts.append(cylinder_x("pin", y, 70, 2.6, -hw - 0.6, hw + 0.6, 12, mats["steel"]))
    parts.append(profile_yz("bar_lock", fillet([(98, 58), (112, 58), (112, 63), (98, 63)], 1.5, 2), -hw - 0.8, -hw + 1, dark))
    parts.append(cylinder_y("collar", 0, BORE, 12.8, y1 - 4, y1 + 9, 24, chrome))
    barrel = cylinder_y("barrel", 0, BORE, BARREL_R, y1, MUZZLE_Y, 24, chrome)
    cut(barrel, cylinder_y("bore", 0, BORE, 9.25, MUZZLE_Y - 40, MUZZLE_Y + 5, 20))
    parts.append(barrel)
    parts.append(cylinder_x("bead", MUZZLE_Y - 9, BORE + BARREL_R + 0.8, 1.7, -1.7, 1.7, 12, mats["steel"]))
    # The magazine tube, its cap (knurled) and the barrel's hanger round it.
    parts.append(cylinder_y("tube", 0, TUBE_Z, TUBE_R, y1, TUBE_END, 24, chrome))
    cap = cylinder_y("cap", 0, TUBE_Z, 12.4, TUBE_END, TUBE_END + 26, 24, chrome)
    bevel(cap, 2.0, 2)
    parts.append(cap)
    parts.append(profile_yz("hanger", fillet([(TUBE_END + 2, TUBE_Z), (TUBE_END + 18, TUBE_Z), (TUBE_END + 18, BORE),
                                              (TUBE_END + 2, BORE)], 3, 2), -6, 6, chrome))
    # The trigger plate and guard (black), the trigger, the cross-bolt safety.
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 4, 8, 6, 8, 6, 3], 3), -5.5, 5.5, dark)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 4, 7, 5, 7, 5, 2], 3), -10, 10))
    bevel(guard, 0.6)
    parts.append(guard)
    parts.append(profile_yz("plate", [(2, 58), (100, 58), (100, 63), (2, 63)], -hw + 0.5, hw - 0.5, dark))
    parts.append(profile_yz("trigger", fillet(TRIGGER, [0, 0, 2, 3, 2, 2, 3, 2], 2), -3.2, 3.2, dark))
    parts.append(cylinder_x("safety", 15, 57, 3.8, -8.5, 8.5, 12, dark))
    # The stock: walnut, tapered, rounded; a black butt pad with a white spacer line.
    stock = profile_yz("stock", fillet(STOCK, STOCK_FILLET, 4), -1, 1, wood)
    taper(stock, stock_half)
    bevel(stock, 4.0, 3)
    parts.append(stock)
    pad = profile_yz("pad", fillet(PAD, [3, 0, 0, 3], 2), -1, 1, mats["rubber"])
    taper(pad, lambda y: 21.0)
    bevel(pad, 3.0, 2)
    parts.append(pad)
    spacer = profile_yz("spacer", [(BUTT_Y + 12.5, 86.5), (BUTT_Y + 14, 86.5), (BUTT_Y + 30, -28.5), (BUTT_Y + 28.5, -28.5)],
                        -20.8, 20.8, material("spacer", (0.75, 0.73, 0.68), roughness=0.5))
    parts.append(spacer)
    for p in parts:
        finish(p, 35)
    gun = join("chromegun", parts)
    # The forend: walnut rings and grooves, its tube nut, the action bars back to the receiver.
    pieces = []
    pitch, groove = 14.0, 4.0
    y = FORE_Y0
    pieces.append(profile_xz("fore_end", fore_section(2.0), FORE_Y0 - 0.1, FORE_Y0 + 4, wood))
    pieces.append(profile_xz("fore_end", fore_section(2.0), FORE_Y1 - 4, FORE_Y1 + 0.1, wood))
    pieces.append(profile_xz("fore_core", fore_section(2.2), FORE_Y0 + 2, FORE_Y1 - 2, wood))
    y = FORE_Y0 + 12
    while y + pitch < FORE_Y1 - 10:
        pieces.append(profile_xz("fore_ring", fore_section(), y + groove, y + pitch, wood))
        y += pitch
    pieces.append(profile_xz("fore_band", fore_section(), FORE_Y0 + 3, FORE_Y0 + 12, wood))
    pieces.append(profile_xz("fore_band", fore_section(), y + groove, FORE_Y1 - 3, wood))
    for side in (-1, 1):
        pieces.append(box("action_bar", side * 10.5 - 1.6, side * 10.5 + 1.6, 150, FORE_Y0 + 10, 64, 71, mats["steel"]))
    nut = cylinder_y("nut", 0, TUBE_Z, 12.0, FORE_Y1, FORE_Y1 + 7, 24, chrome)
    bevel(nut, 1.0)
    pieces.append(nut)
    for p in pieces:
        finish(p, 35)
    pump = join("pump", pieces)
    return gun, pump


def low():
    """The 870 at Vice City's detail, in its moving parts: {part: [objects]}."""
    y0, y1, z0, z1, hw = RECV
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 3, 6, 4, 6, 4, 0], 1), -5.5, 5.5)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 3, 5, 3, 5, 3, 0], 1), -10, 10), solver="EXACT")
    stock = profile_yz("stock", fillet(STOCK, [0, 8, 0, 0, 0, 0, 0, 8, 6, 0], 1), -1, 1)
    taper(stock, stock_half)
    pad = profile_yz("pad", PAD, -1, 1)
    taper(pad, lambda y: 21.0)
    body = [profile_yz("receiver", [(y0, z0), (y1, z0), (y1, z1), (y0 + 14, z1), (y0, z1 - 10)], -hw, hw),
            cylinder_y("barrel", 0, BORE, BARREL_R, y1, MUZZLE_Y, 8),
            box("bead", -1.7, 1.7, MUZZLE_Y - 11, MUZZLE_Y - 7, BORE + BARREL_R - 1, BORE + BARREL_R + 2.4),
            cylinder_y("tube", 0, TUBE_Z, TUBE_R, y1, TUBE_END, 8),
            cylinder_y("cap", 0, TUBE_Z, 12.4, TUBE_END, TUBE_END + 26, 8),
            box("hanger", -6, 6, TUBE_END + 2, TUBE_END + 18, TUBE_Z, BORE),
            guard, stock, pad]
    pump = [profile_xz("forend", fore_section(), FORE_Y0, FORE_Y1),
            cylinder_y("nut", 0, TUBE_Z, 12.0, FORE_Y1, FORE_Y1 + 7, 8)]
    for side in (-1, 1):
        pump.append(box("action_bar", side * 10.5 - 1.6, side * 10.5 + 1.6, 160, FORE_Y0 + 2, 64, 71))
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    return {"body": body, "pump": pump, "trigger": trigger}


def choose(normal, centre):
    """Which photo a face wears: the top where it faces up, the front at the muzzle end, the pump's own back view
    on the forend's rear, the side (the left: the right mirrors it) otherwise; each in its rear or front half."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    rear = centre.y < 160
    if ax == 2 and normal.z > 0:
        return "top_rear" if rear else "top_front"
    if ax == 1 and normal.y > 0 and centre.y > FORE_Y1 - 5:
        return "front"
    if ax == 1 and normal.y < 0 and abs(centre.y - FORE_Y0) < 3:
        return "pump_back"
    return "side_rear" if rear else "side_front"


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "chromegun", REAL_LENGTH)
    frame = hold_pitched(WEB, scale, PITCH)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    gun, pump = detailed(materials())
    both = [gun, pump]
    rgba = kit.atlas(scene, REGIONS, {"side_rear": both, "side_front": both, "top_rear": both, "top_front": both,
                                      "front": both, "pump_back": [pump]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "chromegun-atlas.png")
    kit.save_png(atlas_png, flat)
    gun.hide_render = pump.hide_render = True
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
    print(f"chromegun: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin "
          f"{[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "chromegun-preview.png"), [o for o, _ in objects], atlas_png,
                [((-1.1, 0.15, 0.12), (0, 0.15, 0.07), 35), ((0.7, 0.75, 0.35), (0, 0.2, 0.08), 35),
                 ((-0.16, -0.12, 0.21), (0.0, 0.35, 0.08), 35)])


main()
