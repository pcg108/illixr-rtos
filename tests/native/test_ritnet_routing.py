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
if __name__=='__main__':unittest.main()
