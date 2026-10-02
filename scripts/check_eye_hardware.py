#!/usr/bin/env python3
"""Fail-closed pre-synthesis checks for isolated eye-tracking hardware."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def check(root,mode,cores):
 chip=root/'chipyard';repo=Path(__file__).resolve().parents[1]
 header=chip/'gemmini_params_illixr_int8.h'
 assert header.read_bytes()==(repo/'third_party/ritnet/port/include/gemmini_params.h').read_bytes(),'Generated INT8 ABI differs from firmware header'
 headers={'int8':{'path':str(header),'sha256':sha(header)}}
 if mode=='dual':
  fp=chip/'gemmini_params_illixr.h';text=fp.read_text()
  for line in ['#define XCUSTOM_ACC 3','#define DIM 4','#define BANK_ROWS 512','#define ACC_ROWS 512','typedef float elem_t;','typedef float acc_t;']:
   assert line in text,line
  headers['fp32']={'path':str(fp),'sha256':sha(fp)}
 dts=list((chip/'sims/firesim-staging/generated-src').rglob('*.dts'));assert len(dts)==1,dts
 spec=importlib.util.spec_from_file_location('inspect_platform',root/'control/inspect_firesim_platform.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
 platform=mod.inspect_hardware(dts[0],cores)
 fir=dts[0].with_suffix('.fir');tiles={};current=None
 with fir.open() as f:
  for line in f:
   m=re.match(r'  module (\w+)\s*:',line)
   if m:
    current=m[1]
    if re.fullmatch(r'RocketTile(?:_\d+)?',current):tiles[current]=[]
   m=re.match(r'    inst (\w+) of (\w+)',line)
   if m and current in tiles:tiles[current].append({'instance':m[1],'module':m[2]})
 assert len(tiles)==cores,tiles
 for hart in range(cores):
  instances=tiles['RocketTile'+('_'+str(hart) if hart else '')]
  assert sum(i['module'].startswith('SaturnRocketUnit') for i in instances)==1
  assert sum(i['module'].startswith('Gemmini') for i in instances)==((2 if mode=='dual' else 1) if hart==0 else 0)
 for fix,subrepo in [('rocket-vector-trap-pc.patch','rocket-chip'),('rocket-vector-interrupt-ibuf.patch','rocket-chip'),('saturn-cross-page-dependency.patch','saturn')]:
  subprocess.run(['git','-C',str(chip/'generators'/subrepo),'apply','--reverse','--check',str(repo/'patches'/fix)],check=True)
 rtl=list((chip/'sims/firesim/sim/generated-src').rglob('FireSim-generated.sv'));assert len(rtl)==1,rtl
 report={'generated_rtl':{'path':str(rtl[0]),'sha256':sha(rtl[0])},'passed':True,'mode':mode,'harts':cores,'platform':platform,'headers':headers,'tile_instances':tiles,'firrtl':str(fir),'firrtl_sha256':sha(fir),'int8_hart':0,'int8_opcode':2,'fp32_opcode':3 if mode=='dual' else None}
 (root/'provenance/generated-pre-synthesis.json').write_text(json.dumps(report,indent=2)+'\n')
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--mode',choices=['int8','dual'],required=True);p.add_argument('--cores',type=int,choices=[1,4],required=True);a=p.parse_args();print(json.dumps(check(a.root,a.mode,a.cores),indent=2))
