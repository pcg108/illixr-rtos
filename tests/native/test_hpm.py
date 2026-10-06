import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import hpm_build_audit
import hpm_hardware
import hpm_analysis as hpm
import summarize_performance as summary
from analyze_spike import records


def fixture():
    values=[100,50]+list(range(13))
    return {'hpm_selftests':[{'version':1,'passed':True,'hart_mask':1}], 'hpm_configs':[{'version':1,'enabled':True,'event_map':'rocket-hpm-v1','harts':1,
                           'programmable_width':40,'basic_width':64,'events':hpm.EVENTS,'selectors':hpm.SELECTORS}],
            'hpm_preflights':[{'version':1,'hart':0,'passed':True,'programmable_counters':13}],
            'hpm_work':[{'version':1,'hart':0,'plugin':'openvins','phase':'default','caller':'system','intervals':2,'counters':values.copy()}],
            'hpm_harts':[{'version':1,'hart':0,'begin_timer':5,'end_timer':105,'samples':10,'max_read_cycles':15,'errors':0,'counters':values.copy()}],
            'hpm_threads':[{'version':1,'id':0,'plugin':'openvins','migrations':0}]}


def write_trace(path,data):
    prefixes={'hpm_selftests':'SELFTEST','hpm_configs':'CONFIG','hpm_work':'WORK','hpm_harts':'HART','hpm_threads':'THREAD','hpm_preflights':'PREFLIGHT'}
    path.write_text(''.join('ILLIXR_HPM_'+prefixes[k]+' '+json.dumps(row)+'\n' for k,v in data.items() for row in v))


class HpmTests(unittest.TestCase):
    def test_valid_conserved_accounting_and_ratios(self):
        result,errors=hpm.check(fixture(),1,True)
        self.assertEqual(errors,[]);self.assertEqual(result['plugins'][0]['ipc'],.5)
        self.assertEqual(result['plugins'][0]['cpi'],2)

    def test_legacy_and_required_missing(self):
        self.assertFalse(hpm.check({})[0]['available'])
        self.assertEqual(hpm.check({})[1],[])
        self.assertTrue(hpm.check({},required=True)[1])

    def test_selftest_requires_reported_interrupt_accounting(self):
        # Legacy traces lack this field. A new self-test cannot claim success
        # with missing ISR attribution even when all totals are conserved.
        self.assertEqual(hpm.check(fixture())[1], [])
        for value in (0, -1, True, None, '1'):
            data=fixture();data['hpm_selftests'][0]['isr_instructions']=value
            with self.subTest(value=value):
                self.assertTrue(hpm.check(data)[1])
        data=fixture();data['hpm_selftests'][0]['isr_instructions']=123
        self.assertEqual(hpm.check(data)[1], [])

    def test_duplicate_missing_corrupt_and_wrong_map(self):
        for key,mutate in [('hpm_work',lambda x:x.append(copy.deepcopy(x[0]))),
                           ('hpm_harts',lambda x:x.clear()),
                           ('hpm_preflights',lambda x:x[0].update(passed=False)),
                           ('hpm_work',lambda x:x[0]['counters'].__setitem__(0,101)),
                           ('hpm_work',lambda x:x[0]['counters'].__setitem__(0,-1)),
                           ('hpm_configs',lambda x:x[0].update(selectors=[0]*13)),
                           ('hpm_threads',lambda x:x.append(copy.deepcopy(x[0])))]:
            d=fixture();mutate(d[key])
            with self.subTest(key=key):self.assertTrue(hpm.check(d)[1])

    def test_prediction_inclusive_is_nonadditive(self):
        d=fixture();row=copy.deepcopy(d['hpm_work'][0]);row.update(plugin='pose_prediction',caller='render_loop')
        d['hpm_work'].append(row);d['hpm_harts'][0]['counters']=[v*2 for v in row['counters']]
        result,errors=hpm.check(d)
        self.assertEqual(errors,[])
        self.assertEqual(result['inclusive_callers'][0]['cycles'],100)
        self.assertFalse(result['inclusive_callers'][0]['additive'])

    def test_zero_denominators_are_unavailable(self):
        d=fixture();d['hpm_work'][0]['counters']=[0]*15;d['hpm_harts'][0]['counters']=[0]*15
        result,errors=hpm.check(d);self.assertEqual(errors,[])
        self.assertIsNone(result['plugins'][0]['ipc']);self.assertIsNone(result['plugins'][0]['cpi'])

    def test_end_to_end_export_parse_summary_and_incomplete(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);run=root/'case';run.mkdir();write_trace(run/'console.log',fixture())
            data=records(run/'console.log');self.assertEqual(hpm.check(data)[1],[])
            (run/'analysis.json').write_text(json.dumps({'passed':True,'complete':True,'summary':{'runtime_ns':1000000000,'vio_poses':4,'online_harts':1}}))
            (run/'run.json').write_text(json.dumps({'backend':'firesim-u250','host_elapsed_seconds':2}))
            report=summary.report([run],root/'report')
            self.assertEqual(report['runs'][0]['run']['status'],'pass')
            self.assertTrue((root/'report/plugins.csv').is_file())
            self.assertIn('**spike**: Functional evidence only',
                          (root/'report/summary.md').read_text())
            (run/'classification.json').write_text(json.dumps({'diagnostic':True,'reason':'IRQ sampling'}))
            report=summary.report([run],root/'report')
            self.assertTrue(report['runs'][0]['run']['diagnostic'])
            self.assertIn(str(run/'classification.json'),report['runs'][0]['source_hashes'])
            (run/'run.json').write_text(json.dumps({'interrupted':True}))
            report=summary.report([run],root/'report')
            self.assertEqual(report['runs'][0]['run']['status'],'incomplete')

    def test_overhead_pairs_require_same_hardware_data_backend_and_pass(self):
        row={'status':'pass','dataset_sha256':'data','hardware_config':'quad','harts':4,
             'hardware_bitstream_sha256':'bits','modeled_clock_scale':2,'ticks_per_sec':10000,
             'linalg_backend':'rvv','target_seconds':2,'name':'off','hpm_enabled':False}
        on=dict(row,name='on',hpm_enabled=True,target_seconds=3)
        self.assertEqual(summary.overhead_comparisons([{'run':row},{'run':on}])[0]['target_seconds_ratio'],1.5)
        for change in ({'status':'incomplete'},{'dataset_sha256':'other'},{'linalg_backend':'gemmini'},
                       {'hardware_config':'single'},{'hardware_bitstream_sha256':'different'},{'ticks_per_sec':1000},{'hpm_enabled':None},{'diagnostic':True}):
            self.assertEqual(summary.overhead_comparisons([{'run':row},{'run':dict(on,**change)}]),[])

    def test_compile_audit_rejects_application_only_definition(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'build.ninja'
            rows=['CMakeFiles/app.dir/src/hpm.cpp.obj','plugins/openvins/CMakeFiles/openvins.dir/plugin.cpp.obj']
            def text(on):return ''.join('build '+r+': CXX source.cpp\n  DEFINES = -DILLIXR_HPM_PROFILE='+str(on)+'\n' for r in rows)
            p.write_text(text(1));self.assertTrue(hpm_build_audit.audit(p,True)['passed'])
            p.write_text(text(0));self.assertTrue(hpm_build_audit.audit(p,False)['passed'])
            for bad in (text(1).replace('-DILLIXR_HPM_PROFILE=1','',1),text(1).replace('=1','=0',1)):
                p.write_text(bad)
                with self.assertRaises(ValueError):hpm_build_audit.audit(p,True)

    def test_preflight_report_does_not_require_workload_totals(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);d=fixture();d={k:v for k,v in d.items() if k in ('hpm_preflights','hpm_selftests')}
            write_trace(root/'console.log',d)
            (root/'analysis.json').write_text(json.dumps({'passed':True,'complete':True}))
            (root/'run.json').write_text(json.dumps({'platform_check':True,'harts':1}))
            (root/'firmware_build_manifest.json').write_text(json.dumps({'hpm':{'enabled':True}}))
            result=summary.run(root)
            self.assertEqual(result['run']['status'],'pass')
            self.assertEqual(result['run']['kind'],'preflight')
            self.assertFalse(result['hpm']['available'])
            d['hpm_selftests'][0]['passed']=False;write_trace(root/'console.log',d)
            self.assertEqual(summary.run(root)['run']['status'],'fail')

    def test_generated_split_counter_banks_and_legacy_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'target.fir'
            text='circuit Target :\n'
            for hart in range(2):
                suffix='_'+str(hart) if hart else ''
                text+=f'  module RocketTile{suffix} :\n    inst core of Rocket{suffix}\n'
                text+=f'  module Rocket{suffix} :\n    inst csr of CSRFile{suffix}\n'
                text+=f'  module CSRFile{suffix} :\n'
                text+='    output io : { counters : { eventSel : UInt<64>, flip inc : UInt<1>}[13]}\n'
                for i in range(13):
                    text+=f'    reg small_{i+2} : UInt<6>, clock\n    reg large_{i+2} : UInt<34>, clock\n'
                    text+=f'    node nextSmall_{i+2} = add(small_{i+2}, io.counters[{i}].inc)\n'
            path.write_text(text)
            self.assertEqual(len(hpm_hardware.inspect(path,2)['harts']),2)
            for corrupt in (text.replace('[13]}','[0]}'),text.replace('UInt<34>','UInt<33>',1),
                            text.replace('    reg large_2 : UInt<34>, clock','',1)):
                path.write_text(corrupt)
                with self.assertRaises(ValueError):hpm_hardware.inspect(path,2)

    def test_counter_width_wrap_and_execution_partitions(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'model'
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src'),
                            str(ROOT/'tests/native/hpm_model.cpp'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

if __name__=='__main__':unittest.main()
