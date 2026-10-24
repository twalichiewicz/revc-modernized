#!/usr/bin/env python3
"""Static hook patcher for the macVC (reVC) arm64 binary.

macOS on Apple Silicon refuses to make a modified code page executable again,
so hooks cannot be installed at runtime. Instead this tool rewrites the binary
on disk (it is then re-signed ad hoc):

  * The first instruction of every hooked function F becomes `b T`.
  * T is a 24-byte stub placed in the unused header padding of __TEXT:
        adrp x16, SLOT@PAGE
        ldr  x16, [x16, SLOT@PAGEOFF]
        cbz  x16, 1f          ; slot empty -> behave like vanilla
        br   x16              ; slot set   -> jump to the plugin
    1:  <displaced first instruction of F>
        b    F+4
  * SLOT is an 8-byte pointer in the zero-filled slack at the end of __DATA.
    The plugin fills it from its constructor; until then (or if the plugin is
    missing, since it is a *weak* dylib) the game runs unmodified.
  * A table (magic "VCMMHK01") next to the stubs tells the plugin where each
    function, slot and "call original" entry point lives.

Skip patches use the same stub shape on one instruction inside a function,
but the fourth word is `b TARGET` instead of `br x16`: a non-zero slot then
works as a flag that jumps over a block of code (used to hide HUD items).

Only stdlib is used. Run with -I.
"""
import argparse
import hashlib
import struct
import sys

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_UUID = 0x1B
LC_LOAD_WEAK_DYLIB = 0x80000018
TABLE_MAGIC = b"VCMMHK01"
TABLE_VERSION = 1
NAME_LEN = 96
ENTRY_FMT = "<%dsIIII" % NAME_LEN          # name, func_off, slot_off, orig_off, reserved
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)
HEADER_FMT = "<8sII16s"                    # magic, version, count, uuid of patched image
HEADER_SIZE = struct.calcsize(HEADER_FMT)
STUB_SIZE = 24

# Hooks used by the minimap plugin (mangled names, without the leading '_').
DEFAULT_HOOKS = [
    "_ZN6CRadar12DrawRadarMapEv",
    "_ZN6CRadar32TransformRadarPointToScreenSpaceER9CVector2DRKS0_",
    "_ZN6CRadar9DrawBlipsEv",
    "_ZN6CRadar13DrawCoordBlipEi",
    "_ZN6CRadar14DrawEntityBlipEi",
    "_ZN9CSprite2d4DrawERK5CRectRK5CRGBA",
    "_ZN4CHud4DrawEv",
    "_ZN12CMenuManager16ProcessUserInputEhhhha",
    "_ZN5CFont11PrintStringEffPt",
    "_ZN5CFont8SetScaleEff",
    "_ZN9CSprite2d4DrawERK5CRectRK5CRGBAffffffff",
    # pause menu: tabs, compact map legend
    "_ZN12CMenuManager14DrawBackgroundEb",
    "_ZN12CMenuManager9UserInputEv",
    "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA",
    "_ZN9CSprite2d13Draw2DPolygonEffffffffRK5CRGBA",
    "_ZN6CRadar10DrawLegendEiii",
    # controller: weapon/radio wheels, D-pad actions
    "_ZN4CPad6UpdateEs",
    # over-the-shoulder aiming
    "_ZN4CCam26Process_FollowPedWithMouseERK7CVectorfff",
    # free-aim drive-bys
    "_ZN11CAutomobile18DoDriveByShootingsEv",
    "_ZN5CBike18DoDriveByShootingsEv",
    "_ZN5CBoat18DoDriveByShootingsEv",
    "_ZN7CWeapon19DoDriveByAutoAimingEP7CEntityP8CVehicleP7CVectorS5_",
    "_ZN4CCam20Process_FollowCar_SAERK7CVectorfff",
    "_ZN4CPed9PreRenderEv",
    # phone calls in vehicles, the brick phone
    "_ZN11CTheScripts7ProcessEv",
    "_ZN4CPed15SetAnswerMobileEv",
    "_ZN4CPed17ClearAnswerMobileEv",
]

# Flag-controlled skips inside CHud::Draw: (name, instruction address, expected
# instruction word, where to continue when the flag is set). Each address is
# the single entry of a block, and the target is where the game itself jumps
# when it hides that block (health/armour flashing).
DEFAULT_SKIPS = [
    ("skip:hud_health", 0x1001F8DEC, 0xF94033E8, 0x1001F8FB4),  # ldr x8, [sp, #0x60]
    ("skip:hud_armour", 0x1001F8FDC, 0xBD400108, 0x1001F91F8),  # ldr s8, [x8]
    # In CPlayerPed::ProcessPlayerWeapon: the M4, Ruger and M60 aim in first person
    # (as the sniper rifles do); when set, they take the third-person path instead.
    ("skip:rifle_first_person", 0x1001D480C, 0xAA1403E0, 0x1001D4794),  # mov x0, x20
    # In CAutomobile/CBike/CBoat::DoDriveByShootings: only SMGs shoot from
    # vehicles; when set, the weapon check is passed over (for pistols).
    ("skip:driveby_pistol_car", 0x10024E138, 0x7100151F, 0x10024E140),  # cmp w8, #0x5
    ("skip:driveby_pistol_bike", 0x100261924, 0x7100151F, 0x10026192C),
    ("skip:driveby_pistol_boat", 0x10026A6A8, 0x7100151F, 0x10026A6B0),
]
KIND_HOOK, KIND_SKIP = 0, 1

# The vanilla binary these hooks and the plugin's offsets were derived from.
VANILLA_SHA256 = "a715c2ce363265385b73ac6d2decd386f7d8bbccb61519766907a563a663cc95"


class PatchError(Exception):
    pass


class MachO:
    def __init__(self, data):
        self.data = bytearray(data)
        magic, cputype, _, _, self.ncmds, self.sizeofcmds, _, _ = struct.unpack_from("<IiiIIIII", self.data, 0)
        if magic != MH_MAGIC_64 or cputype != CPU_TYPE_ARM64:
            raise PatchError("not a thin arm64 Mach-O")
        self.segments = {}
        self.sections = []
        self.uuid = None
        self.symtab = None
        off = 32
        for _ in range(self.ncmds):
            cmd, size = struct.unpack_from("<II", self.data, off)
            if cmd == LC_SEGMENT_64:
                name = self.data[off + 8:off + 24].rstrip(b"\0").decode()
                vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", self.data, off + 24)
                nsects = struct.unpack_from("<I", self.data, off + 64)[0]
                self.segments[name] = (vmaddr, vmsize, fileoff, filesize)
                for i in range(nsects):
                    s = off + 72 + i * 80
                    sect = self.data[s:s + 16].rstrip(b"\0").decode()
                    seg = self.data[s + 16:s + 32].rstrip(b"\0").decode()
                    addr, ssize = struct.unpack_from("<QQ", self.data, s + 32)
                    soff = struct.unpack_from("<I", self.data, s + 48)[0]
                    self.sections.append((seg, sect, addr, ssize, soff))
            elif cmd == LC_UUID:
                self.uuid = bytes(self.data[off + 8:off + 24])
            elif cmd == LC_SYMTAB:
                self.symtab = struct.unpack_from("<IIII", self.data, off + 8)
            off += size
        self.base = self.segments["__TEXT"][0]

    def section(self, seg, sect):
        for s in self.sections:
            if s[0] == seg and s[1] == sect:
                return s
        raise PatchError("missing section %s,%s" % (seg, sect))

    def vm_to_file(self, vm):
        for vmaddr, vmsize, fileoff, filesize in self.segments.values():
            if vmaddr <= vm < vmaddr + filesize:
                return fileoff + vm - vmaddr
        raise PatchError("address 0x%x is not file-backed" % vm)

    def symbols(self):
        symoff, nsyms, stroff, _ = self.symtab
        out = {}
        for i in range(nsyms):
            strx, ntype, nsect, _, value = struct.unpack_from("<IBBhQ", self.data, symoff + i * 16)
            if ntype & 0x0E == 0x0E and nsect != 0:  # N_SECT, defined
                end = self.data.index(b"\0", stroff + strx)
                out[self.data[stroff + strx:end].decode()] = value
        return out

    def find_table(self):
        start = 32 + self.sizeofcmds
        text_off = self.section("__TEXT", "__text")[4]
        i = self.data.find(TABLE_MAGIC, start, text_off)
        return i if i >= 0 else None


# --- arm64 encoding helpers -------------------------------------------------

def enc_b(pc, target):
    delta = target - pc
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise PatchError("b out of range")
    return 0x14000000 | ((delta >> 2) & 0x3FFFFFF)


def enc_adrp(pc, target, rd):
    imm = ((target & ~0xFFF) - (pc & ~0xFFF)) >> 12
    if not -(1 << 20) <= imm < (1 << 20):
        raise PatchError("adrp out of range")
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | rd


def enc_ldr_x_uimm(rt, rn, offset):
    if offset % 8 or not 0 <= offset < 4096 * 8:
        raise PatchError("bad ldr offset")
    return 0xF9400000 | ((offset // 8) << 10) | (rn << 5) | rt


def enc_cbz_x(pc, target, rt):
    delta = target - pc
    return 0xB4000000 | (((delta >> 2) & 0x7FFFF) << 5) | rt


def enc_br(rn):
    return 0xD61F0000 | (rn << 5)


def is_adrp(insn):
    return (insn & 0x9F000000) == 0x90000000


def relocate_adrp(insn, old_pc, new_pc):
    """Re-encodes an adrp so it still yields the same page from new_pc (same image)."""
    imm = (((insn >> 5) & 0x7FFFF) << 2) | ((insn >> 29) & 3)
    if imm & (1 << 20):
        imm -= 1 << 21
    page = (old_pc & ~0xFFF) + (imm << 12)
    return enc_adrp(new_pc, page, insn & 0x1F)


def is_pc_relative(insn):
    return ((insn & 0x1F000000) == 0x10000000       # adr / adrp
            or (insn & 0x7C000000) == 0x14000000    # b / bl
            or (insn & 0xFF000010) == 0x54000000    # b.cond
            or (insn & 0x7E000000) == 0x34000000    # cbz / cbnz
            or (insn & 0x7E000000) == 0x36000000    # tbz / tbnz
            or (insn & 0x3B000000) == 0x18000000)   # ldr (literal)


# --- patching -----------------------------------------------------------------

def add_weak_dylib(m, path):
    name = path.encode() + b"\0"
    size = (24 + len(name) + 7) & ~7
    cmd = struct.pack("<IIIIII", LC_LOAD_WEAK_DYLIB, size, 24, 2, 0x10000, 0x10000) + name
    cmd += b"\0" * (size - len(cmd))
    end = 32 + m.sizeofcmds
    if any(m.data[end:end + size]):
        raise PatchError("no free space after load commands")
    m.data[end:end + size] = cmd
    m.ncmds += 1
    m.sizeofcmds += size
    struct.pack_into("<II", m.data, 16, m.ncmds, m.sizeofcmds)


def patch(data, hooks, dylib_path, skips=()):
    m = MachO(data)
    if m.find_table() is not None:
        raise PatchError("binary is already patched")
    syms = m.symbols()

    # (name, address, kind, skip target) for every patched instruction.
    sites = []
    for name in hooks:
        func = syms.get("_" + name)
        if func is None:
            raise PatchError("symbol not found: " + name)
        sites.append((name, func, KIND_HOOK, 0))
    for name, addr, expected, target in skips:
        word = struct.unpack_from("<I", m.data, m.vm_to_file(addr))[0]
        if word != expected:
            raise PatchError("unexpected instruction at 0x%x for %s (0x%08x)" % (addr, name, word))
        sites.append((name, addr, KIND_SKIP, target))
    count = len(sites)

    # Slots: zero-filled slack after the last __DATA section.
    seg_vm, seg_size = m.segments["__DATA"][:2]
    data_end = max(a + s for g, _, a, s, _ in m.sections if g == "__DATA")
    slot_base = (data_end + 7) & ~7
    if slot_base + 8 * count > seg_vm + seg_size:
        raise PatchError("not enough slack at the end of __DATA")

    # Stubs + table: the tail of the header padding, right before __text.
    text_off = m.section("__TEXT", "__text")[4]
    blob_size = HEADER_SIZE + ENTRY_SIZE * count + STUB_SIZE * count
    blob_off = (text_off - blob_size) & ~0xF
    lc_room_needed = 32 + m.sizeofcmds + 256  # new load command + margin
    if blob_off < lc_room_needed or any(m.data[32 + m.sizeofcmds:text_off]):
        raise PatchError("header padding is too small or not empty")

    add_weak_dylib(m, dylib_path)

    stubs_off = blob_off + HEADER_SIZE + ENTRY_SIZE * count
    entries = []
    for i, (name, addr, kind, target) in enumerate(sites):
        addr_file = m.vm_to_file(addr)
        first = struct.unpack_from("<I", m.data, addr_file)[0]
        slot = slot_base + 8 * i
        stub_file = stubs_off + STUB_SIZE * i
        stub = m.base + stub_file          # __TEXT fileoff 0 == vmaddr base
        if is_adrp(first):
            first = relocate_adrp(first, addr, stub + 16)
        elif is_pc_relative(first):
            raise PatchError("instruction at %s is PC-relative (0x%08x)" % (name, first))
        words = [
            enc_adrp(stub, slot, 16),
            enc_ldr_x_uimm(16, 16, slot & 0xFFF),
            enc_cbz_x(stub + 8, stub + 16, 16),
            enc_br(16) if kind == KIND_HOOK else enc_b(stub + 12, target),
            first,
            enc_b(stub + 20, addr + 4),
        ]
        struct.pack_into("<6I", m.data, stub_file, *words)
        struct.pack_into("<I", m.data, addr_file, enc_b(addr, stub))
        entries.append(struct.pack(ENTRY_FMT, name.encode(), addr - m.base, slot - m.base,
                                   stub + 16 - m.base, kind))
    header = struct.pack(HEADER_FMT, TABLE_MAGIC, TABLE_VERSION, count, m.uuid or b"\0" * 16)
    blob = header + b"".join(entries)
    m.data[blob_off:blob_off + len(blob)] = blob
    return bytes(m.data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--dylib", default="@executable_path/../Frameworks/libVCModernHUD.dylib")
    ap.add_argument("--hook", action="append", help="mangled symbol (no leading _); default: minimap hooks")
    ap.add_argument("--any-binary", action="store_true", help="skip the vanilla macVC checksum check")
    args = ap.parse_args()

    data = open(args.input, "rb").read()
    digest = hashlib.sha256(data).hexdigest()
    if not args.any_binary and digest != VANILLA_SHA256:
        sys.exit("refusing: %s is not the vanilla macVC 1.0.5217 binary (sha256 %s)" % (args.input, digest))
    hooks = args.hook or DEFAULT_HOOKS
    skips = [] if args.hook else DEFAULT_SKIPS  # the skips are specific to the vanilla binary
    try:
        out = patch(data, hooks, args.dylib, skips)
    except PatchError as e:
        sys.exit("patch failed: %s" % e)
    with open(args.output, "wb") as f:
        f.write(out)
    print("patched %d hooks and %d skips -> %s (re-sign before running)" % (len(hooks), len(skips), args.output))


if __name__ == "__main__":
    main()
