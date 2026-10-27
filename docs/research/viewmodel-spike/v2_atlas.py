"""Throwaway spike v2: the detailed Colt photographed (orthographic renders) into a 256x256 texture atlas,
the way Vice City's guns wear side-view photos.

blender -b -P v2_atlas.py -- <out.png>
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, os.path.dirname(__file__))
import colt
from lib import MM, clear, material

# Atlas regions (pixels, top-down) and what each sees (mm, in the gun's frame).
SIZE = 256
REGIONS = {
    # name: (x0, y0, w, h), camera position, look direction, up, (centre a, centre b), mm per pixel
    "left": ((0, 0, 256, 160), (-400, 108, 69.5), (1, 0, 0), (0, 0, 1), 232 / 256),
    "top": ((0, 160, 256, 48), (0, 108, 400), (0, 0, -1), (-1, 0, 0), 232 / 256),
    "back": ((0, 208, 96, 48), (0, -400, 118), (0, 1, 0), (0, 0, 1), 44 / 48),
    "front": ((96, 208, 64, 48), (0, 600, 116), (0, -1, 0), (0, 0, 1), 34 / 48),
}


def photo_materials():
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


def setup_scene():
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
    # A studio: bright overhead, a dark floor, a light band at the horizon.
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


def shoot(scene, region):
    (x0, y0, w, h), pos, look, up, mmpp = region
    cam = bpy.data.objects.new("ortho", bpy.data.cameras.new("ortho"))
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = max(w, h) * mmpp * MM
    cam.data.clip_start, cam.data.clip_end = 0.001, 2.0
    scene.collection.objects.link(cam)
    cam.location = Vector(pos) * MM
    q = Vector(look).to_track_quat("-Z", "Y")
    # Align the camera's up to `up`.
    from mathutils import Matrix
    z = -Vector(look).normalized()
    y = Vector(up).normalized()
    x = y.cross(z)
    cam.matrix_world = Matrix((
        (x.x, y.x, z.x, cam.location.x),
        (x.y, y.y, z.y, cam.location.y),
        (x.z, y.z, z.z, cam.location.z),
        (0, 0, 0, 1)))
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = w * 2, h * 2  # supersampled, then halved
    path = bpy.app.tempdir + "view.png"
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h * 2, w * 2, 4)[::-1]
    bpy.data.images.remove(img)
    bpy.data.objects.remove(cam)
    return px.reshape(h, 2, w, 2, 4).mean(axis=(1, 3))


def main():
    out = sys.argv[sys.argv.index("--") + 1]
    clear()
    scene = setup_scene()
    colt.build(photo_materials())
    atlas = np.zeros((SIZE, SIZE, 4), np.float32)
    atlas[..., :3] = 0.35  # what's between the views: mid grey steel
    atlas[..., 3] = 1
    for name, region in REGIONS.items():
        (x0, y0, w, h) = region[0]
        px = shoot(scene, region)
        a = px[..., 3:4]
        atlas[y0:y0 + h, x0:x0 + w, :3] = px[..., :3] * a + atlas[y0:y0 + h, x0:x0 + w, :3] * (1 - a)
    img = bpy.data.images.new("atlas", SIZE, SIZE)
    img.pixels[:] = atlas[::-1].ravel()
    img.filepath_raw = out
    img.file_format = "PNG"
    img.save()


if __name__ == "__main__":
    main()
