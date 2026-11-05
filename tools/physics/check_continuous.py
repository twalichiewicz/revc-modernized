#!/usr/bin/env python3
"""Build isolated IPC/Bullet contact probes, record inputs and physical exits.

No game launch, installation or normal plugin dependency changes. Exit 1 is a
completed physical rejection; exit 2 is a build/provenance/process failure.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_conservation import PIN as BULLET_PIN, result_exit_code

ROOT = Path(__file__).resolve().parents[2]
IPC_PIN = "54d58ebcf1f6dd0cf95bcc58afd79839ca603a99"
LIBRARIES = ["BulletSoftBody", "BulletDynamics", "BulletCollision", "LinearMath"]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source_record(source, patch_file, required_head=None):
    head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if required_head and head != required_head:
        raise RuntimeError(f"Expected {required_head} at {source}, found {head}")
    new = subprocess.check_output(["git", "-C", str(source), "ls-files", "--others", "--exclude-standard"], text=True).splitlines()
    if new:
        raise RuntimeError(f"Untracked source inputs cannot be recorded at {source}: {new[:8]}; stage them first")
    patch = subprocess.check_output(["git", "-C", str(source), "diff", "HEAD", "--binary"])
    patch_file.parent.mkdir(parents=True, exist_ok=True)
    patch_file.write_bytes(patch)
    return {"source": str(source), "revision": head, "patch_sha256": sha(patch), "patch": patch_file.name}


def tree_hash(source):
    """Fingerprint a source archive, including symlink targets without following them."""
    digest = hashlib.sha256()
    count = 0
    for directory, children, files in os.walk(source):
        children[:] = sorted(d for d in children if d != ".git")
        paths = [Path(directory) / name for name in files + children]
        for path in sorted(paths):
            relative = str(path.relative_to(source)).encode()
            if path.is_symlink():
                kind, data = b"L", os.readlink(path).encode()
            elif path.is_dir():
                kind, data = b"D", b""
            elif path.is_file():
                kind, data = b"F", path.read_bytes()
            else:
                raise RuntimeError(f"Unsupported source entry: {path}")
            digest.update(kind + len(relative).to_bytes(8, "little") + relative + bytes.fromhex(sha(data)))
            count += kind != b"D"
    return {"tree_sha256": digest.hexdigest(), "files": count}


def dependency_records(build, output):
    records = {}
    sources = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        match = re.match(r"^([\w-]+)_SOURCE_DIR:[^=]+=(.+)$", line)
        if match and Path(match[2]).is_dir():
            sources.setdefault(Path(match[2]).resolve(), match[1])
    for source, name in sources.items():
        safe_name = re.sub(r"[^\w-]", "_", name)
        if (source / ".git").exists():
            record = source_record(source, output / "dependencies" / f"{safe_name}.patch")
        else:
            record = {"source": str(source), **tree_hash(source)}
        records[name] = record
    return records


def validate_bullet_build(source, build):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line.startswith(("#", "//")) or "=" not in line or ":" not in line:
            continue
        key, value = line.split("=", 1)
        cache[key.split(":", 1)[0]] = value
    actual_source = cache.get("CMAKE_HOME_DIRECTORY")
    if not actual_source or Path(actual_source).resolve() != source.resolve():
        raise RuntimeError(f"Bullet build source does not match recorded source: {actual_source} != {source}")
    if cache.get("USE_DOUBLE_PRECISION") != "ON":
        raise RuntimeError("The Bullet build must use double precision")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("ipc-source", "bullet-source", "bullet-build", "build-dir", "output-dir"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    parser.add_argument("--asan", action="store_true", help="Instrument this separate IPC/probe build with AddressSanitizer")
    parser.add_argument("--persistent", action="store_true", help="Also evaluate the experimental implicit node-island coordinator")
    args = parser.parse_args()
    ipc, bullet, bullet_build, build, output = [p.resolve() for p in
        (args.ipc_source, args.bullet_source, args.bullet_build, args.build_dir, args.output_dir)]
    output.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    records = {"ipc": source_record(ipc, output / "ipc.patch", IPC_PIN),
               "bullet": source_record(bullet, output / "bullet.patch", BULLET_PIN)}
    validate_bullet_build(bullet, bullet_build)
    options = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_STANDARD=17", "-DCMAKE_OSX_ARCHITECTURES=arm64",
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0", "-DIPC_TOOLKIT_BUILD_TESTS=OFF", "-DIPC_TOOLKIT_BUILD_PYTHON=OFF",
        "-DIPC_TOOLKIT_WITH_CUDA=OFF", "-DIPC_TOOLKIT_WITH_FILIB=OFF", "-DIPC_TOOLKIT_WITH_ABSEIL=OFF",
        "-DIPC_TOOLKIT_WITH_ROBIN_MAP=OFF", "-DIPC_TOOLKIT_WITH_MESHFEM_SPARSE=OFF",
        f"-DCPM_SOURCE_CACHE={build.parent / 'ipc-cache'}",
        f"-DCMAKE_PROJECT_IPCToolkit_INCLUDE={ROOT / 'docs/research/world-physics-ccd.cmake'}",
        f"-DWORLD_PHYSICS_BULLET_SOURCE={bullet}", f"-DWORLD_PHYSICS_BULLET_BUILD={bullet_build}"]
    options += ["-DCMAKE_C_FLAGS_RELEASE=-O3 -DNDEBUG", "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG"]
    if not args.asan:
        options += ["-DCMAKE_C_FLAGS=", "-DCMAKE_CXX_FLAGS=", "-DCMAKE_EXE_LINKER_FLAGS="]
    if args.asan:
        options += ["-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer",
                    "-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer", "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address"]
    targets = ["world_physics_ccd", "world_physics_contact"]
    if args.persistent:
        targets += ["world_physics_incremental", "world_physics_persistent"]
    provenance = {"sources": records, "cmake_options": options, "asan": args.asan, "persistent": args.persistent,
        "runner_sha256": sha(Path(__file__).read_bytes()),
        "fixtures_sha256": {p.name: sha(p.read_bytes()) for p in (ROOT / "docs/research").glob("world-physics-*") if p.suffix in (".h", ".cpp", ".cmake")},
        "compiler": subprocess.check_output(["xcrun", "clang++", "--version"], text=True)}
    record_file = output / "provenance.json"
    record_file.write_text(json.dumps(provenance, indent=2) + "\n")
    with (output / "build.log").open("w") as log:
        subprocess.run(["cmake", "--build", str(bullet_build), "--target", *LIBRARIES, "--parallel", "4"], stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["cmake", "-S", str(ipc), "-B", str(build), *options], stdout=log, stderr=subprocess.STDOUT, check=True)
        subprocess.run(["cmake", "--build", str(build), "--target", *targets, "--parallel", "4"], stdout=log, stderr=subprocess.STDOUT, check=True)
    for name, directory in (("ipc", build), ("bullet", bullet_build)):
        cache = (directory / "CMakeCache.txt").read_bytes()
        (output / f"{name}-CMakeCache.txt").write_bytes(cache)
        provenance[f"{name}_cache_sha256"] = sha(cache)
    commands = build / "compile_commands.json"
    if commands.exists():
        (output / "compile_commands.json").write_bytes(commands.read_bytes())
        provenance["compile_commands_sha256"] = sha(commands.read_bytes())
    provenance["dependencies"] = dependency_records(build, output)
    provenance["libraries_sha256"] = {name: sha((bullet_build / "src" / name / f"lib{name}.a").read_bytes()) for name in LIBRARIES}
    provenance["ipc_library_sha256"] = sha((build / "libipc_toolkit.a").read_bytes())
    provenance["executables_sha256"] = {name: sha((build / name).read_bytes()) for name in targets}
    statuses = {}
    runs = [("geometry", "world_physics_ccd", []), ("grazes", "world_physics_ccd", ["--stress"]),
            ("events", "world_physics_contact", []), ("benchmark", "world_physics_ccd", ["--benchmark"])]
    if args.persistent:
        runs += [("incremental-material", "world_physics_incremental", []), ("persistent", "world_physics_persistent", []),
                 ("persistent-benchmark", "world_physics_persistent", ["--benchmark"])]
    for name, executable, arguments in runs:
        run_started = time.monotonic()
        result = subprocess.run([str(build / executable), *arguments], capture_output=True, text=True, timeout=60)
        (output / f"{name}.txt").write_text(result.stdout + result.stderr + f"exit={result.returncode}\nelapsed_s={time.monotonic()-run_started:.6f}\n")
        statuses[name] = result.returncode
        summary = result.stdout.splitlines()[-1] if result.stdout.splitlines() else "no standard output"
        print(f"{name}: exit={result.returncode}; {summary}", flush=True)
    provenance["exit_codes"] = statuses
    provenance["elapsed_seconds"] = time.monotonic() - started
    record_file.write_text(json.dumps(provenance, indent=2) + "\n")
    return result_exit_code(statuses)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Continuous-contact probe could not run: {error}", file=sys.stderr)
        sys.exit(2)
