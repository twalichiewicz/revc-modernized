#!/usr/bin/env python3
"""Reproduce the 5217 native query-authority gap; never launch or patch the game.

Exit 0: facts reproduced AND native authority assertions passed (or --facts-only).
Exit 1: facts reproduced, but native per-instance query authority is missing.
Exit 2: unsupported input, fixture/build/process/provenance failure.
This is not a complete plugin or live-game acceptance test.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
METHODS = (
    "__ZN9CPhysical22ProcessEntityCollisionEP7CEntityP9CColPoint",
    "__ZN6CWorld28ProcessLineOfSightSectorListER8CPtrListRK8CColLineR9CColPointRfRP7CEntitybbb",
    "__ZN6CWorld31GetIsLineOfSightSectorListClearER8CPtrListRK8CColLinebb",
    "__ZN6CWorld29ProcessVerticalLineSectorListER8CPtrListRK8CColLineR9CColPointRfRP7CEntitybP15CStoredCollPoly",
    "__ZN6CWorld27TestSphereAgainstSectorListER8CPtrList7CVectorfP7CEntityb",
    "__ZN10CCollision15TestLineOfSightERK8CColLineRK7CMatrixR9CColModelbb",
    "__ZN10CCollision11TestLineBoxERK8CColLineRK4CBox",
    "__Z6InvertRK7CMatrixRS_", "__ZmlRK7CMatrixRK7CVector",
    "__ZN6CPlane14ProcessControlEv", "__ZN6CPlane14DeleteRwObjectEv",
    "__ZN7CEntity14DeleteRwObjectEv", "__ZN9CPhysical12RemoveAndAddEv",
    "__ZN10CCollision20RemoveTrianglePlanesEP9CColModel",
)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--facts-only", action="store_true",
                        help="return success for reproducing facts, without accepting host authority")
    args = parser.parse_args()
    binary, output = args.binary.resolve(), args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    inputs = [Path(__file__).resolve(), ROOT / "tools/physics/audit_native.py",
              ROOT / "tools/patch_macvc.py", ROOT / "docs/research/world-physics-slice-native-probe.cpp",
              ROOT / "tests/nativejetshadowarm.h", ROOT / "tests/nativeweavearm.h"]
    hashes = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    original_hash = digest(binary)
    # Validate full pristine identity, not UUID alone, before executing any
    # copied instructions. The existing audit also rejects a patched image.
    with (output / "inventory.log").open("w") as log:
        subprocess.run([sys.executable, str(ROOT / "tools/physics/audit_native.py"),
                        "--binary", str(binary), "--output-dir", str(output / "native")],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    spec = importlib.util.spec_from_file_location("slice_patch_reader", ROOT / "tools/patch_macvc.py")
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    image = patch.MachO(bytearray(binary.read_bytes()))
    symbols = image.symbols()
    methods = {}
    for name in METHODS:
        address = symbols[name]
        assembly = subprocess.check_output(["/opt/homebrew/opt/llvm/bin/llvm-objdump", "--macho",
                     "--disassemble", "--dis-symname", name, str(binary)], text=True)
        if "Can't find" in assembly or name + ":" not in assembly:
            raise RuntimeError(f"Missing native method {name}")
        artifact = output / "native" / (name + ".asm")
        artifact.write_text(assembly)
        methods[name] = {"address": hex(address), "assembly": str(artifact.relative_to(output)),
                         "sha256": digest(artifact), "listed_hook": name[1:] in patch.DEFAULT_HOOKS}
    executable = output / "slice-native-probe"
    command = ["xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-arch", "arm64",
               "-mmacosx-version-min=15.0", "-I", str(ROOT / "tests"),
               str(ROOT / "docs/research/world-physics-slice-native-probe.cpp"), "-o", str(executable)]
    with (output / "build.log").open("w") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    runs = {}
    for mode, arguments in (("facts", []), ("authority", ["--require-authority"])):
        run = subprocess.run([str(executable), *arguments], env={**os.environ, "MACVC_NATIVE": str(binary)},
                             capture_output=True, text=True, timeout=10)
        (output / f"{mode}.txt").write_text(run.stdout + run.stderr + f"exit={run.returncode}\n")
        runs[mode] = {"exit": run.returncode, "log": f"{mode}.txt"}
        print(f"{mode}: exit={run.returncode}", flush=True)
        print(run.stdout, end="", flush=True)
        if run.returncode not in (0, 1) or mode == "facts" and run.returncode != 0:
            raise RuntimeError(f"Native {mode} fixture did not complete normally")
    if hashes != {str(p.relative_to(ROOT)): digest(p) for p in inputs} or digest(binary) != original_hash:
        raise RuntimeError("An input changed during the experiment")
    authority = runs["authority"]["exit"] == 0
    manifest = {"binary": str(binary), "binary_sha256": original_hash, "source_sha256": hashes,
                "build_command": command, "executable_sha256": digest(executable), "methods": methods,
                "runs": runs, "native_query_authority_pass": authority,
                "scope": "offline native COL selection and sphere LOS; synthetic objects/geometry; no plugin/game/solver",
                "in_app_contracts_proven": False,
                "facts_only": args.facts_only}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("In-app handoff remains gated on motion, all query routes, publication and lifecycle evidence.")
    return 0 if args.facts_only or authority else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"slice contract experiment failed: {error}", file=sys.stderr)
        sys.exit(2)
