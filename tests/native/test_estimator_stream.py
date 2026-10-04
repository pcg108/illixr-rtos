import importlib.util,struct,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('stream',Path(__file__).resolve().parents[2]/'scripts/analyze_estimator_stream.py');stream=importlib.util.module_from_spec(spec);spec.loader.exec_module(stream)
class StreamTests(unittest.TestCase):
 def data(self):
  state=[1.]+[0.]*15+[1.];matrix=[float(i//15==i%15) for i in range(225)]
  return b'VIOSTAT1'+struct.pack('<Q',1)+struct.pack('<QIIqQ',0,2,0,1000000000,1)+struct.pack('<17d',*state)+struct.pack('<Q',15)+struct.pack('<450d',*(matrix*2))+struct.pack('<QQ',0,0)
 def inspect(self,b):
  with tempfile.TemporaryDirectory() as t:
   p=Path(t)/'states';p.write_bytes(b);return stream.inspect(p)
 def test_valid(self):
  r=self.inspect(self.data());self.assertEqual(r['poses'],1);self.assertEqual(r['trajectory'][0]['quaternion_xyzw'],[0.,0.,0.,1.])
 def test_truncated(self):
  with self.assertRaises(ValueError):self.inspect(self.data()[:-1])
 def test_trailing(self):
  with self.assertRaises(ValueError):self.inspect(self.data()+b'x')
 def test_nonfinite(self):
  b=bytearray(self.data());struct.pack_into('<d',b,16+32+8,float('nan'))
  with self.assertRaises(ValueError):self.inspect(b)
 def test_wrong_order(self):
  b=bytearray(self.data());struct.pack_into('<Q',b,16,1)
  with self.assertRaises(ValueError):self.inspect(b)
if __name__=='__main__':unittest.main()
