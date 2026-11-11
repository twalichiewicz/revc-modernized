import importlib.util,unittest
from types import SimpleNamespace
from pathlib import Path
spec=importlib.util.spec_from_file_location('patch',Path(__file__).resolve().parents[1]/'tools/patch_macvc.py');p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
class Slots(unittest.TestCase):
 def fixture(self):return SimpleNamespace(segments={'__DATA':(0x1000,0x1000,0,0x1000)},sections=[('__DATA','__vchooks',0x1100,2048,0x100)],data=bytearray(0x1000))
 def test_uses_explicit_zero_slot_section_without_moving_segments(self):
  m=self.fixture();before=m.segments.copy();self.assertEqual(p.reserve_slots(m,162),0x1100);self.assertEqual(m.segments,before)
 def test_refuses_occupied_undersized_or_misaligned_contract(self):
  for mode in ['occupied','small','alignment']:
   m=self.fixture()
   if mode=='occupied':m.data[0x100]=1
   if mode=='small':m.sections[0]=('__DATA','__vchooks',0x1100,16,0x100)
   if mode=='alignment':m.sections[0]=('__DATA','__vchooks',0x1101,2048,0x101)
   with self.assertRaises(p.PatchError):p.reserve_slots(m,162)
if __name__=='__main__':unittest.main()
