#!/usr/bin/env python3
"""Run staged OpenBLAS validation using the existing bounded Spike/FireSim runners."""
import argparse,fcntl,json,sys
from pathlib import Path
from types import SimpleNamespace
import run_spike
import run_firesim_matrix as fs
REPO=Path(__file__).resolve().parents[1]
OLD=Path('/scratch/prashanth_illixr_firesim_20260926')
MODES={1:'single',2:'dual',4:'quad'}

def firmware(work,platform,harts,backend,preflight=False):
 selection=fs.read_json(work/'control/firmware-selection.json') or {}
 key=f'{platform}-{MODES[harts]}-{backend}{"-preflight" if preflight else ""}'
 if key in selection:
  item=selection[key]; path=Path(item['elf'])
  if fs.sha256(path)!=item['sha256']: raise ValueError(f'Selected firmware changed: {key}')
  return path
 return work/'software/artifacts'/f'{platform}-{MODES[harts]}-{backend}{"-preflight" if preflight else ""}'/'zephyr.elf'

def expected_cases(stage):
 if stage=='rocket-scalar':
  return {(h,b,p) for h in (1,2,4) for b,p in [('openblas_scalar',True),('eigen',False),('openblas_scalar',False)]}
 backend='openblas_rvv' if stage=='spike-rvv' else 'openblas_scalar'
 return {(h,backend,False) for h in (1,4)}

def save(work,stage,items):
 expected=expected_cases(stage)
 observed={(i.get('harts'),i.get('backend'),i.get('preflight',False)) for i in items}
 value={'stage':stage,'all_passed':len(items)==len(expected) and observed==expected and all(i.get('passed') for i in items),'expected_cases':len(expected),'cases':items}
 fs.write_json(work/'runtime'/f'{stage}.json',value)
 return value

def require_stage(work,stage):
 p=work/'runtime'/f'{stage}.json'
 if not p.is_file(): raise ValueError(f'Required stage has not passed: {stage}')
 d=json.loads(p.read_text())
 observed={(i.get('harts'),i.get('backend'),i.get('preflight',False)) for i in d.get('cases',[])}
 if not d.get('all_passed') or len(d.get('cases',[]))!=len(expected_cases(stage)) or observed!=expected_cases(stage):
  raise ValueError(f'Required stage has not passed: {stage}')
 for case in d['cases']:
  if not case.get('passed') or case.get('elf_sha256')!=fs.sha256(case['elf']):
   raise ValueError(f'Stale or incomplete required stage: {stage}')
  analysis=fs.read_json(Path(case['output'])/'analysis.json') or {}
  if analysis.get('passed') is not True or analysis.get('complete') is not True:
   raise ValueError(f'Missing successful evidence for required stage: {stage}')

def spike_stage(args,backend):
 items=[]
 for harts in (1,4):
  elf=firmware(args.work,'spike',harts,backend)
  name=f'spike-{MODES[harts]}-{backend}-{run_spike.sha256(elf)[:10]}'
  out=args.work/'runtime'/name
  case={'name':name,'elf':str(elf),'harts':harts,'output':str(out),'require_initialized':True,'require_async':True,'require_gpu':True,'placement':'unpinned','linalg_backend':backend}
  if backend=='openblas_rvv':case['isa']='rv64imafdcv_zicsr_zifencei_zicntr_zvl256b'
  previous=fs.read_json(out/'run.json') or {}
  if previous.get('status')=='pass' and previous.get('elf_sha256')==run_spike.sha256(elf):
   passed=True
  else:
   if (out/'run.json').exists():case['output']=str(fs.next_output(out))
   run_args=SimpleNamespace(spike=args.spike,isa=run_spike.DEFAULT_ISA,output=Path(case['output']),native=args.native,dataset=args.dataset,prediction_native=args.prediction_native,timeout=86400,max_instructions=100_000_000_000)
   passed=run_spike.run(run_args,case)
  items.append({'harts':harts,'backend':backend,'elf':str(elf),'elf_sha256':fs.sha256(elf),'output':case['output'],'passed':passed})
  save(args.work,args.stage,items)
  if not passed: return False
 return True

def rocket_stage(args):
 require_stage(args.work,'spike-scalar')
 items=[]
 run_args=SimpleNamespace(work=OLD,native=args.native,dataset=args.dataset,prediction_native=args.prediction_native)
 fs.prepare_board_database(OLD,Path('/opt/firesim-db.json'))
 if (OLD/'control/runtime-recovery-required.json').exists():raise ValueError('Existing FPGA recovery hold')
 with (OLD/'control/runtime.lock').open('a+') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
  for harts in (1,2,4):
   hardware=args.work/'control'/f'hardware-rocket-{harts}.json'
   if not hardware.exists():hardware.write_bytes((OLD/'control'/f'hardware-{harts}.json').read_bytes())
   for backend,preflight in [('openblas_scalar',True),('eigen',False),('openblas_scalar',False)]:
    elf=firmware(args.work,'rocket',harts,backend,preflight)
    name=f'rocket-{MODES[harts]}-{backend}{"-preflight" if preflight else ""}-{fs.sha256(elf)[:10]}'
    case={'name':name,'harts':harts,'placement':'unpinned','platform_check':preflight,'elf':str(elf),'hardware_manifest':str(hardware),'output':str(args.work/'runtime'/name),'timeout_seconds':86400,'max_cycles':100_000_000_000,'memory_profile_interval_cycles':1_000_000,'zero_out_dram':False,'modeled_clock_scale':2,'ticks_per_sec':10000,'require_gpu':not preflight,'linalg_backend':backend}
    previous=fs.inspect_case(case)
    if previous['reusable']:passed=True;interrupted=False
    else:passed,interrupted=fs.run_case(case,run_args)
    case['output']=fs.inspect_case(case).get('output',case['output'])
    items.append({'harts':harts,'backend':backend,'preflight':preflight,'elf':str(elf),'elf_sha256':fs.sha256(elf),'output':case['output'],'passed':passed})
    save(args.work,args.stage,items)
    if not passed or interrupted:return False
 return True

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--work',type=Path,required=True);p.add_argument('--stage',choices=['spike-scalar','rocket-scalar','spike-rvv'],required=True)
 p.add_argument('--rvv-prerequisite',choices=['rocket-scalar','spike-scalar'],default='rocket-scalar',help='Use spike-scalar only when explicitly prioritizing RVV Spike before the Rocket comparison')
 p.add_argument('--native',type=Path,default=Path('/home/prashanth/illixr-spike-validation/native/estimator_replay'));p.add_argument('--prediction-native',type=Path,default=REPO/'tests/native/prediction_reference.py');p.add_argument('--dataset',type=Path,default=Path('/home/prashanth/illixr-headless-reference/data/mav0'));p.add_argument('--spike',default='/home/prashanth/chipyard/.conda-env/riscv-tools/bin/spike');args=p.parse_args()
 if args.stage=='rocket-scalar':passed=rocket_stage(args)
 else:
  if args.stage=='spike-rvv':
   require_stage(args.work,args.rvv_prerequisite)
   fs.write_json(args.work/'runtime/spike-rvv-prerequisite.json',{'required_stage':args.rvv_prerequisite,'rocket_comparison_required_separately':True})
  passed=spike_stage(args,'openblas_scalar' if args.stage=='spike-scalar' else 'openblas_rvv')
 return 0 if passed else 1
if __name__=='__main__':sys.exit(main())
