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
  * A table (magic "VCMMHK01", version 2) next to the stubs tells the plugin
    where each function, slot and "call original" entry point lives, each by
    its name's 32-bit FNV-1a hash (version 1 kept the 96-byte names, which no
    longer fit in the padding with all the hooks).

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
TABLE_VERSION = 2
ENTRY_FMT = "<IIIII"                       # name hash, func_off, slot_off, orig_off, kind
ENTRY_SIZE = struct.calcsize(ENTRY_FMT)
HEADER_FMT = "<8sII16s"                    # magic, version, count, uuid of patched image
HEADER_SIZE = struct.calcsize(HEADER_FMT)
STUB_SIZE = 24


def name_hash(name):
    """FNV-1a, 32 bits: how the table names a hook (src/hookname.h computes the same)."""
    h = 0x811C9DC5
    for c in name.encode():
        h = ((h ^ c) * 0x01000193) & 0xFFFFFFFF
    return h

# Hooks used by the minimap plugin (mangled names, without the leading '_').
DEFAULT_HOOKS = [
    # Car handling (#37): each car's line retuned by type as the game converts it to game units.
    "_ZN16cHandlingDataMgr22ConvertDataToGameUnitsEP13tHandlingData",
    # Supplement verified missing upper-tower surfaces through native collision.
    "_ZN9CPhysical22ProcessEntityCollisionEP7CEntityP9CColPoint",
    "_ZN11CAutomobile22ProcessEntityCollisionEP7CEntityP9CColPoint",
    "_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pf",
    # Preserve ordinary building detail admission at aircraft altitude.
    "_ZN16CSimpleModelInfo21GetAtomicFromDistanceEf",
    # Bound the native traffic scan shared by cars and ambient boats.
    "_ZN8CCarCtrl30FindAngleToWeaveThroughTrafficEP8CVehicleP9CPhysicalff",
    # Cocoa fullscreen Space state is separate from the renderer's video mode.
    "_Z15LoadINISettingsv",
    "_Z15SaveINISettingsv",
    "_psSetVideoMode",
    # the ray-traced shadows' moving geometry, checked before Metal traces it
    "_ZN2rw3gl324mtlrtSetDynamicTrianglesEPKfi",
    "_ZN2rw3gl317mtlrtAddTrianglesEPKfi",
    "_ZN2rw3gl328mtlrtAddTranslucentTrianglesEPKfi",
    "_ZN2rw3gl317mtlrtTraceShadowsEPKNS0_9MtlRtViewEiiPhS4_S4_",
    # This exact native build specializes the local RT light-candidate emitter.
    "_ZNSt3__16vectorIZN10CRTShadows6RenderEvE9CandidateNS_9allocatorIS2_EEE9push_backB9nqe210106ERKS2_",
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
    # the police scanner, only in a police vehicle (scanner.h)
    "_ZN13cAudioManager18ServicePoliceRadioEv",
    # the Stats page's own list, under the redrawn one (statspage.h)
    "_ZN12CMenuManager10PrintStatsEv",
    "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA",
    "_ZN9CSprite2d13Draw2DPolygonEffffffffRK5CRGBA",
    "_ZN6CRadar10DrawLegendEiii",
    # reVC's own menu text, missing from the port's text files
    "_ZN5CText3GetEPKc",
    # controller: weapon/radio wheels, D-pad actions
    "_ZN4CPad6UpdateEs",
    # over-the-shoulder aiming, the gun raised while aiming
    "_ZN4CCam26Process_FollowPedWithMouseERK7CVectorfff",
    "_ZN10CPlayerPed19ProcessPlayerWeaponEP4CPad",
    "_ZN10CPlayerPed14ProcessControlEv",
    # free-aim drive-bys
    "_ZN11CAutomobile18DoDriveByShootingsEv",
    "_ZN5CBike18DoDriveByShootingsEv",
    "_ZN5CBoat18DoDriveByShootingsEv",
    "_ZN7CWeapon19DoDriveByAutoAimingEP7CEntityP8CVehicleP7CVectorS5_",
    "_ZN4CCam20Process_FollowCar_SAERK7CVectorfff",
    "_ZN4CPed9PreRenderEv",
    # Physical NPC reactions: base control and native contact ownership during a fall.
    "_ZN4CPed14ProcessControlEv",
    "_ZN4CPed4SeekEv",                       # police foot-chase pace (police.cpp)
    "_ZN6CCarAI19MellowOutChaseSpeedEP8CVehicle",  # police arrival speed (police.cpp)
    "_ZN4CPed22ProcessEntityCollisionEP7CEntityP9CColPoint",
    "_ZN9CPhysical16ProcessCollisionEv",
    # Resolve articulated NPC contacts after native world collision completes.
    "_ZN6CWorld7ProcessEv",
    # phone calls in vehicles, the brick phone
    "_ZN11CTheScripts7ProcessEv",
    "_ZN4CPed15SetAnswerMobileEv",
    "_ZN4CPed17ClearAnswerMobileEv",
    # controller rumble
    "_ZN4CPad10StartShakeEsh",
    "_ZN4CPad19StartShake_DistanceEshfff",
    "_ZN4CPed13InflictDamageEP7CEntity11eWeaponTypef14ePedPieceTypesh",
    "_ZN4CPed14CollideWithPedEPS_",
    "_ZN4CPed7SetFallEi11AnimationIdh",
    # car radios heard from outside
    "_ZN13cAudioManager19FindVehicleOfPlayerEv",
    "_ZN14cSampleManager23SetStreamedVolumeAndPanEhhhh",
    "_ZN13cMusicManager15ServiceGameModeEv",
    # the station logo instead of its name
    "_ZN13cMusicManager23DisplayRadioStationNameEv",
    # businesses on credit
    "_ZN7CPickup6UpdateEP10CPlayerPedP8CVehiclei",
    "_ZN4CHud14SetHelpMessageEPtbb",
    # swimming and climbing
    "_ZN4CPed15ProcessBuoyancyEv",
    "_ZN4CPed7SetJumpEv",
    "_ZN4CCam15IsTargetInWaterERK7CVector",
    "_ZN8CVehicle13CanPedExitCarEb",
    "_ZN4CPed13PlayFootStepsEv",
    # vehicles' animations wanted before they're loaded (a crash getting off a bus)
    "_ZN12CAnimManager12AddAnimationEPN2rw5ClumpE12AssocGroupId11AnimationId",
    "_ZN12CAnimManager14BlendAnimationEPN2rw5ClumpE12AssocGroupId11AnimationIdf",
    # cars' lights: no light boxes (a GTA III leftover), the lights glowing on the model instead
    "_ZN13CBrightLights11RegisterOneE7CVectorS0_S0_S0_hhhh",
    "_ZN11CAutomobile6RenderEv",
    "_ZN11CAutomobile9PreRenderEv",
    "_ZN10CSpecialFX6RenderEv",
    "_ZN9CRenderer24RenderFirstPersonVehicleEv",
    "_ZN8CCoronas14RegisterCoronaEjhhhhRK7CVectorffhahhhfbf",
    # first person, camera shake, spawn distances; car variety
    "_ZN7CCamera7ProcessEv",
    "_ZN8CCarCtrl14ChooseCarModelEi",
    "_ZN9CRenderer21SetupEntityVisibilityEP7CEntity",
    "_ZN7CWeapon4FireEP7CEntityP7CVector",
    "_ZN7CWeapon14FireInstantHitEP7CEntityP7CVector",
    "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType",
    "_ZN7CWeapon21FireInstantHitFromCarEP8CVehiclebb",
    "_ZN11CBulletInfo9AddBulletEP7CEntity11eWeaponType7CVectorS3_",
    "_ZN11CBulletInfo6UpdateEv",
    "_Z22FireOneInstantHitRoundP7CVectorS0_i",
    "_ZN10CCollision18ProcessLineOfSightERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbb",
    "_ZN7CWeapon31CheckForShootingVehicleOccupantEPP7CEntityP9CColPoint11eWeaponTypeRK7CVectorS8_",
    # Player aircraft forces, rotor world contacts/repair, and scoped native Dodo ownership.
    "_ZN8CVehicle13FlyingControlE12eFlightModel",
    "_ZN11CAutomobile14ProcessControlEv",
    "_ZN11CAutomobile13VehicleDamageEft",
    "_ZN14CDamageManager11ApplyDamageE10tComponentff",
    "_ZN9CPhysical14ApplyCollisionEPS_R9CColPointRfS3_",
    "_ZN9CPhysical14ApplyCollisionER9CColPointRf",
    "_ZN9CPhysical17ApplyCollisionAltEP7CEntityR9CColPointRfR7CVectorS6_",
    "_ZN11CAutomobile13SetDoorDamageEi6eDoorsb",
    "_ZN11CAutomobile15SetBumperDamageEi7ePanelsb",
    "_ZN7CEntity14DeleteRwObjectEv",
    "_ZN13cAudioManager14ProcessVehicleEP8CVehicle",
    "_ZN5CBoat14ProcessControlEv",
    "_ZN10CModelInfo8ShutDownEv",
    "_ZN6CWorld15ClearForRestartEv",
    "_ZN6CWorld8ShutDownEv",
    "_ZN8CVehicle16DoBladeCollisionE7CVectorR7CMatrixsff",
    "_ZN11CAutomobile3FixEv",
    "_ZN6CPlane14ProcessControlEv",
    "_ZN4CPed19SetPedPositionInCarEv",
    "_ZN4CPed11SetEnterCarEP8CVehiclej",
    "_ZN4CPed10SetExitCarEP8CVehiclej",
    "_ZN8CVehicle13InflictDamageEP7CEntity11eWeaponTypef7CVector",
    # gunmen: cover and moving
    "_ZN4CPed19KillCharOnFootArmedER7CVectorS1_S1_",
    # the police helicopter: hits where it is, smoke, less health
    "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D",
    "_ZN5CHeli19TestBulletCollisionEP7CVectorS1_S1_i",
    "_ZN5CHeli15PreRenderAlwaysEv",
    # ... no belly gun, SWAT in its doors, searching where he was seen (the wanted overhaul)
    "_ZN5CHeli14ProcessControlEv",
    # ... its searchlight under its chin
    "_ZN5CHeli6RenderEv",
    "_ZN5CHeli11UpdateHelisEv",
    # Vehicle interior material/light scope and directional race markers.
    "_Z27AtomicDefaultRenderCallBackPN2rw6AtomicE",
    "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhb",
    "_ZN7CEntity22ProcessLightsForEntityEv",
    "_ZN10C3dMarkers6RenderEv",
    # wet streets in the rain (#32): drawn once the world is
    "_ZN9CRenderer24RenderEverythingBarRoadsEv",
    # ...and its rain, in place of the game's streaks
    "_ZN8CWeather17RenderRainStreaksEv",
    # ...and its drops on the screen give way to the mod's
    "_ZN8CWeather7AddRainEv",
    "_ZN14ScreenDroplets6RenderEv",
    # ...and heavy rain closes the view in
    "_ZN10CTimeCycle6UpdateEv",
    # soaked ground under a car's wheels
    "_ZN8CVehicle12ProcessWheelER7CVectorS1_S1_S1_ifffaPfP11tWheelStatet",
    # police come by car, not made on foot near a wanted player
    "_ZN11CPopulation15AddToPopulationEffff",
    # his car's door: opened only at a crawl; faster, they hang on to it
    "_ZN4CPed7SeekCarEv",
    "_ZN8CVehicle14CanPedEnterCarEv",
    # lines of sight kept on the world's grid of sectors (the game reads past it for a point off it)
    "_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb",
    "_ZN6CWorld18ProcessLineOfSightERK7CVectorS2_R9CColPointRP7CEntitybbbbbbbb",
    # BuildPedLists lacks a lower world-sector bound. Clip its local rectangle.
    "_ZN5CRectC1Effff",
    # Keep physical Add/RemoveAndAdd sector membership inside the native grid.
    "_ZN9CPhysical12GetBoundRectEv",
    # High-altitude BIG terrain coverage, camera slant range and horizon.
    "_ZN9CRenderer19ConstructRenderListEv",
    "_ZN9CRenderer26SetupBigBuildingVisibilityEP7CEntity",
    "_ZN16CSimpleModelInfo26GetFirstAtomicFromDistanceEf",
    "_ZN18CVisibilityPlugins18RenderFadingAtomicEPN2rw6AtomicEf",
    "_ZN9CRenderer17RenderOneBuildingEP7CEntityf",
    "_Z23RwCameraSetFarClipPlanePN2rw6CameraEf",
    "_Z22RwCameraSetFogDistancePN2rw6CameraEf",
    "_ZN7CClouds13RenderHorizonEv",
    # the airliners: up in the clouds, lit by the sky, their lights
    "_ZN6CPlane6RenderEv",
    "_ZN6CPlane9PreRenderEv",
    # clouds and lighting; the port's ray-traced shadows' reports
    "_Z9re3_debugPKcz",
    "_ZN7CClouds6RenderEv",
    "_ZN7CCamera16RenderMotionBlurEv",
    # controls: hints name the mod's buttons
    "_ZN9CMessages31InsertPlayerControlKeysInStringEPt",
    "_ZN5CFont10ParseTokenEPtR5CRGBARbS3_",
    "_ZN5CFont10ParseTokenEPt",
    # gunshot sound: the M4's own pitch (gunbank.cpp)
    "_ZN14cSampleManager17InitialiseChannelEjjh",
    "_ZN14cSampleManager19SetChannelFrequencyEjj",
    # the sniper scope: first person's eye, sway, the zoom stops (sniper.cpp)
    "_ZN4CCam14Process_SniperERK7CVectorfff",
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


def reserve_slots(m, count):
    """Use zero-fill tail; the exact supported image may reserve one more page.

    LINKEDIT is address-independent metadata reached through its file offsets.
    Moving its terminal mapping preserves every game section/symbol and chained
    fixup target. This layout and its 5818 rebases were audited for this SHA only.
    """
    data_vm, data_size, data_file, data_bytes = m.segments["__DATA"]
    end = max(a + s for g, _, a, s, _ in m.sections if g == "__DATA")
    base = (end + 7) & ~7
    if base + 8 * count <= data_vm + data_size:
        return base
    page = 0x4000
    if hashlib.sha256(m.data).hexdigest() != VANILLA_SHA256:
        raise PatchError("extra hook storage requires the pristine supported macVC image")
    segments, commands = [], []
    at = 32
    for _ in range(m.ncmds):
        cmd, size = struct.unpack_from("<II", m.data, at)
        commands.append((cmd, at))
        if cmd == LC_SEGMENT_64:
            name = m.data[at + 8:at + 24].rstrip(b"\0").decode()
            segments.append((name, at))
        at += size
    offsets = dict(segments)
    link_vm, link_size, link_file, link_bytes = m.segments["__LINKEDIT"]
    if (segments[-1][0] != "__LINKEDIT" or link_vm != data_vm + data_size or
            any(x % page for x in (data_vm, data_size, link_vm, link_size)) or
            base < data_vm + data_bytes or base + 8 * count > link_vm + page or
            any(g == "__LINKEDIT" for g, _, _, _, _ in m.sections) or
            any(link_vm <= value < link_vm + link_size for value in m.symbols().values())):
        raise PatchError("unsupported writable-tail/LINKEDIT layout for extra hook storage")
    if (struct.unpack_from("<III", m.data, offsets["__DATA"] + 56) != (3, 3, 4) or
            struct.unpack_from("<III", m.data, offsets["__LINKEDIT"] + 56) != (1, 1, 0) or
            any(cmd in (0x22, 0x80000022, 0x1E) for cmd, _ in commands)):
        raise PatchError("unsupported protections or legacy fixups for extra hook storage")
    fixups = [off for cmd, off in commands if cmd == 0x80000034]
    if len(fixups) != 1:
        raise PatchError("supported chained fixups missing")
    fixup_file, fixup_size = struct.unpack_from("<II", m.data, fixups[0] + 8)
    starts = fixup_file + struct.unpack_from("<I", m.data, fixup_file + 4)[0]
    if (starts < fixup_file or starts + 4 + 4 * len(segments) > fixup_file + fixup_size or
            struct.unpack_from("<I", m.data, starts)[0] != len(segments) or
            struct.unpack_from("<I", m.data, starts + 4 + 4 * (len(segments) - 1))[0] != 0):
        raise PatchError("LINKEDIT has unsupported chained fixups")
    struct.pack_into("<Q", m.data, offsets["__DATA"] + 32, data_size + page)
    struct.pack_into("<Q", m.data, offsets["__LINKEDIT"] + 24, link_vm + page)
    m.segments["__DATA"] = (data_vm, data_size + page, data_file, data_bytes)
    m.segments["__LINKEDIT"] = (link_vm + page, link_size, link_file, link_bytes)
    return base


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
    hashes = [name_hash(n) for n, _, _, _ in sites]
    if len(set(hashes)) != count:
        raise PatchError("two hook names share a hash")

    slot_base = reserve_slots(m, count)

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
        entries.append(struct.pack(ENTRY_FMT, name_hash(name), addr - m.base, slot - m.base,
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
