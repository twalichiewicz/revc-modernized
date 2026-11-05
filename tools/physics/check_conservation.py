#!/usr/bin/env python3
"""Build and run isolated ARM64 conservation probes against pinned Bullet source.

Does not install dependencies or change the game. Physical failures return 1;
build/provenance failures return 2. All source modifications and outputs are kept.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

PIN = "d3afe76132fc76b0f9b2359cbaef47a3766e7fbb"
ROOT = Path(__file__).resolve().parents[2]


def result_exit_code(statuses):
    # 1 means a completed physical assertion rejected the candidate. Signals
    # and unexpected process exits are failed experiments/harness errors.
    if any(code not in (0, 1) for code in statuses.values()):
        return 2
    return 1 if any(code == 1 for code in statuses.values()) else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--explicit-forces", action="store_true",
                        help="Evaluate the explicit spring/damping policy instead of the original soft-only momentum phase")
    parser.add_argument("--structure", action="store_true", help="Also evaluate material, structural and geometric-contact gates; failed gates return 1")
    args = parser.parse_args()
    source, build, output = (p.resolve() for p in (args.source, args.build_dir, args.output_dir))
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise RuntimeError(f"Expected source revision {PIN}, found {head}")
    untracked = subprocess.check_output(["git", "-C", str(source), "ls-files", "--others",
                                         "--exclude-standard"], text=True).splitlines()
    if untracked:
        raise RuntimeError(f"Untracked source inputs cannot be recorded: {untracked[:8]}; stage them first")
    patch = subprocess.check_output(["git", "-C", str(source), "diff", "HEAD", "--binary"])
    (output / "source.patch").write_bytes(patch)
    options = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64",
               "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0", "-DCMAKE_CXX_STANDARD=17",
               "-DCMAKE_POLICY_VERSION_MINIMUM=3.5", "-DUSE_DOUBLE_PRECISION=ON",
               "-DBUILD_CPU_DEMOS=OFF", "-DBUILD_BULLET2_DEMOS=OFF",
               "-DBUILD_OPENGL3_DEMOS=OFF", "-DBUILD_EXTRAS=OFF",
               "-DBUILD_UNIT_TESTS=OFF", "-DBUILD_PYBULLET=OFF", "-DINSTALL_LIBS=OFF"]
    libraries = ["BulletSoftBody", "BulletDynamics", "BulletCollision", "LinearMath"]
    provenance = {"revision": head, "source": str(source), "build": str(build),
                  "patch_sha256": hashlib.sha256(patch).hexdigest(), "cmake_options": options,
                  "explicit_forces": args.explicit_forces, "structure": args.structure,
                  "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  "fixtures_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                       for p in (ROOT / "docs" / "research").glob("world-physics-*")
                       if p.suffix in (".cpp", ".h")},
                  "compiler": subprocess.check_output(["xcrun", "clang++", "--version"], text=True)}
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    with (output / "build.log").open("w") as log:
        subprocess.run(["cmake", "-S", str(source), "-B", str(build), *options],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        # Always rebuild before compiling probes; edited inline headers must also
        # reach the world library, not merely the test translation unit.
        subprocess.run(["cmake", "--build", str(build), "--target", *libraries, "--parallel", "4"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
        statuses = {}
        cases = ["initial", "conservation", "step"] + (["material", "face-tree", "structure"] if args.structure else [])
        for name in cases:
            filename = ROOT / "docs" / "research" / ("world-physics-probe.cpp" if name == "initial"
                                                    else f"world-physics-{name}-probe.cpp")
            executable = output / f"probe-{name}"
            subprocess.run(["xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra",
                            "-arch", "arm64", "-mmacosx-version-min=15.0", "-DBT_USE_DOUBLE_PRECISION",
                            "-isystem", str(source / "src"), str(filename),
                            *(str(build / "src" / lib / f"lib{lib}.a") for lib in libraries),
                            "-o", str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
            flags = (["explicit-forces"] if name == "initial" else ["--explicit-forces"])
            result = subprocess.run([str(executable),
                                     *(flags if args.explicit_forces and name in ("initial", "step") else [])],
                                    capture_output=True, text=True, timeout=60)
            (output / f"{name}.txt").write_text(result.stdout + result.stderr + f"exit={result.returncode}\n")
            statuses[name] = result.returncode
            if name == "structure":
                for label, mode in (("geometry", "--contact-gate"), ("benchmark", "--benchmark")):
                    gate = subprocess.run([str(executable), mode], capture_output=True, text=True, timeout=60)
                    (output / f"{label}.txt").write_text(gate.stdout + gate.stderr + f"exit={gate.returncode}\n")
                    statuses[label] = gate.returncode
                    print(f"{label}: exit={gate.returncode}; {gate.stdout.splitlines()[-1] if gate.stdout.splitlines() else 'no standard output'}", flush=True)
            summary = result.stdout.splitlines()[-1] if result.stdout.splitlines() else "no standard output"
            print(f"{name}: exit={result.returncode}; {summary}", flush=True)
    provenance["exit_codes"] = statuses
    provenance["elapsed_seconds"] = time.monotonic() - started
    (output / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    return result_exit_code(statuses)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Conservation probe could not run: {error}", file=sys.stderr)
        sys.exit(2)
