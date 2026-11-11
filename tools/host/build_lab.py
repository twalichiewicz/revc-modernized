#!/usr/bin/env python3
"""Build the opt-in interactive mode; never installs or changes the game app."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from build_host import ROOT, HERE, checkout, run, capture, sha


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--work-dir", type=Path, required=True)
    p.add_argument("--game-data", type=Path, default=Path("/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData"))
    args = p.parse_args(); work = args.work_dir.resolve(); work.mkdir(parents=True, exist_ok=True)
    logs = work / "logs"; logs.mkdir(exist_ok=True)
    lock = json.loads((HERE / "source-lock.json").read_text())
    export_sources = [HERE / "export_lab.cpp", ROOT / "tools/bake/visualmodel.cpp",
                      ROOT / "tools/bake/gamefiles.cpp", ROOT / "src/mapdata.cpp"]
    inputs = [f for f in HERE.iterdir() if f.is_file()]
    dependency_text = capture(["xcrun", "clang++", "-std=c++17", "-I" + str(ROOT / "src"), "-MM", *export_sources])
    for rule in dependency_text.replace("\\\n", " ").splitlines():
        for name in shlex.split(rule.split(":", 1)[1]):
            path = Path(name).resolve()
            if path.is_relative_to(ROOT): inputs.append(path)
    inputs = sorted(set(inputs))
    input_hashes = {str(f.relative_to(ROOT)): sha(f) for f in inputs}
    source = work / "engine"
    # The previously reproduced static GLFW dependency is already pinned and
    # recorded. Build a fresh engine checkout, leaving its evidence untouched.
    base = ROOT / "build/host-source/reviewed"
    manifest = json.loads((base / "manifest.json").read_text())
    if manifest["lock"] != lock: raise RuntimeError("Base dependency lock changed")
    dependencies = {str(f.relative_to(base)): sha(f) for f in sorted((base / "deps").rglob("*")) if f.is_file()}
    for name, recorded in manifest["dependencies"].items():
        prefix = Path("/opt/homebrew/opt") / name
        library = prefix / "lib" / ("libopenal.1.dylib" if name == "openal-soft" else "libmpg123.0.dylib")
        if str(prefix.resolve()) != recorded["prefix"] or sha(library) != recorded["dylib_sha256"]:
            raise RuntimeError("Base system dependency changed: " + name)
        for relative, expected in recorded["headers"].items():
            if sha(prefix / relative) != expected: raise RuntimeError("Dependency header changed: " + relative)
    if not source.exists():
        checkout(lock["engine"], source, logs)
        run(["git", "-C", source, "submodule", "update", "--init", "--depth", "1", "vendor/librw"], logs / "submodules.log")
        for patch in ["host-entry.patch", "null-free.patch", "instance-geometry.patch"]:
            run(["git", "-C", source, "apply", HERE / patch], logs / (patch + ".log"))
        entry = source / "src/skel/glfw/glfw.cpp"
        text = entry.read_text(); needle = '\tRwV2d pos;\n\tRwInt32 i;'
        assert text.count(needle) == 1
        text = text.replace(needle, '\textern int WingStrike(const char *,int,const char *);\n\tif(argc>=2 && !strcmp(argv[1],"--wing-lab")) return argc<3?2:WingStrike(argv[2],argc==5&&!strcmp(argv[3],"--smoke")?120:0,argc==5?argv[4]:nil);\n' + needle)
        entry.write_text(text)
    if capture(["git", "-C", source, "rev-parse", "HEAD"]) != lock["engine"]["commit"]: raise RuntimeError("Engine revision changed")
    librw = source / "vendor/librw"
    if capture(["git", "-C", librw, "rev-parse", "HEAD"]) != lock["librw"]["commit"] or capture(["git", "-C", librw, "status", "--porcelain"]):
        raise RuntimeError("librw revision or working tree changed")
    for name in ["HostWingQuery.cpp", "WingLab.cpp", "WingStrike.cpp", "LabDraw.cpp", "LabDraw.h", "WingPhysics.h"]: shutil.copyfile(HERE / name, source / "src/extras" / name)
    export = work / "export-lab"
    run(["xcrun", "clang++", "-std=c++17", "-O2", "-I" + str(ROOT / "src"), HERE / "export_lab.cpp", ROOT / "tools/bake/visualmodel.cpp", ROOT / "tools/bake/gamefiles.cpp", ROOT / "src/mapdata.cpp", "-lz", "-o", export], logs / "export-build.log")
    data = work / "aircraft.txt"; run([export, args.game_data, data], logs / "export.log")
    run([sys.executable, HERE / "build_physics.py", "--work-dir", work / "physics", "--mesh", data], logs / "physics.log")
    physics = json.loads((work / "physics/manifest.json").read_text())
    link = source / "src/wing-physics.cmake"
    libraries = [physics["adapter_library"], *physics["link_libraries"]]
    link.write_text('target_link_libraries(${EXECUTABLE} PRIVATE\n' + ''.join('  "' + name + '"\n' for name in libraries) + ')\n')
    cmake = source / "src/CMakeLists.txt"
    if 'include(wing-physics.cmake)' not in cmake.read_text():
        with cmake.open('a') as f: f.write('\ninclude(wing-physics.cmake)\n')
    build = work / "build"
    run(["cmake", "-S", source, "-B", build, "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DREVC_INSTALL=OFF", "-DCMAKE_PREFIX_PATH=" + str(base / "deps") + ";/opt/homebrew/opt/openal-soft;/opt/homebrew/opt/mpg123"], logs / "configure.log")
    run(["cmake", "--build", build, "--parallel", "8"], logs / "build.log")
    binary = build / "src/reVC.app/Contents/MacOS/reVC"
    if input_hashes != {str(f.relative_to(ROOT)): sha(f) for f in inputs}:
        raise RuntimeError("Build inputs changed during compilation")
    if dependencies != {str(f.relative_to(base)): sha(f) for f in sorted((base / "deps").rglob("*")) if f.is_file()}:
        raise RuntimeError("Static GLFW dependency changed during compilation")
    result = {"lock": lock, "binary": str(binary), "binary_sha256": sha(binary), "data": str(data), "data_sha256": sha(data), "source_diff": capture(["git", "-C", source, "diff", "--binary"]), "input_sha256": {f.name: sha(f) for f in HERE.iterdir() if f.is_file()}, "local_inputs_sha256": input_hashes, "static_dependency_sha256": dependencies, "base_manifest_sha256": sha(base / "manifest.json"), "toolchain": capture(["xcrun", "clang++", "--version"]), "linked_libraries": capture(["otool", "-L", binary])}
    result["physics"] = physics
    (work / "build-manifest.json").write_text(json.dumps(result, indent=2) + "\n")
    print(binary)


if __name__ == "__main__": main()
