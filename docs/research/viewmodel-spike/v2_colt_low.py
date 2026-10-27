"""Throwaway spike v2: the Colt 45 at Vice City's detail (a few hundred triangles), wearing the
photographed atlas (v2_atlas.py) by projection, as the game's own guns wear theirs."""
import math

import bmesh
import bpy
from mathutils import Vector

import v2_atlas
from colt import back_strap_y, front_strap_y
from lib import MM, box, cut, fillet, join, profile_xz, profile_yz, tris


def build(atlas_path):
    parts = []
    # The slide: its top corners chamfered; serrations, port and sights' detail are in the texture.
    parts.append(profile_xz("slide", [(-11.5, 102), (11.5, 102), (11.5, 126.5), (8.5, 130), (-8.5, 130), (-11.5, 126.5)], 18, 216))
    parts.append(profile_yz("front_sight", [(196, 129), (207, 129), (207, 135.5), (199, 133)], -1.4, 1.4))
    rear = profile_yz("rear_sight", [(21, 129), (32, 129), (32, 135), (21, 133)], -7, 7)
    parts.append(rear)
    # The frame and grip, the trigger guard (cut), trigger, hammer, tang, grip panels.
    outline = [(20, 102), (197, 102), (197, 92.5), (front_strap_y(92), 92), (front_strap_y(9), 9),
               (back_strap_y(24.5), 24.5), (back_strap_y(96), 96)]
    parts.append(profile_yz("frame", fillet(outline, [0, 0, 3, 0, 3, 4, 0], 1), -11, 11))
    guard = profile_yz("guard", fillet([(68, 92), (122, 92), (122, 79), (117, 69.5), (80, 69), (66, 74)], [0, 0, 8, 5, 8, 0], 1),
                       -4.6, 4.6)
    cut(guard, profile_yz("opening", fillet([(76, 94), (116.5, 94), (116.5, 80), (113, 74), (82, 74), (76.5, 79)],
                                            [0, 0, 5, 3, 5, 0], 1), -10, 10), solver="EXACT")
    parts.append(guard)
    parts.append(profile_yz("trigger", [(87, 93), (93, 93), (93.3, 80.5), (88, 79)], -3.2, 3.2))
    parts.append(profile_yz("hammer", [(20, 99), (10, 112), (4, 115.5), (2.5, 113), (12, 100), (14, 98)], -3.4, 3.4))
    parts.append(profile_yz("tang", [(25, 93), (9, 96), (0, 100.5), (3, 104.2), (19, 104.2), (25, 101)], -9.5, 9.5))
    panel = fillet([(front_strap_y(89) - 4.5, 89), (front_strap_y(17) - 4.0, 17), (back_strap_y(30) + 4.5, 30.5),
                    (back_strap_y(89) + 4.5, 89)], 5, 1)
    parts.append(profile_yz("panel_r", panel, 10.5, 14.8))
    parts.append(profile_yz("panel_l", panel, -14.8, -10.5))
    # The barrel's bushing standing out of the slide's front.
    from lib import cylinder_y
    parts.append(cylinder_y("bushing", 0, 117, 8.8, 214, 217.5, 8))
    gun = join("colt45_low", parts)
    project_uvs(gun)
    mat = textured("colt45_tex", atlas_path)
    gun.data.materials.clear()
    gun.data.materials.append(mat)
    return gun, tris(gun)


def region_uv(name, v):
    """Where point v (mm) falls in the atlas region `name`, as a UV."""
    (x0, y0, w, h), pos, look, up, mmpp = v2_atlas.REGIONS[name]
    z = -Vector(look).normalized()
    y = Vector(up).normalized()
    x = y.cross(z)
    d = v - Vector(pos)
    px = x0 + w / 2 + d.dot(x) / mmpp
    py = y0 + h / 2 - d.dot(y) / mmpp
    return px / v2_atlas.SIZE, 1 - py / v2_atlas.SIZE


def project_uvs(ob):
    me = ob.data
    uv = me.uv_layers.new(name="UV")
    for poly in me.polygons:
        n = poly.normal
        centre = sum((me.vertices[i].co for i in poly.vertices), Vector()) / len(poly.vertices) / MM
        ax = max(range(3), key=lambda k: abs(n[k]))
        if ax == 2 and n.z > 0:
            name = "top"
        elif ax == 1 and n.y < 0 and centre.z >= 96:
            name = "back"
        elif ax == 1 and n.y > 0 and centre.z >= 99:
            name = "front"
        else:
            name = "left"  # the sides (the right mirrors the left), and the rest smeared from it
        for li in poly.loop_indices:
            uv.data[li].uv = region_uv(name, me.vertices[me.loops[li].vertex_index].co / MM)


def textured(name, path, roughness=0.55, spec=0.35):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(path)
    t.interpolation = "Linear"
    nt.links.new(t.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = roughness
    for key in ("Specular IOR Level", "Specular"):
        if key in b.inputs:
            b.inputs[key].default_value = spec
            break
    return m
