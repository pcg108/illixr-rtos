#!/usr/bin/env python3
"""Summarize preserved XRSight results without launching hardware or changing evidence."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import sys
import tempfile

from analyze_spike import records, analyze
from hpm_analysis import check as check_hpm, EVENTS
from trace_batches import decode_console


def load(path):
    return json.loads(path.read_text()) if path.is_file() else {}


def stat(values):
    values=sorted(v for v in values if isinstance(v,(int,float)))
    return {'count':len(values),'mean':statistics.mean(values) if values else None,
            'min':min(values) if values else None,'max':max(values) if values else None,
            'p95':values[min(len(values)-1,int(.95*(len(values)-1)))] if values else None}


def discover(paths):
    candidates=set()
    for supplied in paths:
        root=Path(supplied).resolve()
        if not root.exists(): raise ValueError(f'Input does not exist: {root}')
        if root.is_file():candidates.add(root.parent);continue
        for marker in ('analysis.json','run.json','console.log','console.batched.log'):
            if (root/marker).is_file():candidates.add(root)
            candidates.update(p.parent for p in root.rglob(marker))
        for uart in root.rglob('uartlog'):
            if uart.parent.name=='sim_slot_0' and uart.parent.parent.name=='runfarm':
                candidates.add(uart.parent.parent.parent)
            else:candidates.add(uart.parent)
    # Artifacts containing only provenance are not runtime records.
    return sorted(p for p in candidates if any((p/n).is_file() for n in
                  ('analysis.json','console.log','console.batched.log','uartlog','runfarm/sim_slot_0/uartlog')))


def run(directory):
    errors=[];sources={};analysis=load(directory/'analysis.json');meta=load(directory/'run.json')
    if not meta:meta=load(directory/'execution.json')
    case=load(directory/'case.json');classification=load(directory/'classification.json')
    diagnostic=any(item.get('diagnostic') is True for item in (meta,case,classification))
    firmware=load(directory/'firmware_build_manifest.json') or load(directory/'build_manifest.json')
    hardware=load(directory/'hardware_manifest.json')
    console=next((directory/p for p in ('console.log','console.batched.log','runfarm/sim_slot_0/uartlog','uartlog')
                  if (directory/p).is_file()),None)
    data={};transport={}
    if console:
        sources[str(console)]=hashlib.sha256(console.read_bytes()).hexdigest()
        try:
            text,transport=decode_console(console.read_text(errors='replace'))
            with tempfile.TemporaryDirectory(prefix='xrsight-summary-') as tmp:
                decoded=Path(tmp)/'console.log';decoded.write_text(text);data=records(decoded)
                if not analysis and len(data.get('summaries',[]))==1:
                    analysis=analyze(decoded, dataset_manifest=directory/'dataset_manifest.json' if (directory/'dataset_manifest.json').is_file() else None)
                    analysis['derived_from_console']=True
        except (ValueError,OSError) as e:errors.append(str(e))
    preflight=bool(meta.get('platform_check',firmware.get('target',{}).get('platform_check_only',False)))
    if preflight:
        hpm={'available':False,'scope':'platform_preflight','preflights':data.get('hpm_preflights',[]),
             'selftests':data.get('hpm_selftests',[])}
        hpm_errors=[]
        if firmware.get('hpm',{}).get('enabled',False):
            expected=meta.get('harts',firmware.get('target',{}).get('harts'))
            tests=hpm['selftests'];pf=hpm['preflights']
            if not expected or len(tests)!=1 or tests[0].get('passed') is not True or tests[0].get('hart_mask')!=(1<<expected)-1:
                hpm_errors.append('Missing or failed HPM attribution self-test')
            if not expected or len(pf)!=expected or {r.get('hart') for r in pf}!=set(range(expected)) or not all(r.get('passed') is True for r in pf):
                hpm_errors.append('Missing or failed HPM hart preflight')
    else:
        hpm,hpm_errors=check_hpm(data,required=firmware.get('hpm',{}).get('enabled',False))
    errors.extend(hpm_errors)
    summary=analysis.get('summary') or (data.get('summaries') or [{}])[-1]
    name=meta.get('name') or directory.name
    platform=meta.get('backend',meta.get('platform','unknown'))
    interrupted=any(meta.get(k) for k in ('timed_out','interrupted','cycle_limit_reached','fatal'))
    complete=analysis.get('complete') is True and not interrupted
    status=('pass' if analysis.get('passed') is True else 'fail') if complete else 'incomplete'
    if complete and errors:status='fail'
    runtime=summary.get('runtime_ns');seconds=runtime/1e9 if isinstance(runtime,int) and runtime>0 else None
    harts=summary.get('online_harts',meta.get('harts'))
    native=analysis.get('native',analysis.get('comparison',{}))
    # Preserve exact validation structures; do not invent native acceptance.
    numerical={k:v for k,v in analysis.items() if any(t in k for t in ('native','comparison','trajectory','prediction'))}
    config=analysis.get('clock_model',{})
    if platform=='unknown' and ('spike' in name.lower() or meta.get('isa')):platform='spike'
    if platform=='unknown' and analysis.get('firesim_target_cycles') is not None:platform='firesim-u250'
    row={'kind':'preflight' if preflight else 'workload','name':name,'directory':str(directory),'status':status,'complete':complete,
         'diagnostic':diagnostic,'diagnostic_reason':classification.get('reason'),
         'platform':platform,'harts':harts,'hardware_config':analysis.get('hardware_config',meta.get('hardware_config')),
         'linalg_backend':meta.get('linalg_backend',firmware.get('linalg',{}).get('backend')),
         'target_seconds':seconds,'host_seconds':meta.get('host_elapsed_seconds',analysis.get('host_elapsed_seconds')),
         'simulator_target_cycles':analysis.get('firesim_target_cycles'),
         'trace_export_seconds':summary['trace_export_ns']/1e9 if 'trace_export_ns' in summary else None,
         'vio_poses':summary.get('vio_poses'),'camera_processed':summary.get('cam_processed'),
         'camera_per_target_second':summary['cam_processed']/seconds if seconds and isinstance(summary.get('cam_processed'),int) else None,
         'camera_skipped':summary.get('cam_skipped'),'camera_dropped':summary.get('cam_dropped'),
         'imu_published':summary.get('imu_published'),'imu_vio':summary.get('imu_processed'),
         'imu_integrator':summary.get('imu_integrator_processed'),'probe_missed_deadlines':summary.get('probe_missed_deadlines'),
         'queue_imu_vio_high_water':summary.get('imu_vio_highwater'),
         'queue_imu_integrator_high_water':summary.get('imu_integrator_highwater'),
         'queue_camera_high_water':summary.get('cam_highwater'),
         'hardware_bitstream_sha256':hardware.get('bitstream_sha256'),
         'modeled_clock_scale':firmware.get('target',{}).get('modeled_clock_scale'),
         'ticks_per_sec':firmware.get('target',{}).get('ticks_per_sec'),
         'hpm_enabled':firmware.get('hpm',{}).get('enabled',True if hpm['available'] else None),
         'dataset_sha256':hashlib.sha256((directory/'dataset_manifest.json').read_bytes()).hexdigest() if (directory/'dataset_manifest.json').is_file() else None,
         'hpm_available':hpm['available'],'hpm_valid':hpm.get('passed') if hpm['available'] else None}
    eye=data.get('eye_results',[]);eye_reads=data.get('eye_reads',[])
    eye_metrics={'inferences':len(eye) if console else None,
        'inference_latency_ns':stat(r.get('completion_ns',0)-r.get('start_ns',0) for r in eye),
        'read_age_ns':stat(r.get('age_ns') for r in eye_reads),
        'reused_reads':sum(r.get('reused') is True for r in eye_reads),
        'reference_validation':analysis.get('eye_tracking')}
    for p in ('analysis.json','run.json','execution.json','case.json','classification.json','firmware_build_manifest.json','hardware_manifest.json'):
        if (directory/p).is_file():sources[str(directory/p)]=hashlib.sha256((directory/p).read_bytes()).hexdigest()
    return {'run':row,'hpm':hpm,'summary':summary,'clocks':config or data.get('clocks'),
            'validation':{'recorded_passed':analysis.get('passed'),'recorded_errors':analysis.get('errors',[]),
                          'numerical':numerical,'report_errors':errors},
            'consumer':analysis.get('consumer'),'delays':analysis.get('delays'),
            'propagation':analysis.get('propagation'),'dataset_accounting':analysis.get('dataset_accounting'),
            'trajectory_sanity':analysis.get('trajectory_sanity'),
            'placement':analysis.get('placement',data.get('placements',[])),
            'blas':analysis.get('blas'),'blas_work':data.get('blas_work',[]),
            'gemmini':data.get('gemmini',[]),'packing':data.get('gemmini_packing',[]),
            'eye_tracking':eye_metrics,'gpu_pipeline':analysis.get('gpu_pipeline'),
            'gpu_results':data.get('gpu_results',[]),'memory_stats':analysis.get('memory_stats'),
            'source_hashes':sources,'trace_transport':transport}


def csv_file(path,rows):
    if not rows:path.write_text('');return
    fields=list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
        for row in rows:writer.writerow({k:json.dumps(v,sort_keys=True) if isinstance(v,(dict,list)) else v for k,v in row.items()})


def table(rows,columns):
    def cell(v):
        if v is None:return 'unavailable'
        if isinstance(v,float):return f'{v:.6g}'
        return str(v).replace('|','\\|').replace('\n',' ')
    return ('| '+' | '.join(columns)+' |\n| '+' | '.join('---' for _ in columns)+' |\n'+
            ''.join('| '+' | '.join(cell(r.get(k)) for k in columns)+' |\n' for r in rows)+'\n')


def overhead_comparisons(results):
    groups={}
    for result in results:
        row=result['run']
        if row.get('diagnostic') or row.get('kind')=='preflight' or row.get('status')!='pass' or not row.get('dataset_sha256') or not row.get('hardware_bitstream_sha256') or not row.get('ticks_per_sec') or not row.get('modeled_clock_scale'):
            continue
        key=tuple(row.get(k) for k in ('hardware_config','hardware_bitstream_sha256','harts','linalg_backend','dataset_sha256','modeled_clock_scale','ticks_per_sec'))
        groups.setdefault(key,[]).append(row)
    comparisons=[]
    for rows in groups.values():
        for baseline in (r for r in rows if r.get('hpm_enabled') is False):
            for measured in (r for r in rows if r.get('hpm_enabled') is True):
                item={'baseline':baseline['name'],'profiled':measured['name'],
                      'interpretation':'Whole-workload change includes scheduling/delivered-camera differences; not isolated CSR cost.'}
                for metric in ('target_seconds','host_seconds','simulator_target_cycles','trace_export_seconds',
                               'vio_poses','camera_processed','camera_dropped','probe_missed_deadlines'):
                    a,b=baseline.get(metric),measured.get(metric)
                    item[metric+'_delta']=b-a if isinstance(a,(int,float)) and isinstance(b,(int,float)) else None
                    item[metric+'_ratio']=b/a if isinstance(a,(int,float)) and isinstance(b,(int,float)) and a else None
                comparisons.append(item)
    return comparisons


def report(paths,output):
    directories=discover(paths)
    if not directories:raise ValueError('No result directories found')
    output=Path(output).resolve()
    if any(output==d for d in directories):raise ValueError('Output must differ from input run directories')
    output.mkdir(parents=True,exist_ok=True)
    results=[]
    for directory in directories:
        try:results.append(run(directory))
        except (ValueError,OSError,TypeError,KeyError) as e:
            results.append({'run':{'name':directory.name,'directory':str(directory),'status':'incomplete','complete':False},
                            'validation':{'report_errors':[str(e)]}})
    result={'schema_version':1,'runs':results,'profiling_comparisons':overhead_comparisons(results),'definitions':{
        'cpu_counters':'Exclusive scheduled-context totals; core sums are not elapsed cycles.',
        'interlocks':'Overlapping event-active cycles; fractions are not additive.',
        'cache':'Rocket L1 acquire events, not accelerator or L2 misses.',
        'spike':'Functional evidence only; simulator counters are not hardware performance.',
        'legacy':'Unavailable measurements are null; missing comparisons are unvalidated.',
        'gpu':'Asynchronous latency model, not measured GPU hardware.'}}
    (output/'summary.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    rows=[r['run'] for r in results];csv_file(output/'runs.csv',rows)
    csv_file(output/'profiling_overhead.csv',result['profiling_comparisons'])
    plugins=[];hart_rows=[];accelerators=[];placements=[];displays=[]
    for r in results:
        base={'run':r['run']['name'],'directory':r['run']['directory'],'run_status':r['run']['status']}
        plugins.extend(base|p for p in r.get('hpm',{}).get('plugins',[]))
        hart_rows.extend(base|p for p in r.get('hpm',{}).get('rows',[]))
        for key in ('blas_work','gemmini','packing'):
            accelerators.extend(base|{'record':key}|p for p in r.get(key,[]))
        placement=r.get('placement',[])
        if isinstance(placement,dict):placement=placement.get('plugins',[])
        placements.extend(base|p for p in placement if isinstance(p,dict))
        displays.append(base|{'gpu':r.get('gpu_pipeline'),'eye':r.get('eye_tracking')})
    for name,values in [('plugins',plugins),('plugin_harts',hart_rows),('accelerators',accelerators),('placement',placements),('display_eye',displays)]:
        csv_file(output/(name+'.csv'),values)
    md='# XRSight-RTOS performance report\n\n'
    md+=table(rows,['name','status','diagnostic','harts','linalg_backend','target_seconds','host_seconds','vio_poses','camera_processed','hpm_available'])
    md+='## Profiling on/off workload comparisons\n\n'
    md+=table(result['profiling_comparisons'],['baseline','profiled','target_seconds_ratio','vio_poses_delta','camera_processed_delta'])
    md+='Whole-workload differences include changes in scheduling and delivered cameras. Diagnostic runs remain visible but are excluded from automatic profiling comparisons.\n\n'
    md+='## Per-plugin scheduled CPU execution\n\n'
    md+=table(plugins,['run','plugin','cycles','instructions','ipc','branch_direction_mispredict','control_target_mispredict','icache_acquire','dcache_acquire','pipeline_flush','pipeline_replay'])
    md+='## Accelerator and packing measurements\n\n'
    md+=table(accelerators,['run','record','phase','name','calls','packing_ns','unpacking_ns','queue_ns','execution_ns','pack_cycles','unpack_cycles'])
    md+='## Interlocks and blocked cycles (overlapping)\n\n'
    md+=table(plugins,['run','plugin','load_use_interlock','long_latency_interlock','csr_interlock','mul_div_interlock','fp_interlock','icache_blocked','dcache_blocked'])
    md+='## Interpretation\n\n'+''.join('- **'+k.replace('_',' ')+'**: '+v+'\n' for k,v in result['definitions'].items())
    for r in results:
        md+='\n## '+r['run']['name']+'\n\n'
        for key in ('summary','clocks','consumer','delays','propagation','dataset_accounting','eye_tracking','gpu_pipeline','memory_stats','trajectory_sanity','validation'):
            if r.get(key) is not None:md+='### '+key.replace('_',' ')+'\n\n```json\n'+json.dumps(r[key],indent=2)+'\n```\n\n'
    (output/'summary.md').write_text(md)
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',nargs='+',type=Path)
    p.add_argument('--output',required=True,type=Path);args=p.parse_args()
    try:r=report(args.results,args.output)
    except (ValueError,OSError) as e:p.exit(2,str(e)+'\n')
    print(f"Summarized {len(r['runs'])} runs in {args.output}")
    return 0

if __name__=='__main__':sys.exit(main())
