#!/usr/bin/env python3
"""Build isolated custom2 INT8 and custom3 FP32 Spike models into one DSO."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--int8-params',type=Path,required=True);p.add_argument('--fp32-params',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--prefix',type=Path,default=Path('/home/prashanth/chipyard/.conda-env/riscv-tools'));a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
 sources=[]
 for kind,opcode,dim,params in [('int8',2,16,a.int8_params),('fp32',3,4,a.fp32_params)]:
  d=a.output/kind;d.mkdir();name='illixr_gemmini_'+kind
  text=params.read_text();assert re.search(r'#define XCUSTOM_ACC\s+'+str(opcode)+r'\b',text);assert re.search(r'#define DIM\s+'+str(dim)+r'\b',text)
  (d/'gemmini_params.h').write_text(text)
  for file in ['gemmini.h','gemmini.cc']:
   s=(a.source/file).read_text();s=re.sub(r'\bgemmini_t\b',name+'_t',s);s=re.sub(r'\bgemmini_state_t\b',name+'_state_t',s);s=s.replace('"gemmini"','"'+name+'"').replace('REGISTER_EXTENSION(gemmini,','REGISTER_EXTENSION('+name+',').replace('ROCC_OPCODE3, ROCC_OPCODE_MASK',f'ROCC_OPCODE{opcode}, ROCC_OPCODE_MASK')
   if file=='gemmini.cc':
    assert 'p->get_id() != 0' in s
    anchor='static reg_t gemmini_custom(processor_t* p, insn_t insn, reg_t pc) {'
    monitor='''namespace { struct DispatchStats {
 unsigned long long calls=0, mask=0;
 ~DispatchStats() { fprintf(stderr,"ILLIXR_SPIKE_ACCEL {\\"name\\":\\"NAME\\",\\"opcode\\":OPCODE,\\"calls\\":%llu,\\"hart_mask\\":%llu}\\n",calls,mask); }
} dispatch_stats; }
'''.replace('NAME',name).replace('OPCODE',str(opcode))
    s=s.replace(anchor,monitor+anchor)
    anchor2='  reg_t xd = gemmini->CUSTOMFN(XCUSTOM_ACC)(u.r, xs1, xs2);'
    assert anchor2 in s;s=s.replace(anchor2,'  dispatch_stats.calls++; dispatch_stats.mask |= 1ULL << p->get_id();\n'+anchor2)
   (d/file).write_text(s)
  sources.append(d/'gemmini.cc')
 lib=a.output/'libillixr_dual_gemmini.so';cmd=['/usr/bin/g++','-shared','-std=c++17','-fPIC','-O2','-fno-fast-math','-ffp-contract=off','-I'+str(a.prefix/'include'),*map(str,sources),'-o',str(lib)];subprocess.run(cmd,check=True)
 manifest={'library':str(lib),'sha256':sha(lib),'command':cmd,'extensions':['illixr_gemmini_int8','illixr_gemmini_fp32'],'allowed_harts':[0],'mstatus_xs_enforced':False,'xs_limitation':'Uses the previously accepted functional model; does not validate mstatus.XS enable/context semantics. The dynamically loaded XS-checking variant rejected the first custom instruction.','timing_model':'functional, not DMA/cache/AXI timing','sources':{str(p.relative_to(a.output)):sha(p) for p in a.output.rglob('*') if p.is_file()}}
 (a.output/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(lib)
if __name__=='__main__':main()
