"""Throwaway spike v3: Tommy's own model rigged in Blender from his skeleton, so his arms pose as the game's do.

His DFF's bind pose (a T-pose) has y up and z forward; his right arm is along -x. Bones are named b<tag>
(b23 his right forearm, b24 its hand, b25 its fingers), and his skin weights become vertex groups of the
same names, so an Armature modifier moves his mesh as the game's skinning does.
"""
import bpy
from mathutils import Matrix, Vector

import vcdff

# Bind space (x left, y up, z forward) to the world (x right, y forward, z up).
BIND_TO_WORLD = Matrix(((-1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))


def bind_matrices(g, bones):
    out = {}
    for k, b in enumerate(bones):
        m = g["skin"]["inv"][k]
        out[b["tag"]] = Matrix(((m[0], m[4], m[8], m[12]), (m[1], m[5], m[9], m[13]),
                                (m[2], m[6], m[10], m[14]), (0, 0, 0, 1))).inverted()
    return out


def parents(frames, bones):
    """Each bone's parent bone tag, from the frame hierarchy."""
    frame_tag = {b["frame"]: b["tag"] for b in bones if b["frame"] is not None}
    out = {}
    for b in bones:
        f = frames[b["frame"]]["parent"] if b["frame"] is not None else -1
        while f >= 0 and f not in frame_tag:
            f = frames[f]["parent"]
        out[b["tag"]] = frame_tag.get(f)
    return out


def load(dff_bytes, texture_path):
    """Tommy as a mesh bound to an armature. Returns (armature, mesh, binds, g)."""
    mesh, frames, bones, g = vcdff.load(dff_bytes, "tommy")
    for vg in mesh.vertex_groups:
        vg.name = "b" + vg.name.split(":")[0]
    binds = bind_matrices(g, bones)
    par = parents(frames, bones)
    children = {}
    for t, p in par.items():
        children.setdefault(p, []).append(t)
    arm_data = bpy.data.armatures.new("tommy_rig")
    arm = bpy.data.objects.new("tommy_rig", arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for b in bones:
        B = binds[b["tag"]]
        e1, e2, e3 = B.col[0].xyz.normalized(), B.col[1].xyz.normalized(), B.col[2].xyz.normalized()
        kids = [binds[c].translation for c in children.get(b["tag"], [])]
        length = max(0.02, min(((k - B.translation).length for k in kids), default=0.05))
        eb = arm_data.edit_bones.new("b%d" % b["tag"])
        # A Blender bone points along its Y; his along their x.
        eb.matrix = Matrix(((-e2.x, e1.x, e3.x, B.translation.x), (-e2.y, e1.y, e3.y, B.translation.y),
                            (-e2.z, e1.z, e3.z, B.translation.z), (0, 0, 0, 1)))
        eb.length = length
    for b in bones:
        p = par[b["tag"]]
        if p is not None:
            arm_data.edit_bones["b%d" % b["tag"]].parent = arm_data.edit_bones["b%d" % p]
    bpy.ops.object.mode_set(mode="OBJECT")
    mesh.parent = arm
    mod = mesh.modifiers.new("skin", "ARMATURE")
    mod.object = arm
    arm.matrix_world = BIND_TO_WORLD
    # His texture, as the game draws it (no specular to speak of).
    m = bpy.data.materials.new("tommy")
    m.use_nodes = True
    nt = m.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(texture_path)
    t.interpolation = "Linear"
    nt.links.new(t.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.8
    for key in ("Specular IOR Level", "Specular"):
        if key in bsdf.inputs:
            bsdf.inputs[key].default_value = 0.15
            break
    mesh.data.materials.append(m)
    for p in mesh.data.polygons:
        p.use_smooth = True
    return arm, mesh, binds, g
