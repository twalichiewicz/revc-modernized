import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('source_calls',Path(__file__).resolve().parents[1]/'tools/host/source_calls.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Calls(unittest.TestCase):
 def test_only_matching_named_caller_callee_and_count(self):
  old={'f':{'g':[20,36],'h':[40]},'removed':{'g':[60]}}
  new={'f':{'g':[120,164],'h':[180,190]},'other':{'g':[260]}}
  self.assertEqual(m.match_calls(old,new),{20:120,36:164})
 def test_missing_callers_are_not_rebased_by_numeric_proximity(self):
  self.assertEqual(m.match_calls({'f':{'g':[20]}},{'f':{'h':[120]}}),{})
if __name__=='__main__':unittest.main()
