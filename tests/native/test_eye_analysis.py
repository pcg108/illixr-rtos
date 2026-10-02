import copy
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
from eye_analysis import check

class EyeTraceTest(unittest.TestCase):
 def fixture(self):
  reference=json.loads((Path(__file__).resolve().parents[2]/'third_party/ritnet/reference/manifest.json').read_text())
  image={'sequence':1,'scheduled_ns':0,'published_ns':5,'hart':1}
  result={'warp_id':1,'display_slot':1,'inference_id':1,'image_sequence':1,'image_ns':5,'snapshot_begin_ns':10,'request_ns':11,'start_ns':12,'completion_ns':100,'caller_hart':3,'accelerator_hart':0,'cycles':88,'valid':True,'expired':False,'final':False,'decision_ns':100,'x':reference['x'],'y':reference['y'],'output_hash':reference['output_hash']}
  config={'version':1,'enabled':True,'synchronous':True,'hz':120,'width':240,'height':160,'opcode':2,'accelerator_hart':0,'precision':'int8','publications':1,'requests':1}
  return {'eye_configs':[config],'eye_images':[image],'eye_results':[result],'gpu_events':[{'stage':'timewarp','warp_id':1,'display_slot':1,'actual_wake_ns':9,'submit_ns':101}]}
 def test_valid(self):
  self.assertEqual(check(self.fixture(),{'runtime_ns':200},4)[1],[])
 def test_expired_request_is_not_a_completed_warp(self):
  d=self.fixture();r=d['eye_results'][0];r.update(expired=True,warp_id=0,decision_ns=10000000,completion_ns=10000000)
  d['gpu_events']=[];d['warp_slots']=[dict(outcome='missed',first_slot=1,last_slot=1,observed_ns=10000000,final=False)]
  self.assertEqual(check(d,{'runtime_ns':11000000},4)[1],[])
 def test_reject_wrong_array_hart(self):
  d=self.fixture();d['eye_results'][0]['accelerator_hart']=1
  self.assertTrue(check(d,{'runtime_ns':200},4)[1])
 def test_reject_future(self):
  d=self.fixture();d['eye_images'][0]['published_ns']=50;d['eye_results'][0]['image_ns']=50
  self.assertTrue(check(d,{'runtime_ns':200},4)[1])
 def test_reject_old_snapshot(self):
  d=self.fixture();d['eye_images'].append(dict(d['eye_images'][0],sequence=2,published_ns=8,scheduled_ns=0));d['eye_configs'][0]['publications']=2
  self.assertTrue(any('old image' in e for e in check(d,{'runtime_ns':200},4)[1]))
 def test_reject_wrong_output(self):
  d=self.fixture();d['eye_results'][0]['output_hash']^=1
  self.assertTrue(check(d,{'runtime_ns':200},4)[1])
 def test_reject_duplicate_inference(self):
  d=self.fixture();d['eye_results'].append(copy.deepcopy(d['eye_results'][0]));d['gpu_events'].append(copy.deepcopy(d['gpu_events'][0]));d['eye_configs'][0]['requests']=2
  self.assertTrue(check(d,{'runtime_ns':200},4)[1])

class AsyncEyeTraceTest(unittest.TestCase):
 def fixture(self):
  d=EyeTraceTest().fixture()
  c=d['eye_configs'][0];c.update(version=2,synchronous=False,reads=4)
  r=d['eye_results'][0]
  for k in ('warp_id','display_slot','caller_hart','expired','final','decision_ns'):r.pop(k)
  r.update(publication_ns=105,publication_hart=0)
  d['eye_reads']=[];d['gpu_events']=[]
  for i,t in enumerate([20,110,120,130]):
   identity=0 if i==0 else 1
   d['eye_reads'].append(dict(warp_id=i+1,display_slot=i+1,inference_id=identity,begin_ns=t,end_ns=t+1,age_ns=t+1-105 if identity else 0,hart=3,reused=i>1))
   d['gpu_events'].append(dict(stage='timewarp',warp_id=i+1,display_slot=i+1,actual_wake_ns=t-1,submit_ns=t+2))
  return d
 def errors(self,d):return check(d,{'runtime_ns':200},4)[1]
 def test_startup_then_reuse(self):
  report,errors=check(self.fixture(),{'runtime_ns':200},4)
  self.assertEqual(errors,[]);self.assertEqual(report['repeated_reads'],2);self.assertEqual(report['unavailable_reads'],1)
 def test_future_prediction_rejected(self):
  d=self.fixture();d['eye_reads'][0].update(inference_id=1,age_ns=-84)
  self.assertTrue(any('Future eye' in e for e in self.errors(d)))
 def test_missing_available_prediction_rejected(self):
  d=self.fixture();d['eye_reads'][1].update(inference_id=0,age_ns=0)
  self.assertTrue(any('latest prediction' in e for e in self.errors(d)))
 def test_bad_reuse_rejected(self):
  d=self.fixture();d['eye_reads'][2]['reused']=False
  self.assertTrue(any('reuse' in e for e in self.errors(d)))
 def test_duplicate_inference_rejected(self):
  d=self.fixture();d['eye_results'].append(copy.deepcopy(d['eye_results'][0]));d['eye_configs'][0]['requests']=2
  self.assertTrue(self.errors(d))
 def test_wrong_hart_rejected(self):
  d=self.fixture();d['eye_results'][0]['publication_hart']=1
  self.assertTrue(self.errors(d))
 def test_wrong_result_rejected(self):
  d=self.fixture();d['eye_results'][0]['output_hash']^=1
  self.assertTrue(self.errors(d))
 def test_missing_read_rejected(self):
  d=self.fixture();d['eye_reads'].pop();d['eye_configs'][0]['reads']-=1
  self.assertTrue(self.errors(d))
 def test_age_rejected(self):
  d=self.fixture();d['eye_reads'][-1]['age_ns']+=1
  self.assertTrue(self.errors(d))
 def test_concurrent_replacement_allows_old_snapshot(self):
  d=self.fixture();d['eye_images'].append(dict(sequence=2,scheduled_ns=8333333,published_ns=8333334,hart=1));d['eye_configs'][0]['publications']=2
  r=dict(d['eye_results'][0],inference_id=2,image_sequence=2,image_ns=8333334,snapshot_begin_ns=8333335,request_ns=8333336,start_ns=8333337,completion_ns=8333340,publication_ns=8333345)
  d['eye_results'].append(r);d['eye_configs'][0]['requests']=2
  read=d['eye_reads'][-1];read.update(begin_ns=8333344,end_ns=8333346,age_ns=8333346-105)
  d['gpu_events'][-1].update(actual_wake_ns=8333343,submit_ns=8333347)
  self.assertEqual(check(d,{'runtime_ns':9000000},4)[1],[])
  read['begin_ns']=8333346
  self.assertTrue(any('latest prediction' in e for e in check(d,{'runtime_ns':9000000},4)[1]))

if __name__=='__main__':unittest.main()
