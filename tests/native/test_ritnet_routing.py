import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from run_ritnet_standalone import routing
class RoutingTest(unittest.TestCase):
 def test_exclusive_arrays(self):
  text='RITNET_ROUTE_PHASE fp32_begin\nRITNET_ROUTE array=fp32 count=1 opcode=123 funct=7\nRITNET_ROUTE_PHASE fp32_end pass\nRITNET_ROUTE_PHASE int8_begin 0\nRITNET_ROUTE array=int8 count=1 opcode=91 funct=7\nRITNET_ROUTE_PHASE int8_end 0 mismatches=0\n'
  clean,result=routing(text,'dual');self.assertFalse(result['errors']);self.assertNotIn('array=',clean)
  self.assertTrue(routing(text.replace('opcode=91','opcode=123'),'dual')[1]['errors'])
 def test_interleaved_console_recovery(self):
  text='RITNET_ROUTE_PHASE int8_begin 0\nRITNET_ROUTE_PHASE int8_RITNET_ROUTE array=int8 count=1 opcode=91 funct=7\nend 0 mismatches=0\n'
  clean,result=routing(text,'int8');self.assertIn('int8_end 0',clean);self.assertFalse(result['errors'])
 def test_missing_counter(self):
  text='RITNET_ROUTE_PHASE int8_begin 0\nRITNET_ROUTE array=int8 count=2 opcode=91 funct=7\nRITNET_ROUTE_PHASE int8_end 0 mismatches=0\n'
  self.assertTrue(routing(text,'int8')[1]['errors'])
class StandaloneValidationTest(unittest.TestCase):
 def fixture(self,n=32):
  import json
  reference=json.loads((Path(__file__).resolve().parents[2]/'third_party/ritnet/reference/manifest.json').read_text())
  data={'eye_results':[dict(valid=True,accelerator_hart=0,output_hash=reference['output_hash'],inference_id=i+1,cycles=100) for i in range(n)]}
  text='ILLIXR_RITNET_STANDALONE_END pass\n'+''.join(f'RITNET_ROUTE_PHASE int8_end {i} mismatches=0 valid=1 hash=0\nRITNET_INTERRUPT_PROGRESS {i} 5\n' for i in range(n))
  return data,text
 def check(self,data,text,n=32):
  from ritnet_validation import standalone_errors
  return standalone_errors(data,text,1,False,expected_inferences=n)
 def test_legacy_and_diagnostic(self):
  for n in (2,32):self.assertEqual(self.check(*self.fixture(n),n),[])
 def test_diagnostic_requires_explicit_count(self):
  from ritnet_validation import standalone_errors
  self.assertTrue(standalone_errors(*self.fixture(),1,False))
 def test_rejects_missing_duplicate_and_wrong_tensor(self):
  data,text=self.fixture()
  self.assertTrue(self.check(data,text.replace('int8_end 31','int8_end 30')))
  self.assertTrue(self.check(data,text.replace('int8_end 31 mismatches=0','int8_end 31 mismatches=1')))
  self.assertTrue(self.check(data,text.replace('RITNET_INTERRUPT_PROGRESS 31 5','RITNET_INTERRUPT_PROGRESS 31 0')))
  self.assertTrue(self.check(data,text.replace('RITNET_INTERRUPT_PROGRESS 31 5','RITNET_INTERRUPT_PROGRESS 30 5')))
  data['eye_results'][-1]['inference_id']=31
  self.assertTrue(self.check(data,text))
 def test_rejects_bad_hash_hart_and_count(self):
  for key,value in [('output_hash',0),('accelerator_hart',1),('cycles',0),('valid',False)]:
   data,text=self.fixture();data['eye_results'][-1][key]=value
   self.assertTrue(self.check(data,text))
  for n in (0,33,True,2.0):self.assertTrue(self.check(*self.fixture(),n))
 def test_declared_count_cannot_override_firmware(self):
  import json,tempfile
  import run_firesim_matrix as fs
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);(p/'build_manifest.json').write_text(json.dumps({'target':{},'kind':'ritnet_standalone','diagnostic_variant':{'inferences':2}}))
   with self.assertRaisesRegex(ValueError,'count must match'):
    fs.validate_firmware({'elf':str(p/'zephyr.elf'),'ritnet_standalone':True,'ritnet_inferences':32})
if __name__=='__main__':unittest.main()
