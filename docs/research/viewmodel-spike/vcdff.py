"""Throwaway: a skinned Vice City DFF (a ped) into Blender, with its bones as vertex groups.

Frames carry the HAnim bone tags (and names); the skin plugin gives each vertex up to four
(bone, weight) pairs, the bone an index into the HAnim hierarchy's node list.
"""
import struct

import bpy
from mathutils import Matrix, Vector


def chunks(b, start, end):
    p = start
    while p + 12 <= end:
        t, s, v = struct.unpack_from("<III", b, p)
        yield t, p + 12, s, v
        p += 12 + s


def find(b, start, end, kind):
    for t, p, s, v in chunks(b, start, end):
        if t == kind:
            return p, s
    return None


def parse(b):
    clump = find(b, 0, len(b), 0x10)
    cp, cs = clump
    frames = []
    for t, p, s, v in chunks(b, cp, cp + cs):
        if t == 0x0E:  # frame list
            kids = list(chunks(b, p, p + s))
            st = kids[0][1]
            n = struct.unpack_from("<I", b, st)[0]
            for i in range(n):
                o = st + 4 + i * 56
                rot = struct.unpack_from("<9f", b, o)
                pos = struct.unpack_from("<3f", b, o + 36)
                parent = struct.unpack_from("<i", b, o + 48)[0]
                frames.append({"rot": rot, "pos": pos, "parent": parent, "name": "", "tag": None, "hanim": None})
            # Extensions, one per frame.
            for i, (t2, p2, s2, v2) in enumerate(kids[1:1 + n]):
                for t3, p3, s3, v3 in chunks(b, p2, p2 + s2):
                    if t3 == 0x253F2FE:
                        frames[i]["name"] = b[p3:p3 + s3].decode(errors="replace")
                    elif t3 == 0x11E:
                        ver, tag, nb = struct.unpack_from("<Iii", b, p3)
                        frames[i]["tag"] = tag
                        if nb > 0:
                            flags, keysize = struct.unpack_from("<II", b, p3 + 12)
                            nodes = []
                            for k in range(nb):
                                nid, idx, nflags = struct.unpack_from("<iii", b, p3 + 20 + k * 12)
                                nodes.append((nid, idx, nflags))
                            frames[i]["hanim"] = nodes
        elif t == 0x1A:
            geoms = [(p2, s2) for t2, p2, s2, v2 in chunks(b, p, p + s) if t2 == 0x0F]
    return frames, geoms


def geometry(b, p, s):
    kids = list(chunks(b, p, p + s))
    st = kids[0][1]
    flags, ntri, nvert, nmorph = struct.unpack_from("<IiiI", b, st)
    q = st + 16
    ver = kids[0][3]
    lib = (((ver >> 14) & 0x3FF00) + 0x30000) | ((ver >> 16) & 0x3F) if ver & 0xFFFF0000 else ver << 8
    if lib < 0x34000:
        q += 12  # ambient, specular, diffuse
    colors = None
    if flags & 0x08:
        colors = [struct.unpack_from("<4B", b, q + 4 * i) for i in range(nvert)]
        q += 4 * nvert
    uvsets = (flags >> 16) & 0xFF or (1 if flags & 0x04 else 0)
    uvs = []
    for k in range(uvsets):
        cur = [struct.unpack_from("<2f", b, q + 8 * i) for i in range(nvert)]
        q += 8 * nvert
        if k == 0:
            uvs = cur
    tris = []
    for i in range(ntri):
        v2, v1, m, v3 = struct.unpack_from("<HHHH", b, q)
        q += 8
        tris.append((v1, v2, v3, m))
    q += 16
    hasv, hasn = struct.unpack_from("<II", b, q)
    q += 8
    verts = [struct.unpack_from("<3f", b, q + 12 * i) for i in range(nvert)]
    q += 12 * nvert
    normals = [struct.unpack_from("<3f", b, q + 12 * i) for i in range(nvert)] if hasn else None
    skin = None
    for t, pp, ss, vv in kids[1:]:
        if t == 0x03:  # extensions
            for t2, p2, s2, v2 in chunks(b, pp, pp + ss):
                if t2 == 0x116:
                    nb, nused, maxw, _ = struct.unpack_from("<4B", b, p2)
                    o = p2 + 4 + nused
                    idx = [struct.unpack_from("<4B", b, o + 4 * i) for i in range(nvert)]
                    o += 4 * nvert
                    wts = [struct.unpack_from("<4f", b, o + 16 * i) for i in range(nvert)]
                    o += 16 * nvert
                    inv = []
                    for k in range(nb):
                        if lib < 0x34003:
                            o += 4  # 0xDEADDEAD
                        inv.append(struct.unpack_from("<16f", b, o))
                        o += 64
                    skin = {"bones": nb, "idx": idx, "w": wts, "inv": inv}
    return {"verts": verts, "normals": normals, "uvs": uvs, "tris": tris, "colors": colors, "skin": skin}


def load(path_or_bytes, name="ped", texture=None):
    b = path_or_bytes if isinstance(path_or_bytes, (bytes, bytearray)) else open(path_or_bytes, "rb").read()
    frames, geoms = parse(b)
    g = geometry(b, *geoms[0])
    me = bpy.data.meshes.new(name)
    me.from_pydata([Vector(v) for v in g["verts"]], [], [(a, c, d) for a, c, d, m in g["tris"]])
    me.validate()
    if g["uvs"]:
        uv = me.uv_layers.new(name="UV")
        for poly in me.polygons:
            for li in poly.loop_indices:
                vi = me.loops[li].vertex_index
                u, v = g["uvs"][vi]
                uv.data[li].uv = (u, 1 - v)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    # The bones: the HAnim node list (on the root bone's frame) in skin index order.
    hanim = next(f["hanim"] for f in frames if f["hanim"])
    tag_frame = {f["tag"]: i for i, f in enumerate(frames) if f["tag"] is not None}
    bones = []
    for nid, idx, nflags in hanim:
        fi = tag_frame.get(nid)
        bones.append({"tag": nid, "frame": fi, "name": frames[fi]["name"] if fi is not None else str(nid)})
    if g["skin"]:
        groups = [ob.vertex_groups.new(name=f"{b_['tag']}:{b_['name']}") for b_ in bones]
        for vi, (ix, ws) in enumerate(zip(g["skin"]["idx"], g["skin"]["w"])):
            for k in range(4):
                if ws[k] > 0:
                    groups[ix[k]].add([vi], ws[k], "ADD")
    return ob, frames, bones, g
