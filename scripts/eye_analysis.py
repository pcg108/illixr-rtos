"""Validate asynchronous eye publications and legacy synchronous traces."""
import json
import math
from pathlib import Path

def check(data, summary, harts):
    if data.get("eye_configs") and data["eye_configs"][0].get("version")==2:
        return check_async(data, summary, harts)
    return check_legacy(data, summary, harts)

def check_legacy(data, summary, harts):
    errors=[]
    def require(ok,reason):
        if not ok: errors.append(reason)
    try:
        configs=data['eye_configs'];images=data['eye_images'];results=data['eye_results']
        require(len(configs)==1,'Expected one eye configuration')
        c=configs[0]
        require(c['version']==1 and c['enabled'] is True and c['synchronous'] is True,'Invalid eye service configuration')
        require((c['hz'],c['width'],c['height'],c['opcode'],c['accelerator_hart'],c['precision'])==(120,240,160,2,0,'int8'),'Eye hardware or sample configuration changed')
        require(c['publications']==len(images)>0 and c['requests']==len(results)>0,'Incomplete eye trace')
        reference=json.loads((Path(__file__).resolve().parents[1]/'third_party/ritnet/reference/manifest.json').read_text())
        P=1000000000//120
        for i,p in enumerate(images):
            require(p['sequence']==i+1 and 0<=p['scheduled_ns']<=p['published_ns']<=summary['runtime_ns'],'Invalid eye publication identity/time')
            require(p['scheduled_ns']%P==0 and 0<=p['hart']<harts,'Invalid eye publication phase/hart')
            if i:require(p['scheduled_ns']>images[i-1]['published_ns'],'Eye publication catch-up burst')
        warps=[w for w in data['gpu_events'] if w['stage']=='timewarp']
        require(len(warps)==sum(not r['expired'] for r in results),'Eye request/timewarp count mismatch')
        warp_map={w['warp_id']:w for w in warps}
        by_id={p['sequence']:p for p in images}
        for i,r in enumerate(results):
            w=warp_map.get(r['warp_id'])
            require(r['inference_id']==i+1,'Duplicate or missing eye request identity')
            require(type(r['expired']) is bool and type(r['final']) is bool,'Invalid eye outcome flags')
            begin,end=r['snapshot_begin_ns'],r['request_ns']
            require(begin<=end<=r['start_ns']<=r['completion_ns']<=r['decision_ns']<=summary['runtime_ns'],'Invalid synchronous eye timing')
            if r['expired']:
                require(r['warp_id']==0 and r['decision_ns']>=r['display_slot']*P,'Unjustified expired eye request')
                require(any(o['outcome']=='missed' and o['first_slot']==r['display_slot'] and o['observed_ns']==r['decision_ns'] and o['final']==r['final'] for o in data['warp_slots']),'Expired inference missing missed-opportunity record')
            else:
                require(w is not None,'Eye request has no completed warp')
                if w is not None:
                    require(r['display_slot']==w['display_slot'] and w['actual_wake_ns']<=begin and r['completion_ns']<=w['submit_ns'],'Eye request correlation mismatch')
            if 'gpu_results' in data:
                renders=[f for f in data['gpu_events'] if f['stage']=='render']
                selected=next(f for f in renders if f['frame_id']==r['frame_id'])
                require(selected['publication_ns']<=r['frame_selection_ns']<=begin,'Eye request frame selection invalid')
                require(not any(f['frame_id']>selected['frame_id'] and f['publication_ns']<r['frame_selection_ns'] for f in renders),'Old render selected for eye request')
                if r['final']:require(r['frame_id']==renders[-1]['frame_id'],'Final eye request did not select last frame')
            require(0<=r['caller_hart']<harts,'Invalid eye caller hart')
            if not r['image_sequence']:
                require(not r['valid'] and not any(p['published_ns']<begin for p in images),'Eye service missed an available publication')
                continue
            selected=by_id[r['image_sequence']]
            require(selected['published_ns']==r['image_ns']<=end,'Future or inconsistent image selection')
            require(not any(p['sequence']>selected['sequence'] and p['published_ns']<begin for p in images),'Eye service selected an old image')
            require(r['accelerator_hart']==0 and r['cycles']>0,'Missing or wrong-hart INT8 execution')
            require(r['valid'] and r['output_hash']==reference['output_hash'],'RITNet output differs from reference')
            require(all(math.isfinite(r[k]) and abs(r[k]-reference[k])<1e-9 for k in ['x','y']),'Eye coordinates differ from reference')
        require(any(r['valid'] for r in results),'No valid eye inference')
        if 'gpu_results' in data:
            require(results[-1]['final'] and sum(r['final'] for r in results)==1,'Final eye request missing or duplicated')
        return {'publications':len(images),'requests':len(results),'valid_results':sum(r['valid'] for r in results),'expired_requests':sum(r['expired'] for r in results),'max_inference_ns':max(r['completion_ns']-r['start_ns'] for r in results),'max_queue_ns':max(r['start_ns']-r['request_ns'] for r in results),'reference':reference,'passed':not errors},errors
    except (KeyError,IndexError,ValueError,TypeError,StopIteration) as exc:
        return {},errors+['Malformed eye trace: '+str(exc)]


def check_async(data, summary, harts):
    errors=[]
    def require(ok, reason):
        if not ok: errors.append(reason)
    try:
        configs,images,results,reads=(data[k] for k in ('eye_configs','eye_images','eye_results','eye_reads'))
        require(len(configs)==1,'Expected one eye configuration')
        c=configs[0]
        require(c['version']==2 and c['enabled'] is True and c['synchronous'] is False,'Invalid asynchronous eye configuration')
        require((c['hz'],c['width'],c['height'],c['opcode'],c['accelerator_hart'],c['precision'])==(120,240,160,2,0,'int8'),'Eye hardware or sample configuration changed')
        require(c['publications']==len(images)>0 and c['requests']==len(results)>0 and c['reads']==len(reads),'Incomplete eye trace')
        reference=json.loads((Path(__file__).resolve().parents[1]/'third_party/ritnet/reference/manifest.json').read_text())
        period=1000000000//120
        for i,p in enumerate(images):
            require(p['sequence']==i+1 and 0<=p['scheduled_ns']<=p['published_ns']<=summary['runtime_ns'],'Invalid eye publication identity/time')
            require(p['scheduled_ns']%period==0 and 0<=p['hart']<harts,'Invalid eye publication phase/hart')
            if i: require(p['scheduled_ns']>images[i-1]['published_ns'],'Eye publication catch-up burst')
        by_image={p['sequence']:p for p in images}
        for i,r in enumerate(results):
            require(r['inference_id']==i+1,'Duplicate or missing eye inference identity')
            begin,end=r['snapshot_begin_ns'],r['request_ns']
            require(0<=begin<=end<=r['start_ns']<=r['completion_ns']<=r['publication_ns']<=summary['runtime_ns'],'Invalid asynchronous eye timing')
            if i:
                require(r['image_sequence']>results[i-1]['image_sequence'],'Repeated or reordered image inference')
                require(begin>=results[i-1]['publication_ns'],'Overlapping eye inferences')
            selected=by_image[r['image_sequence']]
            require(selected['published_ns']==r['image_ns']<=end,'Future or inconsistent image selection')
            require(not any(p['sequence']>selected['sequence'] and p['published_ns']<begin for p in images),'Eye worker selected an old image')
            require(r['accelerator_hart']==r['publication_hart']==0 and r['cycles']>0,'Missing or wrong-hart INT8 execution/publication')
            require(r['valid'] is True and r['output_hash']==reference['output_hash'],'RITNet output differs from reference')
            require(all(math.isfinite(r[k]) and abs(r[k]-reference[k])<1e-9 for k in ('x','y')),'Eye coordinates differ from reference')
        warps=[w for w in data['gpu_events'] if w['stage']=='timewarp']
        require(len(reads)==len(warps),'Eye read/timewarp count mismatch')
        by_result={r['inference_id']:r for r in results}
        previous=0
        for i,read in enumerate(reads):
            begin,end=read['begin_ns'],read['end_ns']; identity=read['inference_id']
            require(read['warp_id']==i+1,'Duplicate or missing eye read warp identity')
            require(0<=begin<=end<=summary['runtime_ns'] and 0<=read['hart']<harts,'Invalid eye read timing/hart')
            require(type(read['reused']) is bool and read['reused']==bool(identity and identity==previous),'Incorrect eye reuse accounting')
            require(identity>=previous,'Eye prediction regressed')
            if i<len(warps):
                w=warps[i]
                require(read['warp_id']==w['warp_id'] and read['display_slot']==w['display_slot'] and w['actual_wake_ns']<=begin<=end<=w['submit_ns'],'Eye read/warp correlation mismatch')
            if identity:
                selected=by_result[identity]
                require(selected['publication_ns']<=end,'Future eye prediction selected')
                require(read['age_ns']==end-selected['publication_ns']>=0,'Incorrect eye prediction age')
            else:
                require(read['age_ns']==0,'Unavailable eye prediction has an age')
            require(not any(r['inference_id']>identity and r['publication_ns']<begin for r in results),'Eye read missed the latest prediction')
            previous=identity
        require(any(r['inference_id'] for r in reads),'No published eye prediction consumed')
        return {'version':2,'publications':len(images),'requests':len(results),'valid_results':sum(r['valid'] for r in results),
                'reads':len(reads),'repeated_reads':sum(r['reused'] for r in reads),'unavailable_reads':sum(not r['inference_id'] for r in reads),
                'max_prediction_age_ns':max((r['age_ns'] for r in reads),default=0),
                'max_inference_ns':max((r['completion_ns']-r['start_ns'] for r in results),default=0),
                'max_read_ns':max((r['end_ns']-r['begin_ns'] for r in reads),default=0),
                'reference':reference,'passed':not errors},errors
    except (KeyError,IndexError,ValueError,TypeError) as exc:
        return {},errors+['Malformed asynchronous eye trace: '+str(exc)]
