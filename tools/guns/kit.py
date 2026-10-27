"""The viewmodel guns' common build: a detailed model photographed into a 256x256 atlas (side, top, back and
front views, the way Vice City's own guns wear side-view photos), a low-poly model wearing it by projection,
and the .vmg file the mod loads (src/vmgun.h has the format). Runs inside Blender.

Coordinates: the gun's frame, x right, y forward along the barrel, z up, millimetres in the scripts (lib.MM to
metres); the asset is written in metres scaled to the game's own model of the gun.
"""
import math
import os
import struct

import bpy
import numpy as np
from mathutils import Matrix, Vector

from lib import MM, material

SIZE = 256
GREY = 0.35  # what's between the views: mid grey steel


def studio():
    """An empty scene lit as a photo studio: bright overhead, a dark floor, a light band at the horizon."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.samples = 96
    scene.cycles.use_denoising = True
    scene.view_settings.view_transform = "Standard"
    scene.render.film_transparent = True
    world = bpy.data.worlds.new("studio")
    scene.world = world
    world.use_nodes = True
    wn = world.node_tree
    tc = wn.nodes.new("ShaderNodeTexCoord")
    sep = wn.nodes.new("ShaderNodeSeparateXYZ")
    wn.links.new(tc.outputs["Generated"], sep.inputs[0])
    ramp = wn.nodes.new("ShaderNodeValToRGB")
    els = ramp.color_ramp.elements
    els[0].position, els[0].color = 0.45, (0.02, 0.02, 0.02, 1)
    els[1].position, els[1].color = 1.0, (0.75, 0.75, 0.74, 1)
    e = els.new(0.50)
    e.color = (0.22, 0.22, 0.22, 1)
    e = els.new(0.62)
    e.color = (0.30, 0.30, 0.30, 1)
    mr = wn.nodes.new("ShaderNodeMapRange")
    mr.inputs["From Min"].default_value, mr.inputs["From Max"].default_value = -1.0, 1.0
    wn.links.new(sep.outputs["Z"], mr.inputs["Value"])
    wn.links.new(mr.outputs["Result"], ramp.inputs["Fac"])
    wn.links.new(ramp.outputs["Color"], wn.nodes["Background"].inputs[0])
    fill = bpy.data.objects.new("fill", bpy.data.lights.new("fill", "SUN"))
    fill.data.energy = 0.8
    fill.rotation_euler = (math.radians(60), 0, math.radians(-30))
    scene.collection.objects.link(fill)
    return scene


def photo_materials():
    """Steel, blued steel and a chequered black grip, as photographed."""
    steel = material("stainless_photo", (0.80, 0.80, 0.78), metallic=1.0, roughness=0.2)
    dark = material("blued_photo", (0.04, 0.045, 0.05), metallic=0.9, roughness=0.3)
    grip = material("grip_photo", (0.015, 0.014, 0.013), roughness=0.75, spec=0.25)
    nt = grip.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Rotation"].default_value = (0.785, 0, 0)
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    checker = nt.nodes.new("ShaderNodeTexChecker")
    checker.inputs["Scale"].default_value = 420.0
    nt.links.new(mp.outputs["Vector"], checker.inputs["Vector"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    nt.links.new(checker.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    return {"steel": steel, "dark": dark, "grip": grip}


def _camera_axes(look, up):
    z = -Vector(look).normalized()
    y = Vector(up).normalized()
    y = (y - z * y.dot(z)).normalized()
    return y.cross(z), y, z


def shoot(scene, region, visible):
    """An orthographic photo of `visible` (objects) for an atlas region; h x w x 4 floats, top row first."""
    (x0, y0, w, h), pos, look, up, mmpp = region
    hidden = [o for o in scene.objects if o.type == "MESH" and o not in visible]
    for o in hidden:
        o.hide_render = True
    cam = bpy.data.objects.new("ortho", bpy.data.cameras.new("ortho"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(w, h) * mmpp * MM
    cam.data.clip_start, cam.data.clip_end = 0.001, 2.0
    scene.collection.objects.link(cam)
    x, y, z = _camera_axes(look, up)
    p = Vector(pos) * MM
    cam.matrix_world = Matrix(((x.x, y.x, z.x, p.x), (x.y, y.y, z.y, p.y), (x.z, y.z, z.z, p.z), (0, 0, 0, 1)))
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = w * 2, h * 2  # supersampled, then halved
    path = os.path.join(bpy.app.tempdir, "view.png")
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h * 2, w * 2, 4)[::-1]
    bpy.data.images.remove(img)
    bpy.data.objects.remove(cam)
    for o in hidden:
        o.hide_render = False
    return px.reshape(h, 2, w, 2, 4).mean(axis=(1, 3))


def atlas(scene, regions, subjects):
    """The atlas: each region's photo of its subjects (objects) over mid grey; SIZE x SIZE x 4, top row first."""
    out = np.zeros((SIZE, SIZE, 4), np.float32)
    out[..., :3] = GREY
    out[..., 3] = 1
    for name, region in regions.items():
        (x0, y0, w, h) = region[0]
        px = shoot(scene, region, subjects[name])
        a = px[..., 3:4]
        out[y0:y0 + h, x0:x0 + w, :3] = px[..., :3] * a + out[y0:y0 + h, x0:x0 + w, :3] * (1 - a)
    return out


def region_uv(region, v_mm):
    """Where point v (mm) falls in a region, as a Blender UV (v up)."""
    (x0, y0, w, h), pos, look, up, mmpp = region
    x, y, z = _camera_axes(look, up)
    d = Vector(v_mm) - Vector(pos)
    px = x0 + w / 2 + d.dot(x) / mmpp
    py = y0 + h / 2 - d.dot(y) / mmpp
    return px / SIZE, 1 - py / SIZE


def project(ob, regions, choose):
    """UVs for a low-poly object: each face from the region `choose(normal, centre_mm)` names."""
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        centre = sum((me.vertices[i].co for i in poly.vertices), Vector()) / len(poly.vertices) / MM
        region = regions[choose(poly.normal, centre)]
        for li in poly.loop_indices:
            uv.data[li].uv = region_uv(region, me.vertices[me.loops[li].vertex_index].co / MM)


def quantize(rgba, colours=256, rounds=12):
    """A palette of `colours` (k-means over the atlas's colours) and each pixel's index into it."""
    px = np.clip(rgba[..., :3].reshape(-1, 3) * 255.0 + 0.5, 0, 255).astype(np.float32)
    unique, inverse, counts = np.unique(px.astype(np.uint8), axis=0, return_inverse=True, return_counts=True)
    unique = unique.astype(np.float32)
    rng = np.random.default_rng(1)
    k = min(colours, len(unique))
    centres = unique[rng.choice(len(unique), k, replace=False, p=counts / counts.sum())]
    for _ in range(rounds):
        d = (unique ** 2).sum(1)[:, None] - 2 * unique @ centres.T + (centres ** 2).sum(1)[None, :]
        nearest = d.argmin(1)
        for c in range(k):
            m = nearest == c
            if m.any():
                centres[c] = (unique[m] * counts[m, None]).sum(0) / counts[m].sum()
    d = (unique ** 2).sum(1)[:, None] - 2 * unique @ centres.T + (centres ** 2).sum(1)[None, :]
    nearest = d.argmin(1)
    palette = np.zeros((colours, 4), np.uint8)
    palette[:k, :3] = np.clip(centres + 0.5, 0, 255).astype(np.uint8)
    palette[:, 3] = 255
    return palette, nearest[inverse.reshape(-1)].astype(np.uint8)


def save_png(path, rgba):
    img = bpy.data.images.new("png", rgba.shape[1], rgba.shape[0], alpha=True)
    img.pixels[:] = rgba[::-1].ravel()
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)


def _mesh_triangles(ob):
    """An object's triangles as (position, normal, uv) corners, in metres and Blender's UVs."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = ev.to_mesh()
    me.calc_loop_triangles()
    uv = me.uv_layers.active.data
    normals = me.corner_normals
    world = ob.matrix_world
    rot = world.to_3x3()
    out = []
    for lt in me.loop_triangles:
        corners = []
        for li in lt.loops:
            v = me.vertices[me.loops[li].vertex_index].co
            corners.append((world @ v, (rot @ normals[li].vector).normalized(), tuple(uv[li].uv)))
        out.append(corners)
    ev.to_mesh_clear()
    return out


def write_vmg(path, scale, hold, muzzle, sight_rear, sight_front, parts, anchors, objects, palette, pixels,
              framing, two_handed=False, hold_left=None, left_forearm=None):
    """The asset. `parts`: [(name, pivot_mm, axis)]; `anchors`: [(name, pos_mm, part index or None, bend direction
    or None, slack_mm or None)];
    `objects`: [(object, part index)]; `hold`: the gun frame (scaled metres) in the hand's, as x, y, z
    axes and an origin (4 x 3); `hold_left` likewise in his left hand's (two-handed guns); `left_forearm`: the
    way his left forearm runs back from the wrist, in the gun frame; `framing`: ((hip eye x, y, z in real
    metres), hip yaw, hip pitch, aim back, aim above)."""
    s = scale * MM
    vertices, index, triangles = [], {}, []
    for ob, part in objects:
        for tri in _mesh_triangles(ob):
            ids = []
            for p, n, (u, v) in tri:
                key = (round(p.x, 6), round(p.y, 6), round(p.z, 6), round(n.x, 3), round(n.y, 3), round(n.z, 3),
                       round(u, 5), round(v, 5), part)
                if key not in index:
                    index[key] = len(vertices)
                    vertices.append((p * scale, n, u, 1 - v, part))  # the game's v runs down the texture
                ids.append(index[key])
            triangles.append(ids)
    if len(vertices) > 65535:
        raise SystemExit("too many vertices for 16-bit indices")
    b = bytearray()
    b += b"VMG2"
    b += struct.pack("<4I", len(vertices), len(triangles), len(parts), len(anchors))
    b += struct.pack("<f", scale)
    for axis in hold:
        b += struct.pack("<3f", *axis)
    for p in (muzzle, sight_rear, sight_front):
        b += struct.pack("<3f", *(Vector(p) * s))
    b += struct.pack("<I", 1 if two_handed else 0)
    for axis in hold_left or [(1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0)]:
        b += struct.pack("<3f", *axis)
    b += struct.pack("<3f", *(Vector(left_forearm).normalized() if left_forearm else Vector((0, 0, 0))))
    eye, yaw, pitch, back, above = framing
    b += struct.pack("<3f4f", *eye, yaw, pitch, back, above)
    for name, pivot, axis in parts:
        b += struct.pack("<16s", name.encode())
        b += struct.pack("<3f", *(Vector(pivot) * s))
        b += struct.pack("<3f", *Vector(axis).normalized()) if Vector(axis).length else struct.pack("<3f", 0, 0, 0)
    for name, pos, part, bend, slack in anchors:
        b += struct.pack("<16s", name.encode())
        b += struct.pack("<3f", *(Vector(pos) * s))
        b += struct.pack("<I", 0xFFFFFFFF if part is None else part)
        b += struct.pack("<3f", *(Vector(bend).normalized() if bend else Vector((0, 0, 0))))
        b += struct.pack("<3f", *(Vector(slack) * s if slack else Vector((0, 0, 0))))
    for p, n, u, v, part in vertices:
        b += struct.pack("<8fI", p.x, p.y, p.z, n.x, n.y, n.z, u, v, part)
    for t in triangles:
        b += struct.pack("<3H", *t)
    if len(b) % 4:
        b += b"\0" * (4 - len(b) % 4)
    b += struct.pack("<2I", SIZE, SIZE)
    b += palette.tobytes()
    b += pixels.tobytes()
    open(path, "wb").write(bytes(b))
    return len(vertices), len(triangles)


def preview(path, objects, atlas_png, views):
    """The low-poly gun wearing its atlas, rendered from each (location, target, lens) view side by side."""
    scene = bpy.context.scene
    tex = bpy.data.materials.new("atlas")
    tex.use_nodes = True
    nt = tex.node_tree
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(atlas_png)
    t.interpolation = "Linear"
    bsdf = nt.nodes["Principled BSDF"]
    nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.6
    for ob in objects:
        ob.data.materials.clear()
        ob.data.materials.append(tex)
    keep = set(objects)
    for o in scene.objects:
        if o.type == "MESH" and o not in keep:
            o.hide_render = True
    scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "CYCLES"
    scene.render.film_transparent = False
    scene.world.node_tree.nodes["Background"].inputs[0].default_value = (0.2, 0.3, 0.28, 1)
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(40), 0, math.radians(240))
    scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.clip_start = 0.01
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = 640, 480
    shots = []
    for i, (loc, target, lens) in enumerate(views):
        cam.location = Vector(loc)
        d = Vector(target) - cam.location
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = lens
        p = os.path.join(bpy.app.tempdir, f"preview{i}.png")
        scene.render.filepath = p
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(p)
        shots.append(np.array(img.pixels[:], dtype=np.float32).reshape(480, 640, 4)[::-1])
        bpy.data.images.remove(img)
    save_png(path, np.concatenate(shots, axis=1))


# Where every gun sits in his hands. The Colt (colt45.py) is the reference the player approved: the web of his
# right hand on its back strap 85 mm up the grip, which lands here in his hand bone's frame (metres). Other
# guns put their own grip's web point (the back of the pistol grip, or the wrist of a stock, just under the
# receiver) at the same place, held the same way: barrel along his hand, up up.
WEB_IN_HAND = (0.04381, 0.02971, 0.03251)
# His left hand's palm on a handguard, as its bone's frame has it (metres): half way along the hand, on its
# palm side.
PALM_IN_HAND = (0.045, 0.03, 0.0)
# The way a left forearm runs back from the wrist on a handguard: back, left and down (gun frame), so it comes
# in from the lower left of the view as Counter-Strike's do, not straight up the middle.
LEFT_FOREARM = (-0.6, -1.0, -0.45)


def scale_for(game_data, model, real_length_mm, fit=0.95):
    """The gun's scale: the game's model's length (its x extent) over the real gun's, as the Colt's (x 0.95)."""
    v = gamefiles.first_geometry_vertices(gamefiles.Img(game_data).read(model + ".dff"))
    return (max(p[0] for p in v) - min(p[0] for p in v)) / (real_length_mm * MM) * fit


def hold_for(web_mm, scale):
    """The right hand's hold: the gun's web point (mm, gun frame) on the Colt's, barrel along the hand."""
    w = Vector(web_mm) * MM * scale
    # Gun x -> hand -y, gun y -> hand x, gun z -> hand z.
    origin = Vector(WEB_IN_HAND) - Vector((w.y, -w.x, w.z))
    return [(0, -1, 0), (1, 0, 0), (0, 0, 1), tuple(origin)]


def hold_left_for(palm_mm, scale, roll_deg=0.0):
    """The left hand's hold: his palm up under the handguard at `palm_mm` (gun frame), fingers across it to the
    right and curling up its far side, thumb forward along its near side; `roll_deg` turns the hand about the
    barrel (positive: the palm towards the gun's left)."""
    r = math.radians(roll_deg)
    # The hand's axes in the gun frame: x across to the right, y (its palm side) up, z back (x cross y).
    hx = Vector((math.cos(r), 0.0, math.sin(r)))
    hy = Vector((-math.sin(r), 0.0, math.cos(r)))
    hz = hx.cross(hy)
    palm = Vector(palm_mm) * MM * scale
    wrist = palm - hx * PALM_IN_HAND[0] - hy * PALM_IN_HAND[1] - hz * PALM_IN_HAND[2]
    # The gun frame in the hand's: the inverse of (hx, hy, hz, wrist).
    rows = (hx, hy, hz)
    axes = [tuple(row[i] for row in rows) for i in range(3)]  # gun x, y, z in hand coordinates
    origin = tuple(-row.dot(wrist) for row in rows)
    return axes + [origin]


import gamefiles  # noqa: E402 (kept with the helpers that need it)
