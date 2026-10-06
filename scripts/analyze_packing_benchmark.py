#!/usr/bin/env python3
"""Validate paired packing timings; Spike timings are not hardware evidence."""
import argparse,json,re,statistics
from pathlib import Path
SHAPES=((3,3,3),(15,15,15),(21,21,21),(63,63,63),(111,111,111),(135,135,135),(111,15,15),(111,1,111))
def analyze(text,platform):
 checks=[json.loads(x) for x in re.findall(r'ILLIXR_PACKING_TEST (.*)',text)]
 if len(checks)!=1 or checks[0].get('passed') is not True or checks[0].get('failures')!=0 or checks[0].get('checks',0)<=0:raise ValueError('Missing or failed packing equivalence test')
 if 'ILLIXR_PACKING_BENCH_ERROR' in text:raise ValueError('Benchmark execution error')
 rows=[json.loads(x) for x in re.findall(r'ILLIXR_PACKING_BENCH (.*)',text)]
 expected={(m,n,k,l,b,t) for m,n,k in SHAPES for l in range(4) for b in (0,1) for t in range(1,9)};seen=set();groups={}
 for r in rows:
  key=tuple(r[k] for k in ('m','n','k','layout','beta','trial'))
  if key not in expected or key in seen:raise ValueError('Duplicate or unexpected benchmark case')
  seen.add(key)
  for field in ('pack_cycles','unpack_cycles'):
   v=r[field]
   if len(v)!=3 or any(type(x) is not int or x<=0 for x in v):raise ValueError('Invalid cycle samples')
  groups.setdefault(key[:-1],[]).append(r)
 if seen!=expected:raise ValueError('Incomplete benchmark matrix')
 summary=[]
 for key,group in sorted(groups.items()):
  item=dict(zip(('m','n','k','layout','beta'),key))
  for field in ('pack_cycles','unpack_cycles'):
   med=[statistics.median(r[field][i] for r in group) for i in range(3)];item[field]=dict(zip(('scalar','rvv_rows','rvv_contiguous'),med))
  total=[statistics.median(r['pack_cycles'][i]+r['unpack_cycles'][i] for r in group) for i in range(3)]
  item.update(total_cycles=dict(zip(('scalar','rvv_rows','rvv_contiguous'),total)),rvv_rows_speedup=total[0]/total[1],rvv_contiguous_speedup=total[0]/total[2],faster_rvv_traversal='rows' if total[1]<=total[2] else 'contiguous');summary.append(item)
 return {'passed':True,'platform':platform,'hardware_performance_evidence':platform=='firesim','checks':checks[0],'samples':len(rows),'timing':'rdcycle on hart 0; timer interrupts enabled; one warmup and eight alternating trials per case','cases':summary}
def main():
 p=argparse.ArgumentParser();p.add_argument('console',type=Path);p.add_argument('--platform',choices=('spike','firesim'),required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();result=analyze(a.console.read_text(),a.platform);a.output.write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
