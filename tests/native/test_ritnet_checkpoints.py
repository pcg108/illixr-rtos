import copy,ctypes,json,subprocess,sys,tempfile,unittest
from pathlib import Path
R=Path(__file__).resolve().parents[2];sys.path.insert(0,str(R/'scripts'))
from instrument_ritnet import calls
from analyze_ritnet_checkpoints import analyze
from run_ritnet_diagnostics import narrow, execution_is_incomplete
class DiagnosticTermination(unittest.TestCase):
 def test_complete_numerical_failure_is_evidence(self):
  self.assertFalse(execution_is_incomplete({'fatal_markers':['*** FAILED ***'],'returncode':-15},{'complete':True}))
 def test_interrupted_export_is_incomplete(self):
  self.assertTrue(execution_is_incomplete({'interrupted':True},{'complete':False}))
 def test_crash_is_not_numerical_evidence(self):
  self.assertTrue(execution_is_incomplete({'fatal_markers':['CPU exception']},{'complete':True}))
 def test_missing_trace_is_incomplete(self):
  self.assertTrue(execution_is_incomplete({'fatal_markers':['*** FAILED ***']},{'complete':False}))
class DrainNarrowing(unittest.TestCase):
 def test_single_boundary(self):
  answer,count=narrow(list(range(1,65)),lambda ids:17 in ids)
  self.assertEqual(answer,[17]);self.assertLessEqual(count,24)
 def test_multiple_boundaries(self):
  answer,count=narrow(list(range(1,17)),lambda ids:{3,11}.issubset(ids),64)
  self.assertEqual(set(answer),{3,11});self.assertLessEqual(count,64)
 def test_budget(self):
  seen=[];answer,count=narrow(list(range(1,65)),lambda ids:seen.append(ids) or False,3)
  self.assertEqual(count,3);self.assertEqual(len(seen),3);self.assertEqual(len(answer),64)
class TensorLayout(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();p=Path(cls.tmp.name)
  (p/'stubs.c').write_text('#include "diagnostics.h"\nunsigned drain_calls; void rd_drain(void){drain_calls++;} int rd_guards(void){return 0;} uint64_t immutable_value; uint64_t rd_immutable(void){return immutable_value;}\nunsigned before,after; void rd_reference_inputs(const struct rd_record *r){before=*(const unsigned char*)r->op.input[0].data;} void rd_reference_tensor(const struct rd_record *r){after=*(const unsigned char*)r->op.output.data;}')
  port=R/'third_party/ritnet/port'
  subprocess.run(['gcc','-shared','-fPIC','-DRITNET_HOST_REFERENCE=1','-I'+str(port),str(port/'diagnostics.c'),str(p/'stubs.c'),'-o',str(p/'diag.so')],check=True)
  cls.lib=ctypes.CDLL(str(p/'diag.so'))
  class View(ctypes.Structure):_fields_=[('data',ctypes.c_void_p),('rows',ctypes.c_size_t),('cols',ctypes.c_size_t),('stride',ctypes.c_size_t),('element_bytes',ctypes.c_size_t)]
  cls.View=View;cls.lib.rd_hash.argtypes=[View];cls.lib.rd_hash.restype=ctypes.c_uint64
  class Operation(ctypes.Structure):_fields_=[('id',ctypes.c_uint32),('name',ctypes.c_char_p),('kind',ctypes.c_char_p),('input',View*3),('output',View),('parameters',ctypes.c_double*32),('parameter_count',ctypes.c_uint32)]
  cls.Operation=Operation;cls.lib.rd_begin.argtypes=[ctypes.POINTER(Operation)]
  class Record(ctypes.Structure):_fields_=[('op',Operation),('inference',ctypes.c_uint64),('begin_cycle',ctypes.c_uint64),('end_cycle',ctypes.c_uint64),('overhead_cycles',ctypes.c_uint64),('input_hash',ctypes.c_uint64*3),('output_hash',ctypes.c_uint64),('padding_hash',ctypes.c_uint64),('immutable_hash',ctypes.c_uint64),('image_hash',ctypes.c_uint64),('mode',ctypes.c_uint32),('hart',ctypes.c_uint32),('end_hart',ctypes.c_uint32),('guard_error',ctypes.c_uint32),('mismatch',ctypes.c_uint32),('drained',ctypes.c_uint32)]
  cls.Record=Record
  cls.lib.rd_pack.argtypes=[View,ctypes.c_void_p,ctypes.c_size_t];cls.lib.rd_pack.restype=ctypes.c_size_t
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_partial_concatenation(self):
  a=(ctypes.c_ubyte*8)(90,1,2,91,92,3,4,93);v=self.View(ctypes.addressof(a)+1,2,2,4,1)
  h=self.lib.rd_hash(v);a[0]=88;a[3]=87;a[4]=86;a[7]=85
  self.assertEqual(h,self.lib.rd_hash(v));b=(ctypes.c_ubyte*4)();self.assertEqual(self.lib.rd_pack(v,b,4),4);self.assertEqual(bytes(b),bytes([1,2,3,4]))
 def test_fingerprint_byte_order(self):
  a=(ctypes.c_ubyte*4)(1,2,3,4);h=14695981039346656037
  for c in bytes(a):h=((h^c)*1099511628211)&((1<<64)-1)
  self.assertEqual(self.lib.rd_hash(self.View(ctypes.addressof(a),1,2,2,2)),h)
 def test_capture_overflow(self):
  a=(ctypes.c_ubyte*8)();self.assertEqual(self.lib.rd_pack(self.View(ctypes.addressof(a),2,4,4,1),a,7),ctypes.c_size_t(-1).value)
 def test_bad_stride(self):
  a=(ctypes.c_ubyte*8)();self.assertEqual(self.lib.rd_pack(self.View(ctypes.addressof(a),2,4,3,1),a,8),ctypes.c_size_t(-1).value)
 def test_stride_overflow(self):
  a=(ctypes.c_ubyte*8)();self.assertEqual(self.lib.rd_pack(self.View(ctypes.addressof(a),2,1,ctypes.c_size_t(-1).value,2),a,8),ctypes.c_size_t(-1).value)
 def test_in_place_capture_order(self):
  a=(ctypes.c_ubyte*1)(7);v=self.View(ctypes.addressof(a),1,1,1,1)
  op=self.Operation();op.id=1;op.input[0]=v;op.output=v
  self.lib.rd_inference_begin(a,1);self.lib.rd_begin(ctypes.byref(op));a[0]=9;self.assertEqual(self.lib.rd_end(),0)
  self.assertEqual(ctypes.c_uint.in_dll(self.lib,'before').value,7)
  self.assertEqual(ctypes.c_uint.in_dll(self.lib,'after').value,9)
 def test_record_overflow(self):
  count=ctypes.c_uint32.in_dll(self.lib,'ritnet_diag_count');error=ctypes.c_uint32.in_dll(self.lib,'ritnet_diag_error');saved=count.value
  op=self.Operation();op.id=1
  try:
   count.value=64*32;self.lib.rd_begin(ctypes.byref(op));self.assertEqual(self.lib.rd_end(),-1);self.assertEqual(error.value,3);self.assertEqual(count.value,64*32)
  finally:count.value=saved;error.value=0
 def test_post_failure_reads_only_after_explicit_request(self):
  mode=ctypes.c_uint32.in_dll(self.lib,'ritnet_diag_mode');count=ctypes.c_uint32.in_dll(self.lib,'ritnet_diag_count');post=ctypes.c_uint32.in_dll(self.lib,'ritnet_diag_post_count');mask=ctypes.c_uint64.in_dll(self.lib,'ritnet_diag_drain_mask');drains=ctypes.c_uint.in_dll(self.lib,'drain_calls')
  saved=(mode.value,count.value,post.value,mask.value)
  try:
   mode.value=2;count.value=0;post.value=0;mask.value=0
   a=(ctypes.c_ubyte*1)(7);v=self.View(ctypes.addressof(a),1,1,1,1);op=self.Operation();op.input[0]=v;op.output=v
   self.lib.rd_inference_begin(a,1);before=drains.value
   for i in range(1,65):op.id=i;self.lib.rd_begin(ctypes.byref(op));self.assertEqual(self.lib.rd_end(),0)
   self.lib.rd_inference_end();self.assertEqual(post.value,0);self.assertEqual(drains.value,before)
   a[0]=9;ctypes.c_uint64.in_dll(self.lib,'immutable_value').value=77
   self.lib.rd_final_failure_snapshot();self.assertEqual(post.value,2);self.assertEqual(drains.value,before+1)
   snapshots=(self.Record*2).in_dll(self.lib,'ritnet_diag_post_records')
   self.assertEqual(snapshots[0].image_hash,self.lib.rd_hash(v));self.assertEqual(snapshots[0].input_hash[0],self.lib.rd_hash(v));self.assertEqual(snapshots[0].immutable_hash,77)
   self.lib.rd_final_failure_snapshot();self.assertEqual(post.value,2);self.assertEqual(drains.value,before+1)
  finally:
   mode.value,count.value,post.value,mask.value=saved;ctypes.c_uint64.in_dll(self.lib,'immutable_value').value=0
 def test_all_operations_covered(self):
  subprocess.run([sys.executable,str(R/'scripts/instrument_ritnet.py'),'--check'],check=True,stdout=subprocess.DEVNULL)
  self.assertEqual(len(calls((R/'third_party/ritnet/port/ritnet.c').read_text())),64)
class TraceValidation(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name)
  view=dict(rows=1,cols=1,stride=1,element_bytes=1)
  self.g=dict(id=1,name='output',kind='test',input_hash=[1,2,3],output_hash=4,image_hash=5,immutable_hash=6,views=[view.copy() for _ in range(4)],parameters=[1.0])
  (self.p/'operations.jsonl').write_text(json.dumps(self.g)+'\n');(self.p/'op01-output0.bin').write_bytes(b'\x04')
  op=dict(self.g,inference=1,begin_cycle=1,end_cycle=2,overhead_cycles=1,hart=0,end_hart=0,mode=3,drained=1,guard_error=0,mismatch=0)
  self.d=dict(config=[dict(mode=3,records=1,error=0,capture_size=0)],operations=[op],capture=[])
 def tearDown(self):self.tmp.cleanup()
 def test_valid(self):self.assertTrue(analyze(self.d,self.p)['passed'])
 def test_duplicate(self):
  self.d['operations']*=2;self.d['config'][0]['records']=2
  self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_missing(self):
  self.d['operations'][0]['id']=2
  self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_unfinished(self):
  self.d['operations'][0]['drained']=0
  self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_localize_with_matching_inputs(self):
  self.d['operations'][0].update(output_hash=7,mismatch=8);self.d['config'][0].update(error=5,capture_size=1,capture_operation=1);self.d['capture']=[dict(offset=0,hex='05')]
  a=analyze(self.d,self.p);self.assertTrue(a['trace_valid']);self.assertTrue(a['first_divergence']['inputs_match']);self.assertEqual(a['capture_difference']['actual'],5)
 def test_bad_capture_offset(self):
  self.d['capture']=[dict(offset=1,hex='05')]
  self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_metadata_must_not_hash(self):
  self.d['config'][0]['mode']=1;self.d['operations'][0]['mode']=1
  self.assertFalse(analyze(self.d,self.p)['trace_valid'])
class PostFailureRuntime(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();p=Path(cls.tmp.name);cls.binary=p/'check'
  h=((14695981039346656037^7)*1099511628211)&((1<<64)-1)
  (p/'ritnet_diagnostic_expected.h').write_text('static const uint64_t rd_expected_image='+str(h)+'ULL;\nstatic const uint64_t rd_expected_immutable=123;\nstatic const struct rd_expected rd_expected_operations[64]={'+','.join('{{'+str(h)+'ULL,0,0},'+str(h)+'ULL}' for _ in range(64))+'};\n')
  (p/'check.c').write_text(r'''#include "diagnostics.h"
#include <assert.h>
#include <stdlib.h>
static unsigned drains;static uint64_t assets=123;
void rd_drain(void){drains++;}int rd_guards(void){return 0;}uint64_t rd_immutable(void){return assets;}
int main(int argc,char **argv){
 unsigned char image=7,input=7,producer=7,consumer=7;
 struct rd_view in={&input,1,1,1,1};struct rd_operation op={0};
 ritnet_diag_mode=2;ritnet_diag_drain_mask=0;rd_inference_begin(&image,1);
 op.input[0]=in;
 for(unsigned id=1;id<=64;id++){op.id=id;op.output=(struct rd_view){id==32?&consumer:&producer,1,1,1,1};rd_begin(&op);assert(!rd_end());}
 rd_inference_end();assert(drains==0&&ritnet_diag_post_count==0);
 if(atoi(argv[1])==0)consumer=9;else{image=8;assets=456;}
 rd_final_failure_snapshot();assert(drains==1&&ritnet_diag_post_count==2&&!ritnet_diag_error);
 assert(!ritnet_diag_records[31].input_hash[0]&&!ritnet_diag_records[31].output_hash);
 if(atoi(argv[1])==0){
  assert(!ritnet_diag_post_records[0].mismatch&&ritnet_diag_post_records[1].mismatch==8);
  assert(ritnet_diag_capture_size==1&&ritnet_diag_capture_id==32&&ritnet_diag_capture[0]==9);
 }else{
  assert(ritnet_diag_post_records[0].mismatch==96&&ritnet_diag_post_records[1].mismatch==96);
  assert(!ritnet_diag_capture_size);
 }
 return 0;
}
''')
  port=R/'third_party/ritnet/port'
  subprocess.run(['gcc','-O2','-DRITNET_HOST_REFERENCE=1','-DRITNET_DIAG_EXPECTED=1','-I'+str(port),'-I'+str(p),str(port/'diagnostics.c'),str(p/'check.c'),'-o',str(cls.binary)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_bad_consumer_capture(self):subprocess.run([str(self.binary),'0'],check=True)
 def test_image_and_weight_recheck(self):subprocess.run([str(self.binary),'1'],check=True)
class PostFailureValidation(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.p=Path(self.tmp.name);gold=[];ops=[]
  for i in range(1,65):
   v=dict(rows=1,cols=1,stride=1,element_bytes=1)
   g=dict(id=i,name='op'+str(i),kind='test',input_hash=[1,2,3],output_hash=4,image_hash=5,immutable_hash=6,views=[v.copy() for _ in range(4)],parameters=[]);gold.append(g)
   ops.append(dict(g,inference=1,input_hash=[0,0,0],output_hash=0,begin_cycle=i*2,end_cycle=i*2+1,overhead_cycles=0,hart=0,end_hart=0,mode=2,drained=0,guard_error=0,mismatch=0,views=[dict(v,address=i*100+j*10) for j in range(4)]))
  (self.p/'operations.jsonl').write_text('\n'.join(map(json.dumps,gold))+'\n')
  post=[dict(observation='after_failed_inference',inference=1,id=i,begin_cycle=200,end_cycle=201,hart=0,end_hart=0,drained=1,guard_error=0,mismatch=0,image_hash=5,immutable_hash=6,input_hash=[1,2,3],output_hash=4) for i in (31,32)]
  self.d=dict(config=[dict(mode=2,records=64,error=0,drain_mask=0,capture_size=0,post_records=2)],operations=ops,capture=[],post=post)
 def tearDown(self):self.tmp.cleanup()
 def test_end_state_not_first_operation_divergence(self):
  self.d['post'][1].update(output_hash=7,mismatch=8);a=analyze(self.d,self.p)
  self.assertTrue(a['trace_valid']);self.assertFalse(a['passed']);self.assertIsNone(a['first_divergence']);self.assertTrue(a['post_failure_observations'][1]['inputs_match'])
 def test_immutable_recheck(self):
  self.d['post'][0].update(immutable_hash=8,mismatch=64);a=analyze(self.d,self.p);self.assertTrue(a['trace_valid']);self.assertFalse(a['post_failure_observations'][0]['immutable_matches'])
 def test_premature_observer(self):
  self.d['post'][0]['begin_cycle']=0;self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_missing_post_record(self):
  self.d['post'].pop();self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_later_overwrite_rejected(self):
  self.d['operations'][40]['views'][3]=self.d['operations'][30]['views'][3].copy();self.assertFalse(analyze(self.d,self.p)['trace_valid'])
 def test_partial_concatenation_not_false_overlap(self):
  from analyze_ritnet_checkpoints import views_overlap
  a=dict(address=1000,rows=150,cols=32,stride=96,element_bytes=1)
  self.assertFalse(views_overlap(a,dict(a,address=1032)));self.assertTrue(views_overlap(a,dict(a,address=1016)))
if __name__=='__main__':unittest.main()
