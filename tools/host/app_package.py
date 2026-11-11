#!/usr/bin/env python3
"""Stage, validate and reversibly install the opt-in wing test app."""
import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
PROTECTED=("Contents/MacOS/macVC","Contents/Frameworks/libVCModernHUD.dylib")


def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()


def fingerprint(app):
    names=[*PROTECTED,"Contents/Info.plist"]
    names.extend(name for name in ("Contents/MacOS/vcLauncher","Contents/MacOS/vcPhysicsHost","Contents/Resources/PhysicsTest/aircraft.txt") if (app/name).exists())
    names.extend(name for name in ("Contents/MacOS/vcGameplayHost","Contents/Resources/PhysicsData/aircraft.txt") if (app/name).exists())
    private=app/"Contents/Frameworks/SourceGame"
    if private.exists():names.extend(str(f.relative_to(app)) for f in sorted(private.glob("*.dylib")))
    return {name:digest(app/name) for name in names}


def matches(app,expected):
    actual=fingerprint(app)
    # Prior release journals contain only game/mod/plist identities. Retain
    # their recovery capability while new journals also guard the test payload.
    return bool(expected) and all(actual.get(name)==value for name,value in expected.items())


def experimental_directories(app):
    private=app/"Contents/Frameworks/PhysicsTest";resource=app/"Contents/Resources/PhysicsTest"
    private.mkdir(parents=True,exist_ok=True);resource.mkdir(parents=True,exist_ok=True)
    return private,resource


def atomic_record(path,manifest):
    """Preserve the previous recovery journal if writing a replacement fails."""
    output=tempfile.NamedTemporaryFile(mode="w",dir=path.parent,prefix=".package-manifest-",delete=False)
    temporary=Path(output.name)
    try:
        with output:
            json.dump(manifest,output,indent=2);output.write("\n");output.flush();os.fsync(output.fileno())
        os.replace(temporary,path)
    finally:
        temporary.unlink(missing_ok=True)


def install_recorded(staged,app,backup,record,manifest,validate):
    if backup.exists():raise FileExistsError(backup)
    if not matches(app,manifest["original"]):raise RuntimeError("Installed app changed since staging")
    intent={**manifest,"installed":str(app),"backup":str(backup),"installed_fingerprint":fingerprint(staged)}
    # Everything rollback needs is durable before the first directory rename.
    # No journal update is required after the swap, even if storage fills up.
    atomic_record(record,intent)
    install(staged,app,backup,manifest["original"],validate,payload=manifest.get("payload"))
    return intent


def install(staged,app,backup,expected,validate,payload=None):
    if not matches(app,expected): raise RuntimeError("Installed app changed since staging")
    if backup.exists(): raise FileExistsError(backup)
    if any(digest(staged/name)!=expected[name] for name in PROTECTED): raise RuntimeError("Staged normal game bytes changed")
    if payload and any(digest(staged/name)!=value for name,value in payload.items()): raise RuntimeError("Validated staging payload changed")
    app.rename(backup)
    try:
        staged.rename(app)
        validate(app)
    except BaseException:
        if app.exists(): app.rename(staged)
        backup.rename(app)
        raise


def restore(app,backup,archive,installed,original):
    if not matches(backup,original):raise RuntimeError("Backup changed; refusing to overwrite it")
    if not app.exists():
        # Recovery after interruption between the two installation renames.
        backup.rename(app)
        return
    if not matches(app,installed): raise RuntimeError("App changed; refusing to overwrite it")
    if archive.exists(): raise FileExistsError(archive)
    app.rename(archive)
    try: backup.rename(app)
    except BaseException:
        archive.rename(app)
        raise


def run(args):
    try:return subprocess.check_output([str(x) for x in args],stderr=subprocess.STDOUT,text=True)
    except subprocess.CalledProcessError as e:
        print(e.output)
        raise


def check_smoke_results(output):
    report=json.loads(Path(str(output)+".json").read_text())
    if not (report["kind"]=="wing-strike" and report["failures"]==0 and report["accepted"]>=320 and report["contacts"]>0 and report["fragments"]>=2 and report["mounts"]>0 and report["miss_mounts"]==0 and report["hit_speed"]<5.9 and abs(report["miss_speed"]-6)<1e-6 and report["native_steps"]==0):
        raise RuntimeError("Required physical strike/miss assertions failed")
    intact=Path(str(output)+".intact.ppm").read_bytes()
    damaged=output.read_bytes()
    repaired=Path(str(output)+".repaired.ppm").read_bytes()
    if not intact.startswith(b"P6\n") or len(intact)<10000 or intact==damaged or repaired!=intact:
        raise RuntimeError("Rendered damage/repair capture failed")


def validate(app,output):
    run(["codesign","--verify","--deep","--strict",app])
    launcher=app/"Contents/MacOS/vcLauncher"
    normal=run([launcher,"--check-launcher","--test-native-argument"])
    if normal.splitlines()!=[str(app/"Contents/MacOS/macVC"),"--test-native-argument"]: raise RuntimeError("Normal game routing failed")
    lab=run([launcher,"--check-launcher","--physics-test"])
    if lab.splitlines()!=[str(app/"Contents/MacOS/vcPhysicsHost"),"--wing-lab",str(app/"Contents/Resources/PhysicsTest/aircraft.txt")]: raise RuntimeError("Test routing failed")
    run([launcher,"--lab-smoke",output])
    check_smoke_results(output)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--app",type=Path,default=Path("/Applications/Grand Theft Auto Vice City.app"));p.add_argument("--work-dir",type=Path,default=ROOT/"build/installed-wing");mode=p.add_mutually_exclusive_group();mode.add_argument("--install",action="store_true");mode.add_argument("--rollback",action="store_true");args=p.parse_args()
    app=args.app.resolve();work=args.work_dir.resolve();stage=work/"Grand Theft Auto Vice City.app";record=work/"package-manifest.json"
    running=lambda:subprocess.run(["pgrep","-f",re.escape(str(app))+"/Contents/MacOS/"],stdout=subprocess.DEVNULL).returncode==0
    if args.rollback:
        if running():raise RuntimeError("Quit the game/test before rollback")
        manifest=json.loads(record.read_text());backup=Path(manifest["backup"]);archive=backup.with_name("Vice City physics-test-"+datetime.now().strftime("%Y%m%d-%H%M%S")+".app")
        restore(app,backup,archive,manifest["installed_fingerprint"],manifest["original"]);manifest["rolled_back"]=True;atomic_record(record,manifest);print("Restored:",app);return
    if not record.exists():
        if stage.exists():raise RuntimeError("Unrecorded staging app already exists")
        build=json.loads((work/"build-manifest.json").read_text())
        for name in ["WingLab.cpp","HostWingQuery.cpp","export_lab.cpp","source-lock.json","build_lab.py"]:
            if build["input_sha256"][name]!=digest(ROOT/"tools/host"/name):raise RuntimeError("Rebuild the host; compiled inputs changed")
        for name,expected in build["local_inputs_sha256"].items():
            if expected!=digest(ROOT/name):raise RuntimeError("Rebuild the host; local dependency changed")
        for name,expected in build["physics"]["local_inputs"].items():
            if expected!=digest(ROOT/name):raise RuntimeError("Rebuild the physics adapter; local dependency changed")
        if build["binary_sha256"]!=digest(Path(build["binary"])) or build["data_sha256"]!=digest(work/"aircraft.txt"):
            raise RuntimeError("Built host/data changed after compilation")
        expected=fingerprint(app);run(["cp","-cR",app,stage])
        launcher=stage/"Contents/MacOS/vcLauncher";run(["xcrun","clang++","-std=c++17","-O2","-fobjc-arc",ROOT/"tools/host/launcher.mm","-framework","Cocoa","-framework","ApplicationServices","-o",launcher])
        host=stage/"Contents/MacOS/vcPhysicsHost";run(["cp",work/"build/src/reVC.app/Contents/MacOS/reVC",host])
        private,resource=experimental_directories(stage)
        libraries=run(["otool","-L",host]).splitlines()[1:]
        for line in libraries:
            source=line.strip().split(" ",1)[0]
            if source.startswith("/opt/homebrew/"):
                file=Path(source);dest=private/file.name;run(["cp",file,dest]);run(["install_name_tool","-change",source,"@executable_path/../Frameworks/PhysicsTest/"+file.name,host]);run(["install_name_tool","-id","@loader_path/"+file.name,dest]);run(["codesign","--force","--sign","-",dest])
        linked=run(["otool","-L",host])
        if "/opt/homebrew/" in linked or "libVCModernHUD" in linked:raise RuntimeError("Invalid source-host dependency")
        # The shipped dependency signatures have unsatisfied designated
        # requirements when nested. Give copied dependencies local ad-hoc
        # signatures; retain normal game/mod bytes exactly.
        for library in (stage/"Contents/Frameworks").glob("*.dylib"):
            if library.name!="libVCModernHUD.dylib":run(["codesign","--force","--sign","-",library])
        run(["cp",work/"aircraft.txt",resource/"aircraft.txt"])
        info=stage/"Contents/Info.plist";data=plistlib.loads(info.read_bytes());data["CFBundleExecutable"]="vcLauncher";info.write_bytes(plistlib.dumps(data))
        run(["codesign","--force","--sign","-",host]);run(["codesign","--force","--sign","-",stage])
        validate(stage,work/"staged-smoke.ppm")
        files=[host,launcher,info,resource/"aircraft.txt",*list((stage/"Contents/Frameworks").rglob("*.dylib"))]
        atomic_record(record,{"original":expected,"stage":str(stage),"payload":{str(f.relative_to(stage)):digest(f) for f in files},"build_manifest_sha256":digest(work/"build-manifest.json"),"launcher_source_sha256":digest(ROOT/"tools/host/launcher.mm")})
    manifest=json.loads(record.read_text())
    if args.install:
        if running():raise RuntimeError("Quit the game/test before installation")
        if manifest["build_manifest_sha256"]!=digest(work/"build-manifest.json") or manifest["launcher_source_sha256"]!=digest(ROOT/"tools/host/launcher.mm"):raise RuntimeError("Build changed after staging; prepare a fresh package")
        backup=app.with_name("Grand Theft Auto Vice City.before-physics-"+datetime.now().strftime("%Y%m%d-%H%M%S")+".app")
        install_recorded(stage,app,backup,record,manifest,lambda installed:validate(installed,work/"installed-smoke.ppm"))
        print("Installed:",app,"\nRollback app:",backup)
    else:print("Validated staging app:",stage)


if __name__=="__main__":main()
