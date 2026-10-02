#!/usr/bin/env python3
"""Bounded standalone BLAS validation on Spike or a matching Verilator model."""
import argparse,hashlib,json,time,shutil
from pathlib import Path
from analyze_spike import records, startup_checks
from blas_analysis import vector_preflight_errors, gemmini_edge_preflight_errors
from run_rocket import simulator_command, capture
from simulator_counter import counter_parameters
from fixture_oracle_analysis import analyze_oracle

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--platform',choices=['spike','verilator'],required=True)
 p.add_argument('--simulator',type=Path,required=True);p.add_argument('--elf',type=Path,required=True)
 p.add_argument('--extension',type=Path);p.add_argument('--harts',type=int,choices=[1,4],required=True)
 p.add_argument('--chipyard',type=Path,default=Path('/home/prashanth/chipyard'))
 p.add_argument('--output',type=Path,required=True);p.add_argument('--timeout',type=int,default=86400)
 p.add_argument('--counter-manifest',type=Path)
 p.add_argument('--oracle-manifest',type=Path)
 a=p.parse_args();counter=counter_parameters(a.platform,a.simulator,a.counter_manifest)
 oracle=None
 if a.oracle_manifest:
  oracle=json.loads(a.oracle_manifest.read_text())
  if oracle.get('elf_sha256')!=digest(a.elf) or oracle.get('mode') not in ('capture','verify','table'):
   p.error('Compiled oracle manifest does not match the requested ELF/mode')
 a.output.mkdir(parents=True,exist_ok=False)
 original_elf=a.elf
 shutil.copy2(a.elf,a.output/'firmware.elf');a.elf=(a.output/'firmware.elf').resolve()
 if a.platform=='spike':
  if not a.extension:p.error('Spike requires the FP32 Gemmini extension')
  cmd=[str(a.simulator),'--extlib='+str(a.extension),'--extension=gemmini',f'-p{a.harts}',
       '-m0x80000000:0x10000000','--isa=rv64imafdcv_zicsr_zifencei_zicntr_zvl256b',
       '--instructions=100000000000',str(a.elf)]
 else:cmd=simulator_command(a.simulator,a.elf,a.chipyard,counter['counter_limit'])
 result={'status':'running','command':cmd,'original_elf':str(original_elf),'elf_sha256':digest(a.elf),'simulator_sha256':digest(a.simulator),
         'platform':a.platform,'harts':a.harts,'timeout_seconds':a.timeout,
         **counter}
 if a.extension:result['extension_sha256']=digest(a.extension)
 if oracle:
  shutil.copy2(a.oracle_manifest,a.output/'oracle_manifest.json')
  result['oracle_manifest_sha256']=digest(a.oracle_manifest)
 output=a.output/'run.json';output.write_text(json.dumps(result,indent=2)+'\n');start=time.monotonic()
 try:
  execution=capture(cmd,a.output,a.timeout)
  result.update(execution)
  code=execution['returncode']
  data=records(a.output/'console.log');text=(a.output/'console.log').read_text(errors='replace')
  errors=startup_checks(data,a.harts,10_000_000 if a.platform=='spike' else 1_000_000,
                       None if a.platform=='spike' else 1_000_000_000,a.platform!='spike')
  oracle_records,oracle_errors=analyze_oracle(text,oracle['mode'] if oracle else None)
  errors.extend(oracle_errors)
  if oracle:result['fixture_oracle']=oracle_records
  if len(data['platforms'])!=1:errors.append('Missing platform counter progression check')
  if code!=0 or 'ILLIXR_GEMMINI_STANDALONE_END pass' not in text:errors.append('No successful normal HTIF completion')
  tests=data['blas_selftests'];gtests=data['gemmini_selftests'];vectors=data['vector_checks'];g=data['gemmini']
  if len(tests)!=1 or tests[0].get('passed') is not True:errors.append('BLAS selftest failed')
  if len(gtests)!=1 or gtests[0].get('passed') is not True or gtests[0].get('errors')!=0 or gtests[0].get('caller_hart_mask')!=(1<<a.harts)-1 or gtests[0].get('caller_migrations')!=(4*a.harts if a.harts>1 else 0):errors.append('Gemmini selftest failed')
  errors.extend(vector_preflight_errors(data,a.harts))
  errors.extend(gemmini_edge_preflight_errors(data))
  if len(g)!=4 or {r.get('name') for r in g}!={'sgemm','dgemm','sgemv','dgemv'}:errors.append('Missing/duplicate accelerator records')
  for r in g:
   if r.get('phase')!='selftest' or r.get('submissions',0)<=0 or r.get('accelerator_hart_mask')!=1:errors.append('Missing Gemmini execution or incorrect hart')
   if r.get('name')=='dgemm' and any(c==0 for c in r.get('caller_harts',[])[:a.harts]):errors.append('Not all harts exercised the service')
  complete=code==0 and 'ILLIXR_GEMMINI_STANDALONE_END' in text and not any(
      execution[k] for k in ('timed_out','interrupted','cycle_limit_reached'))
  if not complete:errors.append('Target execution is incomplete')
  result.update(status='pass' if not errors else ('fail' if complete else 'incomplete'),
                passed=not errors,complete=complete,exit_code=code,errors=errors,evidence=data)
 except (OSError,ValueError,KeyError) as exc:
  result.update(status='incomplete',passed=False,complete=False,error=repr(exc))
 finally:
  result['host_seconds']=time.monotonic()-start;output.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:v for k,v in result.items() if k!='evidence'},indent=2))
 return 0 if result.get('passed') else 1
if __name__=='__main__':raise SystemExit(main())
