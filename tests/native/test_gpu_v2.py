import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'scripts'))
import analyze_spike as a


class DisplayScheduleTrace(unittest.TestCase):
    def setUp(self):
        P=8_333_333; O=1_000_000_000
        self.P=P
        r=dict(version=2,stage='render',frame_id=1,slot=0,scheduled_wake_ns=1_000_000,
               actual_wake_ns=1_000_000,submit_ns=1_000_001,scheduled_complete_ns=7_944_446,
               observed_complete_ns=8_000_000,publication_ns=8_000_001,presentation_ns=P,
               target_ns=O+P,source_ns=O,source_sequence=1,prediction_status='valid',
               prediction_horizon_ns=P,position=[0,0,0],orientation=[1,0,0,0],hart=0,processing_hart=0,publication_hart=0)
        self.events=[r]
        for slot in (2,3):
            wake=slot*P-2_000_000
            self.events.append(dict(r,stage='timewarp',warp_id=slot-1,display_slot=slot,scheduled_wake_ns=wake,
                actual_wake_ns=wake,selection_ns=wake+1,submit_ns=wake+2,
                scheduled_complete_ns=wake+1_000_002,observed_complete_ns=wake+1_000_002,
                publication_ns=wake+1_000_003,frame_publication_ns=r['publication_ns'],frame_age_ns=wake+1-r['publication_ns'],
                source_ns=O+(slot-1)*P,source_sequence=slot,target_ns=O+slot*P,prediction_horizon_ns=P,
                reused=slot==3,final=slot==3,render_source_sequence=1,render_prediction_status='valid',
                render_orientation=[1,0,0,0],transform=[1 if x%5==0 else 0 for x in range(16)],
                hart=1,processing_hart=1,publication_hart=1))
        self.gpu=dict(version=2,render_submitted=1,render_completed=1,render_skipped_slots=0,
            timewarp_submitted=2,timewarp_completed=2,distinct_selected=1,never_selected=0,repeated_uses=1,
            empty_opportunities=1,missed_opportunities=0,render_deadlines_missed=0,timewarp_deadlines_missed=0,
            fresh_warp_completed=2,new_outputs=2,repeated_outputs=0,no_outputs=1,fresh_on_time_presentations=2,
            display_slots=3,trace_overflow=0,render_closed_ns=17_000_000,timewarp_done_ns=self.events[-1]['publication_ns'],
            period_ns=P,render_offset_ns=1_000_000,timewarp_margin_ns=1_000_000,render_delay_ns=6_944_445,
            timewarp_delay_ns=1_000_000,render_closed=True,timewarp_done=True)
        self.slots=[dict(version=2,first_slot=i,last_slot=i,scheduled_wake_ns=i*P-2_000_000,
                         observed_ns=i*P-2_000_000,outcome='empty' if i==1 else 'submitted',final=i==3) for i in (1,2,3)]
        self.displays=[dict(version=2,display_slot=i,boundary_ns=i*P,observed_ns=3*P,observer_lateness_ns=(3-i)*P,
                 warp_id=i-1,frame_id=0 if i==1 else 1,publication_ns=0 if i==1 else self.events[i-1]['publication_ns'],
                 outcome='none' if i==1 else 'new',fresh_on_time=i!=1,hart=0) for i in (1,2,3)]

    def check(self):
        predictions=[]; placements=[]
        for e in self.events:
            predictions.append(dict(caller=int(e['stage']=='timewarp'),processing_hart=e['processing_hart'],
                publication_hart=e['publication_hart'],status=0,source_ns=e['source_ns'],source_seq=e['source_sequence'],
                target_ns=e['target_ns'],horizon_ns=e['prediction_horizon_ns'],computed_ns=e['submit_ns'],
                position=e['position'],orientation=e['orientation']))
        for caller in (0,1):
            counts=[sum(p['caller']==caller and p['processing_hart']==h for p in predictions) for h in (0,1)]
            placements.append(dict(caller=caller,work_counts=counts,publication_counts=counts,hart_mask=sum(1<<h for h in (0,1) if counts[h])))
        data=dict(gpu_results=[self.gpu],gpu_events=self.events,warp_slots=self.slots,displays=self.displays,
                  predictions=predictions,prediction_placements=placements,
                  prediction_summaries=[dict(calls=len(predictions),overflow=0,invalid=0,max_horizon_ns=50_000_000)])
        return a.gpu_checks(data,dict(origin_ns=1_000_000_000,runtime_ns=40_000_000),2)

    def reject(self,needle):
        _,errors=self.check()
        self.assertTrue(any(needle in e for e in errors),errors)

    def test_reuse_with_fresh_prediction_and_delayed_observer(self):
        result,errors=self.check()
        self.assertEqual(errors,[])
        self.assertEqual(result['fresh_reuse_with_updated_prediction'],1)

    def test_duplicate_warp_id(self):
        self.events[-1]['warp_id']=1
        self.reject('duplicate or missing warp_id')

    def test_future_frame(self):
        self.events[1]['selection_ns']=7_000_000
        self.reject('Future frame')

    def test_wrong_reuse_flag(self):
        self.events[-1]['reused']=False
        self.reject('repeated-frame use flag')

    def test_wrong_reuse_count(self):
        self.gpu['repeated_uses']=0
        self.reject('reuse accounting')

    def test_hidden_miss(self):
        self.events[-1]['publication_ns']=3*self.P+1
        self.reject('hidden deadline misses')

    def test_no_retarget(self):
        self.events[-1]['target_ns']+=self.P
        self.reject('original prediction deadline')

    def test_phase_drift(self):
        self.events[-1]['scheduled_wake_ns']+=1
        self.reject('shared display phase')

    def test_saved_pose_immutable(self):
        self.events[-1]['render_orientation']=[0,1,0,0]
        self.reject('Immutable saved render pose')

    def test_future_presentation(self):
        self.displays[0]['warp_id']=1
        self.reject('unavailable at boundary')

    def test_observer_lateness(self):
        self.displays[0]['observer_lateness_ns']=0
        self.reject('observer lateness')

    def test_missing_opportunity(self):
        self.slots.pop(0)
        self.reject('Missing/duplicate timewarp opportunity')

    def test_no_final_frame(self):
        self.events[-1]['final']=False
        self.reject('Final-frame shutdown')

    def test_early_gpu_completion(self):
        self.events[-1]['observed_complete_ns']=0
        self.reject('completion/publication ordering')

    def test_expired_wake(self):
        self.events[-1]['actual_wake_ns']=3*self.P
        self.reject('expired opportunity')

    def test_unknown_version(self):
        self.gpu['version']=3
        self.reject('Unsupported GPU trace version')

    def test_malformed_trace(self):
        del self.events[-1]['warp_id']
        self.reject('Malformed GPU v2')

if __name__=='__main__': unittest.main()
