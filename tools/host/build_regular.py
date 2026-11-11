#!/usr/bin/env python3
"""Build the normal-game source host and UUID-specific mod, without installing.

Start in a new scratch directory. Original game data and user profiles are not
modified. The separate scene is deliberately absent from this executable.
"""
import argparse,json,shlex,shutil,subprocess,sys
from pathlib import Path
from build_host import ROOT,HERE,checkout,run,capture,sha

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--game-data',type=Path,default=Path('/Applications/Grand Theft Auto Vice City.app/Contents/Resources/GameData'));p.add_argument('--asan',action='store_true');p.add_argument('--jobs',type=int,default=8);a=p.parse_args();work=a.work_dir.resolve()
 if work.exists() and any(work.iterdir()):raise RuntimeError('Use a fresh work directory; existing evidence is preserved')
 if not 1<=a.jobs<=16:raise RuntimeError('jobs must be in 1..16')
 work.mkdir(parents=True,exist_ok=True);logs=work/'logs';logs.mkdir();source=work/'engine';lock=json.loads((HERE/'source-lock.json').read_text());base=ROOT/'build/host-source/reviewed'
 inputs=[HERE/f for f in ['regular-engine.patch','regular-librw.patch','SourceABI.cpp','SourceGates.cpp','SourceGates.h','RegularGameSmoke.cpp','AircraftBody.cpp','AircraftBody.h','BodyContact.h','BodyLoads.h','BodyAffine.h','BodyPublication.h','AircraftBodySmoke.cpp','AircraftBodySmoke.h','VehicleBodySmoke.cpp','VehicleBodySmoke.h','GameplayWing.cpp','GameplayWing.h','DodoRig.cpp','DodoRig.h','DodoSmoke.cpp','DodoSmoke.h','LiveWingGeometry.cpp','LiveWingGeometry.h','WingMapping.h','WingPhysics.h','export_lab.cpp','PrivateSemaphore.h','ControllerButtons.h','CompressedTexture.h','GameCache.h','HostWingQuery.cpp','build_regular.py']]
 shared_names=['boundsindex.h','vec3.h','aircraftbodybridge.h','aircraftbodymodel.h','aircraftbodymodel.cpp','vehiclebodylayout.h','mechanicalmodel.cpp','aircraftconnectivity.h','skinmodel.h','airframemodel.h','airframemodel.cpp','projectilemodel.h','materialmodel.h','mechanicalmodel.h','firemodel.h']
 inputs += [ROOT/'src'/name for name in shared_names]
 export_sources=[HERE/'export_lab.cpp',ROOT/'tools/bake/visualmodel.cpp',ROOT/'tools/bake/gamefiles.cpp',ROOT/'src/mapdata.cpp']
 dependency_text=capture(['xcrun','clang++','-std=c++17','-I'+str(ROOT/'src'),'-MM',*export_sources])
 for rule in dependency_text.replace('\\\n',' ').splitlines():
  for name in shlex.split(rule.split(':',1)[1]):
   file=Path(name).resolve()
   if file.is_relative_to(ROOT):inputs.append(file)
 inputs+= [HERE/'build_plugin.py',HERE/'source_calls.py',HERE/'build_physics.py',HERE/'source-lock.json']
 inputs=sorted(set(inputs));hashes={str(f.relative_to(ROOT)):sha(f) for f in inputs}
 manifest=json.loads((base/'manifest.json').read_text())
 if manifest['lock']!=lock:raise RuntimeError('Static GLFW dependency lock changed')
 for name,record in manifest['dependencies'].items():
  prefix=Path('/opt/homebrew/opt')/name;library=prefix/'lib'/('libopenal.1.dylib' if name=='openal-soft' else 'libmpg123.0.dylib')
  if str(prefix.resolve())!=record['prefix'] or sha(library)!=record['dylib_sha256']:raise RuntimeError('Audio dependency changed: '+name)
  for relative,expected in record['headers'].items():
   if sha(prefix/relative)!=expected:raise RuntimeError('Audio header changed: '+relative)
 static={str(f.relative_to(base/'deps')):sha(f) for f in sorted((base/'deps').rglob('*')) if f.is_file()}
 checkout(lock['engine'],source,logs);run(['git','-C',source,'submodule','update','--init','--depth','1','vendor/librw'],logs/'submodules.log')
 if capture(['git','-C',source/'vendor/librw','rev-parse','HEAD'])!=lock['librw']['commit']:raise RuntimeError('librw revision mismatch')
 run(['git','-C',source,'apply',HERE/'regular-engine.patch'],logs/'engine-patch.log');run(['git','-C',source/'vendor/librw','apply',HERE/'regular-librw.patch'],logs/'librw-patch.log')
 for f in inputs:
  if f.parent==HERE and f.suffix in ['.h','.cpp'] and f.name not in ['CompressedTexture.h','export_lab.cpp']:shutil.copyfile(f,source/'src/extras'/f.name)
 for name in shared_names:shutil.copyfile(ROOT/'src'/name,source/'src/extras'/name)
 shutil.copyfile(HERE/'CompressedTexture.h',source/'vendor/librw/src/CompressedTexture.h')
 # Export the actual installed Dodo model; the resulting asset is also carried
 # in the normal game's resources, with its hash in the build record.
 export=work/'export-aircraft';data=work/'aircraft.txt'
 run(['xcrun','clang++','-std=c++17','-O2','-I'+str(ROOT/'src'),HERE/'export_lab.cpp',ROOT/'tools/bake/visualmodel.cpp',ROOT/'tools/bake/gamefiles.cpp',ROOT/'src/mapdata.cpp','-lz','-o',export],logs/'export-build.log')
 run([export,a.game_data.resolve(),data],logs/'export.log')
 run([sys.executable,HERE/'build_physics.py','--work-dir',work/'physics','--mesh',data],logs/'physics.log')
 physics=json.loads((work/'physics/manifest.json').read_text());libraries=[physics['adapter_library'],*physics['link_libraries']]
 (source/'src/gameplay-physics.cmake').write_text('target_link_libraries(${EXECUTABLE} PRIVATE\n'+''.join('  "'+name+'"\n' for name in libraries)+')\n')
 build=work/'build';options=['-DCMAKE_BUILD_TYPE='+('RelWithDebInfo' if a.asan else 'Release'),'-DCMAKE_OSX_ARCHITECTURES=arm64','-DCMAKE_OSX_DEPLOYMENT_TARGET=26.0','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON','-DREVC_INSTALL=OFF','-DCMAKE_PREFIX_PATH='+str(base/'deps')+';/opt/homebrew/opt/openal-soft;/opt/homebrew/opt/mpg123']
 if a.asan:options+=['-DCMAKE_CXX_FLAGS=-fsanitize=address -fno-omit-frame-pointer','-DCMAKE_C_FLAGS=-fsanitize=address -fno-omit-frame-pointer','-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address']
 run(['cmake','-S',source,'-B',build,*options],logs/'configure.log');run(['cmake','--build',build,'--parallel',a.jobs],logs/'build.log')
 binary=build/'src/reVC.app/Contents/MacOS/reVC';resources=binary.parents[1]/'Resources/PhysicsData';resources.mkdir(parents=True,exist_ok=True);shutil.copyfile(data,resources/'aircraft.txt');run([sys.executable,HERE/'build_plugin.py','--host',binary,'--work-dir',work/'plugin',*(['--asan'] if a.asan else [])],logs/'plugin.log')
 if hashes!={str(f.relative_to(ROOT)):sha(f) for f in inputs}:raise RuntimeError('Host inputs changed during build')
 if static!={str(f.relative_to(base/'deps')):sha(f) for f in sorted((base/'deps').rglob('*')) if f.is_file()}:raise RuntimeError('Static dependency changed during build')
 result={'physics':physics,'data_sha256':sha(data),'lock':lock,'host':str(binary),'host_sha256':sha(binary),'inputs':hashes,'static_glfw':static,'dependencies':manifest['dependencies'],'options':options,'plugin':json.loads((work/'plugin/manifest.json').read_text()),'toolchain':capture(['xcrun','clang++','--version']),'source_diff':capture(['git','-C',source,'diff','--binary'])}
 (work/'manifest.json').write_text(json.dumps(result,indent=2)+'\n');print(binary)
if __name__=='__main__':main()
