"""Validate bounded BLAS evidence without rejecting historical Eigen-only traces."""
NAMES = ('dgemm','dgemv','dtrmm','dtrmv','dtrsm','daxpy','sgemm','sgemv','strmm','strmv','strsm','saxpy')
def vector_preflight_errors(data, harts):
 """Require complete context/CSR, FP64, per-hart and migration evidence."""
 vector=data.get('vector_checks',[])
 if len(vector)!=1: return ['Missing or duplicate vector context preflight']
 v=vector[0]
 if (v.get('passed') is not True or v.get('errors')!=0 or
     v.get('vlenb')!=32 or v.get('fp64') is not True or
     v.get('hart_mask')!=(1<<harts)-1 or
     v.get('worker_rounds')!=[32]*(2*harts) or
     v.get('migrations')!=(4*harts if harts>1 else 0)):
  return ['Vector context preflight failed']
 return []

def valid_gemmini_edge_record(record):
 return (record.get('passed') is True and record.get('errors')==0 and
         record.get('cases')==590 and record.get('checks')==46552 and
         record.get('largest_dim')==135 and
         record.get('reference')=='exact_dyadic_and_closed_form')

def gemmini_edge_preflight_errors(data):
 preflight=data.get('gemmini_selftests',[])
 if len(preflight)!=1: return []  # The caller validates the main result.
 version=preflight[0].get('fixture_version',1)
 if version not in (1,2): return ['Unknown Gemmini fixture version']
 edges=data.get('gemmini_edges',[])
 if version==1 and not edges: return []  # Historical traces remain supported.
 if len(edges)!=1 or not valid_gemmini_edge_record(edges[0]):
  return ['Gemmini boundary/observed-size preflight missing or failed']
 return []

def check_blas(data, harts):
 headers=data.get('blas',[]); work=data.get('blas_work',[]); memory=data.get('blas_memory',[]); tests=data.get('blas_selftests',[])
 if not headers:
  return {'backend':'legacy-eigen'}, ['BLAS records without backend identity'] if work or memory or tests else []
 if len(headers)!=1: return {}, ['Expected one BLAS backend identity']
 header=headers[0]; backend=header.get('backend');errors=[]
 result={'backend':backend,'configuration':header,'work':work,'memory':memory,'selftests':tests}
 if backend=='eigen':
  if work or memory or tests: errors.append('Eigen-only trace unexpectedly contains BLAS work')
  return result,errors
 if backend not in ('openblas_scalar','openblas_rvv','openblas_gemmini_fp32'): return result,['Unknown linear algebra backend']
 if header.get('scratch_bytes')!=32*1024*1024 or header.get('interface_bits')!=32 or header.get('threads')!=1: errors.append('BLAS runtime configuration mismatch')
 if len(tests)!=1 or tests[0].get('passed') is not True or tests[0].get('checks',0)<=0: errors.append('BLAS numerical preflight did not pass')
 if len(memory)!=1 or memory[0].get('outstanding')!=0 or memory[0].get('reserved_bytes')!=32*1024*1024: errors.append('BLAS scratch accounting failed')
 if sorted(r.get('name','') for r in work)!=sorted(NAMES): errors.append('Missing or duplicate BLAS operation records')
 for r in work:
  counts=r.get('harts',[]);mask=r.get('hart_mask',-1);calls=r.get('calls',-1)
  if len(counts)!=4 or any(type(v)is not int or v<0 for v in counts) or sum(counts)!=calls: errors.append('BLAS work counts inconsistent');continue
  if type(mask)is not int or mask<0 or mask & ~((1<<harts)-1) or any(counts[h] for h in range(harts,4)): errors.append('BLAS work on unavailable hart')
  if any(counts[h] and not(mask & (1<<h)) for h in range(4)): errors.append('BLAS work mask omits observed hart')
  if bool(calls)!=bool(mask): errors.append('BLAS work mask/call count mismatch')
  if any(type(r.get(key))is not int or r[key]<0 for key in ('cycles','wait_cycles','max_m','max_n','max_k')): errors.append('Invalid BLAS counters')
  if r.get('counter_version') == 2:
   keys=('cycles_samples','cycles_migrated','wait_cycles_migrated','elapsed_ns','wait_ns')
   if any(type(r.get(key)) is not int or r[key]<0 for key in keys): errors.append('Invalid BLAS migration counters')
   elif r['cycles_samples']+r['cycles_migrated']!=calls or r['wait_cycles_migrated']>calls: errors.append('Inconsistent BLAS migration counters')
 if not any(r.get('name')=='dgemm' and r.get('calls',0)>0 for r in work): errors.append('No estimator DGEMM calls observed')
 if backend in ('openblas_rvv','openblas_gemmini_fp32'):
  vector=data.get('vector_checks',[]);kernels=data.get('rvv_kernels',[])
  result.update(vector_checks=vector,rvv_kernels=kernels)
  errors.extend(vector_preflight_errors(data,harts))
  expected={(p,n) for p in ('selftest','work') for n in ('dgemm_kernel','dgemv_n','dgemv_t')}
  if len(kernels)!=6 or {(k.get('phase'),k.get('name')) for k in kernels}!=expected: errors.append('Missing or duplicate RVV kernel counters')
  for k in kernels:
   counts=k.get('harts',[]);calls=k.get('calls',-1)
   if len(counts)!=4 or any(type(v)is not int or v<0 for v in counts) or sum(counts)!=calls or any(counts[harts:]): errors.append('Invalid RVV kernel hart counters')
   if backend=='openblas_rvv' and (k.get('phase')=='selftest' or k.get('name')=='dgemm_kernel') and calls<=0: errors.append('Required RVV kernel did not execute')
 if backend=='openblas_gemmini_fp32':
  records=data.get('gemmini',[]);preflight=data.get('gemmini_selftests',[])
  result.update(gemmini=records,gemmini_selftests=preflight,computation_precision='fp32-gemm-gemv')
  packing=data.get('gemmini_packing',[])
  result['gemmini_packing']=packing
  if packing:
   expected_packing={(r.get('phase'),r.get('name')) for r in records}
   if len(packing)!=len(records) or {(r.get('phase'),r.get('name')) for r in packing}!=expected_packing: errors.append('Missing or duplicate packing records')
   if len({r.get('implementation') for r in packing})!=1: errors.append('Inconsistent packing implementation')
   for r in packing:
    if r.get('implementation') not in ('scalar','rvv') or any(type(r.get(k))is not int or r[k]<0 for k in ('pack_elements','unpack_elements')): errors.append('Invalid packing record')
   for r in packing:
    # Early packing traces contain only element counts; retain their support.
    if 'pack_cycles' not in r: continue
    keys=('pack_cycles','unpack_cycles','pack_read_bytes','pack_write_bytes','unpack_read_bytes','unpack_write_bytes','vector_calls','hart_mask')
    if any(type(r.get(k)) is not int or r[k]<0 for k in keys):
     errors.append('Invalid packing timing/byte counters');continue
    match=[g for g in records if (g.get('phase'),g.get('name'))==(r.get('phase'),r.get('name'))]
    if len(match)!=1: continue
    g=match[0];width=8 if r['name'].startswith('d') else 4
    if (r['pack_write_bytes']!=r['pack_elements']*4 or
        r['unpack_read_bytes']!=r['unpack_elements']*4 or
        r['unpack_write_bytes']!=r['unpack_elements']*width or
        r['pack_read_bytes']>r['pack_elements']*width):
     errors.append('Inconsistent packing byte counts')
    if r['vector_calls']!=(g['submissions'] if r['implementation']=='rvv' else 0):
     errors.append('Packing vector dispatch count mismatch')
    if r['hart_mask']!=g['accelerator_hart_mask']: errors.append('Packing hart mismatch')
  result['gemmini_edges']=data.get('gemmini_edges',[])
  errors.extend(gemmini_edge_preflight_errors(data))
  expected={(phase,op) for phase in ('selftest','work') for op in ('sgemm','dgemm','sgemv','dgemv')}
  if len(records)!=8 or {(r.get('phase'),r.get('name')) for r in records}!=expected: errors.append('Missing or duplicate Gemmini records')
  if len(preflight)!=1 or preflight[0].get('passed') is not True or preflight[0].get('errors')!=0 or preflight[0].get('caller_hart_mask')!=(1<<harts)-1 or preflight[0].get('caller_migrations')!=(4*harts if harts>1 else 0): errors.append('Gemmini preflight failed')
  for r in records:
   counts=r.get('caller_harts',[]);calls=r.get('calls',-1);submissions=r.get('submissions',-1)
   if len(counts)!=4 or any(type(v)is not int or v<0 for v in counts) or sum(counts)!=calls or any(counts[harts:]): errors.append('Invalid Gemmini caller counts')
   if type(submissions)is not int or not 0<=submissions<=calls: errors.append('Invalid Gemmini submission count')
   if r.get('accelerator_hart_mask')!=(1 if submissions>0 else 0): errors.append('Gemmini executed outside hart zero or lacks placement evidence')
   if r.get('precision')!='fp32' or not 0<=r.get('scratch_high_water',-1)<=32*1024*1024: errors.append('Gemmini precision/scratch mismatch')
   if any(type(r.get(k))is not int or r[k]<0 for k in ('packing_ns','unpacking_ns','queue_ns','execution_ns','cycles','max_m','max_n','max_k')): errors.append('Invalid Gemmini timing/dimension counters')
   if (r.get('phase')=='selftest' or r.get('name')=='dgemm') and submissions<=0: errors.append('Required Gemmini operation did not execute')
   if r.get('phase')=='work':
    matching=[w for w in work if w.get('name')==r.get('name')]
    if len(matching)!=1 or calls>matching[0].get('calls',-1): errors.append('Gemmini/BLAS call accounting mismatch')
 return result,errors
