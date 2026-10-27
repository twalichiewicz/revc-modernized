"""The game's own models, read to measure them (nothing of them is kept): gta3.img's entries and a DFF's
first geometry. No Blender needed.

From the viewmodel spike's vcextract.py and vcdff.py. RenderWare 3.3 geometry carries 12 bytes of lighting
after its header.
"""
import struct


class Img:
    def __init__(self, game_data):
        d = open(game_data + "/models/gta3.dir", "rb").read()
        self.f = open(game_data + "/models/gta3.img", "rb")
        self.entries = {}
        for i in range(0, len(d), 32):
            off, size = struct.unpack_from("<II", d, i)
            self.entries[d[i + 8:i + 32].split(b"\0")[0].decode().lower()] = (off, size)

    def read(self, name):
        off, size = self.entries[name.lower()]
        self.f.seek(off * 2048)
        return self.f.read(size * 2048)


def chunks(b, start, end):
    p = start
    while p + 12 <= end:
        t, s, v = struct.unpack_from("<III", b, p)
        yield t, p + 12, s, v
        p += 12 + s


def first_geometry_vertices(b):
    """The vertices (x, y, z) of a DFF's first geometry."""
    clump = next((p, s) for t, p, s, v in chunks(b, 0, len(b)) if t == 0x10)
    glist = next((p, s) for t, p, s, v in chunks(b, *_span(clump)) if t == 0x1A)
    geo = next((p, s) for t, p, s, v in chunks(b, *_span(glist)) if t == 0x0F)
    kids = list(chunks(b, *_span(geo)))
    st, ver = kids[0][1], kids[0][3]
    flags, ntri, nvert, nmorph = struct.unpack_from("<IiiI", b, st)
    q = st + 16
    lib = (((ver >> 14) & 0x3FF00) + 0x30000) | ((ver >> 16) & 0x3F) if ver & 0xFFFF0000 else ver << 8
    if lib < 0x34000:
        q += 12  # ambient, specular, diffuse
    if flags & 0x08:
        q += 4 * nvert  # prelit colours
    uvsets = (flags >> 16) & 0xFF or (1 if flags & 0x04 else 0)
    q += 8 * nvert * uvsets
    q += 8 * ntri
    q += 16 + 8  # bounding sphere, has vertices / normals
    return [struct.unpack_from("<3f", b, q + 12 * i) for i in range(nvert)]


def _span(ps):
    p, s = ps
    return p, p + s
