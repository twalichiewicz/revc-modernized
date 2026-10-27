"""Vice City's rocket launcher (an RPG-7 with a PG-7V grenade loaded: a steel tube with its flared blast cone, a
wooden heat shield, iron sights, two wooden grips and the olive warhead) for the first-person viewmodel. Scoped
in the game (it draws its own view when aiming), so only the hip view matters: on his right shoulder, the tube
running back past the eye's right, his right hand on the pistol grip, his left on the forward grip.

Vice City's own model puts the trigger at its forward grip; this one has it at the rear grip, under his right
hand, with his left on the forward one, as a shoulder-fired tube is held.

blender -b -P tools/guns/rocketla.py -- <GameData> <out.vmg> <work dir>

Millimetres, y forward along the bore, z up, x to the right; the blast cone's mouth at y = 0, the bore at z = 0.
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(__file__))
import kit
from lib import MM, _object, bevel, box, cylinder_x, cylinder_y, fillet, finish, join, material, profile_yz

LENGTH = 1335.0  # the 950 mm launcher and the grenade's 385 mm out of its muzzle
SPLIT = 705.0  # the side and top photos are in two halves, behind and ahead of this (y): the chamber's front
# The launcher's surface of revolution (y, radius), in its pieces: the blast cone and rear tube, the wooden heat
# shield, the steel chamber over the grips, the front tube; the grenade.
CONE = [(0, 48.0), (6, 47.0), (150, 26.0), (205, 26.0)]
SHIELD = [(205, 28.0), (400, 37.0), (559, 37.0)]
CHAMBER = [(559, 37.0), (697, 37.0), (705, 28.0)]
TUBE = [(705, 26.5), (950, 26.5)]
GRENADE = [(950, 21.0), (985, 21.0), (1000, 27.0), (1085, 42.5), (1125, 42.5), (1240, 22.0), (1290, 11.0),
           (1298, 8.0), (1326, 7.0), (1335, 4.5)]
GRENADE_LOW = [GRENADE[i] for i in (0, 1, 2, 3, 4, 5, 6, 9)]
TRIGGER_FACE = 640.0


def grip_front_y(z):
    """The pistol grip's front edge, behind the guard, raked back about 7 degrees."""
    return 606.0 + (z + 60.0) * 12.0 / 92.0


def grip_back_y(z):
    return 560.0 + (z + 48.0) * 12.0 / 102.0


# Outlines (y, z): the trigger housing under the chamber, its guard and trigger; the pistol grip; the forward grip's
# clamp and the grip under it; the sights.
HOUSING = [(576, -30), (690, -30), (690, -44), (676, -58), (600, -60), (580, -50)]
GUARD = [(614, -56), (614, -80), (622, -88), (668, -88), (676, -80), (678, -56), (672, -56), (670, -76), (664, -82),
         (628, -82), (620, -76), (620, -56)]
TRIGGER = [(634, -50), (TRIGGER_FACE, -50), (TRIGGER_FACE + 2.0, -68), (TRIGGER_FACE - 1.0, -76), (634, -75),
           (636, -62)]
GRIP = [(grip_back_y(-40), -40), (604, -40), (grip_front_y(-60), -60), (grip_front_y(-150), -150),
        (grip_front_y(-150) - 4, -156), (grip_back_y(-150) + 4, -156), (grip_back_y(-150), -150)]
FORE_CLAMP = [(728, -20), (800, -20), (800, -50), (790, -62), (738, -62), (728, -50)]
FORE_GRIP = [(752, -56), (790, -56), (787, -158), (783, -162), (759, -162), (755, -158)]
HALF = {"housing": 13.0, "grip": 15.0, "clamp": 16.0, "fore_grip": 13.0}

# The atlas: (x0, y0, w, h) pixels from the top left, the camera's position, look and up (mm, gun frame), mm a pixel.
# The side and top in two halves each, then the back (the blast cone's mouth) and the front (the warhead's nose).
SIDE_MM = 2.80
FRONT_MM = 2.6
REGIONS = {
    "left": ((0, 0, 256, 80), (-400, 350, -54), (1, 0, 0), (0, 0, 1), SIDE_MM),
    "left_front": ((0, 80, 256, 80), (-400, 1030, -60), (1, 0, 0), (0, 0, 1), FRONT_MM),
    "top": ((0, 160, 256, 36), (0, 350, 400), (0, 0, -1), (-1, 0, 0), SIDE_MM),
    "top_front": ((0, 196, 256, 34), (0, 1030, 400), (0, 0, -1), (-1, 0, 0), FRONT_MM),
    "back": ((0, 230, 34, 26), (0, -400, 0), (0, 1, 0), (0, 0, 1), 4.0),
    "front": ((34, 230, 34, 26), (0, 1800, -5), (0, -1, 0), (0, 0, 1), 3.4),
}

# Where his fingertips go (a pad's middle, a finger's half-thickness off the surface). Right hand: the index on the
# trigger's face, the others on the pistol grip's left side behind its front edge, the thumb along the housing.
# Left hand: round the forward grip from its left, the fingers' tips on its right side, the thumb up its left.
FINGER_R = 8.0
GRIP_BACK = 30.0  # mm behind the grip's front edge


def on_grip(z, back=GRIP_BACK):
    return (-HALF["grip"] - FINGER_R, grip_front_y(z) - back, z)


WEB = (0.0, 560.0, -60.0)  # the web of his right hand: the top of the pistol grip's back, under the tube
HAND_YAW, HAND_PITCH = 6.0, 0.0
PALM = (-HALF["fore_grip"], 750.0, -86.0)  # his left palm, on the forward grip's left side
LEFT_PITCH, LEFT_YAW = 5.0, 30.0
FORE_RIGHT = HALF["fore_grip"] + FINGER_R
ROUND = (0.0, 1.0, 0.0)  # a left finger's middle joint: out round the forward grip's front
ALONG_FORE = (0.0, -14.0, 0.0)  # slack: back along its right side (longer fingers)
ALONG_GRIP = (0.0, 16.0, 0.0)
ANCHORS = [  # name, position (mm), part, which way its middle joint goes out, slack (mm)
    ("r_index", (-1.0, TRIGGER_FACE + FINGER_R - 2.0, -64.0), "trigger", (1.0, 0.6, 0.0), None),
    ("r_middle", on_grip(-78.0), None, None, ALONG_GRIP),
    ("r_ring", on_grip(-98.0), None, None, ALONG_GRIP),
    ("r_pinky", on_grip(-118.0, GRIP_BACK + 2.0), None, None, ALONG_GRIP),
    ("r_thumb", (-HALF["housing"] - FINGER_R - 3.0, 604.0, -48.0), None, None, (0.0, -14.0, 0.0)),
    ("l_index", (FORE_RIGHT, 774.0, -70.0), None, ROUND, ALONG_FORE),
    ("l_middle", (FORE_RIGHT, 772.0, -92.0), None, ROUND, ALONG_FORE),
    ("l_ring", (FORE_RIGHT, 770.0, -114.0), None, ROUND, ALONG_FORE),
    ("l_pinky", (FORE_RIGHT, 768.0, -134.0), None, ROUND, ALONG_FORE),
    ("l_thumb", (-HALF["clamp"] - FINGER_R, 790.0, -52.0), None, None, (0.0, -12.0, 0.0)),
]
PARTS = [  # name, pivot (mm), the way it moves
    ("body", (0, 0, 0), (0, 0, 0)),
    ("trigger", (0, 0, 0), (0, -1, 0)),
]
MUZZLE = (0, 950.0, 0)  # the launcher's muzzle (the grenade leaves from here)
SIGHT_REAR, SIGHT_FRONT = (0, 640, 52), (0, 905, 44)  # the iron sights' tops
# Seen from: the eye left of the tube and over it, a third of the way along (real metres), so the tube runs back past
# its right; looking along the bore (levelled: it points where he aims). The aim values are the Colt's (the game
# draws its own view).
FRAMING = ((-0.155, 0.351, 0.071), 0.0, 0.0, 0.22, 0.02)  # levelled: its barrel where he aims (vcmm-viewmodel --level)
LEFT_FOREARM = (-0.574, -0.748, -0.334)  # kit's, turned with the levelled framing: runs where it did in the view


def lathe_y(name, stations, segments, mat=None, x=0.0, z=0.0, cap=(True, True)):
    """A surface of revolution about the line through (x, z) along y: stations [(y, radius)], mm."""
    bm = bmesh.new()
    rings = []
    for y, r in stations:
        rings.append([bm.verts.new(((x + r * math.cos(2 * math.pi * k / segments)) * MM, y * MM,
                                    (z + r * math.sin(2 * math.pi * k / segments)) * MM)) for k in range(segments)])
    for a, b in zip(rings, rings[1:]):
        for k in range(segments):
            j = (k + 1) % segments
            bm.faces.new((a[k], a[j], b[j], b[k]))
    if cap[0]:
        bm.faces.new(list(reversed(rings[0])))
    if cap[1]:
        bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def piece(name, outline, half, mat=None, chamfer=0.0, segments=3):
    """A detailed slab: its side outline `half` mm either side of the middle, its edges rounded."""
    ob = profile_yz(name, outline, -half, half, mat)
    if chamfer:
        bevel(ob, chamfer, segments, 30)
    return ob


def inset(outline, d):
    """A closed outline (y, z) moved `d` mm inwards, its corners mitred."""
    n = len(outline)
    area = sum(outline[i][0] * outline[(i + 1) % n][1] - outline[(i + 1) % n][0] * outline[i][1] for i in range(n))
    side = 1.0 if area > 0 else -1.0  # counter-clockwise: inwards is to the left of each edge
    out = []
    for i in range(n):
        p, a, b = Vector(outline[i]), Vector(outline[i - 1]), Vector(outline[(i + 1) % n])
        e0, e1 = (p - a).normalized(), (b - p).normalized()
        n0, n1 = Vector((-e0.y, e0.x)) * side, Vector((-e1.y, e1.x)) * side
        m = n0 + n1
        k = d / max(0.35, m.length / 2) if m.length > 1e-6 else d
        out.append(tuple(p + m.normalized() * k * (1.0 if m.length > 1e-6 else 0.0)))
    return out


def slab(name, outline, half, chamfer):
    """A low-poly slab: its side outline either side of the middle, its long edges chamfered (`chamfer` mm)."""
    rings = [(inset(outline, chamfer), -half), (outline, -half + chamfer), (outline, half - chamfer),
             (inset(outline, chamfer), half)]
    bm = bmesh.new()
    vs = [[bm.verts.new((x * MM, y * MM, z * MM)) for y, z in ring] for ring, x in rings]
    n = len(outline)
    for a, b in zip(vs, vs[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(vs[0])
    bm.faces.new(list(reversed(vs[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm)


def hold_turned(web_mm, scale, yaw_deg, pitch_deg):
    """kit.hold_for with his hand turned about the web point: `yaw_deg` its knuckles towards the gun's left (his
    wrist out to the right), `pitch_deg` its knuckles up."""
    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    turn = Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(pitch, 3, "X")
    hx, hy, hz = turn @ Vector((0, 1, 0)), turn @ Vector((-1, 0, 0)), turn @ Vector((0, 0, 1))
    axes = [Vector((hx[i], hy[i], hz[i])) for i in range(3)]  # gun x, y, z in the hand's frame
    w = Vector(web_mm) * MM * scale
    origin = Vector(kit.WEB_IN_HAND) - (axes[0] * w.x + axes[1] * w.y + axes[2] * w.z)
    return [tuple(a) for a in axes] + [tuple(origin)]


def hold_left_upright(palm_mm, scale, pitch_deg, yaw_deg):
    """kit.hold_left_for for an upright grip: his left hand round it from its left, as his right holds a pistol
    grip: fingers forward round its front (`pitch_deg` up, `yaw_deg` towards the right), palm on its left side
    facing right, thumb up. The hand's frame turned from kit.hold_left_for's (fingers right, palm up, thumb
    forward), so its handedness is the same."""
    turn = Matrix.Rotation(math.radians(-yaw_deg), 3, "Z") @ Matrix.Rotation(math.radians(pitch_deg), 3, "X")
    hx, hy = turn @ Vector((0, 1, 0)), turn @ Vector((1, 0, 0))
    hz = hx.cross(hy)
    palm = Vector(palm_mm) * MM * scale
    wrist = palm - hx * kit.PALM_IN_HAND[0] - hy * kit.PALM_IN_HAND[1] - hz * kit.PALM_IN_HAND[2]
    rows = (hx, hy, hz)
    axes = [tuple(row[i] for row in rows) for i in range(3)]  # gun x, y, z in hand coordinates
    origin = tuple(-row.dot(wrist) for row in rows)
    return axes + [origin]


def grain(m, scale, dark, light, stretch=(90.0, 6.0, 90.0)):
    """Streaks along y through a material's colour (wood grain)."""
    nt = m.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = stretch
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = scale
    noise.inputs["Detail"].default_value = 6.0
    noise.inputs["Distortion"].default_value = 1.5
    nt.links.new(mp.outputs["Vector"], noise.inputs["Vector"])
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.35, (*dark, 1)
    els[1].position, els[1].color = 0.65, (*light, 1)
    nt.links.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])
    return m


def materials():
    """Vice City's RPG: bright worn steel, orange wooden heat shield, brown grips, an olive warhead."""
    m = kit.photo_materials()
    m["gunmetal"] = material("gunmetal_photo", (0.30, 0.33, 0.35), metallic=1.0, roughness=0.28)
    m["shield"] = grain(material("shield_photo", (0.45, 0.17, 0.05), roughness=0.45, spec=0.35), 1.0,
                        (0.30, 0.10, 0.03), (0.60, 0.25, 0.08))
    m["wood"] = grain(material("grip_wood_photo", (0.22, 0.09, 0.04), roughness=0.55, spec=0.3), 1.0,
                      (0.12, 0.045, 0.02), (0.30, 0.13, 0.06), (60.0, 60.0, 8.0))
    m["olive"] = material("olive_photo", (0.10, 0.11, 0.05), metallic=0.4, roughness=0.45)
    m["pad"] = material("soot_photo", (0.015, 0.015, 0.015), roughness=0.9, spec=0.1)
    return m


def detailed(mats):
    """The photo shoot's RPG-7."""
    steel, dark, metal = mats["steel"], mats["dark"], mats["gunmetal"]
    parts = [lathe_y("cone", CONE, 40, metal), lathe_y("shield", SHIELD, 40, mats["shield"]),
             lathe_y("chamber", CHAMBER, 40, metal), lathe_y("tube", TUBE, 40, metal),
             lathe_y("grenade", GRENADE[:-4], 40, mats["olive"]), lathe_y("fuse", GRENADE[-5:], 24, steel),
             lathe_y("motor_band", [(950, 22.0), (962, 22.0)], 24, dark),
             cylinder_y("cone_mouth", 0, 0, 44.0, 0.5, 1.5, 40, mats["pad"])]
    for y0 in (300.0, 380.0, 470.0):  # the heat shield's clamps
        r = SHIELD[0][1] + (SHIELD[1][1] - SHIELD[0][1]) * min(1.0, (y0 - 205.0) / 195.0) + 1.0
        parts.append(lathe_y("band", [(y0, r), (y0 + 9, r)], 40, metal))
    parts.append(box("band_block", -5, 5, 296, 316, 30, 40, dark))
    parts.append(box("band_block", -5, 5, 376, 396, 34, 44, dark))
    parts.append(piece("rear_sight", fillet([(604, 30), (676, 30), (676, 46), (664, 52), (616, 52), (604, 44)],
                                            [0, 0, 2, 2, 2, 2], 2), 11.0, dark, 1.0, 2))
    parts.append(box("sight_leaf", -6, 6, 636, 644, 50, 60, dark))
    parts.append(piece("front_sight", [(886, 22), (924, 22), (924, 38), (916, 44), (894, 44), (886, 36)], 7.0, dark,
                       1.0, 2))
    parts.append(box("sight_post", -1.5, 1.5, 902, 908, 42, 50, dark))
    parts.append(cylinder_x("bracket_screw", 650, 0, 6.0, -40, -36, 16, steel))  # where an optic's bracket goes
    parts.append(box("bracket", -38, -36, 600, 690, -16, 16, metal))
    parts.append(piece("housing", HOUSING, HALF["housing"], steel, 1.5, 2))
    parts.append(cylinder_x("hammer_pin", 590, -42, 4.0, -HALF["housing"] - 1, HALF["housing"] + 1, 12, dark))
    guard = profile_yz("guard", fillet(GUARD, [0, 6, 6, 6, 6, 0, 0, 5, 5, 5, 4, 0], 3), -3.0, 3.0, steel)
    bevel(guard, 0.6)
    parts.append(guard)
    parts.append(profile_yz("trigger", fillet(TRIGGER, [0, 0, 2, 2, 0, 0], 2), -3.0, 3.0, dark))
    parts.append(piece("grip", fillet(GRIP, [3, 3, 4, 3, 3, 3, 3], 3), HALF["grip"], mats["wood"], 4.0, 3))
    parts.append(piece("fore_clamp", FORE_CLAMP, HALF["clamp"], steel, 1.5, 2))
    parts.append(piece("fore_grip", fillet(FORE_GRIP, [0, 0, 3, 3, 3, 3], 3), HALF["fore_grip"], mats["wood"], 4.0, 3))
    for sx in (-1, 1):
        parts.append(cylinder_x("clamp_screw", 764, -40, 4.0, sx * (HALF["clamp"] - 1), sx * (HALF["clamp"] + 1.2), 12,
                                dark))
    for p in parts:
        finish(p, 35)
    return join("rocketla", parts)


def low():
    """The RPG-7 at Vice City's detail, in its moving parts: {part: [objects]}."""
    body = [lathe_y("cone", CONE, 10), lathe_y("shield", SHIELD, 10), lathe_y("chamber", CHAMBER, 10),
            lathe_y("tube", TUBE, 8), lathe_y("grenade", GRENADE_LOW, 8),
            box("band_block", -5, 5, 296, 316, 26, 40), box("band_block", -5, 5, 376, 396, 32, 44),
            profile_yz("rear_sight", [(604, 30), (676, 30), (676, 46), (664, 52), (616, 52), (604, 44)], -11, 11),
            profile_yz("front_sight", [(886, 20), (924, 20), (924, 38), (916, 44), (894, 44), (886, 36)], -7, 7),
            profile_yz("housing", HOUSING, -HALF["housing"], HALF["housing"]),
            profile_yz("guard", GUARD, -3.0, 3.0),
            slab("grip", GRIP, HALF["grip"], 4.0),
            profile_yz("fore_clamp", FORE_CLAMP, -HALF["clamp"], HALF["clamp"]),
            slab("fore_grip", FORE_GRIP, HALF["fore_grip"], 4.0)]
    trigger = [profile_yz("trigger", TRIGGER, -3.0, 3.0)]
    return {"body": body, "trigger": trigger}


def seen_by(objects):
    """Whether a low-poly face shows in a region's photo of `objects` (the photographed model): nothing of them
    between it and the camera but their own surface where it is (within 6 mm)."""
    dg = bpy.context.evaluated_depsgraph_get()
    trees = [BVHTree.FromObject(o, dg) for o in objects]

    def seen(region, normal, centre):
        d = -Vector(region[2]).normalized()
        start = centre * MM + Vector(normal) * 0.0005
        for tree in trees:
            o, gone = Vector(start), 0.0
            for _ in range(8):
                hit = tree.ray_cast(o, d, 2.0)
                if hit[0] is None:
                    break
                gone += hit[3]
                if gone > 0.006:
                    return False
                o = hit[0] + d * 0.0002
                gone += 0.0002
        return True
    return seen


def covers(region, points_mm):
    """Whether a region's photo takes in all these points (mm)."""
    (x0, y0, w, h) = region[0]
    for p in points_mm:
        u, v = kit.region_uv(region, p)
        px, py = u * kit.SIZE, (1 - v) * kit.SIZE
        if not (x0 - 0.5 <= px <= x0 + w + 0.5 and y0 - 0.5 <= py <= y0 + h + 0.5):
            return False
    return True


def project(ob, choose):
    """kit.project, with each face's photo chosen knowing its corners: one that doesn't take them all in gives way
    to the side photo."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        corners = [me.vertices[i].co / MM for i in poly.vertices]
        centre = sum(corners, Vector()) / len(corners)
        name = choose(poly.normal, centre)
        if not covers(REGIONS[name], corners):
            name = "left_front" if centre.y > SPLIT else "left"
        for li in poly.loop_indices:
            uv.data[li].uv = kit.region_uv(REGIONS[name], me.vertices[me.loops[li].vertex_index].co / MM)


def chooser(seen):
    """Which photo a face wears: the top, back or front where it faces them and they see it, the left side otherwise
    (the right mirrors it); the front half's own photos ahead of SPLIT."""
    def choose(normal, centre):
        ax = max(range(3), key=lambda k: abs(normal[k]))
        side = "left_front" if centre.y > SPLIT else "left"
        if ax == 2 and normal.z > 0:
            pick = "top_front" if centre.y > SPLIT else "top"
        elif ax == 1 and normal.y < 0:
            pick = "back"
        elif ax == 1 and normal.y > 0:
            pick = "front"
        else:
            return side
        return pick if seen(REGIONS[pick], normal, centre) else side
    return choose


def main():
    game_data, out, work = sys.argv[sys.argv.index("--") + 1:][:3]
    os.makedirs(work, exist_ok=True)
    scale = kit.scale_for(game_data, "rocketla", LENGTH)
    frame = hold_turned(WEB, scale, HAND_YAW, HAND_PITCH)
    hold_left = hold_left_upright(PALM, scale, LEFT_PITCH, LEFT_YAW)
    scene = kit.studio()
    gun = detailed(materials())
    rgba = kit.atlas(scene, REGIONS, {name: [gun] for name in REGIONS})
    palette, pixels = kit.quantize(rgba)
    flat = palette[pixels].reshape(kit.SIZE, kit.SIZE, 4).astype("float32") / 255.0
    atlas_png = os.path.join(work, "rocketla-atlas.png")
    kit.save_png(atlas_png, flat)
    choose = chooser(seen_by([gun]))
    gun.hide_render = True
    pieces = low()
    objects = []
    names = [p[0] for p in PARTS]
    for part, obs in pieces.items():
        for ob in obs:
            project(ob, choose)
            finish(ob, 35)
            objects.append((ob, names.index(part)))
    anchors = [(n, p, None if part is None else names.index(part), bend, slack) for n, p, part, bend, slack in ANCHORS]
    count = kit.write_vmg(out, scale, frame, MUZZLE, SIGHT_REAR, SIGHT_FRONT, PARTS, anchors, objects, palette, pixels,
                          FRAMING, two_handed=True, hold_left=hold_left, left_forearm=LEFT_FOREARM)
    print(f"rocketla: {count[1]} triangles, {count[0]} vertices, scale {scale:.4f}, hold origin {[round(c, 4) for c in frame[3]]}")
    kit.preview(os.path.join(work, "rocketla-preview.png"), [o for o, _ in objects], atlas_png,
                [((-2.0, 0.67, 0.0), (0, 0.67, -0.03), 50), ((0.9, 1.3, 0.45), (0, 0.6, 0.0), 35),
                 ((-0.15, 0.25, 0.08), (0.0, 0.9, 0.0), 30)])


main()
