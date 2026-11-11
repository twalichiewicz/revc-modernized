"""Map direct call returns only when named caller/callee counts agree.

Every runtime user still validates the relocated BL instruction's destination.
This does not relocate arbitrary instructions, data, inlined functions or skips.
"""
import sys,struct,json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from patch_macvc import MachO

def match_calls(old,new):
 result={}
 for caller,targets in old.items():
  for callee,sites in targets.items():
   other=new.get(caller,{}).get(callee,[])
   if len(sites)==len(other):result.update(zip(sites,other))
 return result

def calls(path):
 m=MachO(Path(path).read_bytes());symbols=m.symbols();section=m.section('__TEXT','__text');lo,hi=section[2],section[2]+section[3]
 by_address={}
 for name,address in symbols.items():
  if lo<=address<hi:by_address.setdefault(address,[]).append(name)
 starts=sorted(by_address);result={}
 for i,start in enumerate(starts):
  stop=starts[i+1] if i+1<len(starts) else hi;targets={}
  for address in range(start,stop,4):
   word=struct.unpack_from('<I',m.data,m.vm_to_file(address))[0]
   if word&0xfc000000!=0x94000000:continue
   immediate=word&0x3ffffff
   if immediate&0x2000000:immediate-=0x4000000
   destination=address+immediate*4
   for callee in by_address.get(destination,[]):targets.setdefault(callee,[]).append(address+4-m.base)
  for caller in by_address[start]:result[caller]=targets
 return result

def generate(old_path,new_path,header):
 old=calls(old_path);new=calls(new_path);mapping=match_calls(old,new)
 # The compiler merges CCarAI::UpdateCarAI's repeated leave-car calls. A named,
 # non-inlined source wrapper identifies the inspected firefighting branch.
 callee='__ZN6CCarAI23TellOccupantsToLeaveCarEP8CVehicle'
 old_site=0x50814
 owners=[name for name,targets in old.items() if old_site in targets.get(callee,[])]
 special=new.get('_VCSourceFireCrewLeave',{}).get(callee,[])
 if owners!=['__ZN6CCarAI11UpdateCarAIEP8CVehicle'] or len(special)!=1:
  raise ValueError('Fire-crew source call contract changed: '+repr(owners))
 mapping[old_site]=special[0]
 # Aliases must not assign the same return address contradictory identities.
 reverse={}
 for old,new in mapping.items():
  if new in reverse and reverse[new]!=old:raise ValueError('Ambiguous source return mapping')
  reverse[new]=old
 Path(header).write_text('#pragma once\n#include <cstdint>\nstruct VCSourceCall {uint32_t original,source;};\nstatic const VCSourceCall kVCSourceCalls[]={\n'+''.join('{0x%x,0x%x},\n'%x for x in sorted(mapping.items()))+'};\nstatic const VCSourceCall kVCSourceReverse[]={\n'+''.join('{0x%x,0x%x},\n'%(old,new) for new,old in sorted(reverse.items()))+'};\n')
 print('verified caller/callee-count pairs:',len(mapping))
 return mapping
if __name__=='__main__':generate(*sys.argv[1:])
