"""Vice City's Combat Shotgun ("shotgspa"): a Franchi SPAS-12 with its stock folded over the top, for the
first-person viewmodel.

blender -b -P tools/guns/shotgspa.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right (as colt45.py): the receiver's rear at y = 0, the
bore 100 mm up. Real dimensions: 810 mm long with the stock folded, the barrel 546 mm (21.5 inches), the receiver
250 mm. As the game's model: the stock folded over the receiver (a pressed-steel arm with lightening holes, its
butt hook over the heat shield), a pistol grip, the long pump under the barrel, the perforated heat shield over it,
a tall front sight (for sighting over the folded stock), all in Vice City's light phosphate grey.

Held in both hands: his right on the pistol grip (the Colt's hold), his left under the pump (his left hand's
anchors move with it as it racks). The game's model is small for a SPAS-12 (its pitch in his hand shortens it
further along his hand), so the gun is held at 0.84 of real size; the grip and trigger reach are a little
generous, as the game's own are.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

REAL_LENGTH = 810.0
BORE = 100.0
RECV = (0.0, 250.0, 60.0, 118.0, 17.0)  # the receiver: y0, y1, z0, z1, half width
BARREL_R, MUZZLE_Y = 11.0, 800.0
TUBE_Z, TUBE_R, TUBE_END = 74.0, 12.0, 742.0  # the magazine tube
PUMP_Y0, PUMP_Y1 = 340.0, 570.0  # the pump, forward; it racks back 84 mm (the mod's 70 at this scale)
SHIELD_Y1 = 520.0  # the heat shield's front
STOCK_TOP = 133.0  # the folded stock's top plate
GRIP_HALF = 15.5
RAKE = math.tan(math.radians(18))


def back_y(z):
    """The pistol grip's back, raked 18 degrees as the Colt's."""
    return -6.0 - (60.0 - z) * RAKE


def front_y(z):
    """The grip's front, 58 mm ahead of its back (the game's grip is deep)."""
    return back_y(z) + 58.0


GRIP = [(back_y(62), 62), (back_y(-36), -36), (front_y(-36), -36), (front_y(36), 36), (front_y(36) + 6, 50),
        (front_y(36) + 6, 62)]
GUARD = [(50, 62), (128, 62), (126, 54), (114, 30), (70, 24), (56, 28), (50, 40)]
GUARD_OPENING = [(60, 64), (118, 64), (116, 56), (106, 36), (72, 31), (62, 36), (60, 44)]
TRIGGER = [(76, 62), (83, 62), (83.5, 52), (86, 43), (90, 38), (86, 37), (80, 43), (77, 52)]
HOOK = [(316, STOCK_TOP - 7), (334, STOCK_TOP), (356, STOCK_TOP - 3), (372, 116), (368, 104), (356, 102), (358, 114),
        (348, 124), (322, 121)]


def pump_section(inset=0.0):
    """The pump's cross-section (x, z): a deep box round the magazine tube, `inset` mm in."""
    pts = [(-18, 40), (18, 40), (25, 47), (26, 88), (21, 96), (-21, 96), (-26, 88), (-25, 47)]
    out = []
    for x, z in pts:
        d = Vector((x, z - 68.0))
        k = max(0.0, 1.0 - inset / d.length)
        out.append((d.x * k, 68.0 + d.y * k))
    return out


SHIELD = [(-13, 106), (13, 106), (15, 113), (10, 121), (-10, 121), (-15, 113)]

# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# Side and top each in two halves: the grip, receiver and stock; the barrel, shield and pump.
REAR_C, FRONT_C = (-50 + 400) / 2, (290 + 810) / 2
REGIONS = {
    "side_rear": ((0, 0, 256, 104), (-400, REAR_C, 49.5), (1, 0, 0), (0, 0, 1), 450 / 256),
    "side_front": ((0, 104, 256, 48), (-400, FRONT_C, 86.5), (1, 0, 0), (0, 0, 1), 520 / 256),
    "top_rear": ((0, 152, 256, 26), (0, REAR_C, 400), (0, 0, -1), (-1, 0, 0), 450 / 256),
    "top_front": ((0, 178, 256, 26), (0, FRONT_C, 400), (0, 0, -1), (-1, 0, 0), 520 / 256),
    "front": ((0, 204, 52, 52), (0, 900, 86), (0, -1, 0), (0, 0, 1), 1.8),
    "pump_back": ((52, 204, 52, 52), (0, -400, 68), (0, 1, 0), (0, 0, 1), 1.2),
    "back": ((104, 204, 52, 52), (0, -600, 96), (0, 1, 0), (0, 0, 1), 1.5),
}

FINGER_R = 8.0
# The right hand: the Colt's hold, its web on the grip's back 16 mm under the receiver.
WEB = (0.0, back_y(44.0), 44.0)
PANEL_X = -GRIP_HALF


def on_panel(z, back=36.0):
    """A fingertip on the grip's left panel, `back` mm behind its front."""
    return (PANEL_X - FINGER_R, front_y(z) - back, z)


FORWARD = (0.0, 22.0, 22.0 * RAKE)  # slack: along the panel towards the front (longer fingers)
PALM_Y = PUMP_Y0 + 60.0
PALM = (0.0, PALM_Y, 37.0)
LEFT_ROLL = 15.0
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 90.0, 47.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_panel(14.0), None, None, FORWARD),
    ("r_ring", on_panel(-6.0), None, None, FORWARD),
    ("r_pinky", on_panel(-25.0, 38.0), None, None, FORWARD),
    ("r_thumb", (-RECV[4] - FINGER_R, 40.0, 70.0), None, None, (0.0, -14.0, 0.0)),
    ("l_index", (26 + FINGER_R, PALM_Y + 24, 90.0), "pump", None, None),
    ("l_middle", (26 + FINGER_R, PALM_Y + 6, 92.0), "pump", None, None),
    ("l_ring", (26 + FINGER_R, PALM_Y - 12, 90.0), "pump", None, None),
    ("l_pinky", (25 + FINGER_R, PALM_Y - 28, 86.0), "pump", None, None),
    ("l_thumb", (-26 - FINGER_R, PALM_Y + 40, 72.0), "pump", None, None),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("pump", (0, 0, 0), (0, -1, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
# Sighted over the folded stock to the tall front sight.
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, BORE), (0, 20, STOCK_TOP), (0, MUZZLE_Y - 18, STOCK_TOP - 1)
FRAMING = ((-0.201, -0.268, 0.182), 2.0, 0.0, 0.25, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
LEFT_FOREARM = (-0.45, -0.7, -0.6)  # kit's, turned with the levelled framing and steeper: his shoulder showed


def taper(ob, half):
    """An object made 2 mm wide (x -1..1) given its half width `half(y, z)` (mm)."""
    for v in ob.data.vertices:
        v.co.x = math.copysign(half(v.co.y / MM, v.co.z / MM) * MM, v.co.x)
    ob.data.update()


def speckle(m, scale, strength, dark, light):
    """Breaks a material's colour and surface up with fine noise: the mottled look of Vice City's photo textures."""
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
    """The game's SPAS is a light phosphate grey all over (its texture averages (138, 137, 127)); the grip and pump
    a darker grey polymer."""
    mats = kit.photo_materials()
    mats["phosphate"] = speckle(material("phosphate", (0.42, 0.42, 0.40), metallic=0.55, roughness=0.5),
                                (220.0, 220.0, 220.0), 0.2, (0.30, 0.30, 0.28), (0.50, 0.50, 0.47))
    mats["barrel"] = speckle(material("barrel", (0.30, 0.30, 0.29), metallic=0.6, roughness=0.45),
                             (220.0, 220.0, 220.0), 0.15, (0.22, 0.22, 0.21), (0.36, 0.36, 0.34))
    mats["polymer"] = speckle(material("polymer", (0.12, 0.12, 0.115), roughness=0.7, spec=0.3),
                              (400.0, 400.0, 400.0), 0.3, (0.09, 0.09, 0.085), (0.16, 0.16, 0.15))
    return mats


def detailed(mats):
    """The photo shoot's SPAS-12: every part the texture shows. Returns (gun, pump)."""
    metal, poly, dark = mats["phosphate"], mats["polymer"], mats["dark"]
    y0, y1, z0, z1, hw = RECV
    parts = []
    recv = profile_yz("receiver", fillet([(y0, z0), (y1, z0), (y1, z1), (y0 + 14, z1), (y0, z1 - 8)], [1, 1, 2, 6, 2], 3),
                      -hw, hw, metal)
    cutters = [box("port", hw - 3, hw + 3, 110, 200, 84, 112), box("loading_port", -12, 12, 130, 240, z0 - 2, z0 + 6)]
    # The receiver's side panels: a shallow recess along each side.
    for s in (-1, 1):
        cutters.append(box("panel", s * hw - 1.2, s * hw + 1.2, 30, 230, 70, 92))
    cut(recv, join("cutters", cutters))
    bevel(recv, 1.5)
    parts.append(recv)
    # Left side: the bolt release, the pins.
    parts.append(profile_yz("release", fillet([(150, 66), (172, 66), (172, 74), (150, 74)], 2, 2), -hw - 1.5, -hw + 1, dark))
    for y, z in ((24, 98), (70, 66), (226, 66)):
        parts.append(cylinder_x("pin", y, z, 2.8, -hw - 0.6, hw + 0.6, 12, dark))
    # The folded stock: hinge block, a pressed-steel arm with lightening holes, its butt hook over the shield.
    parts.append(profile_yz("hinge", fillet([(-10, 108), (16, 108), (16, STOCK_TOP - 2), (-10, STOCK_TOP - 2)], [3, 0, 0, 6], 2),
                            -12, 12, metal))
    # The arm: a pressed-steel channel (open below), lightening holes in its top; dark inside, so the holes read.
    arm = profile_xz("arm", [(-15, STOCK_TOP - 9), (15, STOCK_TOP - 9), (15, STOCK_TOP), (-15, STOCK_TOP)], -4, 330, metal)
    cut(arm, profile_xz("channel", [(-12.5, STOCK_TOP - 12), (12.5, STOCK_TOP - 12), (12.5, STOCK_TOP - 3),
                                    (-12.5, STOCK_TOP - 3)], 18, 318))
    holes = [box("hole", -9, 9, 90 + 52 * k, 126 + 52 * k, STOCK_TOP - 5, STOCK_TOP + 2) for k in range(4)]
    cut(arm, join("holes", holes))
    bevel(arm, 0.8)
    parts.append(arm)
    parts.append(box("inside", -12, 12, 20, 316, STOCK_TOP - 9, STOCK_TOP - 5, dark))
    hook = profile_yz("hook", fillet(HOOK, [2, 6, 6, 4, 3, 3, 3, 6, 2], 3), -22, 22, metal)
    bevel(hook, 1.0)
    parts.append(hook)
    # Rear sight ears on the receiver (under the folded arm).
    parts.append(profile_yz("rear_sight", [(8, z1), (30, z1), (30, z1 + 6), (8, z1 + 6)], -10, 10, metal))
    # Barrel, heat shield (perforated), magazine tube and cap, front sight.
    barrel = cylinder_y("barrel", 0, BORE, BARREL_R, y1, MUZZLE_Y, 24, mats["barrel"])
    cut(barrel, cylinder_y("bore", 0, BORE, 9.3, MUZZLE_Y - 40, MUZZLE_Y + 5, 20))
    parts.append(barrel)
    shield = profile_xz("shield", SHIELD, y1, SHIELD_Y1, metal)
    vents = [box("vent", -20, 20, y1 + 16 + 24 * k, y1 + 30 + 24 * k, 109, 116) for k in range(10)]
    vents += [box("vent", -6, 6, y1 + 16 + 24 * k, y1 + 30 + 24 * k, 116, 125) for k in range(10)]
    cut(shield, join("vents", vents))
    bevel(shield, 0.6)
    parts.append(shield)
    parts.append(cylinder_y("tube", 0, TUBE_Z, TUBE_R, y1, TUBE_END, 24, metal))
    cap = cylinder_y("cap", 0, TUBE_Z, 13.0, TUBE_END, TUBE_END + 22, 24, metal)
    bevel(cap, 2.0, 2)
    parts.append(cap)
    parts.append(profile_yz("swivel", fillet([(TUBE_END + 4, TUBE_Z - 22), (TUBE_END + 18, TUBE_Z - 22), (TUBE_END + 18, TUBE_Z),
                                              (TUBE_END + 4, TUBE_Z)], 4, 2), -3, 3, dark))
    parts.append(profile_yz("hanger", fillet([(TUBE_END + 2, TUBE_Z), (TUBE_END + 16, TUBE_Z), (TUBE_END + 16, BORE),
                                              (TUBE_END + 2, BORE)], 3, 2), -7, 7, metal))
    parts.append(cylinder_y("sight_band", 0, BORE, 13.5, MUZZLE_Y - 30, MUZZLE_Y - 8, 24, metal))
    parts.append(profile_yz("sight_post", [(MUZZLE_Y - 26, 110), (MUZZLE_Y - 10, 110), (MUZZLE_Y - 14, STOCK_TOP - 1),
                                           (MUZZLE_Y - 21, STOCK_TOP - 1)], -1.6, 1.6, dark))
    for s in (-1, 1):
        parts.append(profile_yz("sight_ear", [(MUZZLE_Y - 28, 108), (MUZZLE_Y - 8, 108), (MUZZLE_Y - 12, STOCK_TOP - 4),
                                              (MUZZLE_Y - 24, STOCK_TOP - 4)], s * 7, s * 9.5, metal))
    # Pistol grip (polymer, finger grooves), trigger guard, trigger, safety.
    grip = profile_yz("grip", fillet(GRIP, [0, 6, 6, 3, 4, 0], 3), -GRIP_HALF, GRIP_HALF, poly)
    grooves = [cylinder_x("groove", front_y(z) + 3, z, 5.0, -20, 20, 12) for z in (24, 6, -12)]
    cut(grip, join("grooves", grooves))
    bevel(grip, 2.5, 2)
    parts.append(grip)
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 4, 8, 6, 6, 3], 3), -6, 6, poly)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 4, 7, 5, 5, 2], 3), -10, 10))
    bevel(guard, 0.8)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(TRIGGER, [0, 0, 2, 3, 2, 2, 3, 2], 2), -3.2, 3.2, dark))
    parts.append(cylinder_x("safety", 122, 58, 4.0, -hw - 1.5, hw + 1.5, 12, dark))
    for p in parts:
        finish(p, 35)
    gun = join("shotgspa", parts)
    # The pump: polymer, grooved along its sides, a lip at its front.
    pieces = [profile_xz("pump", pump_section(), PUMP_Y0, PUMP_Y1 - 10, poly),
              profile_xz("lip", pump_section(-2.5), PUMP_Y1 - 10, PUMP_Y1, poly)]
    grooves = []
    for z in (54, 64, 74):
        for s in (-1, 1):
            grooves.append(box("groove", s * 30 - 5, s * 30 + 5, PUMP_Y0 + 18, PUMP_Y1 - 22, z - 2, z + 2))
            grooves[-1].location.x = -s * 3.6 * MM
    cut(pieces[0], join("grooves", grooves))
    bevel(pieces[0], 1.5)
    for p in pieces:
        finish(p, 35)
    pump = join("pump", pieces)
    return gun, pump


def low():
    """The SPAS-12 at Vice City's detail, in its moving parts: {part: [objects]}."""
    y0, y1, z0, z1, hw = RECV
    guard = profile_yz("guard", fillet(GUARD, [0, 0, 3, 6, 4, 3, 0], 1), -6, 6)
    cut(guard, profile_yz("opening", fillet(GUARD_OPENING, [0, 0, 3, 5, 3, 3, 0], 1), -10, 10), solver="EXACT")
    body = [profile_yz("receiver", [(y0, z0), (y1, z0), (y1, z1), (y0 + 12, z1), (y0, z1 - 8)], -hw, hw),
            profile_yz("hinge", [(-10, 108), (16, 108), (16, STOCK_TOP - 2), (-10, STOCK_TOP - 2)], -12, 12),
            profile_xz("arm", [(-15, STOCK_TOP - 9), (15, STOCK_TOP - 9), (15, STOCK_TOP), (-15, STOCK_TOP)], -4, 330),
            profile_yz("hook", HOOK, -22, 22),
            cylinder_y("barrel", 0, BORE, BARREL_R, y1, MUZZLE_Y, 8),
            profile_xz("shield", SHIELD, y1, SHIELD_Y1),
            cylinder_y("tube", 0, TUBE_Z, TUBE_R, y1, TUBE_END, 8),
            cylinder_y("cap", 0, TUBE_Z, 13.0, TUBE_END, TUBE_END + 22, 8),
            box("hanger", -7, 7, TUBE_END + 2, TUBE_END + 16, TUBE_Z, BORE),
            cylinder_y("sight_band", 0, BORE, 13.5, MUZZLE_Y - 30, MUZZLE_Y - 8, 8),
            profile_yz("sight_post", [(MUZZLE_Y - 26, 110), (MUZZLE_Y - 10, 110), (MUZZLE_Y - 14, STOCK_TOP - 1),
                                      (MUZZLE_Y - 21, STOCK_TOP - 1)], -1.8, 1.8),
            profile_yz("grip", fillet(GRIP, [0, 5, 5, 0, 3, 0], 1), -GRIP_HALF, GRIP_HALF),
            guard]
    pump = [profile_xz("pump", pump_section(), PUMP_Y0, PUMP_Y1)]
    trigger = [profile_yz("trigger", TRIGGER, -3.2, 3.2)]
    return {"body": body, "pump": pump, "trigger": trigger}


def choose(normal, centre):
    """Which photo a face wears: the top where it faces up, the front at the muzzle end, the pump's own back view on
    its rear, the back on the receiver's and the hinge's rear, the side (the left: the right mirrors it) otherwise;
    each in its rear or front half."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    rear = centre.y < 290
    if ax == 2 and normal.z > 0:
        return "top_rear" if rear else "top_front"
    if ax == 1 and normal.y > 0 and centre.y > SHIELD_Y1 + 20:
        return "front"
    if ax == 1 and normal.y < 0 and abs(centre.y - PUMP_Y0) < 3:
        return "pump_back"
    if ax == 1 and normal.y < 0 and centre.y < 20 and centre.z > 62:
        return "back"
    return "side_rear" if rear else "side_front"


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "shotgspa", REAL_LENGTH)
    frame = kit.hold_for(WEB, scale)
    hold_left = kit.hold_left_for(PALM, scale, LEFT_ROLL)
    scene = kit.studio()
    gun, pump = detailed(materials())
    both = [gun, pump]
    rgba = kit.atlas(scene, REGIONS, {"side_rear": both, "side_front": both, "top_rear": both, "top_front": both,
                                      "front": both, "pump_back": [pump], "back": [gun]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "shotgspa-atlas.png")
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
    print(f"shotgspa: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin "
          f"{[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "shotgspa-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.9, 0.3, 0.12), (0, 0.33, 0.07), 35), ((0.6, 0.65, 0.35), (0, 0.25, 0.07), 35),
                 ((-0.15, -0.15, 0.2), (0.0, 0.35, 0.08), 35)])


main()
