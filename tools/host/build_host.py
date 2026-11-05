#!/usr/bin/env python3
"""Build a distinct pinned source host and execute its actual engine wing queries.

Never installs into /Applications or starts normal gameplay. Uses the user's
installed DFF read-only. Requires the locked Homebrew audio versions. Two clean
build directories reproduce the host test; --control also builds the original
shared-model publication failure. Exit2 means build/process/provenance failure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def capture(args, **kw):
    return subprocess.check_output([str(x) for x in args], text=True, **kw).strip()


def run(args, log, expected=0, **kw):
    with log.open("w") as out:
        result = subprocess.run([str(x) for x in args], stdout=out, stderr=subprocess.STDOUT, **kw)
    if result.returncode != expected:
        raise RuntimeError(f"exit{result.returncode}, expected{expected}; see {log}")
    return result.returncode


def checkout(spec, path, logs):
    run(["git", "init", path], logs / (path.name + "-init.log"))
    run(["git", "-C", path, "remote", "add", "origin", spec["url"]], logs / (path.name + "-remote.log"))
    run(["git", "-C", path, "fetch", "--depth", "1", "origin", spec["commit"]], logs / (path.name + "-fetch.log"))
    run(["git", "-C", path, "checkout", "--detach", "FETCH_HEAD"], logs / (path.name + "-checkout.log"))
    if capture(["git", "-C", path, "rev-parse", "HEAD"]) != spec["commit"]:
        raise RuntimeError("Source revision mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True, help="new empty scratch directory")
    parser.add_argument("--game-data", type=Path, default=Path("/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData"))
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--control", action="store_true")
    args = parser.parse_args()
    work = args.work_dir.resolve()
    if work.exists() and any(work.iterdir()):
        raise RuntimeError("Work directory must be new/empty; existing artifacts are preserved")
    if not 1 <= args.jobs <= 16:
        raise RuntimeError("jobs must be in1..16")
    work.mkdir(parents=True, exist_ok=True)
    logs = work / "logs"; logs.mkdir()
    lock = json.loads((HERE / "source-lock.json").read_text())
    inputs = list(sorted(HERE.glob("*"))) + [ROOT / "tools/bake/visualmodel.cpp", ROOT / "tools/bake/visualmodel.h",
              ROOT / "tools/bake/gamefiles.cpp", ROOT / "tools/bake/gamefiles.h", ROOT / "src/mapdata.cpp", ROOT / "src/mapdata.h"]
    inputs = [p for p in inputs if p.is_file()]
    hashes = {str(p.relative_to(ROOT)): sha(p) for p in inputs}
    dependencies = {}
    for name, version in lock["system_dependencies"].items():
        prefix = Path("/opt/homebrew/opt") / name
        if not prefix.exists() or prefix.resolve().name != version:
            raise RuntimeError(f"Requires existing {name}{version} under {prefix}; no global packages are installed")
        dylib = prefix / "lib" / ("libopenal.1.dylib" if name == "openal-soft" else "libmpg123.0.dylib")
        headers = sorted((prefix / "include").rglob("*.h"))
        dependencies[name] = {"version": version, "prefix": str(prefix.resolve()), "dylib_sha256": sha(dylib),
                              "headers": {str(p.relative_to(prefix)): sha(p) for p in headers}}
    source, glfw, deps = work / "engine", work / "glfw", work / "deps"
    checkout(lock["engine"], source, logs)
    run(["git", "-C", source, "submodule", "update", "--init", "--depth", "1", "vendor/librw"], logs / "librw-checkout.log")
    if capture(["git", "-C", source / "vendor/librw", "rev-parse", "HEAD"]) != lock["librw"]["commit"]:
        raise RuntimeError("librw revision mismatch")
    checkout(lock["glfw"], glfw, logs)
    common = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + lock["deployment_target"]]
    run(["cmake", "-S", glfw, "-B", work / "glfw-build", *common, "-DCMAKE_INSTALL_PREFIX=" + str(deps),
         "-DGLFW_BUILD_EXAMPLES=OFF", "-DGLFW_BUILD_TESTS=OFF", "-DGLFW_BUILD_DOCS=OFF"], logs / "glfw-configure.log")
    run(["cmake", "--build", work / "glfw-build", "--parallel", args.jobs], logs / "glfw-build.log")
    run(["cmake", "--install", work / "glfw-build"], logs / "glfw-install.log")
    for name in ["host-entry.patch", "null-free.patch", "instance-geometry.patch"]:
        run(["git", "-C", source, "apply", HERE / name], logs / (name + ".log"))
    shutil.copyfile(HERE / "HostWingQuery.cpp", source / "src/extras/HostWingQuery.cpp")
    run(["xcrun", "clang++", "-std=c++17", "-O2", "-I" + str(ROOT / "src"), HERE / "export_wing.cpp",
         ROOT / "tools/bake/visualmodel.cpp", ROOT / "tools/bake/gamefiles.cpp", ROOT / "src/mapdata.cpp",
         "-lz", "-o", work / "export-wing"], logs / "export-build.log")
    wing = work / "wing.txt"
    run([work / "export-wing", args.game_data.resolve(), wing], logs / "export.log")
    builds = {}
    for label in (["control"] if args.control else []) + ["host-a", "host-b"]:
        build = work / label
        flags = f"-ffile-prefix-map={work}=/source-host -fmacro-prefix-map={work}=/source-host"
        if label == "control": flags += " -DHOST_WING_QUERY_BASELINE"
        command = ["cmake", "-S", source, "-B", build, *common, "-DCMAKE_CXX_FLAGS=" + flags,
                   "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DREVC_INSTALL=OFF",
                   "-DCMAKE_PREFIX_PATH=" + str(deps) + ";/opt/homebrew/opt/openal-soft;/opt/homebrew/opt/mpg123"]
        run(command, logs / (label + "-configure.log"))
        run(["cmake", "--build", build, "--parallel", args.jobs], logs / (label + "-build.log"))
        app = build / "src/reVC.app"; binary = app / "Contents/MacOS/reVC"
        expected = 1 if label == "control" else 0
        run([binary, "--host-wing-query", wing], logs / (label + "-query.log"), expected, timeout=30)
        text = (logs / (label + "-query.log")).read_text()
        summary = re.findall(r"host-wing-query checks=(\d+) failures=(\d+) faces=(\d+) .*", text)
        if len(summary) != 1 or int(summary[0][0]) < 53 or int(summary[0][2]) != 48 or (int(summary[0][1]) != (10 if expected else 0)):
            raise RuntimeError(f"Missing/incorrect query assertions in {label}")
        if label != "control":
            # Early argument failure must exit2 normally, including static COL
            # destruction before RenderWare initialized its memory callbacks.
            run([binary, "--host-wing-query", work / "missing-wing"], logs / (label + "-early-exit.log"), 2, timeout=10)
        builds[label] = {"app": str(app), "executable_sha256": sha(binary), "query_exit": expected, "summary": summary[0],
                         "linked_libraries": capture(["otool", "-L", binary]), "configure_command": [str(x) for x in command]}
        print(label, summary[0], "exit", expected, flush=True)
    if hashes != {str(p.relative_to(ROOT)): sha(p) for p in inputs}:
        raise RuntimeError("Build inputs changed during reproduction")
    manifest = {"lock": lock, "input_sha256": hashes, "dependencies": dependencies, "wing_sha256": sha(wing),
                "source_dff_sha256": sha(Path(str(wing) + ".dff")),
                "export": (logs / "export.log").read_text(), "engine_diff": capture(["git", "-C", source, "diff", "--binary"]),
                "toolchain": capture(["xcrun", "clang++", "--version"]), "sdk": capture(["xcrun", "--show-sdk-path"]),
                "cmake": capture(["cmake", "--version"]), "os": capture(["sw_vers"]), "builds": builds,
                "binary_identical": builds["host-a"]["executable_sha256"] == builds["host-b"]["executable_sha256"],
                "scope": "different source host; engine query test, no gameplay/solver/render damage acceptance"}
    (work / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print("Manifest:", work / "manifest.json", flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
