#!/usr/bin/env python3
"""Reproduce the bounded native world-scheduling experiment without launching macVC."""

import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Audit rejects an unsupported or already-patched input before any JIT run.
    subprocess.run([sys.executable, str(ROOT / "tools/physics/audit_native.py"),
                    "--binary", str(args.binary.resolve()), "--output-dir", str(output)], check=True)
    executable = output / "native-world-probe"
    with (output / "native-build.log").open("w") as log:
        subprocess.run(["xcrun", "clang++", "-std=c++17", "-O2", "-Wall", "-Wextra",
                        "-arch", "arm64", "-mmacosx-version-min=15.0", "-I", str(ROOT / "tests"),
                        str(ROOT / "docs/research/world-physics-native-probe.cpp"),
                        "-o", str(executable)], stdout=log, stderr=subprocess.STDOUT, check=True)
    environment = {**os.environ, "MACVC_NATIVE": str(args.binary.resolve())}
    for name, arguments, expected in (("guarded", [], 0),
                                     ("unguarded", ["--no-suppression"], 1)):
        result = subprocess.run([str(executable), *arguments], env=environment,
                                capture_output=True, text=True, timeout=10)
        (output / f"{name}.txt").write_text(result.stdout + result.stderr + f"exit={result.returncode}\n")
        print(f"{name}: expected_exit={expected}; observed_exit={result.returncode}", flush=True)
        print(result.stdout, end="")
        if result.returncode != expected:
            return 1
        if name == "unguarded" and result.stdout.count("FAIL") != 3:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
