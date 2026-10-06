"""Validate Rocket HPM v1 accounting; absent legacy measurements are not zero."""
from collections import defaultdict

EVENTS = ['cycles', 'instructions', 'load_use_interlock', 'long_latency_interlock',
          'csr_interlock', 'icache_blocked', 'dcache_blocked', 'branch_direction_mispredict',
          'control_target_mispredict', 'pipeline_flush', 'pipeline_replay',
          'mul_div_interlock', 'fp_interlock', 'icache_acquire', 'dcache_acquire']
SELECTORS = [(1 << (8+i)) | 1 for i in range(11)] + [0x102, 0x202]
OWNERS = {'system', 'offline_imu', 'offline_cam', 'openvins', 'imu_integrator',
          'pose_prediction', 'offline_eye', 'eye_tracking', 'render_loop', 'timewarp',
          'main_probe', 'idle', 'isr_body', 'switch_gap', 'profiler'}
PHASES = {'default', 'packing', 'accelerator', 'unpacking'}


def uint(value):
    return type(value) is int and 0 <= value < 1 << 64


def metrics(values):
    result = dict(zip(EVENTS, values))
    cycles, instructions = values[:2]
    result.update(ipc=instructions/cycles if cycles else None,
                  cpi=cycles/instructions if instructions else None)
    result['per_kinstruction'] = {k: result[k]*1000/instructions if instructions else None
                                for k in EVENTS[2:]}
    result['event_active_cycle_fraction'] = {
        k: result[k]/cycles if cycles else None for k in EVENTS
        if k.endswith('_interlock') or k.endswith('_blocked')}
    return result


def check(data, harts=None, required=False):
    errors = []
    configs = data.get('hpm_configs', [])
    present = any(data.get(k) for k in ('hpm_configs', 'hpm_work', 'hpm_harts', 'hpm_threads'))
    if not present:
        return {'available': False, 'passed': not required, 'reason': 'HPM data unavailable'}, (
            ['Required HPM records are missing'] if required else [])
    result = {'available': True, 'passed': False, 'plugins': [], 'rows': [],
              'inclusive_callers': [], 'hart_totals': [], 'errors': errors}
    def need(condition, message):
        if not condition: errors.append(message)
    need(len(configs)==1, 'Expected exactly one HPM configuration')
    if len(configs)!=1: return result, errors
    config=configs[0]; result['configuration']=config
    need(config.get('version')==1 and config.get('enabled') is True, 'Unsupported HPM configuration')
    need(config.get('event_map')=='rocket-hpm-v1' and config.get('events')==EVENTS and
         config.get('selectors')==SELECTORS, 'HPM event assignment differs from pinned Rocket map')
    count=config.get('harts')
    need(type(count) is int and count in (1,2,4) and (harts is None or count==harts), 'Invalid HPM hart count')
    need(config.get('programmable_width')==40 and config.get('basic_width')==64, 'HPM width mismatch')
    if errors: return result, errors
    totals=defaultdict(lambda:[0]*len(EVENTS)); plugin_totals=defaultdict(lambda:[0]*len(EVENTS))
    seen=set(); inclusive=defaultdict(lambda:[0]*len(EVENTS))
    for row in data.get('hpm_work',[]):
        hart=row.get('hart'); values=row.get('counters'); plugin=row.get('plugin')
        key=(hart,plugin,row.get('phase'),row.get('caller'))
        valid=(row.get('version')==1 and type(hart) is int and 0<=hart<count and plugin in OWNERS and
               row.get('phase') in PHASES and row.get('caller') in OWNERS and
               isinstance(values,list) and len(values)==len(EVENTS) and all(map(uint,values)) and
               uint(row.get('intervals')) and row['intervals']>0)
        need(valid, 'Invalid HPM work record')
        if not valid: continue
        need(key not in seen, 'Duplicate HPM work record'); seen.add(key)
        for i,value in enumerate(values):
            totals[hart][i]+=value;plugin_totals[plugin][i]+=value
            if plugin=='pose_prediction' and row['caller'] in ('render_loop','timewarp'):
                inclusive[row['caller']][i]+=value
        result['rows'].append({k:row[k] for k in ('hart','plugin','phase','caller','intervals')} | metrics(values))
    hart_seen=set()
    for row in data.get('hpm_harts',[]):
        hart=row.get('hart');values=row.get('counters')
        valid=(row.get('version')==1 and type(hart)is int and 0<=hart<count and
               isinstance(values,list) and len(values)==len(EVENTS) and all(map(uint,values)) and
               all(uint(row.get(k)) for k in ('begin_timer','end_timer','samples','max_read_cycles','errors')))
        need(valid, 'Invalid HPM hart record')
        if not valid:continue
        need(hart not in hart_seen, 'Duplicate HPM hart record');hart_seen.add(hart)
        need(row['errors']==0, f'HPM accounting error on hart {hart}')
        need(row['end_timer']>=row['begin_timer'] and row['samples']>0, f'Invalid HPM measurement window on hart {hart}')
        need(totals[hart]==values, f'HPM accounting conservation failed on hart {hart}')
        result['hart_totals'].append(row | metrics(values))
    need(hart_seen==set(range(count)), 'Missing HPM hart totals')
    preflight=data.get('hpm_preflights',[])
    need(len(preflight)==count and {p.get('hart') for p in preflight}==set(range(count)) and
         all(p.get('passed') is True and p.get('programmable_counters')==13 and p.get('version')==1 for p in preflight),
         'Missing or failed HPM per-hart preflight')
    selftests=data.get('hpm_selftests',[])
    need(len(selftests)==1 and selftests[0].get('version')==1 and selftests[0].get('passed') is True and
         selftests[0].get('hart_mask')==(1<<count)-1, 'Missing or failed HPM attribution self-test')
    result['selftests']=selftests
    for row in selftests:
        if 'isr_instructions' in row:
            need(uint(row['isr_instructions']) and row['isr_instructions']>0,
                 'HPM attribution self-test did not observe interrupt instructions')
    thread_seen=set();migrations=defaultdict(int)
    for row in data.get('hpm_threads',[]):
        valid=row.get('version')==1 and uint(row.get('id')) and row.get('plugin') in OWNERS and uint(row.get('migrations'))
        need(valid,'Invalid HPM thread record')
        if valid:
            need(row['id'] not in thread_seen,'Duplicate HPM thread ID');thread_seen.add(row['id'])
            migrations[row['plugin']]+=row['migrations']
    need(bool(thread_seen),'Missing HPM thread records')
    for plugin, values in sorted(plugin_totals.items()):
        result['plugins'].append({'plugin':plugin,'migrations':migrations[plugin],**metrics(values)})
    for caller, extra in sorted(inclusive.items()):
        result['inclusive_callers'].append({'plugin':caller,'additive':False,
            **metrics([a+b for a,b in zip(plugin_totals[caller],extra)])})
    result['limitations']=['Counters describe scheduled contexts, not perfect instruction-level causality.',
        'ISR body excludes trap entry/return outside Zephyr hooks; surrounding kernel work remains attributed.',
        'Profiler hook-body overhead is a lower bound; ordered CSR reads have sampling skew.',
        'Interlock/blocked events overlap. Cache acquires exclude accelerator traffic outside Rocket L1.']
    result['passed']=not errors
    return result,errors
