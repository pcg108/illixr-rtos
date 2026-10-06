#!/usr/bin/env python3
"""Validate full-precision Spike estimator snapshots and compare exact streams."""
import argparse,hashlib,json,struct,math
from pathlib import Path

def inspect(path):
 poses=[];counts={1:0,2:0};first_init=None
 with path.open('rb') as f:
  def take(n):
   b=f.read(n)
   if len(b)!=n:raise ValueError('Truncated state stream at '+str(f.tell()))
   return b
  def unpack(fmt):return struct.unpack(fmt,take(struct.calcsize(fmt)))
  def finite(n):
   v=unpack('<'+str(n)+'d')
   if not all(math.isfinite(x) for x in v):raise ValueError('Nonfinite state')
   return v
  if take(8)!=b'VIOSTAT1':raise ValueError('Unsupported stream')
  count,=unpack('<Q')
  for event in range(count):
   eid,kind,index,ns,initialized=unpack('<QIIqQ')
   if eid!=event or kind not in counts or index!=counts[kind] or initialized not in (0,1):raise ValueError('Ordering/header mismatch')
   counts[kind]+=1;values=finite(17);timestamp,*state=values
   if initialized and first_init is None:first_init={'event':event,'kind':kind,'index':index,'ns':ns}
   if initialized and abs(sum(v*v for v in state[12:16])-1)>1e-5:raise ValueError('Quaternion normalization')
   if kind==2:
    dim,=unpack('<Q')
    if not 15<=dim<=4096:raise ValueError('Invalid covariance dimension')
    finite(dim*dim);finite(225);nc,=unpack('<Q')
    if dim!=15+6*nc:raise ValueError('Clone dimension mismatch')
    for _ in range(nc):finite(9)
    nf,=unpack('<Q')
    if nf>100000:raise ValueError('Invalid feature count')
    for _ in range(nf):
     fid,flags,no=unpack('<QQQ')
     if flags&~3 or no>100000:raise ValueError('Invalid feature')
     if flags&1:finite(3)
     finite(no*5)
    if initialized:poses.append({'camera':index,'ns':ns,'position':state[:3],'quaternion_xyzw':state[12:16]})
  if f.read(1):raise ValueError('Trailing bytes')
 h=hashlib.sha256()
 with path.open('rb') as f:
  for b in iter(lambda:f.read(8*1024**2),b''):h.update(b)
 return {'events':count,'imus':counts[1],'cameras':counts[2],'poses':len(poses),'first_initialization':first_init,'sha256':h.hexdigest(),'trajectory':poses}

def main():
 p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args();r=inspect(a.reference);c=inspect(a.candidate);same=True;offset=0;first=None
 with a.reference.open('rb') as x,a.candidate.open('rb') as y:
  while True:
   b=x.read(1024**2);d=y.read(1024**2)
   if b!=d:
    same=False;first=offset+next((i for i,(v,w) in enumerate(zip(b,d)) if v!=w),min(len(b),len(d)));break
   if not b:break
   offset+=len(b)
 report={'passed':same,'exact_bytes':same,'first_differing_byte':first,'reference':r,'candidate':c};a.output.write_text(json.dumps(report,indent=2)+'\n');print('exact state agreement:',same);raise SystemExit(0 if same else 1)
if __name__=='__main__':main()
