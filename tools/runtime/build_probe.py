#!/usr/bin/env python3
"""Build an ad-hoc signed diagnostic sidecar, separate from production plugin."""
from __future__ import annotations
import argparse
import os
from pathlib import Path
import re
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[2]
ABI_PATTERNS = (
    "types.h", "game.h", "body.h", "config.h", "projection.h",
    "heat.h", "heat.cpp", "sight.h", "sight.cpp", "heli.cpp",
    "aircraft.h", "aircraft.cpp", "helizones.h", "helizones.cpp",
    "jetwheels.h", "jetwheels.cpp",
    "airportjets.h", "airportjets.cpp",
    "camera.h", "camera.cpp",
    "planeflight.h", "planeflight.cpp",
    "react*.h", "react*.cpp",
)

def stale_abi_sources(plugin: Path, root: Path = ROOT) -> list[Path]:
    """Reject current C++ mirrors paired with an older selected binary.

    No production build provenance stamp is emitted by this repo's Makefile.
    This conservative mtime check does not prove provenance for an arbitrary
    copied/touched binary; build --plugin from this source tree first.
    """
    built_at = plugin.stat().st_mtime_ns
    sources = {path for pattern in ABI_PATTERNS for path in (root / "src").glob(pattern)}
    return sorted(path for path in sources if path.stat().st_mtime_ns > built_at)

def run(*args: str) -> str:
    return subprocess.check_output(args, text=True)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin", type=Path, default=ROOT / "build/libVCModernHUD.dylib")
    parser.add_argument("--game", type=Path, default=Path("/Applications/Grand Theft Auto Vice City.app/Contents/MacOS/macVC"))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--no-hidden", action="store_true", help="native-only probe; do not inspect or read any plugin-local globals")
    args = parser.parse_args()
    args.out = args.out or ROOT / ("build/runtime-native" if args.no_hidden else "build/runtime")
    if not args.no_hidden:
        stale = stale_abi_sources(args.plugin)
        if stale:
            paths = "\n  ".join(str(path.relative_to(ROOT)) for path in stale)
            raise SystemExit(
                "Refusing hidden-layout build: ABI sources are newer than the selected plugin:\n  "
                + paths
                + "\nRebuild the production plugin from this source tree first, then rebuild the probe; "
                  "or use --no-hidden for native-only baseline profiling."
            )
    args.out.mkdir(parents=True, exist_ok=True)
    game_symbols = set(run("xcrun", "nm", "-gj", str(args.game)).splitlines())
    bindings = re.findall(r'B\([^,]+,\s*"([^"\n]+)"\)', (ROOT / "tools/runtime/probe.cpp").read_text())
    missing = [name for name in bindings if "_" + name not in game_symbols]
    if missing:
        raise SystemExit("Missing native exports: " + ", ".join(missing))
    print(f"Verified {len(bindings)} native symbol names in {args.game}")
    symbols = {}
    for line in ([] if args.no_hidden else run("xcrun", "nm", "-n", str(args.plugin)).splitlines()):
        match = re.fullmatch(r"([0-9a-fA-F]+)\s+\w\s+(.+)", line)
        if match:
            symbols[match[2]] = int(match[1], 16)
    plugin_uuid = uuid.UUID(int=0)
    if not args.no_hidden:
        info = run("xcrun", "dwarfdump", "--uuid", str(args.plugin))
        match = re.search(r"UUID: ([0-9A-Fa-f-]+) \(arm64\)", info)
        if not match:
            raise SystemExit("arm64 plugin UUID not found")
        plugin_uuid = uuid.UUID(match[1])
    people = symbols.get("__ZN9reactions12_GLOBAL__N_17gPeopleE", 0)
    grade = symbols.get("__ZN5grade12_GLOBAL__N_111gGradeReadyE", 0)
    wheel_snapshots = [name for name in symbols if name.startswith("__ZN9jetwheels8SnapshotE")]
    if len(wheel_snapshots) > 1:
        raise SystemExit("Ambiguous wheel snapshot ABI")
    extra = {
        "Wanted": "__ZN6wanted12_GLOBAL__N_16gModelE",
        "Eyes": "__ZN5sight12_GLOBAL__N_15gEyesE",
        "Wear": "__ZN4heli12_GLOBAL__N_15gWearE",
        "Crews": "__ZN4heli12_GLOBAL__N_16gCrewsE",
        "Config": "_gConfig",
        "PlaneForcesReady": "__ZN11planeflight12_GLOBAL__N_112gForcesReadyE",
        "PlaneRosterReady": "__ZN11planeflight12_GLOBAL__N_112gRosterReadyE",
        "PlaneAttempt": "__ZN11planeflight12_GLOBAL__N_18gAttemptE",
        "PlanePrevious": "__ZN11planeflight12_GLOBAL__N_19gPreviousE",
        "AirportReady": "__ZN11airportjets12_GLOBAL__N_16gReadyE",
        "AirportModel": "__ZN11airportjets12_GLOBAL__N_16gModelE",
        "AirportShapeReady": "__ZN11airportjets12_GLOBAL__N_111gShapeReadyE",
        "JetWheelSnapshot": wheel_snapshots[0] if wheel_snapshots else "",
        "JetGearDown": "__ZN11airportjets8GearDownEPKh",
        "JetEnginePower": "__ZN11planeflight11EnginePowerEPKh",
        "CameraCockpit": "__ZN6camera12_GLOBAL__N_18gCockpitE",
        "CameraFirstPerson": "__ZN6camera12_GLOBAL__N_112gFirstPersonE",
        "CameraHeadReady": "__ZN6camera12_GLOBAL__N_110gHeadReadyE",
        "CameraLookPitch": "__ZN6camera12_GLOBAL__N_110gLookPitchE",
        "CameraLookYaw": "__ZN6camera12_GLOBAL__N_18gLookYawE",
        "CameraHeadAbout": "__ZN6camera12_GLOBAL__N_110gHeadAboutE",
        "CameraHeadX": "__ZN6camera12_GLOBAL__N_110gHeadLocalE.0",
        "CameraHeadY": "__ZN6camera12_GLOBAL__N_110gHeadLocalE.1",
        "CameraHeadZ": "__ZN6camera12_GLOBAL__N_110gHeadLocalE.2",
        "Neck": "__ZNK12reactphysics4Body4NeckEv",
        "HeadAxis": "__ZNK12reactphysics4Body8HeadAxisEv",
    }
    wear_address = symbols.get(extra["Wear"], 0)
    next_address = min((value for value in symbols.values() if value > wear_address), default=wear_address)
    wear_stride = (next_address - wear_address) // 4 if wear_address else 0
    (args.out / "probe_offsets.h").write_text(
        "// Generated by tools/runtime/build_probe.py. Native-only builds emit no hidden offsets.\n"
        "#pragma once\n#include <cstdint>\n"
        f"constexpr bool kProbeHiddenEnabled = {'false' if args.no_hidden else 'true'};\n"
        "constexpr uint8_t kProbePluginUuid[16] = {" + ",".join(f"0x{b:02x}" for b in plugin_uuid.bytes) + "};\n"
        f"constexpr uintptr_t kProbePeopleAddress = 0x{people:x};\n"
        f"constexpr uintptr_t kProbeGradeAddress = 0x{grade:x};\n"
        + "".join(f"constexpr uintptr_t kProbe{name}Address = 0x{symbols.get(symbol, 0):x};\n" for name, symbol in extra.items())
        + f"constexpr size_t kProbeWearStride = {wear_stride};\n"
    )
    common = ["xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-arch", "arm64", "-mmacosx-version-min=15.0"]
    test = args.out / "probe_protocol_test"
    subprocess.run([*common, str(ROOT / "tools/runtime/probe_protocol_test.cpp"), "-o", str(test)], check=True)
    subprocess.run([str(test)], check=True)
    actions_test = args.out / "probe_actions_test"
    subprocess.run([*common, str(ROOT / "tools/runtime/probe_actions_test.cpp"), "-o", str(actions_test)], check=True)
    subprocess.run([str(actions_test)], check=True)
    output = args.out / "libVCMMProbe.dylib"
    # Replace the inode only after signing, so rebuilding cannot truncate the
    # mapped code of an existing diagnostic process.
    temporary = args.out / "libVCMMProbe.next.dylib"
    subprocess.run([*common, "-dynamiclib", "-fvisibility=hidden", "-I", str(args.out), str(ROOT / "tools/runtime/probe.cpp"), "-framework", "CoreFoundation", "-lobjc", "-o", str(temporary)], check=True)
    subprocess.run(["codesign", "--force", "--sign", "-", str(temporary)], check=True)
    os.replace(temporary, output)
    print(f"Built: {output}")
    print("Mode: native-only; all hidden globals disabled" if args.no_hidden else f"Plugin UUID: {plugin_uuid}; reactions=0x{people:x}; grade=0x{grade:x}")

if __name__ == "__main__":
    main()
