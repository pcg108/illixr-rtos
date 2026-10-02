#!/usr/bin/env python3
"""Build a private FP32 Spike extension with hart-zero-only RoCC execution."""
import argparse,hashlib,json,shutil,subprocess
from pathlib import Path

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True)
 p.add_argument('--params',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--spike-prefix',type=Path,default=Path('/home/prashanth/chipyard/.conda-env/riscv-tools'))
 a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 for name in ('gemmini.cc','gemmini.h'):shutil.copy2(a.source/name,a.output/name)
 shutil.copy2(a.params,a.output/'gemmini_params.h')
 path=a.output/'gemmini.cc';text=path.read_text()
 marker='static reg_t gemmini_custom(processor_t* p, insn_t insn, reg_t pc) {'
 if text.count(marker)!=1:raise ValueError('Unknown Spike Gemmini interface')
 text=text.replace(marker,marker+'\n  if (p->get_id() != 0) throw trap_illegal_instruction(insn.bits());')
 path.write_text(text)
 lib=a.output/'libgemmini_illixr_fp32.so'
 cmd=['/usr/bin/g++','-shared','-std=c++17','-fPIC','-O2','-fno-fast-math','-ffp-contract=off',
      '-I'+str(a.spike_prefix/'include'),str(path),'-o',str(lib)]
 subprocess.run(cmd,check=True)
 record={'command':cmd,'library':str(lib),'sha256':hashlib.sha256(lib.read_bytes()).hexdigest(),
         'params_sha256':hashlib.sha256(a.params.read_bytes()).hexdigest(),'allowed_harts':[0],
         'timing_model':'functional only; Gemmini counters are not target cycle measurements',
         'sources':{name:hashlib.sha256((a.output/name).read_bytes()).hexdigest() for name in ('gemmini.cc','gemmini.h')}}
 (a.output/'manifest.json').write_text(json.dumps(record,indent=2)+'\n')
 print(lib)
if __name__=='__main__':main()
