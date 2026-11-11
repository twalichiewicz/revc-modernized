#!/usr/bin/env python3
"""Compile the host adapter against the already pinned research libraries."""
import argparse,json,shlex,subprocess,sys,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def header_fingerprint(root):
    h=hashlib.sha256()
    for file in sorted(root.rglob('*')):
        if file.is_file() and file.suffix in ['.h','.hpp','.inl','.ipp']:
            h.update(str(file.relative_to(root)).encode());h.update(file.read_bytes())
    return h.hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--mesh',type=Path,default=ROOT/'build/installed-wing/aircraft.txt');a=p.parse_args();w=a.work_dir.resolve();w.mkdir(exist_ok=True,parents=True);(w/'manifest.json').unlink(missing_ok=True)
    sdk=ROOT/'build/world-physics-evaluation/ipc-build';flags={}
    for line in (sdk/'CMakeFiles/world_physics_rigid.dir/flags.make').read_text().splitlines():
        if ' = ' in line:
            k,v=line.split(' = ',1);flags[k]=shlex.split(v)
    includes=flags['CXX_DEFINES']+flags['CXX_INCLUDES']+['-isystem',str(ROOT/'build/world-physics-evaluation/ipc-cache/onetbb/3244/include')]
    roots=[]
    for i,flag in enumerate(includes):
        if flag.startswith('-I') and len(flag)>2:roots.append(Path(flag[2:]))
        elif flag=='-isystem':roots.append(Path(includes[i+1]))
    headers={str(root):header_fingerprint(root) for root in sorted(set(roots))}
    link=shlex.split((sdk/'CMakeFiles/world_physics_rigid.dir/link.txt').read_text());libs=[str((sdk/x).resolve()) for x in link if x.endswith('.a')]
    files=[ROOT/'tools/host/WingPhysics.cpp',ROOT/'tools/host/WingPhysics.h',Path(__file__).resolve(),ROOT/'tests/test_wing_strike.cpp',ROOT/'tests/test_wing_world.cpp',ROOT/'tests/test_wing_contact_filter.cpp',*list((ROOT/'docs/research').glob('world-physics-*.h')),*list((ROOT/'docs/research').glob('world-physics-*.cpp'))]
    record={'header_roots_sha256':headers,'sdk_cache_sha256':sha(sdk/'CMakeCache.txt'),'local_inputs':{str(f.relative_to(ROOT)):sha(f) for f in files},'libraries':{f:sha(Path(f)) for f in libs},'include_flags':includes,'link_libraries':libs}
    def run(args):subprocess.run([str(x) for x in args],check=True)
    run(['xcrun','clang++','-std=c++17','-O3','-DNDEBUG','-arch','arm64','-mmacosx-version-min=26.0',*includes,'-c',ROOT/'tools/host/WingPhysics.cpp','-o',w/'WingPhysics.o'])
    run(['ar','rcs',w/'libWingPhysics.a',w/'WingPhysics.o'])
    run(['xcrun','clang++','-std=c++17','-O2','-I'+str(ROOT/'tools/host'),ROOT/'tests/test_wing_strike.cpp',w/'libWingPhysics.a',*libs,'-o',w/'test_wing_strike'])
    run(['xcrun','clang++','-std=c++17','-O2','-I'+str(ROOT/'tools/host'),ROOT/'tests/test_wing_world.cpp',w/'libWingPhysics.a',*libs,'-o',w/'test_wing_world'])
    run(['xcrun','clang++','-std=c++17','-O2',*includes,ROOT/'tests/test_wing_contact_filter.cpp',*libs,'-o',w/'test_wing_contact_filter'])
    run([w/'test_wing_contact_filter'])
    run([w/'test_wing_world',a.mesh])
    record.update({'adapter_library':str(w/'libWingPhysics.a'),'adapter_sha256':sha(w/'libWingPhysics.a'),'test_sha256':sha(w/'test_wing_strike'),'mesh_sha256':sha(a.mesh)})
    if record['local_inputs']!={str(f.relative_to(ROOT)):sha(f) for f in files} or record['libraries']!={f:sha(Path(f)) for f in libs}:
        raise RuntimeError('Physics inputs changed during compilation')
    run([w/'test_wing_strike',a.mesh])
    if headers!={str(root):header_fingerprint(root) for root in sorted(set(roots))}:raise RuntimeError('Physics headers changed during compilation')
    (w/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
if __name__=='__main__':main()
