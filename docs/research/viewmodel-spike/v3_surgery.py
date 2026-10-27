"""Throwaway spike v3: Tommy's own hands articulated. His fingers are one hooked slab (bone 25/35);
it's cut across its width into four fingers, the cuts capped and the fingers parted a little, and
each finger gets a base and a tip bone (bending at the hook's own corner). His thumb, part of the
hand's mesh (bone 24/34), gets two bones of its own. Nothing is added but the caps: it's his mesh
and his texture throughout.

Bind space: y up, z forward-ish across the hand; his right hand is at -x, his left at +x.
"""
import math

import bmesh
import bpy
from mathutils import Vector

HANDS = {
    # side: (forearm tag, hand tag, finger tag, x sign)
    "R": (23, 24, 25, -1.0),
    "L": (33, 34, 35, 1.0),
}
FINGERS = ("index", "middle", "ring", "pinky")  # from the thumb's side (z high) to the little finger's
GAP = 0.86        # each finger's width kept, about its own middle
THUMB_BELOW = 0.505  # y: the hand's vertices under this are his thumb
CAP_UV = (64 / 256, 1 - 60 / 256)  # plain skin


def smooth(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3 - 2 * t)


def main_group(v, dl):
    d = v[dl]
    return max(d.items(), key=lambda kv: kv[1])[0] if len(d) else None


def articulate(mesh_ob, arm_ob):
    me = mesh_ob.data
    groups = {vg.name: vg.index for vg in mesh_ob.vertex_groups}
    plan = {}
    bm = bmesh.new()
    bm.from_mesh(me)
    dl = bm.verts.layers.deform.verify()
    uvl = bm.loops.layers.uv.active
    for side, (fa_tag, hand_tag, fing_tag, sx) in HANDS.items():
        g_f, g_h = groups["b%d" % fing_tag], groups["b%d" % hand_tag]
        slab = [v for v in bm.verts if main_group(v, dl) == g_f]
        z0, z1 = min(v.co.z for v in slab), max(v.co.z for v in slab)
        cuts = [z0 + (z1 - z0) * k / 4 for k in (1, 2, 3)]
        # Cut the slab's own faces across at each quarter.
        for zc in cuts:
            faces = [f for f in bm.faces if all(main_group(v, dl) == g_f for v in f.verts)]
            edges = list({e for f in faces for e in f.edges})
            verts = list({v for f in faces for v in f.verts})
            bmesh.ops.bisect_plane(bm, geom=faces + edges + verts, dist=1e-7, plane_co=(0, 0, zc), plane_no=(0, 0, 1))
        on_cut = lambda v: any(abs(v.co.z - zc) < 1e-6 for zc in cuts)
        cut_edges = [e for e in bm.edges if all(on_cut(v) for v in e.verts) and all(main_group(v, dl) == g_f for v in e.verts)
                     and any(all(main_group(v, dl) == g_f for v in f.verts) for f in e.link_faces)]
        bmesh.ops.split_edges(bm, edges=cut_edges)
        # Which finger each slab vertex is now in: by its faces' middles (a vertex on a cut belongs to one side).
        def band(z):
            return 3 - sum(1 for zc in cuts if z > zc)  # 0 = index (highest z) .. 3 = pinky
        finger_of = {}
        for v in bm.verts:
            if main_group(v, dl) != g_f:
                continue
            fz = [f.calc_center_median().z for f in v.link_faces] or [v.co.z]
            finger_of[v] = band(sum(fz) / len(fz))
        # Cap each cut: its boundary edges on the plane, chained, closed into a face.
        for zc in cuts:
            bnd = [e for e in bm.edges if e.is_boundary and all(abs(v.co.z - zc) < 1e-6 for v in e.verts)]
            for chain in chains(bnd):
                if len(chain) < 3:
                    continue
                try:
                    f = bm.faces.new(chain)
                except ValueError:
                    continue
                piece_z = sum(finger_of.get(v, 0) for v in chain) / len(chain)
                piece_centre = (z0 + (z1 - z0) * (3.5 - piece_z) / 4)
                f.normal_update()
                if (f.normal.z > 0) != (zc > piece_centre):
                    f.normal_flip()
                for loop in f.loops:
                    loop[uvl].uv = CAP_UV
        # Part the fingers: each kept a little narrower about its own middle.
        for v, k in finger_of.items():
            centre = z0 + (z1 - z0) * (3.5 - k) / 4
            v.co.z = centre + (v.co.z - centre) * GAP
        # The thumb's vertices and its axis (base by the wrist, tip by the fingers).
        thumb = [v for v in bm.verts if main_group(v, dl) == g_h and v.co.y < THUMB_BELOW]
        plan[side] = {"cuts": cuts, "z": (z0, z1), "thumb": [v.co.copy() for v in thumb]}
    bm.to_mesh(me)
    me.update()
    bm.free()
    rig(mesh_ob, arm_ob, plan)


def chains(edges):
    """Edges into ordered vertex chains (closed or open)."""
    left = set(edges)
    out = []
    while left:
        e = left.pop()
        chain = [e.verts[0], e.verts[1]]
        for end in (1, 0):
            while True:
                tip = chain[-1] if end else chain[0]
                nxt = next((x for x in left if tip in x.verts), None)
                if not nxt:
                    break
                left.discard(nxt)
                other = nxt.other_vert(tip)
                if other in chain:
                    break
                if end:
                    chain.append(other)
                else:
                    chain.insert(0, other)
        out.append(chain)
    return out


def rig(mesh_ob, arm_ob, plan):
    """New bones for the fingers and thumbs, and his weights moved onto them."""
    me = mesh_ob.data
    bpy.context.view_layer.objects.active = arm_ob
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_ob.data.edit_bones
    made = {}
    for side, (fa_tag, hand_tag, fing_tag, sx) in HANDS.items():
        p = plan[side]
        z0, z1 = p["z"]
        knuckle = eb["b%d" % fing_tag].head.copy()
        hand = eb["b%d" % hand_tag]
        for k, name in enumerate(FINGERS):
            zf = z0 + (z1 - z0) * (3.5 - k) / 4
            base_h = Vector((knuckle.x, knuckle.y, zf))
            bend = Vector((sx * 0.861, 0.474, zf))
            tip = Vector((sx * 0.818, 0.462, zf))
            b = eb.new(f"{side}_{name}")
            b.head, b.tail, b.parent = base_h, bend, hand
            b.align_roll(Vector((0, 0, 1)).cross(bend - base_h))
            t = eb.new(f"{side}_{name}_tip")
            t.head, t.tail, t.parent = bend, tip, b
            t.align_roll(Vector((0, 0, 1)).cross(tip - bend))
            made[(side, name)] = (b.name, t.name)
        # The thumb: along its vertices' main axis, from the wrist end to the tip.
        pts = p["thumb"]
        c = sum(pts, Vector()) / len(pts)
        axis = max_axis(pts, c)
        if axis.x * sx < 0:
            axis = -axis  # towards the fingers
        ts = sorted((q - c).dot(axis) for q in pts)
        lo, hi = ts[0], ts[-1]
        p["thumb_frame"] = (c, axis, lo, hi)
        a = c + axis * (lo + (hi - lo) * 0.2)
        m = c + axis * (lo + (hi - lo) * 0.6)
        e = c + axis * hi
        bend_axis = axis.cross(Vector((0, 1, 0))).normalized()
        tb = eb.new(f"{side}_thumb")
        tb.head, tb.tail, tb.parent = a, m, hand
        tb.align_roll(bend_axis.cross(m - a))
        tt = eb.new(f"{side}_thumb_tip")
        tt.head, tt.tail, tt.parent = m, e, tb
        tt.align_roll(bend_axis.cross(e - m))
    bpy.ops.object.mode_set(mode="OBJECT")
    # Weights.
    vg = mesh_ob.vertex_groups
    for side, (fa_tag, hand_tag, fing_tag, sx) in HANDS.items():
        p = plan[side]
        g_f, g_h = vg["b%d" % fing_tag], vg["b%d" % hand_tag]
        new = {}
        for k, name in enumerate(FINGERS):
            for bn in made[(side, name)]:
                new[bn] = vg.new(name=bn)
        new["thumb"] = vg.new(name=f"{side}_thumb")
        new["thumb_tip"] = vg.new(name=f"{side}_thumb_tip")
        z0, z1 = p["z"]
        cuts = p["cuts"]
        for v in me.vertices:
            w = {gg.group: gg.weight for gg in v.groups}
            wf = w.get(g_f.index, 0.0)
            if wf > 0:
                k = 3 - sum(1 for zc in cuts if v.co.z > zc)
                base, tip = made[(side, FINGERS[k])]
                # Past the hook's front corner, along its underside: the tip.
                tipness = smooth(0.0, 0.012, 0.857 - sx * v.co.x) * smooth(0.0, 0.01, 0.482 - v.co.y)
                g_f.remove([v.index])
                new[base].add([v.index], wf * (1 - tipness), "ADD")
                new[tip].add([v.index], wf * tipness, "ADD")
            wh = w.get(g_h.index, 0.0)
            if wh > 0 and v.co.y < THUMB_BELOW + 0.004:
                c, axis, lo, hi = p["thumb_frame"]
                t = ((v.co - c).dot(axis) - lo) / (hi - lo)
                below = smooth(THUMB_BELOW + 0.004, THUMB_BELOW - 0.004, v.co.y)
                on_thumb = smooth(0.1, 0.3, t) * below
                tipw = smooth(0.5, 0.65, t) * on_thumb
                basew = on_thumb - tipw
                if on_thumb > 0:
                    g_h.add([v.index], wh * (1 - on_thumb), "REPLACE")
                    new["thumb"].add([v.index], wh * basew, "ADD")
                    new["thumb_tip"].add([v.index], wh * tipw, "ADD")


def max_axis(pts, c):
    """The direction the points spread most along (power iteration on their covariance)."""
    cov = [[0.0] * 3 for _ in range(3)]
    for q in pts:
        d = q - c
        for i in range(3):
            for j in range(3):
                cov[i][j] += d[i] * d[j]
    v = Vector((1, 0.1, 0.1))
    for _ in range(50):
        v = Vector((sum(cov[0][j] * v[j] for j in range(3)), sum(cov[1][j] * v[j] for j in range(3)),
                    sum(cov[2][j] * v[j] for j in range(3)))).normalized()
    return v
