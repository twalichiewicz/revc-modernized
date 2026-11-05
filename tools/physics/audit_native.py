#!/usr/bin/env python3
"""Read-only 5217 motion/virtual-dispatch inventory. Does not launch or patch macVC."""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
UUID = "02983a69-6bd0-33f7-b72b-2aca8bcb3c33"
SHA256 = "a715c2ce363265385b73ac6d2decd386f7d8bbccb61519766907a563a663cc95"
CLASSES = ("7CEntity", "9CPhysical", "11CAutomobile", "5CBoat", "5CHeli", "6CPlane", "4CPed",
           "5CBike", "7CObject", "9CBuilding", "6CDummy", "10CPlayerPed",
           "15CCutsceneObject", "12CDummyObject")
METHODS = ("_ZN6CWorld7ProcessEv", "_ZN9CPhysical14ProcessControlEv",
           "_ZN9CPhysical12ApplyGravityEv", "_ZN9CPhysical14ApplyMoveSpeedEv",
           "_ZN9CPhysical12ProcessShiftEv", "_ZN9CPhysical16ProcessCollisionEv",
           "_ZN11CAutomobile14ProcessControlEv", "_ZN5CBoat14ProcessControlEv",
           "_ZN5CHeli14ProcessControlEv", "_ZN6CPlane14ProcessControlEv", "_ZN4CPed14ProcessControlEv")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--llvm", type=Path, default=Path("/opt/homebrew/opt/llvm/bin"))
    args = parser.parse_args()
    # Reuse the repository's existing Mach-O parser; importing never calls its patcher main.
    spec = importlib.util.spec_from_file_location("physics_patch_reader", ROOT / "tools/patch_macvc.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    original = args.binary.read_bytes()
    image = module.MachO(bytearray(original))
    actual_uuid = str(uuid.UUID(bytes=image.uuid))
    if actual_uuid != UUID or hashlib.sha256(original).hexdigest() != SHA256 or image.find_table() is not None:
        raise RuntimeError("Audit requires the pristine supported 5217 image")
    symbols = image.symbols()
    # This image stores chained offsets, not relocated absolute vtable pointers.
    # Restrict decoding to the advertised format rather than guessing high bits.
    command = 32
    pointer_formats = set()
    for _ in range(image.ncmds):
        kind, size = struct.unpack_from("<II", original, command)
        if kind == 0x80000034:  # LC_DYLD_CHAINED_FIXUPS
            fixups = struct.unpack_from("<I", original, command + 8)[0]
            version, starts_offset = struct.unpack_from("<II", original, fixups)
            if version != 0:
                raise RuntimeError("Unsupported chained fixups version")
            starts = fixups + starts_offset
            count = struct.unpack_from("<I", original, starts)[0]
            for segment in range(count):
                offset = struct.unpack_from("<I", original, starts + 4 + 4 * segment)[0]
                if offset:
                    pointer_formats.add(struct.unpack_from("<H", original, starts + offset + 6)[0])
        command += size
    if pointer_formats != {6}:  # DYLD_CHAINED_PTR_64_OFFSET
        raise RuntimeError(f"Unsupported chained pointer formats: {pointer_formats}")
    names = {}
    for name, address in symbols.items():
        names.setdefault(address, []).append(name)
    result = {"uuid": actual_uuid, "sha256": hashlib.sha256(original).hexdigest(),
              "method": "LLVM static disassembly and file-backed vtable pointers; no game execution",
              "vtables": {}, "methods": {}}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    methods = list(METHODS)
    methods += [name for name in module.DEFAULT_HOOKS
                if (name.startswith("_ZN6CWorld") and ("LineOfSight" in name or "VerticalLine" in name))]
    methods += ["_ZN9CPhysical12RemoveAndAddEv", "_ZN6CWorld3AddEP7CEntity", "_ZN6CWorld6RemoveEP7CEntity"]
    for cls in CLASSES:
        base = symbols["__ZTV" + cls] + 16  # Itanium ABI: skip offset-to-top and RTTI.
        slots = {}
        for offset in (0x48, 0x50, 0x58):
            raw = struct.unpack_from("<Q", original, image.vm_to_file(base + offset))[0]
            if raw >> 63 or ((raw >> 36) & 0xFF):
                raise RuntimeError("Vtable slot is bound or tagged; explicit decoding is required")
            target = image.base + (raw & ((1 << 36) - 1))
            if target not in names:
                raise RuntimeError(f"Vtable target {target:#x} has no defined symbol")
            slots[hex(offset)] = {"raw": hex(raw), "target": hex(target), "symbols": names[target]}
            if offset == 0x48:
                control = next((name for name in names[target]
                                if name.endswith("14ProcessControlEv")), None)
                if control:
                    methods.append(control[1:])
        result["vtables"][cls] = slots
    for name in dict.fromkeys(methods):
        disassembly = subprocess.check_output([str(args.llvm / "llvm-objdump"), "--macho",
                         "--disassemble", "--dis-symname", "_" + name, str(args.binary)], text=True)
        if "Can't find" in disassembly:
            raise RuntimeError(f"Missing symbol in disassembly: {name}")
        (args.output_dir / f"{name}.asm").write_text(disassembly)
        calls = []
        lines = disassembly.splitlines()
        for index, line in enumerate(lines):
            parts = line.split("\t")
            if len(parts) >= 4 and parts[2] in ("bl", "blr", "b"):
                calls.append({"address": parts[0].rstrip(":"), "instruction": parts[2],
                              "operand": "\t".join(parts[3:]), "context": lines[max(0,index-3):index+1]})
        result["methods"][name] = {"address": hex(symbols["_" + name]),
            "listed_hook": name in module.DEFAULT_HOOKS, "calls": calls}
    (args.output_dir / "ownership.json").write_text(json.dumps(result, indent=2) + "\n")
    if args.binary.read_bytes() != original:
        raise RuntimeError("Input binary changed during read-only audit")
    print(f"Inventoried {len(CLASSES)} vtables and {len(result['methods'])} methods; UUID {actual_uuid}.")
    print("Static evidence only; runtime ownership and query synchronization remain separate gates.")


if __name__ == "__main__":
    main()
