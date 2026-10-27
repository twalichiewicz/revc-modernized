"""Renders the viewmodel tool's OBJs (build/vm) in Blender, to look at. The gun is the one named in <dir>/gun.txt.

blender -b -P tools/viewmodel/render.py -- <dir> hands [outfit ...]
    each outfit's right hand in its own frame, from the side, below, front and above, as <dir>/<outfit>_hand.png
blender -b -P tools/viewmodel/render.py -- <dir> fp [outfit ...]
    the first-person view (<outfit>_fp.obj and colt45_fp.obj, already in the view's frame) at 1600x900,
    a 62 degree vertical field of view, as <dir>/<outfit>_fp.png
"""
import math
import os
import sys

import bpy
import numpy as np
from mathutils import Vector

args = sys.argv[sys.argv.index("--") + 1:]
folder, mode, outfits = args[0], args[1], args[2:] or ["player"]
gun_name = open(os.path.join(folder, "gun.txt")).read().strip() if os.path.exists(os.path.join(folder, "gun.txt")) else "colt45"


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    for e in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT", "CYCLES"):
        try:
            scene.render.engine = e
            break
        except TypeError:
            pass
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("w")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.20, 0.30, 0.28, 1)
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sun.data.energy = 2.6
    sun.rotation_euler = (math.radians(40), 0, math.radians(240))
    scene.collection.objects.link(sun)
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    cam.data.clip_start = 0.002
    scene.collection.objects.link(cam)
    scene.camera = cam
    return scene, cam


def load(path):
    bpy.ops.wm.obj_import(filepath=path, forward_axis="Y", up_axis="Z")
    ob = bpy.context.selected_objects[0]
    for m in ob.data.materials:
        if m and m.use_nodes:
            for n in m.node_tree.nodes:
                if n.type == "TEX_IMAGE":
                    n.interpolation = "Linear"
    return ob


def shoot(scene, path, w, h):
    scene.render.resolution_x, scene.render.resolution_y = w, h
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(h, w, 4)
    bpy.data.images.remove(img)
    return px


def save(path, px):
    img = bpy.data.images.new("out", px.shape[1], px.shape[0], alpha=True)
    img.pixels[:] = px.ravel()
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()


for name in outfits:
    scene, cam = reset()
    if mode == "hands":
        load(os.path.join(folder, f"{name}_hand.obj"))
        centre = Vector((0.08, 0.03, 0.0))
        views = [Vector((0.08, 0.03, 0.35)), Vector((0.08, 0.35, 0.02)), Vector((0.40, 0.04, 0.05)), Vector((0.08, -0.30, 0.05))]
        shots = []
        for i, pos in enumerate(views):
            cam.location = pos
            cam.rotation_euler = (centre - pos).to_track_quat("-Z", "Y").to_euler()
            cam.data.lens = 70
            shots.append(shoot(scene, os.path.join(bpy.app.tempdir, f"h{i}.png"), 480, 360))
        save(os.path.join(folder, f"{name}_hand.png"), np.concatenate(shots, axis=1))
    elif mode == "close":
        # The spike's close-ups, in the gun's frame: the trigger side, under it, the left, below.
        load(os.path.join(folder, f"{name}_gun.obj"))
        if os.path.exists(os.path.join(folder, f"{name}_gun_left.obj")):
            load(os.path.join(folder, f"{name}_gun_left.obj"))
        gun = load(os.path.join(folder, f"{gun_name}_gun.obj"))
        bpy.context.view_layer.update()
        views = [((-0.28, 0.10, 0.09), (0, 0.08, 0.08), 55), ((-0.22, 0.12, -0.12), (0, 0.06, 0.06), 50),
                 ((-0.30, 0.04, 0.04), (0, 0.04, 0.04), 60), ((0.05, 0.06, -0.30), (0, 0.04, 0.04), 60)]
        shots = []
        for i, (pos, target, lens) in enumerate(views):
            cam.location = Vector(pos)
            cam.rotation_euler = (Vector(target) - Vector(pos)).to_track_quat("-Z", "Y").to_euler()
            cam.data.lens = lens
            shots.append(shoot(scene, os.path.join(bpy.app.tempdir, f"c{i}.png"), 800, 450))
        top, bottom = np.concatenate(shots[:2], axis=1), np.concatenate(shots[2:], axis=1)
        save(os.path.join(folder, f"{name}_{gun_name}_close.png"), np.concatenate([bottom, top], axis=0))
    else:
        load(os.path.join(folder, f"{name}_fp.obj"))
        if os.path.exists(os.path.join(folder, f"{name}_fp_left.obj")):
            load(os.path.join(folder, f"{name}_fp_left.obj"))
        load(os.path.join(folder, f"{gun_name}_fp.obj"))
        cam.location = Vector((0, 0, 0))
        cam.rotation_euler = Vector((0, 1, 0)).to_track_quat("-Z", "Y").to_euler()
        cam.data.sensor_fit = "VERTICAL"
        cam.data.angle = math.radians(62)
        scene.render.film_transparent = False
        save(os.path.join(folder, f"{name}_{gun_name}_fp.png"), shoot(scene, os.path.join(bpy.app.tempdir, "fp.png"), 1600, 900))
