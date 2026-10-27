"""Throwaway spike helpers: building low-poly hard-surface parts from 2D profiles in Blender."""
import math

import bmesh
import bpy
from mathutils import Matrix, Vector

MM = 0.001


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def fillet(points, radii, segments=3):
    """A closed 2D outline with its corners rounded: radii[i] for points[i] (0 = sharp)."""
    out = []
    n = len(points)
    for i in range(n):
        p, r = Vector(points[i]), radii[i] if isinstance(radii, (list, tuple)) else radii
        if r <= 0:
            out.append(p)
            continue
        a, b = Vector(points[i - 1]), Vector(points[(i + 1) % n])
        da, db = (a - p).normalized(), (b - p).normalized()
        half = math.acos(max(-1.0, min(1.0, da.dot(db)))) / 2
        if half < 1e-3 or half > math.pi / 2 - 1e-3:
            out.append(p)
            continue
        d = min(r / math.tan(half), (a - p).length * 0.49, (b - p).length * 0.49)
        rr = d * math.tan(half)
        p0, p1 = p + da * d, p + db * d
        bis = (da + db).normalized()
        c = p + bis * (rr / math.sin(half))
        a0 = math.atan2(p0.y - c.y, p0.x - c.x)
        a1 = math.atan2(p1.y - c.y, p1.x - c.x)
        da_ = (a1 - a0 + math.pi) % (2 * math.pi) - math.pi
        for k in range(segments + 1):
            t = a0 + da_ * k / segments
            out.append(c + Vector((math.cos(t), math.sin(t))) * rr)
    return out


def _object(name, bm, mat=None):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    if mat:
        me.materials.append(mat)
    return ob


def profile_yz(name, outline, x0, x1, mat=None, scale=MM):
    """A side outline (y forward, z up, mm) extruded across x0..x1 (mm)."""
    bm = bmesh.new()
    vs0 = [bm.verts.new((x0 * scale, y * scale, z * scale)) for y, z in outline]
    vs1 = [bm.verts.new((x1 * scale, y * scale, z * scale)) for y, z in outline]
    bm.faces.new(list(reversed(vs0)) if x1 > x0 else vs0)
    bm.faces.new(vs1 if x1 > x0 else list(reversed(vs1)))
    n = len(outline)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((vs0[i], vs0[j], vs1[j], vs1[i]) if x1 > x0 else (vs0[j], vs0[i], vs1[i], vs1[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def profile_xz(name, outline, y0, y1, mat=None, scale=MM):
    """A cross-section (x right, z up, mm) extruded along y0..y1 (mm)."""
    bm = bmesh.new()
    vs0 = [bm.verts.new((x * scale, y0 * scale, z * scale)) for x, z in outline]
    vs1 = [bm.verts.new((x * scale, y1 * scale, z * scale)) for x, z in outline]
    bm.faces.new(vs0)
    bm.faces.new(list(reversed(vs1)))
    n = len(outline)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((vs0[i], vs1[i], vs1[j], vs0[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return _object(name, bm, mat)


def box(name, x0, x1, y0, y1, z0, z1, mat=None, scale=MM):
    return profile_xz(name, [(x0, z0), (x1, z0), (x1, z1), (x0, z1)], y0, y1, mat, scale)


def cylinder_y(name, x, z, r, y0, y1, segments=12, mat=None, scale=MM, cap=True):
    outline = [(x + r * math.cos(2 * math.pi * k / segments), z + r * math.sin(2 * math.pi * k / segments))
               for k in range(segments)]
    return profile_xz(name, outline, y0, y1, mat, scale)


def cylinder_x(name, y, z, r, x0, x1, segments=12, mat=None, scale=MM):
    outline = [(y + r * math.cos(2 * math.pi * k / segments), z + r * math.sin(2 * math.pi * k / segments))
               for k in range(segments)]
    return profile_yz(name, outline, x0, x1, mat, scale)


def modifier(ob, kind, **kw):
    m = ob.modifiers.new(kind.lower(), kind)
    for k, v in kw.items():
        setattr(m, k, v)
    return m


def apply_all(ob):
    bpy.context.view_layer.objects.active = ob
    for m in list(ob.modifiers):
        bpy.ops.object.modifier_apply(modifier=m.name)


def cut(ob, cutter, solver="EXACT"):
    m = modifier(ob, "BOOLEAN", operation="DIFFERENCE", object=cutter, solver=solver)
    if solver == "EXACT":
        m.use_self = True  # the cutter's own parts may overlap
    apply_all(ob)
    bpy.data.objects.remove(cutter)


def bevel(ob, width_mm, segments=1, angle=35):
    modifier(ob, "BEVEL", width=width_mm * MM, segments=segments, limit_method="ANGLE",
             angle_limit=math.radians(angle), harden_normals=False)
    apply_all(ob)


def finish(ob, smooth_angle=35):
    """Smooth shading split at hard edges (by angle), triangulated."""
    bpy.context.view_layer.objects.active = ob
    ob.select_set(True)
    try:
        bpy.ops.object.shade_auto_smooth(angle=math.radians(smooth_angle))
    except Exception:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(smooth_angle))
    ob.select_set(False)


def join(name, obs):
    bpy.ops.object.select_all(action="DESELECT")
    for o in obs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]
    bpy.ops.object.join()
    obs[0].name = name
    return obs[0]


def tris(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    me = ob.evaluated_get(dg).to_mesh()
    n = sum(len(p.vertices) - 2 for p in me.polygons)
    ob.evaluated_get(dg).to_mesh_clear()
    return n


def material(name, color, metallic=0.0, roughness=0.5, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = roughness
    for key in ("Specular IOR Level", "Specular"):
        if key in b.inputs:
            b.inputs[key].default_value = spec
            break
    return m


def look_at(ob, target):
    d = Vector(target) - ob.location
    ob.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()


def render_engine(scene):
    for e in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            scene.render.engine = e
            return e
        except TypeError:
            pass
    scene.render.engine = "CYCLES"
    return "CYCLES"
