#!/usr/bin/env python3
"""Reject missing wrappers, unexpected BLAS calls, and archive/ELF ABI mismatches."""
import argparse,json,pathlib,re,subprocess,hashlib
p=argparse.ArgumentParser();p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--artifact',type=pathlib.Path,required=True);p.add_argument('--nm',required=True);a=p.parse_args()
cache={}
for line in (a.build/'CMakeCache.txt').read_text().splitlines():
 if not line.startswith(('#','//')) and '=' in line:
  k,v=line.split('=',1);cache[k.split(':',1)[0]]=v
backend=cache.get('ILLIXR_LINALG_BACKEND','eigen');result={'backend':backend}
if backend!='eigen':
 allowed={p+op+'_' for p in 'ds' for op in ('gemm','gemv','trmm','trmv','trsm','axpy')}
 text=subprocess.check_output([a.nm,'-u',str(a.build/'app/libapp.a')],text=True)
 used=set(re.findall(r'\bU ([sdcz][a-z0-9]+_)$',text,re.M))
 oracle=a.build/'CMakeFiles/app.dir/src/blas_reference.cpp.obj'
 oracle_symbols=subprocess.check_output([a.nm,'-u',str(oracle)],text=True)
 if re.search(r'\bU (?:__wrap_)?[sdcz][a-z0-9]+_$',oracle_symbols,re.M):
  raise SystemExit('Eigen-only numerical oracle unexpectedly calls BLAS')
 if used-allowed: raise SystemExit('BLAS calls without wrappers: '+str(sorted(used-allowed)))
 symbols=subprocess.check_output([a.nm,str(a.artifact/'zephyr.elf')],text=True)
 for name in used:
  if '__wrap_'+name not in symbols: raise SystemExit('Missing linked wrapper: '+name)
 if re.search(r'\b(?:gotoblas_init|openblas_read_env|blas_thread_server|blas_server)\b',symbols): raise SystemExit('Desktop OpenBLAS runtime linked unexpectedly')
 archive=pathlib.Path(cache['ILLIXR_OPENBLAS_ARCHIVE']);manifest=json.loads((archive.parent.parent/'manifest.json').read_text())
 if manifest['backend']!=backend or manifest['sha256']!=hashlib.sha256(archive.read_bytes()).hexdigest(): raise SystemExit('Wrong archive identity')
 result.update(referenced_blas=sorted(used),archive=manifest,oracle='isolated-eigen-no-blas')
 if backend=='openblas_scalar':
  attrs=subprocess.check_output([a.nm.replace('-nm','-readelf'),'-A',str(a.artifact/'zephyr.elf')],text=True)
  if re.search(r'_v\d',attrs): raise SystemExit('Scalar build unexpectedly requires V')
 else:
  attributes=subprocess.check_output([a.nm.replace('-nm','-readelf'),'-h','-A',str(a.artifact/'zephyr.elf')],text=True)
  if 'double-float ABI' not in attributes or not re.search(r'_v1',attributes): raise SystemExit('RVV firmware ISA/ABI mismatch')
  kernel_evidence={}
  for kernel in ('dgemm_kernel','dgemv_n','dgemv_t'):
   if backend=='openblas_gemmini_fp32' and '__wrap_'+kernel not in symbols: continue
   if '__wrap_'+kernel not in symbols: raise SystemExit('RVV kernel lacks entry counter: '+kernel)
   assembly=subprocess.check_output([a.nm.replace('-nm','-objdump'),'-d','--disassemble='+kernel,str(a.artifact/'zephyr.elf')],text=True)
   if not re.search(r'\bvfm(?:acc|ul)',assembly): raise SystemExit('No vector arithmetic in '+kernel)
   path=a.artifact/(kernel+'.disassembly.txt');path.write_text(assembly)
   kernel_evidence[kernel]={'disassembly':path.name,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
  result['rvv_kernels']=kernel_evidence
  if backend=='openblas_gemmini_fp32':
   for name in ('illixr_gemmini_gemm','illixr_gemmini_gemv'):
    if not re.search(r'\bT '+name+r'$',symbols,re.M): raise SystemExit('Missing Gemmini RTOS dispatch: '+name)
   assembly=subprocess.check_output([a.nm.replace('-nm','-objdump'),'-d',str(a.artifact/'zephyr.elf')],text=True)
   custom3=[word for word in re.findall(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s',assembly,re.M) if int(word,16)&127==0x7b]
   if not custom3: raise SystemExit('No Gemmini custom3 instructions in ELF')
   result['gemmini']={'custom3_instruction_sites':len(custom3),'params_sha256':manifest['gemmini_params_sha256'],'precision':'fp32-gemm-gemv','execution_hart':0}
(a.artifact/'blas-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print('BLAS symbol/ABI audit passed:',backend)
