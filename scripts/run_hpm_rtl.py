#!/usr/bin/env python3
"""Bounded Rocket HPM regression using newly elaborated, unmodified core logic.

Extracts complete modules from target FIRRTL, lowers them with the matching
compiler, and adds observation-only ports. All outputs belong to --work.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]


def execute(a):
    w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
    spec=importlib.util.spec_from_file_location('rtl_common',a.chipyard/'tests/rtl_fixes/common.py')
    common=importlib.util.module_from_spec(spec);spec.loader.exec_module(common)
    text=a.firrtl.read_text();markers=list(re.finditer(r'^  (?:ext)?module (\w+)\s*:',text,re.M))
    modules={m[1]:text[m.start():markers[i+1].start() if i+1<len(markers) else len(text)] for i,m in enumerate(markers)}
    pending=['Rocket'];selected=set()
    while pending:
        name=pending.pop()
        if name in selected:continue
        selected.add(name)
        for child in re.findall(r'^    inst \w+ of (\w+)',modules[name],re.M):
            if child not in modules:raise ValueError(f'Missing generated dependency {child}')
            pending.append(child)
    # Preserve the real top so abstract resets resolve through original wiring.
    # The Verilator compiler subsequently selects only complete Rocket dependencies.
    fir=w/'Target.fir';fir.write_text(text)
    widths=dict((n,int(bits)) for n,bits in re.findall(r'    reg (\w+) : UInt<(\d+)>',modules['CSRFile']))
    smalls=dict((int(i),n) for n,i in re.findall(r'node \w+ = add\((small(?:_\d+)?), io.counters\[(\d+)\].inc\)',modules['CSRFile']))
    if set(smalls)!=set(range(13)):raise ValueError('Expected thirteen HPM banks')
    del text,modules
    anno=w/'ports.anno.json';anno.write_text('[{"class":"firrtl2.transforms.NoDCEAnnotation$"}]\n')
    generated=w/'generated.sv';env=os.environ.copy();env['JAVA_TOOL_OPTIONS']='-Xmx8G -Xss16M -XX:ActiveProcessorCount='+str(a.jobs)
    if a.lowering_cache:
        cache=a.lowering_cache
        provenance=json.loads((cache/'lowering-cache.json').read_text())
        if (provenance['firrtl_sha256']!=common.sha(fir) or
            provenance['compiler_sha256']!=common.sha(a.classpath) or
            provenance['generated_sha256']!=common.sha(cache/'generated.sv')):
            raise ValueError('Lowering cache differs from source/compiler/artifact hashes')
        shutil.copy2(cache/'generated.sv',generated)
        lower={'reused':str(cache),'provenance':provenance}
    else:
        lower=common.command([a.chipyard/'.conda-env/bin/java','-cp',a.classpath,'firrtl2.stage.FirrtlMain','-i',fir,'-o',generated,'-X','sverilog','-faf',anno,'--target-dir',w],w/'lower.log',env=env,timeout=1800)
    (w/'lowering-cache.json').write_text(json.dumps({'firrtl_sha256':common.sha(fir),
        'compiler_sha256':common.sha(a.classpath),'generated_sha256':common.sha(generated)},indent=2)+'\n')
    for n in ['EICG_wrapper.v','plusarg_reader.v']:
        sources=list((a.chipyard/'generators/rocket-chip/src/main/resources').rglob(n))
        if len(sources)!=1:raise ValueError(f'Missing resource {n}')
        shutil.copy2(sources[0],w/n)
    s=generated.read_text()
    probes={'dbg_inhibit':('[15:0]','csr.reg_mcountinhibit'),
            'dbg_retire':('','wb_valid'),'dbg_exception':('','csr_io_exception'),
            'dbg_pc':('[39:0]','csr_io_pc'),'dbg_cause':('[63:0]','csr_io_cause'),
            'dbg_cycle':('[63:0]','csr.large_1 * 64 + csr.small_1'),
            'dbg_instret':('[63:0]','csr.large_ * 64 + csr.small_'),
            'dbg_csr_retire':('','csr_io_retire'),
            'dbg_cycle_run':('','!csr_io_csr_stall && !csr.reg_mcountinhibit[0]')}
    for i,small in smalls.items():
        large='large'+small[len('small'):]
        probes[f'dbg_count_{i}']=('[39:0]',f'csr.{large} * {1<<widths[small]} + csr.{small}')
    selectors=[(1<<(i+8))|1 for i in range(11)]+[0x102,0x202]
    probes['dbg_selectors_valid']=('', ' && '.join(f'csr_io_counters_{i}_eventSel == 64\'d{value}' for i,value in enumerate(selectors)))
    probes['dbg_registered_inc']=('[12:0]', ' | '.join(f'(csr_io_counters_{i}_inc << {i})' for i in range(13)))
    # Independent gate expressions from the published event definitions; inputs
    # are observed core state, never forced internal state.
    gates=[
      'id_ex_hazard && ex_ctrl_mem || id_mem_hazard && mem_ctrl_mem || id_wb_hazard && wb_ctrl_mem',
      'id_sboard_hazard',
      'id_ex_hazard && ex_ctrl_csr != 0 || id_mem_hazard && mem_ctrl_csr != 0 || id_wb_hazard && wb_ctrl_csr != 0',
      'icache_blocked','id_ctrl_mem && dcache_blocked',
      'take_pc_mem && mem_direction_misprediction',
      'take_pc_mem && mem_wrong_npc && mem_cfi && !mem_direction_misprediction && !icache_blocked',
      'wb_reg_flush_pipe','replay_wb',
      'id_ex_hazard && (ex_ctrl_mul || ex_ctrl_div) || id_mem_hazard && (mem_ctrl_mul || mem_ctrl_div) || id_wb_hazard && wb_ctrl_div',
      'id_ex_hazard && ex_ctrl_fp || id_mem_hazard && mem_ctrl_fp || id_wb_hazard && wb_ctrl_fp || id_ctrl_fp && id_stall_fpu',
      'io_imem_perf_acquire','io_dmem_perf_acquire']
    probes['dbg_events']=('[12:0]',' | '.join(f'(({g}) << {i})' for i,g in enumerate(gates)))
    # Add output-only observations without rewriting a functional assignment.
    a0,b0,e0=common.module(s,'Rocket');end=e0-len('endmodule')
    decl=',\n'+',\n'.join('output '+width+' '+name for name,(width,expr) in probes.items())+'\n'
    assigns='\n'+'\n'.join('assign '+name+' = '+expr+';' for name,(width,expr) in probes.items())+'\n'
    observed=s[:b0]+decl+s[b0:end]+assigns+s[end:]
    model=w/'observed.sv';model.write_text(observed)
    (w/'zero_inputs.h').write_text(common.zero_inputs(common.ports(s,'Rocket')))
    (w/'read_counts.h').write_text('\n'.join(f'counts[{i}]=d.dbg_count_{i};' for i in range(13)))
    binary,build=common.compile_model(model,w,'Rocket','VHpmRocket',ROOT/'tests/hpm/rocket.cpp',a.jobs)
    run=common.command([binary],w/'run.log',timeout=120,expect=None)
    log=(w/'run.log').read_text();marker=re.search(r'HPM_RTL checks=(\d+) failures=(\d+)',log)
    coverage=re.search(r'HPM_COVERAGE((?: \d+){13})',log)
    passed=bool(run['exit_code']==0 and marker and int(marker[1])>100000 and int(marker[2])==0 and coverage and all(int(v)>0 for v in coverage[1].split()))
    result={'passed':passed,'complete':marker is not None,'checks':int(marker[1]) if marker else 0,
            'coverage':list(map(int,coverage[1].split())) if coverage else None,
            'input_firrtl':str(a.firrtl),'input_sha256':common.sha(a.firrtl),
            'selected_modules':sorted(selected),'selected_firrtl_sha256':common.sha(fir),
            'generated_sha256':common.sha(generated),'observed_sha256':common.sha(model),
            'lowering':lower,'build':build,'execution':run,
            'scope':'Rocket instruction stream with external frontend/cache/FPU protocol stimuli; no internal state forcing.'}
    (w/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    if not passed:raise RuntimeError('HPM RTL failed/incomplete: '+str(w/'run.log'))


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--firrtl',type=Path,required=True)
    p.add_argument('--classpath',type=Path,required=True);p.add_argument('--chipyard',type=Path,required=True)
    p.add_argument('--work',type=Path,required=True);p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--lowering-cache',type=Path,help='Reuse exact hashed source/compiler lowering only')
    a=p.parse_args()
    if not 1<=a.jobs<=4:p.error('Use one to four workers')
    execute(a)
if __name__=='__main__':main()
