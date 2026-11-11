#!/usr/bin/env python3
"""Build an isolated UUID-bound source-host plugin; never installs it."""
import argparse,concurrent.futures,hashlib,json,shlex,subprocess,sys
from pathlib import Path
from build_host import ROOT,HERE,sha
sys.path.insert(0,str(ROOT/'tools'))
from patch_macvc import MachO
from source_calls import generate

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',type=Path,required=True);p.add_argument('--work-dir',type=Path,required=True);p.add_argument('--asan',action='store_true');a=p.parse_args();work=a.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
 host=a.host.resolve();uuid=MachO(host.read_bytes()).uuid
 (work/'source-uuid.h').write_text('#define VC_SOURCE_UUID_BYTES '+','.join('0x%02x'%b for b in uuid)+'\n')
 generate(Path.home()/'Library/Application Support/macVC/vcmodernhud/macVC.vanilla',host,work/'source-calls.h')
 subprocess.run([sys.executable,ROOT/'tools/guns/embed.py',work/'gunassets.S',work/'gunlist.h',*sorted((ROOT/'assets/guns').glob('*.vmg'))],check=True,cwd=ROOT)
 rule='print-source-plugin:\n\t@printf "%s\\n" $(PLUGIN)'
 files=subprocess.check_output(['make','--no-print-directory','-f','Makefile','-f','-','BUILD='+str(work),'print-source-plugin'],input=rule+'\n',text=True,cwd=ROOT).splitlines()
 flags=['-std=c++17','-O1' if a.asan else '-O2','-g','-Wall','-Wextra','-arch','arm64','-mmacosx-version-min=26.0','-I'+str(work),'-include',str(work/'source-uuid.h'),'-fvisibility=hidden','-fobjc-arc']
 if a.asan:flags+=['-fsanitize=address','-fno-omit-frame-pointer']
 headers=sorted((ROOT/'src').glob('*.h'))+[work/'source-uuid.h',work/'source-calls.h',work/'gunlist.h']
 source_hashes={str(f):sha((ROOT/f).resolve()) for f in files}
 header_hashes={str(f.relative_to(ROOT)) if f.is_relative_to(ROOT) else str(f):sha(f) for f in headers}
 salt=json.dumps(flags)+''.join(sha(f) for f in headers)
 objdir=work/'objects';objdir.mkdir(exist_ok=True)
 def compile_file(name):
  src=(ROOT/name).resolve();obj=objdir/(src.name+'.o');sig=hashlib.sha256((salt+sha(src)).encode()).hexdigest();record=obj.with_suffix('.json')
  if obj.exists() and record.exists() and record.read_text()==sig:return obj
  with obj.with_suffix('.log').open('w') as log:subprocess.run(['xcrun','clang++',*flags,'-c',str(src),'-o',str(obj)],check=True,stdout=log,stderr=subprocess.STDOUT,cwd=ROOT)
  record.write_text(sig);return obj
 with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:objects=list(pool.map(compile_file,files))
 output=work/'libVCModernHUD.dylib'
 subprocess.run(['xcrun','clang++','-arch','arm64','-dynamiclib',*(['-fsanitize=address'] if a.asan else []),'-install_name','@executable_path/../Frameworks/libVCModernHUD.dylib','-o',str(output),*map(str,objects),*[arg for framework in ['Foundation','AppKit','GameController','CoreHaptics','AudioToolbox'] for arg in ['-framework',framework]]],check=True)
 if source_hashes!={str(f):sha((ROOT/f).resolve()) for f in files} or header_hashes!={str(f.relative_to(ROOT)) if f.is_relative_to(ROOT) else str(f):sha(f) for f in headers}:raise RuntimeError('Plugin inputs changed during compilation')
 (work/'manifest.json').write_text(json.dumps({'headers':header_hashes,'builder_sha256':sha(Path(__file__)),'host_sha256':sha(host),'uuid':uuid.hex(),'plugin_sha256':sha(output),'sources':source_hashes,'flags':flags},indent=2)+'\n');print(output)
if __name__=='__main__':main()
