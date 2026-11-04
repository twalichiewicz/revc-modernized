"""Read-only supported-native capacity regression; no app/process writes."""
import importlib.util
from pathlib import Path
import hashlib,os,struct,unittest
spec=importlib.util.spec_from_file_location('vcpatch',Path(__file__).resolve().parents[1]/'tools/patch_macvc.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class Capacity(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  path=Path(os.environ.get('MACVC_NATIVE',str(Path.home()/'Library/Application Support/macVC/vcmodernhud/macVC.vanilla')))
  cls.raw=path.read_bytes();assert hashlib.sha256(cls.raw).hexdigest()==p.VANILLA_SHA256
 def test_full_native_table_preserves_existing_addresses(self):
  before=p.MachO(self.raw);after=p.MachO(p.patch(self.raw,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS))
  self.assertEqual(before.sections,after.sections);self.assertEqual(before.symbols(),after.symbols());self.assertEqual(len(after.data),len(self.raw))
  self.assertEqual(after.segments['__DATA'],(before.segments['__DATA'][0],before.segments['__DATA'][1]+0x4000,*before.segments['__DATA'][2:]))
  self.assertEqual(after.segments['__LINKEDIT'],(before.segments['__LINKEDIT'][0]+0x4000,*before.segments['__LINKEDIT'][1:]))
  for name in before.segments:
   if name not in ('__DATA','__LINKEDIT'):self.assertEqual(before.segments[name],after.segments[name])
  at=after.find_table();magic,version,count,uuid=struct.unpack_from(p.HEADER_FMT,after.data,at);self.assertEqual(magic,p.TABLE_MAGIC);self.assertEqual(version,2);self.assertEqual(uuid,before.uuid)
  self.assertEqual(count,len(p.DEFAULT_HOOKS)+len(p.DEFAULT_SKIPS));slots=[]
  end=max(a+s for g,_,a,s,_ in before.sections if g=='__DATA');data=after.segments['__DATA'];link=after.segments['__LINKEDIT']
  self.assertEqual(data[0]+data[1],link[0])
  for i in range(count):
   _,_,slot,original,_=struct.unpack_from(p.ENTRY_FMT,after.data,at+p.HEADER_SIZE+i*p.ENTRY_SIZE);slot+=after.base;slots.append(slot)
   self.assertEqual(slot%8,0);self.assertGreaterEqual(slot,end);self.assertLessEqual(slot+8,data[0]+data[1]);self.assertGreaterEqual(slot,data[0]+data[3])
   stub=after.base+original-16;adrp,ldr=struct.unpack_from('<II',after.data,after.vm_to_file(stub));imm=((adrp>>29)&3)|(((adrp>>5)&0x7ffff)<<2)
   if imm&(1<<20):imm-=1<<21
   self.assertEqual((stub&~0xfff)+imm*4096+((ldr>>10)&0xfff)*8,slot)
  self.assertEqual(len(set(slots)),count)
 def test_existing_fitting_table_keeps_original_layout(self):
  before=p.MachO(self.raw);after=p.MachO(p.patch(self.raw,p.DEFAULT_HOOKS[:122-len(p.DEFAULT_SKIPS)],'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS))
  self.assertEqual(before.segments,after.segments);self.assertEqual(before.sections,after.sections)
 def test_ragdoll_world_phase_can_be_installed(self):
  before=p.MachO(self.raw);after=p.MachO(p.patch(self.raw,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS))
  at=after.find_table();_,_,count,_=struct.unpack_from(p.HEADER_FMT,after.data,at)
  entries=[struct.unpack_from(p.ENTRY_FMT,after.data,at+p.HEADER_SIZE+i*p.ENTRY_SIZE) for i in range(count)]
  name='_ZN6CWorld7ProcessEv'
  entry=next((entry for entry in entries if entry[0]==p.name_hash(name)),None)
  self.assertIsNotNone(entry,'late ragdoll world phase is missing from the installed native patch table')
  self.assertEqual(entry[1]+after.base,before.symbols()['_'+name]);self.assertEqual(entry[4],0)
  instruction=struct.unpack_from('<I',after.data,after.vm_to_file(entry[1]+after.base))[0]
  self.assertEqual(instruction&0xfc000000,0x14000000)
 def test_unknown_image_cannot_grow(self):
  modified=bytearray(self.raw);modified[28]^=1
  with self.assertRaises(p.PatchError):p.patch(modified,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS)
 def test_physical_ped_line_hits_can_be_installed(self):
  before=p.MachO(self.raw);after=p.MachO(p.patch(self.raw,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS))
  at=after.find_table();_,_,count,_=struct.unpack_from(p.HEADER_FMT,after.data,at)
  name='_ZN10CCollision18ProcessLineOfSightERK8CColLineRK7CMatrixR9CColModelR9CColPointRfbb'
  entries=[struct.unpack_from(p.ENTRY_FMT,after.data,at+p.HEADER_SIZE+i*p.ENTRY_SIZE) for i in range(count)]
  entry=next((entry for entry in entries if entry[0]==p.name_hash(name)),None)
  self.assertIsNotNone(entry,'physical ped hits are missing from the installed native patch table')
  self.assertEqual(entry[1]+after.base,before.symbols()['_'+name]);self.assertEqual(entry[4],0)
 def test_growth_is_limited_to_one_page(self):
  with self.assertRaises(p.PatchError):p.reserve_slots(p.MachO(self.raw),3000)
 def test_exact_capacity_boundary(self):
  before=p.MachO(self.raw)
  for hooks,growth in [(123-len(p.DEFAULT_SKIPS),0),(124-len(p.DEFAULT_SKIPS),0x4000)]:
   with self.subTest(slots=hooks+len(p.DEFAULT_SKIPS)):
    after=p.MachO(p.patch(self.raw,p.DEFAULT_HOOKS[:hooks],'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS))
    self.assertEqual(after.segments['__DATA'][1]-before.segments['__DATA'][1],growth)
    self.assertEqual(after.segments['__LINKEDIT'][0]-before.segments['__LINKEDIT'][0],growth)
 def test_progressive_fire_hooks_and_scoped_native_gates(self):
  after=p.MachO(p.patch(self.raw,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS))
  at=after.find_table();_,_,count,_=struct.unpack_from(p.HEADER_FMT,after.data,at)
  entries={row[0]:row for row in (struct.unpack_from(p.ENTRY_FMT,after.data,at+p.HEADER_SIZE+i*p.ENTRY_SIZE) for i in range(count))}
  for name in ['_ZN5CFire11ProcessFireEv','_ZN5CFire10ExtinguishEv','_ZN8CCarCtrl28GenerateEmergencyServicesCarEv','_ZNK12CFireManager19GetTotalActiveFiresEv','_ZN12CFireManager15FindNearestFireE7CVectorPf']:
   self.assertIn(p.name_hash(name),entries,'native fire lifetime/dispatch hook missing')
  for name,address in [('skip:car_fire_countdown',0x10024d25c),('skip:car_rollover_drain',0x10024eb7c),('skip:car_ordinary_lethal',0x10027daa8)]:
   self.assertEqual(entries[p.name_hash(name)][1]+after.base,address);self.assertEqual(entries[p.name_hash(name)][4],1)
 def test_already_patched_image_is_rejected(self):
  output=p.patch(self.raw,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS)
  with self.assertRaises(p.PatchError):p.patch(output,p.DEFAULT_HOOKS,'@executable_path/../Frameworks/libVCModernHUD.dylib',p.DEFAULT_SKIPS)
if __name__=='__main__':unittest.main()
