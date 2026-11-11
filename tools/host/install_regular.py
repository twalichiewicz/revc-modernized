#!/usr/bin/env python3
"""Stage and transactionally install ordinary GTA:VC with the bounded wing system.
The old Metal executable/mod remain byte-identical and available as Previous build.
"""
import argparse,hashlib,json,plistlib,shutil,subprocess,sys
from datetime import datetime
from pathlib import Path
import app_package as transaction
from build_host import ROOT,HERE,sha
sys.path.insert(0,str(ROOT/'tools'))
import patch_macvc

def run(args):return subprocess.check_output([str(x)for x in args],text=True,stderr=subprocess.STDOUT)
def check_sources(records):
 for name,expected in records.items():
  file=Path(name);file=file if file.is_absolute() else ROOT/file
  if sha(file)!=expected:raise RuntimeError('Build input changed: '+name)
def seed_profile():
 original=Path.home()/'Library/Application Support/macVC';profile=original.with_name('macVC-source');profile.mkdir(parents=True,exist_ok=True)
 # Copy only absent files. Neither original settings/saves nor an existing
 # source-host profile is overwritten by an upgrade.
 for source in original.rglob('*'):
  relative=source.relative_to(original)
  if source.is_symlink() or any(p.startswith('.') for p in relative.parts):continue
  destination=profile/relative
  if source.is_dir():destination.mkdir(parents=True,exist_ok=True)
  elif source.is_file() and not destination.exists():destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
 if (profile/'macVC.ini').exists() and not (profile/'reVC.ini').exists():shutil.copy2(profile/'macVC.ini',profile/'reVC.ini')
 return profile

def validate(app):
 run(['codesign','--verify','--deep','--strict',app]);launcher=app/'Contents/MacOS/vcLauncher'
 expected=[str(app/'Contents/MacOS/vcGameplayHost'),'--check-native-argument']
 if run([launcher,'--check-launcher','--check-native-argument']).splitlines()!=expected:raise RuntimeError('Ordinary game launch routing failed')
 if run([launcher,'--check-launcher','--legacy-game']).splitlines()!=[str(app/'Contents/MacOS/macVC')]:raise RuntimeError('Previous game routing failed')
 host=app/'Contents/MacOS/vcGameplayHost'
 links=run(['otool','-L',host])
 if '/opt/homebrew/' in links or 'libclang_rt.asan' in links:raise RuntimeError('Test or unbundled dependency in candidate')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--app',type=Path,default=Path('/Applications/Grand Theft Auto Vice City.app'));p.add_argument('--install',action='store_true');a=p.parse_args()
 build=a.build.resolve();work=a.work_dir.resolve();app=a.app.resolve();work.mkdir(parents=True,exist_ok=True);record=work/'package-manifest.json';stage=work/app.name
 manifest=json.loads((build/'manifest.json').read_text());host=Path(manifest['host']);plugin=build/'plugin/libVCModernHUD.dylib'
 if sha(host)!=manifest['host_sha256'] or sha(plugin)!=manifest['plugin']['plugin_sha256'] or sha(build/'aircraft.txt')!=manifest['data_sha256']:raise RuntimeError('Built output changed')
 check_sources(manifest['inputs']);check_sources(manifest['physics']['local_inputs']);check_sources(manifest['plugin']['sources']);check_sources(manifest['plugin'].get('headers',{}))
 if not record.exists():
  if stage.exists():raise RuntimeError('Unrecorded staging app exists')
  original=transaction.fingerprint(app);run(['cp','-cR',app,stage]);
  for relative in ['Contents/MacOS/vcPhysicsHost','Contents/Resources/PhysicsTest','Contents/Frameworks/PhysicsTest']:
   retired=stage/relative
   if retired.is_dir():shutil.rmtree(retired)
   else:retired.unlink(missing_ok=True)
  destination=stage/'Contents/MacOS/vcGameplayHost';private=stage/'Contents/Frameworks/SourceGame';private.mkdir(parents=True,exist_ok=True)
  macho=patch_macvc.MachO(host.read_bytes());symbols=macho.symbols();hooks=[name for name in patch_macvc.DEFAULT_HOOKS if '_'+name in symbols]
  destination.write_bytes(patch_macvc.patch(host.read_bytes(),hooks,'@executable_path/../Frameworks/SourceGame/libVCModernHUD.dylib'));destination.chmod(0o755);shutil.copyfile(plugin,private/plugin.name)
  for line in run(['otool','-L',destination]).splitlines()[1:]:
   dependency=line.strip().split(' ',1)[0]
   if dependency.startswith('/opt/homebrew/'):
    source=Path(dependency);library=private/source.name;shutil.copyfile(source,library);run(['install_name_tool','-change',dependency,'@executable_path/../Frameworks/SourceGame/'+library.name,destination]);run(['install_name_tool','-id','@loader_path/'+library.name,library])
  for library in private.glob('*.dylib'):run(['codesign','--force','--sign','-',library])
  resources=stage/'Contents/Resources/PhysicsData';resources.mkdir(parents=True,exist_ok=True);shutil.copyfile(build/'aircraft.txt',resources/'aircraft.txt')
  launcher=stage/'Contents/MacOS/vcLauncher';run(['xcrun','clang++','-std=c++17','-O2','-fobjc-arc','-mmacosx-version-min=26.0',HERE/'game_launcher.mm','-framework','Cocoa','-framework','ApplicationServices','-o',launcher])
  info=stage/'Contents/Info.plist';properties=plistlib.loads(info.read_bytes());properties['CFBundleExecutable']='vcLauncher';info.write_bytes(plistlib.dumps(properties))
  run(['codesign','--force','--sign','-',destination]);run(['codesign','--force','--sign','-',stage]);validate(stage)
  files=[destination,launcher,info,resources/'aircraft.txt',*private.glob('*.dylib')]
  transaction.atomic_record(record,{'original':original,'stage':str(stage),'build':str(build),'build_manifest_sha256':sha(build/'manifest.json'),'launcher_sha256':sha(HERE/'game_launcher.mm'),'installer_sha256':sha(Path(__file__)),'payload':{str(f.relative_to(stage)):sha(f) for f in files}})
 journal=json.loads(record.read_text())
 if journal['build_manifest_sha256']!=sha(build/'manifest.json') or journal['launcher_sha256']!=sha(HERE/'game_launcher.mm') or journal['installer_sha256']!=sha(Path(__file__)):raise RuntimeError('Staging inputs changed; use a new staging directory')
 if a.install:
  # Authorization is the user's explicit request to install here. A running
  # game would be using files from this bundle, so never replace it in place.
  processes=run(['ps','-axo','command'])
  if any(line.startswith(str(app/'Contents/MacOS')+'/')for line in processes.splitlines()):raise RuntimeError('Installed game is still running')
  validate(stage)
  profile=seed_profile();backup=app.with_name('Grand Theft Auto Vice City.before-regular-physics-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'.app')
  transaction.install_recorded(stage,app,backup,record,journal,validate);print('Installed:',app);print('Previous bundle:',backup);print('Profile:',profile)
 else:print('Validated staging:',stage)
if __name__=='__main__':main()
