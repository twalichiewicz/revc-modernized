"""Throwaway: Vice City DFF -> OBJ (+MTL), TXD -> PNGs, from gta3.img.

usage: python3 -I vcextract.py <GameData> <outdir> <name> [<name> ...]   (names without extension)
"""
import os
import struct
import sys
import zlib


def chunks(b, start, end):
    p = start
    while p + 12 <= end:
        t, s, v = struct.unpack_from("<III", b, p)
        yield t, p + 12, s, v
        p += 12 + s


class Img:
    def __init__(self, gd):
        d = open(gd + "/models/gta3.dir", "rb").read()
        self.f = open(gd + "/models/gta3.img", "rb")
        self.ent = {}
        for i in range(0, len(d), 32):
            off, size = struct.unpack_from("<II", d, i)
            self.ent[d[i + 8:i + 32].split(b"\0")[0].decode().lower()] = (off, size)

    def read(self, name):
        off, size = self.ent[name.lower()]
        self.f.seek(off * 2048)
        return self.f.read(size * 2048)


def png(path, w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def c565(c):
    return [((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31]


def dxt(data, w, h, kind):
    out = bytearray(w * h * 4)
    p = 0
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            alpha = None
            if kind == 3:
                alpha = [((data[p + i // 2] >> (4 * (i % 2))) & 15) * 17 for i in range(16)]
                p += 8
            c0, c1, bits = struct.unpack_from("<HHI", data, p)
            p += 8
            a, b = c565(c0), c565(c1)
            if c0 > c1 or kind == 3:
                pal = [a + [255], b + [255], [(2 * a[i] + b[i]) // 3 for i in range(3)] + [255],
                       [(a[i] + 2 * b[i]) // 3 for i in range(3)] + [255]]
            else:
                pal = [a + [255], b + [255], [(a[i] + b[i]) // 2 for i in range(3)] + [255], [0, 0, 0, 0]]
            for i in range(16):
                x, y = bx + i % 4, by + i // 4
                if x < w and y < h:
                    px = list(pal[(bits >> (2 * i)) & 3])
                    if alpha:
                        px[3] = alpha[i]
                    out[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes(px)
    return out


def txd(b, outdir):
    names = []
    for t, p, s, v in chunks(b, 12, len(b)):
        if t != 0x15:
            continue
        st = list(chunks(b, p, p + s))[0][1]
        name = b[st + 8:st + 40].split(b"\0")[0].decode()
        raster, alpha = struct.unpack_from("<II", b, st + 72)
        w, h, depth, levels, rtype, comp = struct.unpack_from("<HHBBBB", b, st + 80)
        q = st + 88
        pal = None
        if raster & 0x2000:
            pal = b[q:q + 1024]
            q += 1024
        elif raster & 0x4000:
            pal = b[q:q + 64]
            q += 64
        size = struct.unpack_from("<I", b, q)[0]
        q += 4
        data = b[q:q + size]
        if comp in (1, 3):
            rgba = dxt(data, w, h, comp)
        elif pal:
            rgba = bytearray(w * h * 4)
            for i in range(w * h):
                idx = data[i] if raster & 0x2000 else (data[i // 2] >> (4 * (i % 2))) & 15
                rgba[i * 4:i * 4 + 4] = pal[idx * 4:idx * 4 + 4]
        elif depth == 32:  # BGRA
            rgba = bytearray(w * h * 4)
            for i in range(w * h):
                bb, g, r, a = data[i * 4:i * 4 + 4]
                rgba[i * 4:i * 4 + 4] = bytes([r, g, bb, a if raster & 0x500 == 0x500 or alpha else 255])
        elif depth == 16:  # 1555 / 565
            rgba = bytearray(w * h * 4)
            for i in range(w * h):
                c = struct.unpack_from("<H", data, i * 2)[0]
                if raster & 0xF00 == 0x200:
                    rgba[i * 4:i * 4 + 4] = bytes(c565(c) + [255])
                else:
                    rgba[i * 4:i * 4 + 4] = bytes([((c >> 10) & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31,
                                                   (c & 31) * 255 // 31, 255 if c & 0x8000 else 0])
        else:
            print("  skip texture", name, hex(raster), depth, comp)
            continue
        png(os.path.join(outdir, name.lower() + ".png"), w, h, rgba)
        names.append((name, w, h))
    return names


def dff(b, name, outdir):
    # Frames (for the atomics' placement), geometries, atomics.
    frames, geoms, atomics = [], [], []
    for t, p, s, v in chunks(b, 0, len(b)):
        if t != 0x10:
            continue
        for t2, p2, s2, v2 in chunks(b, p, p + s):
            if t2 == 0x0E:
                st = list(chunks(b, p2, p2 + s2))
                n = struct.unpack_from("<I", b, st[0][1])[0]
                for i in range(n):
                    o = st[0][1] + 4 + i * 56
                    rot = struct.unpack_from("<9f", b, o)
                    pos = struct.unpack_from("<3f", b, o + 36)
                    parent = struct.unpack_from("<i", b, o + 48)[0]
                    frames.append((rot, pos, parent))
            elif t2 == 0x1A:
                for t3, p3, s3, v3 in chunks(b, p2, p2 + s2):
                    if t3 == 0x0F:
                        geoms.append(geometry(b, p3, s3))
            elif t2 == 0x14:
                st = list(chunks(b, p2, p2 + s2))[0][1]
                fi, gi = struct.unpack_from("<II", b, st)
                atomics.append((fi, gi))

    def world(fi, v):
        while fi >= 0:
            rot, pos, parent = frames[fi]
            v = (rot[0] * v[0] + rot[3] * v[1] + rot[6] * v[2] + pos[0],
                 rot[1] * v[0] + rot[4] * v[1] + rot[7] * v[2] + pos[1],
                 rot[2] * v[0] + rot[5] * v[1] + rot[8] * v[2] + pos[2])
            fi = parent
        return v

    mats = set()
    with open(os.path.join(outdir, name + ".obj"), "w") as f:
        f.write(f"mtllib {name}.mtl\n")
        base = 1
        for k, (fi, gi) in enumerate(atomics or [(-1, i) for i in range(len(geoms))]):
            g = geoms[gi]
            f.write(f"o {name}_{k}\n")
            for v in g["verts"]:
                w = world(fi, v) if fi >= 0 else v
                f.write("v %f %f %f\n" % w)
            for uv in g["uvs"]:
                f.write("vt %f %f\n" % (uv[0], 1 - uv[1]))
            cur = None
            for a, bb, m, c in g["tris"]:
                tex = g["mats"][m] if m < len(g["mats"]) else None
                if tex != cur:
                    f.write(f"usemtl {tex or 'none'}\n")
                    mats.add(tex or "none")
                    cur = tex
                if g["uvs"]:
                    f.write(f"f {a+base}/{a+base} {bb+base}/{bb+base} {c+base}/{c+base}\n")
                else:
                    f.write(f"f {a+base} {bb+base} {c+base}\n")
            base += len(g["verts"])
    with open(os.path.join(outdir, name + ".mtl"), "w") as f:
        for m in mats:
            f.write(f"newmtl {m}\nKd 1 1 1\n")
            if m != "none":
                f.write(f"map_Kd {m.lower()}.png\n")
    return len(atomics), sum(len(g["tris"]) for g in geoms)


def geometry(b, p, s):
    kids = list(chunks(b, p, p + s))
    st = kids[0][1]
    flags, ntri, nvert, nmorph = struct.unpack_from("<IiiI", b, st)
    q = st + 16
    native = flags & 0x01000000
    uvsets = (flags >> 16) & 0xFF or (1 if flags & 0x04 else 0)
    if flags & 0x08:
        q += 4 * nvert
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
        tris.append((v1, v2, m, v3))
    q += 16  # bounding sphere
    hasv, hasn = struct.unpack_from("<II", b, q)
    q += 8
    verts = [struct.unpack_from("<3f", b, q + 12 * i) for i in range(nvert)] if hasv else []
    mats = []
    for t, pp, ss, vv in kids[1:]:
        if t == 0x08:
            for t2, p2, s2, v2 in chunks(b, pp, pp + ss):
                if t2 == 0x07:
                    tex = None
                    for t3, p3, s3, v3 in chunks(b, p2, p2 + s2):
                        if t3 == 0x06:
                            for t4, p4, s4, v4 in chunks(b, p3, p3 + s3):
                                if t4 == 0x02:
                                    tex = b[p4:p4 + s4].split(b"\0")[0].decode()
                                    break
                    mats.append(tex)
    return {"verts": verts, "uvs": uvs, "tris": tris, "mats": mats, "native": native}


if __name__ == "__main__":
    gd, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    img = Img(gd)
    for n in sys.argv[3:]:
        if n + ".txd" in img.ent:
            print(n, "txd:", txd(img.read(n + ".txd"), outdir))
        if n + ".dff" in img.ent:
            print(n, "dff atomics, tris:", dff(img.read(n + ".dff"), n, outdir))
