"""Step 3: Tommy (his own arms, articulated) holding the low-poly Colt in first person.

The gun goes in his right hand where the game puts its Colt (the hand bone's own frame), scaled to the
game's Colt; his arm reaches it by IK from his shoulder; the camera is his eye.

blender -b -P v3_step3.py -- <out> <GameData> <player.png> <atlas.png> <background.jpg> [poses...]
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.dirname(__file__) + "/../tools")
import v2_colt_low
import v3_rig
import v3_surgery
import vcdff
import vcextract
from comp import composite
from lib import MM, look_at, render_engine

out, gd, tex, atlas, background = sys.argv[sys.argv.index("--") + 1:][:5]
os.makedirs(out, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
img = vcextract.Img(gd)
scene = bpy.context.scene
render_engine(scene)
scene.view_settings.view_transform = "Standard"
W, H = 1600, 900
scene.render.resolution_x, scene.render.resolution_y = W, H

arm, mesh, binds, g = v3_rig.load(img.read("player.dff"), tex)
v3_surgery.articulate(mesh, arm)

# Only his arms are drawn (his head is where the eye is).
ARM_GROUPS = {"b22", "b23", "b24", "b25"}
keep = {mesh.vertex_groups[n].index for n in ARM_GROUPS if n in mesh.vertex_groups}
keep |= {vg.index for vg in mesh.vertex_groups if vg.name.startswith("R_")}
bm = bmesh.new()
bm.from_mesh(mesh.data)
dl = bm.verts.layers.deform.verify()
gone = [f for f in bm.faces if not all(max(v[dl].items(), key=lambda kv: kv[1])[0] in keep for v in f.verts if len(v[dl]))]
bmesh.ops.delete(bm, geom=gone, context="FACES")
bm.to_mesh(mesh.data)
bm.free()

# The game's Colt, in his hand bone's frame: where its rear top, top and middle are.
b = img.read("colt45.dff")
frames, geoms = vcdff.parse(b)
vc = [Vector(v) for v in vcdff.geometry(b, *geoms[0])["verts"]]
x0, x1 = min(v.x for v in vc), max(v.x for v in vc)
z1 = max(v.z for v in vc)
yc = (min(v.y for v in vc) + max(v.y for v in vc)) / 2
scale = (x1 - x0) / 0.216 * 0.95
# Ours (x right, y forward, z up; metres) into the game's (x forward, z up): p_vc = A p.
GUN_FWD = float(os.environ.get("VM_GUN_FWD", "0.015"))
A = Matrix(((0, scale, 0, x0 + GUN_FWD), (-scale, 0, 0, yc), (0, 0, scale, z1 - 0.1355 * scale - 0.04), (0, 0, 0, 1)))  # grips lined up: ours lower and a little forward, as the game's longer grip sits in his fist
print("VC colt: x", round(x0, 3), round(x1, 3), "top", round(z1, 3), "y mid", round(yc, 3), "scale", round(scale, 3))

gun, _ = v2_colt_low.build(atlas)
gun.matrix_world = Matrix.Scale(scale, 4)  # our frame is the world's; scaled to the game's Colt

# The hand, so the gun sits in it as the game holds it: hand (game frame, world) = A^-1 (scaled).
hand_vc = Matrix.Scale(scale, 4) @ A.inverted() @ Matrix.Scale(scale, 4)
hand_vc = A.inverted_safe()
# A maps our gun (unscaled metres) to hand space; the gun in the world is S (scale); so hand = S A^-1.
hand_vc = Matrix.Scale(scale, 4) @ A.inverted_safe()
# Blender's bone axes for that frame (its y along the game's x).
def bone_frame(m):
    e1, e2, e3 = m.col[0].xyz.normalized(), m.col[1].xyz.normalized(), m.col[2].xyz.normalized()
    t = m.translation
    return Matrix(((-e2.x, e1.x, e3.x, t.x), (-e2.y, e1.y, e3.y, t.y), (-e2.z, e1.z, e3.z, t.z), (0, 0, 0, 1)))

# Tommy stands so his eye is the camera's, facing where it looks.
eye_world = Vector((-0.150, -0.195, 0.160)) * scale
yaw, pitch = math.radians(13), math.radians(-3)
ahead = Vector((math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), math.sin(pitch)))
head = binds[5].translation
eye_bind = head + Vector((0, 0.07, 0.085))  # bind: y up, z forward
# As a viewmodel: his shoulders behind the camera (CS attaches its arms there), so his arm reaches out nearly straight.
DOWN = float(os.environ.get("VM_DOWN", "0.04"))
REACH = float(os.environ.get("VM_REACH", "0.9"))  # of his arm's length, shoulder to wrist: a slight bend
def place(back):
    return Matrix.Translation(eye_world - ahead * back - Vector((0, 0, DOWN))) @ Matrix.Rotation(-yaw, 4, "Z") @ v3_rig.BIND_TO_WORLD @ Matrix.Translation(-eye_bind)
arm_len = (binds[23].translation - binds[22].translation).length + (binds[24].translation - binds[23].translation).length
wrist_target = (Matrix.Scale(scale, 4) @ A.inverted_safe()).translation
BACK = min((b / 200 for b in range(-40, 80)), key=lambda b: abs((place(b) @ binds[22].translation - wrist_target).length - REACH * arm_len))
print("viewmodel: shoulders", round(BACK, 3), "m behind the eye; arm", round(arm_len, 3), "m")
arm.matrix_world = place(BACK)
bpy.context.view_layer.update()

def target(name, m):
    e = bpy.data.objects.new(name, None)
    scene.collection.objects.link(e)
    e.matrix_world = m
    return e

# The bone's world matrix that moves his mesh (bind space) so the hand's bind frame lands on hand_vc:
# world = target @ rest^-1 @ v must equal hand_vc @ B^-1 @ v.
rest24 = arm.data.bones["b24"].matrix_local
# His arm set bone by bone (no IK: a viewmodel's arm only has to look right): the hand where the gun
# needs it; the forearm carrying on from it, angled down and out a little, so the wrist is near straight;
# the upper arm back past the camera. For bone k, the matrix that puts its bind frame F_k in the world
# moving his mesh is F_k @ B_k^-1 @ rest_k.
def frame(origin, along, up_hint):
    e1 = along.normalized()
    e3 = (up_hint - e1 * up_hint.dot(e1)).normalized()
    e2 = e3.cross(e1)
    return Matrix(((e1.x, e2.x, e3.x, origin.x), (e1.y, e2.y, e3.y, origin.y), (e1.z, e2.z, e3.z, origin.z), (0, 0, 0, 1)))

DOWN_DEG, OUT_DEG = float(os.environ.get("VM_ARM_DOWN", "28")), float(os.environ.get("VM_ARM_OUT", "12"))
wrist = hand_vc.translation
e1h, e3h = hand_vc.col[0].xyz, hand_vc.col[2].xyz
back = (-e1h + Vector((0, 0, -math.tan(math.radians(DOWN_DEG)))) + Vector((math.tan(math.radians(OUT_DEG)), 0, 0))).normalized()
L_fore = (binds[24].translation - binds[23].translation).length
L_upper = (binds[23].translation - binds[22].translation).length
elbow = wrist + back * L_fore
shoulder = elbow + (back + Vector((0.35, 0, 0.55))).normalized() * L_upper
F = {24: hand_vc, 23: frame(elbow, wrist - elbow, e3h), 22: frame(shoulder, elbow - shoulder, e3h)}
pb = arm.pose.bones
for k, Fk in F.items():
    t = target(f"bone{k}", Fk @ binds[k].inverted() @ arm.data.bones[f"b{k}"].matrix_local)
    c = pb[f"b{k}"].constraints.new("COPY_TRANSFORMS")
    c.target = t
rt = bpy.data.objects["bone24"]

# His fingers: each fingertip goes to an anchor the gun defines (its own frame, mm), resting on the surface
# a finger's half-thickness out from it; each finger's two joints are solved (IK) to reach it.
from colt import back_strap_y, front_strap_y
FINGER_R = float(os.environ.get("VM_FINGER_R", "8"))  # mm: half his finger's thickness (the game's are chunky)
PANEL_X = -14.8                                       # the left grip panel's face
PANEL_BACK = float(os.environ.get("VM_PANEL_BACK", "14"))  # mm behind the front strap
def on_panel(z, back=None):
    back = PANEL_BACK if back is None else back
    return Vector((PANEL_X - FINGER_R, front_strap_y(z) - back, z))
ANCHORS = {
    "index": Vector((-1.0, 93.3 + FINGER_R, 85.0)),   # the trigger's face, its pad pressing it from the front
    "middle": on_panel(60.0),
    "ring": on_panel(43.0),
    "pinky": on_panel(27.0, back=PANEL_BACK + 2.0),
    "thumb": Vector((-11.0 - FINGER_R, 62.0, 97.0)),  # along the frame's left side, under the slide
}
dots = []
for name, at in ANCHORS.items():
    e = target(f"anchor_{name}", Matrix.Translation(gun.matrix_world @ (at * MM)))
    tip = f"R_{name}_tip"
    c = pb[tip].constraints.new("IK")
    c.target, c.chain_count, c.use_tail = e, 2, True
    dots.append(e.matrix_world.translation.copy())
if os.environ.get("VM_SHOW_ANCHORS"):
    for d in dots:
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.003, location=d)
        bpy.context.active_object.data.materials.append(bpy.data.materials.new("dot"))
        bpy.context.active_object.active_material.diffuse_color = (1, 0, 0, 1)

# Left arm: not drawn in this one-handed check.
for f in mesh.data.polygons:
    pass

# Light and camera, as before.
world = bpy.data.worlds.new("w")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.20, 0.30, 0.28, 1)
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
sun.data.energy = 2.6
sun.data.color = (0.9, 1.0, 0.95)
sun.rotation_euler = (math.radians(40), 0, math.radians(240))
scene.collection.objects.link(sun)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
cam.data.clip_start = 0.01
scene.collection.objects.link(cam)
scene.camera = cam
bpy.context.view_layer.update()

centre = Vector((0, 0.06, 0.07)) * scale
if os.environ.get("VM_DEBUG"):
    dg = bpy.context.evaluated_depsgraph_get()
    arm_e = arm.evaluated_get(dg)
    hb = arm_e.matrix_world @ arm_e.pose.bones["b24"].matrix
    print("DBG target", [round(x, 3) for x in rt.matrix_world.translation], "bone", [round(x, 3) for x in hb.translation])
    print("DBG target y-axis", [round(x, 3) for x in rt.matrix_world.col[1][:3]], "bone", [round(x, 3) for x in hb.col[1][:3]])
    fa = arm_e.matrix_world @ arm_e.pose.bones["b23"].matrix
    print("DBG forearm head", [round(x, 3) for x in fa.translation], "tail", [round(x, 3) for x in (arm_e.matrix_world @ arm_e.pose.bones["b23"].tail)])
    me_e = mesh.evaluated_get(dg).to_mesh()
    gfing = {vg.index for vg in mesh.vertex_groups if vg.name.startswith("R_") and "thumb" not in vg.name}
    Sinv = Matrix.Scale(1 / scale, 4)
    for v, ve in list(zip(mesh.data.vertices, me_e.vertices)):
        if v.groups and max(v.groups, key=lambda gg: gg.weight).group in gfing:
            expect = A.inverted_safe() @ (binds[24].inverted() @ v.co)
            got = Sinv @ (mesh.matrix_world @ ve.co)
            print("DBG fist vert expect(mm)", [round(x * 1000) for x in expect], "got", [round(x * 1000) for x in got])
            break
    for bn in ("b24", "R_middle", "R_index", "b25"):
        pbe = arm_e.pose.bones[bn]
        rel = arm_e.pose.bones["b24"].matrix.inverted() @ pbe.matrix
        rest_rel = arm.data.bones["b24"].matrix_local.inverted() @ arm.data.bones[bn].matrix_local
        print("DBG", bn, "parent", pbe.parent.name if pbe.parent else None, "pose rel to hand", [round(x, 3) for x in rel.translation], "rest rel", [round(x, 3) for x in rest_rel.translation])
    pv = next(v for v in mesh.data.vertices if v.groups and mesh.vertex_groups[max(v.groups, key=lambda gg: gg.weight).group].name == "b24")
    expect = A.inverted_safe() @ (binds[24].inverted() @ pv.co)
    got = Sinv @ (mesh.matrix_world @ me_e.vertices[pv.index].co)
    print("DBG palm vert expect(mm)", [round(x * 1000) for x in expect], "got", [round(x * 1000) for x in got], "groups", [(mesh.vertex_groups[gg.group].name, round(gg.weight, 2)) for gg in pv.groups])
    print("DBG mesh parent inverse identity:", mesh.matrix_parent_inverse == Matrix.Identity(4), "mods", [m.type for m in mesh.modifiers])
    print("DBG constraints b24", [(c.type, c.is_valid, c.influence) for c in arm.pose.bones["b24"].constraints], "b23", [(c.type, c.is_valid) for c in arm.pose.bones["b23"].constraints])
    sys.exit(0)
views = {
    "fp": (eye_world, eye_world + ahead, None),
    "left": (centre + Vector((-0.45, 0.0, 0.05)), centre, 45),
    "right": (centre + Vector((0.45, -0.05, 0.05)), centre, 45),
    "front": (centre + Vector((0.05, 0.5, 0.05)), centre, 45),
    "high": (centre + Vector((-0.2, -0.45, 0.5)), centre + Vector((0, -0.1, 0)), 30),
    "close_left": (Vector((-0.30, 0.04, 0.04)), Vector((0, 0.04, 0.04)), 60),
    "close_right": (Vector((0.30, 0.04, 0.04)), Vector((0, 0.04, 0.04)), 60),
    "below": (Vector((0.05, 0.06, -0.30)), Vector((0, 0.04, 0.04)), 60),
    "trigger": (Vector((-0.28, 0.10, 0.09)), Vector((0, 0.08, 0.08)), 55),
    "under_left": (Vector((-0.22, 0.12, -0.12)), Vector((0, 0.06, 0.06)), 50),
}
only = set(os.environ.get("VM_VIEWS", "").split(",")) - {""}
views = {k: v for k, v in views.items() if not only or k in only}
for name, (pos, tgt, lens) in views.items():
    cam.location = pos
    look_at(cam, tgt)
    if lens:
        cam.data.lens = lens
        scene.render.film_transparent = False
    else:
        cam.data.sensor_fit = "VERTICAL"
        cam.data.angle = math.radians(62)
        scene.render.film_transparent = True
    path = os.path.join(out, f"{name}.png")
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    if not lens:
        composite(path, background, os.path.join(out, f"{name}_scene.png"))
