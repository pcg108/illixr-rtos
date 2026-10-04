import sys,json,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from analyze_packing_benchmark import analyze,SHAPES
class BenchmarkTests(unittest.TestCase):
 def records(self):return [dict(m=m,n=n,k=k,layout=l,beta=b,trial=t,pack_cycles=[30,20,10],unpack_cycles=[12,8,4]) for m,n,k in SHAPES for l in range(4) for b in (0,1) for t in range(1,9)]
 def text(self,rows):return 'ILLIXR_PACKING_TEST '+json.dumps(dict(passed=True,failures=0,checks=100))+'\n'+''.join('ILLIXR_PACKING_BENCH '+json.dumps(r)+'\n' for r in rows)
 def test_valid(self):
  r=analyze(self.text(self.records()),'firesim');self.assertEqual(r['samples'],512);self.assertEqual(r['cases'][0]['rvv_contiguous_speedup'],3);self.assertTrue(r['hardware_performance_evidence'])
 def test_spike_not_performance(self):self.assertFalse(analyze(self.text(self.records()),'spike')['hardware_performance_evidence'])
 def test_missing(self):
  with self.assertRaises(ValueError):analyze(self.text(self.records()[:-1]),'firesim')
 def test_duplicate(self):
  r=self.records();r[-1]=r[0]
  with self.assertRaises(ValueError):analyze(self.text(r),'firesim')
 def test_invalid(self):
  r=self.records();r[0]['pack_cycles'][0]=0
  with self.assertRaises(ValueError):analyze(self.text(r),'firesim')
if __name__=='__main__':unittest.main()
