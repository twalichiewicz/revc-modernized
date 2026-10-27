"""Vice City's Python: a stainless Colt Python .357 Magnum with the 6-inch barrel (292 mm overall), its vent rib
and full underlug, black target grips with the gold medallion, for the first-person viewmodel.

blender -b -P tools/guns/python.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the barrel, z up, x to the right; the grip's heel at y = 0, z = 0, the bore 125 up.
Built as tools/guns/colt45.py (the worked example) is. Its cylinder is a part of its own that turns a chamber on
each shot, so its sides wear a photo of their own (every flute alike) that stays right as it turns.
"""
import math
import os
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, bevel, box, cut, cylinder_x, cylinder_y, fillet, finish, join, material, profile_xz, profile_yz

B = 125.0  # the bore's height
LENGTH = 292.0  # overall, muzzle to heel
MUZZLE_Y = 292.0
CYL_Z, CYL_R, CYL_Y0, CYL_Y1 = 112.0, 19.8, 99.5, 140.5  # the cylinder: its axis's height, radius, back and front
FRAME_BOTTOM = 85.0
TOP_STRAP = 138.0
RAKE = math.tan(math.radians(22.5))  # the grip's rake back from vertical


def back_y(z):
    """The back of the grip (its back strap, under the wood) at height z."""
    return 1.0 + z * RAKE


def front_y(z):
    """The grip's front edge at height z."""
    return 84.0 - (84.0 - z) * RAKE


# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
SIDE_MMPP = 300 / 256
REGIONS = {
    "left": ((0, 0, 256, 132), (-400, 146, 73), (1, 0, 0), (0, 0, 1), SIDE_MMPP),
    "top": ((0, 132, 256, 44), (0, 146, 400), (0, 0, -1), (-1, 0, 0), SIDE_MMPP),
    "back": ((0, 176, 72, 80), (0, -400, 118), (0, 1, 0), (0, 0, 1), 0.7),
    "front": ((72, 176, 64, 80), (0, 600, 116.5), (0, -1, 0), (0, 0, 1), 0.78),
    "cylinder": ((136, 176, 120, 80), (-400, 120, CYL_Z), (1, 0, 0), (0, 0, 1), 0.55),
}

# Where his right hand's fingertips go (a pad's middle, a finger's half-thickness off the surface), as the Colt's:
# the index on the trigger's face, the others on the grip's left side behind its front edge, the thumb along the
# frame's left side behind the cylinder, over the latch.
FINGER_R = 8.0
GRIP_X = 15.5  # the grips' half-width
GRIP_BACK = 24.0  # the fingers' pads behind the grip's front edge


def on_grip(z, back=GRIP_BACK):
    return (-GRIP_X - FINGER_R, front_y(z) - back, z)


FORWARD = (0.0, 20.0, 20.0 * RAKE)  # along the grip towards its front edge
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, 104.3 + FINGER_R, 76.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(52.0), None, None, FORWARD),
    ("r_ring", on_grip(35.0), None, None, FORWARD),
    ("r_pinky", on_grip(18.0, GRIP_BACK + 2.0), None, None, FORWARD),
    ("r_thumb", (-14.5 - FINGER_R, 82.0, 108.0), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves (a cylinder: the axis it turns about)
    ("body", (0, 0, 0), (0, 0, 0)),
    ("cylinder", (0, (CYL_Y0 + CYL_Y1) / 2, CYL_Z), (0, 1, 0)),  # clockwise seen from behind, as a Colt's
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
MUZZLE, SIGHT_REAR, SIGHT_FRONT = (0, MUZZLE_Y, B), (0, 88, 144.5), (0, 287.5, 146.5)
WEB = (0.0, back_y(93.0), 93.0)  # where the web of his hand rests: the back strap, under the frame's hump
# Seen from: as the Colt's, as far back and as high as the Uzi's (closer, it read large and held high), looking 12
# degrees right of the barrel, level.
FRAMING = ((-0.190, -0.300, 0.200), 12.0, 0.0, 0.24, 0.02)


def photo_materials():
    mats = kit.photo_materials()
    # Polished stainless, a little blue as the game's is; satin enough that the round barrel and cylinder shade
    # smoothly in the studio rather than mirroring its dark floor.
    mats["steel"] = material("python_stainless", (0.80, 0.82, 0.86), metallic=0.9, roughness=0.34)
    mats["matte"] = material("python_matte", (0.62, 0.63, 0.66), metallic=0.8, roughness=0.6)
    mats["gold"] = material("medallion", (0.86, 0.64, 0.26), metallic=1.0, roughness=0.3)
    return mats


def barrel_section(detail):
    """The barrel's cross-section (x, z): the vent rib on top, the round barrel, the full underlug below."""
    rib = [(4.6, B + 15.0), (4.6, B + 10.6)]
    if detail:
        arc = [(9.6 * math.cos(math.radians(a)), B + 9.6 * math.sin(math.radians(a))) for a in (58, 45, 30, 15, 0)]
        lug = [(8.7, B - 8.0), (8.7, B - 20.0), (7.6, B - 23.0), (5.5, B - 24.5)]
    else:
        arc = [(7.6, B + 6.4), (9.4, B + 1.0)]
        lug = [(8.7, B - 7.0), (8.5, B - 20.5), (6.0, B - 24.5)]
    right = rib + arc + lug
    return right + [(-x, z) for x, z in reversed(right)]


def rear_outline():
    """The frame behind the cylinder (y, z): the recoil shield, the hump over his hand, down into the grip."""
    return [(99.3, FRAME_BOTTOM), (99.3, 127.0), (84.0, 127.0), (72.0, 124.0), (60.0, 116.0), (50.0, 107.5),
            (44.0, 101.5), (back_y(92.0), 92.0), (52.0, 80.0), (80.0, 80.0), (84.0, FRAME_BOTTOM)]


def top_strap_outline():
    return [(78.0, 124.5), (78.0, 133.5), (84.0, TOP_STRAP), (152.0, TOP_STRAP), (152.0, 132.3), (99.0, 132.3),
            (99.0, 124.5)]


def front_outline():
    return [(140.8, FRAME_BOTTOM), (146.0, FRAME_BOTTOM), (152.0, 92.0), (152.0, TOP_STRAP), (140.8, TOP_STRAP)]


def hammer_outline():
    return [(81.0, 119.0), (75.0, 129.0), (67.0, 134.5), (58.0, 136.5), (52.5, 135.0), (54.0, 131.5), (62.0, 128.5),
            (70.0, 121.0), (74.0, 114.0)]


def grip_outline():
    return [(front_y(84.0) + 2.0, 84.0), (front_y(12.0), 12.0), (front_y(12.0) + 1.5, 5.0), (front_y(0.0) - 1.0, 0.0),
            (5.0, 0.0), (back_y(3.0), 3.0), (back_y(96.0), 96.0), (back_y(101.0) + 1.5, 102.5), (47.0, 104.5),
            (51.0, 100.0), (55.0, 86.0), (62.0, 83.5)]


def guard_outline():
    return [(78.0, FRAME_BOTTOM), (128.0, FRAME_BOTTOM), (127.0, 75.0), (122.0, 65.0), (113.0, 60.0), (90.0, 59.5),
            (82.0, 63.0), (front_y(72.0) - 1.0, 72.0)]


def guard_opening():
    return [(83.5, FRAME_BOTTOM + 3), (123.5, FRAME_BOTTOM + 3), (122.8, 76.0), (118.5, 68.0), (111.5, 64.0),
            (92.0, 63.5), (86.0, 66.5), (82.8, 73.0)]


def trigger_outline():
    return [(98.0, FRAME_BOTTOM + 1), (104.0, FRAME_BOTTOM + 1), (104.6, 78.0), (103.4, 70.5), (100.8, 67.0),
            (99.2, 68.0), (100.2, 75.0), (98.4, 81.0)]


def detailed(mats):
    """The photo shoot's Python: every part the texture shows. Returns (gun, cylinder)."""
    steel, dark, grip, matte, gold = mats["steel"], mats["dark"], mats["grip"], mats["matte"], mats["gold"]
    parts = []
    # The barrel: rib, barrel and lug in one section, the rib's vents cut through (dark behind them, as the
    # game's texture has them), the bore, and the front sight's ramp.
    barrel = profile_xz("barrel", barrel_section(True), 148.0, MUZZLE_Y, steel)
    cutters = [cylinder_y("bore", 0, B, 4.6, MUZZLE_Y - 30, MUZZLE_Y + 5, 16)]
    vents = []
    for k in range(5):
        y0 = 166.0 + k * 22.0
        cutters.append(box("vent", -6, 6, y0, y0 + 14.0, B + 9.7, B + 12.6))
        vents.append(box("vent_shadow", -1.2, 1.2, y0 - 1, y0 + 15.0, B + 9.5, B + 12.8, dark))
    cut(barrel, join("cutters", cutters))
    bevel(barrel, 0.5, 1, 40)
    parts += [barrel] + vents
    parts.append(box("rib_top", -4.0, 4.0, 152.0, MUZZLE_Y - 1.0, B + 15.0, B + 15.3, matte))
    sight = profile_yz("front_sight", fillet([(262, B + 15), (291, B + 15), (291, B + 22), (288, B + 22.5), (276, B + 20)],
                                             [0, 0, 1, 1, 3], 2), -1.7, 1.7, steel)
    parts.append(sight)
    crown = cylinder_y("crown", 0, B, 5.6, MUZZLE_Y - 0.6, MUZZLE_Y + 0.05, 20, dark)
    cut(crown, cylinder_y("crown_bore", 0, B, 4.6, MUZZLE_Y - 2, MUZZLE_Y + 2, 16))
    parts.append(crown)
    # The frame: behind the cylinder (its recoil shield and the hump over his hand), the top strap with the rear
    # sight, the front around the barrel's shank and the bottom strap under the cylinder.
    rear = profile_yz("frame_rear", fillet(rear_outline(), [0, 3, 2, 6, 8, 8, 6, 4, 6, 3, 0], 3), -14.5, 14.5, steel)
    bevel(rear, 0.8)
    shield = cylinder_y("shield", 0, CYL_Z, 19.0, 94.0, 99.3, 40, steel)
    bevel(shield, 0.8)
    strap = profile_yz("top_strap", fillet(top_strap_outline(), [0, 2, 3, 0, 0, 0, 0], 3), -9.0, 9.0, steel)
    bevel(strap, 0.8)
    front = profile_yz("frame_front", fillet(front_outline(), [0, 3, 3, 0, 0], 3), -11.0, 11.0, steel)
    bevel(front, 0.8)
    bottom = box("bottom_strap", -10.0, 10.0, 99.0, 141.0, FRAME_BOTTOM, CYL_Z - CYL_R - 0.4, steel)
    bevel(bottom, 0.6)
    parts += [rear, shield, strap, front, bottom]
    parts.append(box("strap_matte", -6.5, 6.5, 100.0, 151.5, TOP_STRAP, TOP_STRAP + 0.25, matte))
    # The crane's line on the left, under the cylinder's front.
    parts.append(profile_yz("crane", fillet([(128, FRAME_BOTTOM + 1), (146, FRAME_BOTTOM + 1), (151, 93), (151, 104),
                                            (141, 104), (138, 95)], [0, 2, 2, 2, 2, 2], 2), -11.6, -10.9, steel))
    # The rear sight (blued, adjustable), its notch.
    rsight = profile_yz("rear_sight", fillet([(86, TOP_STRAP), (101, TOP_STRAP), (101, 141.5), (92, 143.5), (86, 146)],
                                             [0, 0, 1, 2, 1], 2), -6.5, 6.5, dark)
    cut(rsight, box("notch", -1.5, 1.5, 80, 95, 143.6, 150))
    parts.append(rsight)
    # The hammer, its spur chequered.
    hammer = profile_yz("hammer", fillet(hammer_outline(), [0, 4, 3, 2, 1.5, 1.5, 2, 3, 0], 2), -4.0, 4.0, steel)
    grooves = [box("g", -5, 5, 53.0 + k * 2.4, 54.0 + k * 2.4, 133.5, 140) for k in range(6)]
    cut(hammer, join("grooves", grooves))
    parts.append(hammer)
    # The cylinder latch (left, behind the shield) and the side plate's screws.
    latch = profile_yz("latch", fillet([(80, 109), (92, 109), (93, 116), (80, 117)], [1.5, 1.5, 1.5, 1.5], 2), -17.0, -14.0,
                       steel)
    cut(latch, join("latch_g", [box("g", -18, -16.3, 82.0 + k * 2.0, 82.9 + k * 2.0, 108, 118) for k in range(5)]))
    parts.append(latch)
    for y, z in ((64.0, 97.0), (88.0, 92.0), (72.0, 108.0)):
        parts.append(cylinder_x("screw", y, z, 2.4, -15.2, -14.2, 10, steel))
    # The trigger guard and the trigger.
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 6, 6, 6, 6, 5, 3], 3), -4.4, 4.4, steel)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 5, 5, 5, 5, 4, 3], 3), -10, 10))
    bevel(guard, 0.5)
    parts.append(guard)
    trig = profile_yz("trigger", fillet(trigger_outline(), [0, 0, 3, 3, 1, 1, 3, 0], 2), -3.2, 3.2, steel)
    parts.append(trig)
    # The grips: black, chequered, the medallion.
    g = profile_yz("grip", fillet(grip_outline(), [3, 4, 3, 3, 4, 3, 2, 6, 4, 3, 3, 3], 3), -GRIP_X, GRIP_X, grip)
    bevel(g, 2.5, 2, 30)
    parts.append(g)
    for sx in (-1, 1):
        parts.append(cylinder_x("medallion", 54.0, 72.0, 5.2, sx * (GRIP_X - 0.6), sx * (GRIP_X + 0.4), 24, gold))
    for p in parts:
        finish(p, 35)
    gun = join("python", parts)
    return gun, detailed_cylinder(steel)


def detailed_cylinder(steel):
    """Six flutes (one facing left, so every face of the low cylinder can wear it), the bolt notches, the chambers."""
    cyl = cylinder_y("cylinder", 0, CYL_Z, CYL_R, CYL_Y0, CYL_Y1, 72, steel)
    bevel(cyl, 1.6, 2, 30)
    cutters = []
    for k in range(6):
        t = math.radians(60 * k)
        d = Vector((-math.cos(t), math.sin(t)))  # theta from the left, towards up
        c = d * (CYL_R + 2.4)
        cutters.append(cylinder_y("flute", c.x, CYL_Z + c.y, 4.4, CYL_Y0 + 9.0, CYL_Y1 - 10.0, 16))
        for y in (CYL_Y0 + 9.0, CYL_Y1 - 10.0):
            bpy.ops.mesh.primitive_uv_sphere_add(radius=4.4 * MM, location=(c.x * MM, y * MM, (CYL_Z + c.y) * MM),
                                                 segments=16, ring_count=8)
            cutters.append(bpy.context.active_object)
        # The bolt's notch, over each chamber, and its lead.
        t2 = math.radians(60 * k + 30)
        e = Vector((-math.cos(t2), math.sin(t2)))
        n = e * CYL_R
        notch = box("notch", -1.6, 1.6, CYL_Y0 + 2.5, CYL_Y0 + 7.0, -2.0, 2.0)
        notch.rotation_euler = (0, math.atan2(e.x, e.y), 0)  # its z out along the radius
        notch.location = (n.x * MM, 0, (CYL_Z + n.y) * MM)
        cutters.append(notch)
        cutters.append(cylinder_y("chamber", e.x * 12.8, CYL_Z + e.y * 12.8, 4.8, CYL_Y1 - 14.0, CYL_Y1 + 2.0, 16))
    cutter = join("cutters", cutters)
    bpy.context.view_layer.objects.active = cutter
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    cut(cyl, cutter)
    finish(cyl, 35)
    return cyl


def ngon_y(name, cx, cz, r, n, y0, y1, phase):
    """An n-sided prism along y, a face's middle at angle `phase` (degrees, from the left towards up)."""
    pts = []
    for k in range(n):
        t = math.radians(phase + (k + 0.5) * 360.0 / n)
        pts.append((cx - r * math.cos(t), cz + r * math.sin(t)))
    return profile_xz(name, pts, y0, y1)


def low():
    """The Python at Vice City's detail, in its moving parts: {part: [objects]}."""
    guard = profile_yz("guard", fillet(guard_outline(), [0, 0, 5, 4, 4, 4, 3, 0], 1), -4.4, 4.4)
    cut(guard, profile_yz("opening", fillet(guard_opening(), [0, 0, 4, 3, 3, 3, 2, 0], 1), -10, 10), solver="EXACT")
    body = [profile_xz("barrel", barrel_section(False), 148.0, MUZZLE_Y),
            profile_yz("front_sight", [(262, B + 15), (291, B + 15), (291, B + 22.5), (276, B + 20)], -1.7, 1.7),
            profile_yz("rear_sight", [(86, TOP_STRAP), (101, TOP_STRAP), (101, 141.5), (86, 146)], -6.5, 6.5),
            profile_yz("top_strap", top_strap_outline(), -9.0, 9.0),
            profile_yz("frame_front", front_outline(), -11.0, 11.0),
            box("bottom_strap", -10.0, 10.0, 99.0, 141.0, FRAME_BOTTOM, CYL_Z - CYL_R - 0.4),
            profile_yz("frame_rear", rear_outline(), -14.5, 14.5),
            profile_yz("hammer", [(81, 119), (75, 129), (67, 134.5), (58, 136.5), (52.5, 135), (54, 131.5), (70, 121),
                                  (74, 114)], -4.0, 4.0),
            profile_yz("grip", grip_outline(), -GRIP_X, GRIP_X),
            guard]
    cylinder = [ngon_y("cylinder", 0, CYL_Z, CYL_R / math.cos(math.radians(15)) * 0.985, 12, CYL_Y0, CYL_Y1, 0.0)]
    trigger = [profile_yz("trigger", [(98, FRAME_BOTTOM + 1), (104, FRAME_BOTTOM + 1), (104.6, 78), (103.4, 70.5),
                                      (100.8, 67), (100.2, 75), (98.4, 81)], -3.2, 3.2)]
    return {"body": body, "cylinder": cylinder, "trigger": trigger}


def choose(normal, centre):
    """Which photo a face wears: the top, back or front where it faces them, the left side otherwise (the right
    mirrors it)."""
    ax = max(range(3), key=lambda k: abs(normal[k]))
    if ax == 2 and normal.z > 0:
        return "top"
    if ax == 1 and normal.y < 0 and centre.z >= 92:
        return "back"
    if ax == 1 and normal.y > 0 and centre.z >= 84:
        return "front"
    return "left"


def project_cylinder(ob):
    """UVs for the low cylinder: each side face turned (by whole chambers) to face left, then from the cylinder's
    own photo, so a flute or the land beside it is where the face is and a turn of a chamber changes nothing; the
    ends from the front and back photos."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    region = REGIONS["cylinder"]
    step = math.pi / 3
    for poly in me.polygons:
        n = poly.normal
        if abs(n.y) > 0.7:
            r = REGIONS["front" if n.y > 0 else "back"]
            for li in poly.loop_indices:
                uv.data[li].uv = kit.region_uv(r, me.vertices[me.loops[li].vertex_index].co / MM)
            continue
        # Its flute (at a whole number of chambers from the left) or its land (half a chamber on): turned to the
        # flute that faces left in the photo, or to the land just above it, so every land is lit alike.
        t0 = math.atan2(n.z, -n.x)
        whole = math.floor(t0 / step)
        m = t0 - whole * step
        turn = -(whole + (1 if m > 0.75 * step else 0)) * step
        for li in poly.loop_indices:
            v = me.vertices[me.loops[li].vertex_index].co / MM
            dx, dz = v.x, v.z - CYL_Z
            t = math.atan2(dz, -dx) + turn
            r = math.hypot(dx, dz)
            uv.data[li].uv = kit.region_uv(region, (-r * math.cos(t), v.y, CYL_Z + r * math.sin(t)))


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "python", LENGTH)
    frame = kit.hold_for(WEB, scale)
    scene = kit.studio()
    gun, cyl = detailed(photo_materials())
    both = [gun, cyl]
    rgba = kit.atlas(scene, REGIONS, {"left": both, "top": both, "back": both, "front": both, "cylinder": [cyl]})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "python-atlas.png")
    kit.save_png(atlas_png, flat)
    gun.hide_render = cyl.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            if part == "cylinder":
                project_cylinder(ob)
            else:
                kit.project(ob, REGIONS, choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING)
    print(f"python: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "python-preview.png"), [o for o, _ in objects], atlas_png,
                [((-0.40, 0.04, 0.18), (0, 0.15, 0.09), 50), ((0.36, 0.34, 0.24), (0, 0.14, 0.09), 50),
                 ((-0.15, -0.16, 0.19), (0.0, 0.30, 0.11), 35)])


main()
