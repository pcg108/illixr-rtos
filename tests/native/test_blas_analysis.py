import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from blas_analysis import check_blas,NAMES
class BlasEvidenceTests(unittest.TestCase):
 def fixture(self):
  return {'blas':[dict(backend='openblas_scalar',scratch_bytes=32*1024*1024,interface_bits=32,threads=1)],'blas_selftests':[dict(passed=True,checks=30)],'blas_memory':[dict(outstanding=0,reserved_bytes=32*1024*1024)],'blas_work':[dict(name=n,calls=1,cycles=42,wait_cycles=0,max_m=15,max_n=15,max_k=15,hart_mask=2,harts=[0,1,0,0]) for n in NAMES]}
 def test_legacy(self): self.assertEqual(check_blas({},4)[1],[])
 def test_complete(self): self.assertEqual(check_blas(self.fixture(),4)[1],[])
 def test_wrong_hart(self): self.assertTrue(check_blas(self.fixture(),1)[1])
 def test_duplicate_operation(self):
  d=self.fixture();d['blas_work'][1]=d['blas_work'][0];self.assertTrue(check_blas(d,4)[1])
 def test_no_dgemm(self):
  d=self.fixture();d['blas_work'][0].update(calls=0,hart_mask=0,harts=[0]*4);self.assertTrue(check_blas(d,4)[1])
 def test_missing_preflight(self):
  d=self.fixture();d['blas_selftests']=[];self.assertTrue(check_blas(d,4)[1])
 def test_scratch_leak(self):
  d=self.fixture();d['blas_memory'][0]['outstanding']=1;self.assertTrue(check_blas(d,4)[1])
 def test_count_mismatch(self):
  d=self.fixture();d['blas_work'][0]['calls']=2;self.assertTrue(check_blas(d,4)[1])
 def test_migrating_cycle_samples(self):
  d=self.fixture();r=d['blas_work'][0]
  r.update(counter_version=2,cycles_samples=0,cycles_migrated=1,wait_cycles_migrated=1,elapsed_ns=1000,wait_ns=20,cycles=0)
  self.assertEqual(check_blas(d,4)[1],[])
  r['cycles_samples']=1
  self.assertTrue(check_blas(d,4)[1])
 def vector_fixture(self):
  d=self.fixture();d['blas'][0]['backend']='openblas_rvv'
  d['vector_checks']=[dict(passed=True,errors=0,vlenb=32,fp64=True,hart_mask=15,worker_rounds=[32]*8,migrations=16)]
  d['rvv_kernels']=[dict(phase=p,name=n,calls=1,harts=[0,1,0,0]) for p in ('selftest','work') for n in ('dgemm_kernel','dgemv_n','dgemv_t')]
  return d
 def test_vector_evidence(self):self.assertEqual(check_blas(self.vector_fixture(),4)[1],[])
 def test_missing_vector_context(self):
  d=self.vector_fixture();d['vector_checks']=[];self.assertTrue(check_blas(d,4)[1])
 def test_no_actual_vector_kernel_work(self):
  d=self.vector_fixture();d['rvv_kernels'][3].update(calls=0,harts=[0]*4);self.assertTrue(check_blas(d,4)[1])
 def test_missing_migration(self):
  d=self.vector_fixture();d['vector_checks'][0]['migrations']=0;self.assertTrue(check_blas(d,4)[1])
 def gemmini_fixture(self):
  d=self.vector_fixture();d['blas'][0]['backend']='openblas_gemmini_fp32'
  d['gemmini_selftests']=[dict(passed=True,errors=0,caller_hart_mask=15,caller_migrations=16)]
  d['gemmini']=[dict(phase=p,name=n,precision='fp32',calls=1,submissions=1,accelerator_hart_mask=1,caller_harts=[0,1,0,0],packing_ns=1,unpacking_ns=1,queue_ns=1,execution_ns=1,cycles=1,scratch_high_water=4096,max_m=4,max_n=4,max_k=4) for p in ('selftest','work') for n in ('sgemm','dgemm','sgemv','dgemv')]
  return d
 def test_gemmini_evidence(self):self.assertEqual(check_blas(self.gemmini_fixture(),4)[1],[])
 def test_gemmini_v2_requires_boundary_fixtures(self):
  d=self.gemmini_fixture();d['gemmini_selftests'][0]['fixture_version']=2
  self.assertTrue(check_blas(d,4)[1])
  d['gemmini_edges']=[dict(passed=True,errors=0,cases=590,checks=46552,largest_dim=135,reference='exact_dyadic_and_closed_form')]
  self.assertEqual(check_blas(d,4)[1],[])
  d['gemmini_edges'][0]['checks']-=1
  self.assertTrue(check_blas(d,4)[1])
 def test_gemmini_wrong_accelerator_hart(self):
  d=self.gemmini_fixture();d['gemmini'][0]['accelerator_hart_mask']=2;self.assertTrue(check_blas(d,4)[1])
 def test_gemmini_duplicate(self):
  d=self.gemmini_fixture();d['gemmini'][1]=d['gemmini'][0];self.assertTrue(check_blas(d,4)[1])
 def test_gemmini_no_acceleration(self):
  d=self.gemmini_fixture();d['gemmini'][5].update(submissions=0,accelerator_hart_mask=0);self.assertTrue(check_blas(d,4)[1])
 def test_gemmini_arena_overflow(self):
  d=self.gemmini_fixture();d['gemmini'][0]['scratch_high_water']=33*1024*1024;self.assertTrue(check_blas(d,4)[1])
 def packing_fixture(self):
  d=self.gemmini_fixture()
  d['gemmini_packing']=[dict(phase=g['phase'],name=g['name'],implementation='rvv',pack_elements=48,unpack_elements=16,pack_cycles=30,unpack_cycles=10,pack_read_bytes=48*(8 if g['name'].startswith('d') else 4),pack_write_bytes=192,unpack_read_bytes=64,unpack_write_bytes=16*(8 if g['name'].startswith('d') else 4),vector_calls=1,hart_mask=1) for g in d['gemmini']]
  return d
 def test_packing_evidence(self):self.assertEqual(check_blas(self.packing_fixture(),4)[1],[])
 def test_packing_rejects_corrupt_counters(self):
  for key,value in [('pack_cycles',-1),('unpack_read_bytes',128),('vector_calls',0),('hart_mask',2),('pack_write_bytes',0)]:
   with self.subTest(key=key):
    d=self.packing_fixture();d['gemmini_packing'][0][key]=value;self.assertTrue(check_blas(d,4)[1])
 def test_packing_duplicate(self):
  d=self.packing_fixture();d['gemmini_packing'][1]=d['gemmini_packing'][0];self.assertTrue(check_blas(d,4)[1])
 def test_packing_legacy_counts_only(self):
  d=self.packing_fixture()
  d['gemmini_packing']=[{k:r[k] for k in ('phase','name','implementation','pack_elements','unpack_elements')} for r in d['gemmini_packing']]
  self.assertEqual(check_blas(d,4)[1],[])
if __name__=='__main__': unittest.main()
