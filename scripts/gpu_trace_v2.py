"""Validate independently scheduled workers and timestamped modeled presentation.

Version 1 remains in analyze_spike. All IDs and counters here refer to v2;
frame IDs may repeat across warps but warp IDs and display slots cannot.
"""
import bisect
from collections import Counter
import math


def check(data, summary, harts, prediction_checks):
    gpu = data['gpu_results'][0]
    errors = []
    def require(condition, message):
        if not condition:
            errors.append(message)
    counts = ('render_submitted', 'render_completed', 'render_skipped_slots', 'timewarp_submitted',
              'timewarp_completed', 'distinct_selected', 'never_selected', 'repeated_uses',
              'empty_opportunities', 'missed_opportunities', 'render_deadlines_missed',
              'timewarp_deadlines_missed', 'fresh_warp_completed', 'new_outputs', 'repeated_outputs',
              'no_outputs', 'fresh_on_time_presentations', 'display_slots', 'trace_overflow',
              'render_closed_ns', 'timewarp_done_ns')
    for key in counts:
        require(type(gpu[key]) is int and gpu[key] >= 0, 'Invalid GPU counter: ' + key)
    P, offset = gpu['period_ns'], gpu['render_offset_ns']
    lead = gpu['timewarp_delay_ns'] + gpu['timewarp_margin_ns']
    require(P > 0 and 0 <= offset < P and 0 < lead < P and gpu['timewarp_margin_ns'] >= 0,
            'Invalid display phase or timewarp lead')
    require(gpu['trace_overflow'] == 0, 'GPU trace overflowed')
    require(gpu['render_closed'] is True and gpu['timewarp_done'] is True, 'GPU workers did not shut down')
    origin, runtime = summary['origin_ns'], summary['runtime_ns']
    events = {stage: [e for e in data['gpu_events'] if e.get('stage') == stage] for stage in ('render', 'timewarp')}
    require(sum(map(len, events.values())) == len(data['gpu_events']), 'Unknown GPU event stage')
    result = {'summary': gpu, 'stages': {}, 'model': 'absolute display schedule; timestamped modeled presentation; independent fixed GPU delays'}
    for stage, items in events.items():
        require(len(items) == gpu[stage+'_submitted'] == gpu[stage+'_completed'], stage+': incomplete work trace')
        delay = gpu[stage+'_delay_ns']
        require(delay > 0, stage+': invalid GPU latency')
        id_key = 'frame_id' if stage == 'render' else 'warp_id'
        require([e[id_key] for e in items] == list(range(1, len(items)+1)), stage+': duplicate or missing '+id_key)
        slots = [e['slot' if stage == 'render' else 'display_slot'] for e in items]
        require(all(b > a for a, b in zip(slots, slots[1:])), stage+': reordered or duplicate scheduled slots')
        source_sequences = [e['source_sequence'] for e in items]
        require(all(b >= a for a,b in zip(source_sequences,source_sequences[1:])), stage+': prediction source regressed')
        deadline_lateness, gpu_lateness, wake_lateness, horizons, source_ages, durations, wake_delays = [], [], [], [], [], [], []
        observed_harts = set()
        for i, e in enumerate(items):
            require(e['version'] == 2, 'Mixed GPU trace versions')
            slot = slots[i]
            deadline = (slot+1)*P if stage == 'render' else slot*P
            scheduled = slot*P+offset if stage == 'render' else slot*P-lead
            require(e['scheduled_wake_ns'] == scheduled, stage+': shared display phase changed')
            require(scheduled <= e['actual_wake_ns'] < deadline, stage+': expired opportunity submitted')
            require(e['actual_wake_ns'] <= e['submit_ns'] <= e['scheduled_complete_ns'] <=
                    e['observed_complete_ns'] <= e['publication_ns'] <= runtime, stage+': invalid completion/publication ordering')
            require(e['scheduled_complete_ns']-e['submit_ns'] == delay, stage+': wrong asynchronous delay')
            require(e['target_ns'] == origin+deadline, stage+': changed original prediction deadline')
            if stage == 'render':
                require(e['presentation_ns'] == deadline, 'Render saved presentation target changed')
            if i:
                require(scheduled > items[i-1]['publication_ns'], stage+': catch-up burst or overlapping same worker')
            for key in ('processing_hart','publication_hart'):
                require(type(e[key]) is int and 0 <= e[key] < harts, stage+': invalid '+key)
                observed_harts.add(e[key])
            require(e['hart'] == e['publication_hart'], stage+': inconsistent publication hart')
            status=e['prediction_status']
            require(status in ('valid','fallback','stale'), stage+': invalid prediction status')
            pose=e['position']+e['orientation']
            require(len(pose)==7 and all(isinstance(x,(int,float)) and math.isfinite(x) for x in pose), stage+': nonfinite pose')
            require(len(e['orientation'])==4 and abs(sum(x*x for x in e['orientation'])-1) <= 2e-5, stage+': unnormalized quaternion')
            if status in ('valid','stale'):
                require(e['prediction_horizon_ns']==e['target_ns']-e['source_ns'], stage+': incorrect prediction horizon')
            if status=='valid':
                require(e['source_sequence']>0 and 0 <= e['prediction_horizon_ns'] <= 50_000_000, stage+': falsely valid prediction')
            deadline_lateness.append(max(0,e['publication_ns']-deadline))
            gpu_lateness.append(max(0,e['observed_complete_ns']-deadline))
            wake_lateness.append(e['actual_wake_ns']-scheduled)
            horizons.append(e['prediction_horizon_ns'])
            if e['source_sequence']: source_ages.append(origin+e['submit_ns']-e['source_ns'])
            durations.append(e['observed_complete_ns']-e['submit_ns'])
            wake_delays.append(e['observed_complete_ns']-e['scheduled_complete_ns'])
        misses=sum(x>0 for x in deadline_lateness)
        require(misses==gpu[stage+'_deadlines_missed'], stage+': hidden deadline misses or incorrect deadline accounting')
        result['stages'][stage]={'completed':len(items),'observed_harts':sorted(observed_harts),
            'prediction_status_counts':dict(Counter(e['prediction_status'] for e in items)),
            'maximum_horizon_ns':max(horizons,default=0),'maximum_source_age_ns':max(source_ages,default=0),
            'maximum_deadline_lateness_ns':max(deadline_lateness,default=0),
            'original_presentation_deadlines_missed':misses,'selected_target_deadlines_missed':misses,
            'maximum_original_presentation_lateness_ns':max(deadline_lateness,default=0),
            'gpu_completion_deadlines_missed':sum(x>0 for x in gpu_lateness),
            'maximum_scheduled_wake_lateness_ns':max(wake_lateness,default=0),
            'minimum_observed_delay_ns':min(durations,default=0),'maximum_observed_delay_ns':max(durations,default=0),
            'maximum_wakeup_delay_ns':max(wake_delays,default=0),'retargeted_frames':0,'retargeted_target_deadlines_missed':0,
            'completed_frames_per_target_second':len(items)*1e9/runtime if runtime else 0}
    renders,warps=events['render'],events['timewarp']
    by_frame={e['frame_id']:e for e in renders}
    render_publications=[e['publication_ns'] for e in renders]
    selected=set(); repeats=fresh=fresh_reuse_updated=0
    previous=None
    for e in warps:
        frame=by_frame[e['frame_id']]
        eligible=bisect.bisect_right(render_publications,e['selection_ns'])
        strictly_before=bisect.bisect_left(render_publications,e['selection_ns'])
        # CLINT timestamps are quantized. Publications at the exact same timer
        # tick as selection may linearize on either side of the snapshot lock.
        possible=renders[max(0,strictly_before-1):eligible]
        require(any(f['frame_id']==e['frame_id'] for f in possible), 'Future frame or non-latest completed frame selected')
        require(e['actual_wake_ns'] <= e['selection_ns'] <= e['submit_ns'], 'Invalid frame selection time')
        require(e['frame_publication_ns']==frame['publication_ns'] <= e['selection_ns'], 'Future frame selected before publication')
        require(e['frame_age_ns']==e['selection_ns']-frame['publication_ns'], 'Incorrect frame age')
        require(e['render_source_sequence']==frame['source_sequence'] and e['render_prediction_status']==frame['prediction_status'] and
                e['render_orientation']==frame['orientation'] and e['presentation_ns']==frame['presentation_ns'] and e['slot']==frame['slot'],
                'Immutable saved render pose or target changed')
        repeated=e['frame_id'] in selected
        require(type(e['reused']) is bool and e['reused']==repeated, 'Incorrect repeated-frame use flag')
        require(type(e['final']) is bool and (e['selection_ns']>=gpu['render_closed_ns'] if e['final'] else e['selection_ns']<=gpu['render_closed_ns']), 'Incorrect final closed-frame selection')
        repeats+=repeated; selected.add(e['frame_id'])
        is_fresh=e['prediction_status']=='valid' and frame['prediction_status']=='valid'
        fresh+=is_fresh
        fresh_reuse_updated += bool(repeated and is_fresh and previous and e['source_sequence']>previous['source_sequence'])
        matrix=e['transform']
        require(len(matrix)==16 and all(isinstance(x,(float,int)) and math.isfinite(x) for x in matrix), 'Invalid rotational transform')
        previous=e
    require(gpu['distinct_selected']==len(selected) and gpu['never_selected']==len(renders)-len(selected), 'Completed render selection accounting mismatch')
    require(gpu['repeated_uses']==repeats and len(warps)==len(selected)+repeats, 'Incorrect reuse accounting')
    require(fresh==gpu['fresh_warp_completed'] and fresh>0, 'No matching fresh render/timewarp completion')
    if warps:
        require(warps[-1]['final'] and sum(e['final'] for e in warps)==1 and warps[-1]['frame_id']==renders[-1]['frame_id'], 'Final-frame shutdown missing or not unique')
    require(not renders or renders[-1]['slot']+1 <= len(renders)+gpu['render_skipped_slots'], 'Unaccounted render opportunities')
    if 'render_next_slot' in gpu:
        require(gpu['render_next_slot']==len(renders)+gpu['render_skipped_slots'], 'Render opportunity accounting mismatch')
    if 'trace_export_ns' in summary:
        require(runtime <= summary['trace_export_start_ns'] <= summary['trace_export_end_ns'] and
                summary['trace_export_ns']==summary['trace_export_end_ns']-summary['trace_export_start_ns'],
                'Invalid trace export timing')
    # Every scheduled opportunity is covered exactly once, with compact ranges
    # for missed wakes. A miss advances to a future wake, never relabels work.
    next_slot=1; slot_counts=Counter(); submitted=[]
    for o in data['warp_slots']:
        first,last=o['first_slot'],o['last_slot']; outcome=o['outcome']
        require(o['version']==2 and first==next_slot and last>=first, 'Missing/duplicate timewarp opportunity')
        require(o['scheduled_wake_ns']==first*P-lead and o['observed_ns']>=first*P-lead, 'Invalid opportunity wake')
        require(outcome in ('submitted','empty','missed'), 'Unknown timewarp opportunity outcome')
        if outcome=='missed':
            require(last*P-lead <= o['observed_ns'] < (last+1)*P-lead, 'Missed opportunity did not advance to next future wake')
        else:
            require(first==last and o['observed_ns']<first*P, 'Expired or ranged non-missed opportunity')
        if outcome=='submitted': submitted.append(first)
        slot_counts[outcome]+=last-first+1
        next_slot=last+1
    require(submitted==[e['display_slot'] for e in warps], 'Warp opportunity/submission mismatch')
    for key,outcome in (('timewarp_completed','submitted'),('empty_opportunities','empty'),('missed_opportunities','missed')):
        require(gpu[key]==slot_counts[outcome], 'Incorrect '+outcome+' opportunity accounting')
    for o,e in zip((o for o in data['warp_slots'] if o['outcome']=='submitted'),warps):
        require(o['observed_ns']==e['actual_wake_ns'] and o['final']==e['final'], 'Opportunity differs from its warp')
    require(bool(data['warp_slots']) and data['warp_slots'][-1]['final'] is True, 'Missing final scheduled opportunity')
    # Reconstruct the presentation oracle solely from immutable publications.
    publications=[e['publication_ns'] for e in warps]
    display_counts=Counter(); previous_id=0; fresh_presented=0; observer_lateness=[]
    for index,d in enumerate(data['displays'],1):
        require(d['version']==2 and d['display_slot']==index and d['boundary_ns']==index*P, 'Invalid display slot identity or phase')
        require(d['observed_ns']>=index*P and d['observer_lateness_ns']==d['observed_ns']-index*P, 'Hidden presentation observer lateness')
        require(0 <= d['hart'] < harts, 'Invalid display observer hart')
        eligible=bisect.bisect_right(publications,index*P)
        e=warps[eligible-1] if eligible else None
        identifier=e['warp_id'] if e else 0
        outcome='none' if not e else 'repeated' if identifier==previous_id else 'new'
        fresh_on_time=bool(e and outcome=='new' and e['display_slot']==index and e['prediction_status']=='valid' and e['render_prediction_status']=='valid')
        require(d['warp_id']==identifier and d['frame_id']==(e['frame_id'] if e else 0) and
                d['publication_ns']==(e['publication_ns'] if e else 0), 'Presentation selected output unavailable at boundary')
        require(d['outcome']==outcome and d['fresh_on_time']==fresh_on_time, 'Incorrect presentation outcome or freshness')
        display_counts[outcome]+=1; fresh_presented+=fresh_on_time; previous_id=identifier
        observer_lateness.append(d['observer_lateness_ns'])
    end=publications[-1] if publications else gpu['timewarp_done_ns']
    require(len(data['displays'])==gpu['display_slots']==(end+P-1)//P, 'Incomplete or excessive final presentation history')
    for key,outcome in (('new_outputs','new'),('repeated_outputs','repeated'),('no_outputs','none')):
        require(gpu[key]==display_counts[outcome], 'Display accounting mismatch: '+outcome)
    require(fresh_presented==gpu['fresh_on_time_presentations'] and fresh_presented>0, 'No fresh on-time modeled presentation')
    require(gpu['render_closed_ns'] <= gpu['timewarp_done_ns'] <= runtime, 'Invalid EOS chronology')
    result.update(prediction_checks(data,gpu,events,harts,errors))
    # Per-call correlation also prevents reusing an old prediction for a reused
    # image. The service must be invoked after this warp's new selection.
    for caller,stage in enumerate(('render','timewarp')):
        for call,e in zip((p for p in data['predictions'] if p['caller']==caller),events[stage]):
            start=e['actual_wake_ns'] if caller==0 else e['selection_ns']
            require(start <= call['computed_ns'] <= e['submit_ns'], stage+': prediction was not requested for this opportunity')
            for field in ('position','orientation'):
                require(len(call[field])==len(e[field]) and all(abs(x-y)<=1e-8*(1+abs(x)) for x,y in zip(call[field],e[field])),
                        stage+': returned pose differs from prediction service')
    result.update(fresh_warp_completed=fresh,fresh_reuse_with_updated_prediction=fresh_reuse_updated,
                  presentation={'counts':dict(display_counts),'fresh_on_time':fresh_presented,
                                'maximum_observer_lateness_ns':max(observer_lateness,default=0)},
                  maximum_frame_age_ns=max((e['frame_age_ns'] for e in warps),default=0),
                  opportunity_counts=dict(slot_counts))
    return result,errors
