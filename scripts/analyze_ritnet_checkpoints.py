#!/usr/bin/env python3
"""Locate completed RITNet tensor divergence; metadata is not numerical evidence."""
import argparse,hashlib,json,struct
from collections import Counter
from pathlib import Path

def views_overlap(a,b):
 if not a.get('address') or not b.get('address') or not a['rows'] or not b['rows']:return False
 ap=a['address'];bp=b['address'];aw=a['cols']*a['element_bytes'];bw=b['cols']*b['element_bytes']
 astep=a['stride']*a['element_bytes'];bstep=b['stride']*b['element_bytes']
 if ap+(a['rows']-1)*astep+aw<=bp or bp+(b['rows']-1)*bstep+bw<=ap:return False
 i=j=0
 while i<a['rows'] and j<b['rows']:
  x=ap+i*astep;y=bp+j*bstep
  if x<y+bw and y<x+aw:return True
  if x+aw<=y:i+=1
  else:j+=1
 return False

def read_trace(path):
 out={'config':[],'operations':[],'capture':[],'post':[]}
 for line in Path(path).read_text(errors='replace').splitlines():
  for tag,key in [('ILLIXR_RITNET_DIAG ','config'),('ILLIXR_RITNET_OP ','operations'),('ILLIXR_RITNET_CAPTURE ','capture'),('ILLIXR_RITNET_POST ','post')]:
   if line.startswith(tag):out[key].append(json.loads(line[len(tag):]))
 return out

def analyze(data,reference):
 golden=[json.loads(s) for s in (Path(reference)/'operations.jsonl').read_text().splitlines()]
 errors=[];first=None;ops=data['operations'];counts={};last={};completed=0
 if len(data['config'])!=1:return dict(passed=False,errors=['Expected one diagnostic configuration'])
 cfg=data['config'][0];mode=cfg['mode']
 if mode not in (1,2,3):errors.append('Unsupported diagnostic mode')
 if cfg['records']!=len(ops):errors.append('Incomplete record stream')
 if cfg['error'] not in (0,5):errors.append('Diagnostic overflow/integrity/capture failure: '+str(cfg['error']))
 for r in ops:
  if first is not None:errors.append('Operation recorded after first divergence')
  identity=r['inference'];id=r['id'];expected_id=last.get(identity,0)+1
  if id!=expected_id or identity<1 or identity>32 or not 1<=id<=len(golden):errors.append('Duplicate/missing/reordered operation identity');continue
  if identity>1 and (last.get(identity-1)!=len(golden)):errors.append('Inference started before previous complete sequence')
  last[identity]=id;counts[identity]=counts.get(identity,0)+1;g=golden[id-1]
  if r['name']!=g['name'] or r['kind']!=g['kind']:errors.append('Operation catalog changed')
  if any({k:v for k,v in v.items() if k!='address'}!=want for v,want in zip(r['views'],g['views'])) or len(r['views'])!=4:errors.append('Tensor geometry changed')
  if r['parameters']!=g['parameters']:errors.append('Operation scalar parameters changed')
  if r['hart']!=0 or r['end_hart']!=0 or r['end_cycle']<r['begin_cycle']:errors.append('Invalid execution hart/time')
  if r['mode']!=mode:errors.append('Mixed diagnostic modes')
  if mode==1 and (r['drained'] or any(r['input_hash']) or r['output_hash']):errors.append('Metadata-only control read/drained a tensor')
  if mode==2 and (any(r['input_hash']) or r['output_hash']):errors.append('Drain-only control read a tensor')
  if mode==2 and r['drained']!=bool(cfg['drain_mask']&(1<<(id-1))):errors.append('Wrong selected drain boundary')
  if mode==3:
   if not r['drained']:errors.append('Unfinished tensor compared')
   inputs=r['input_hash']==g['input_hash'];output=r['output_hash']==g['output_hash']
   if r['image_hash']!=g['image_hash'] or r['immutable_hash']!=g['immutable_hash']:errors.append('Input image or immutable asset fingerprint differs')
   mismatch=sum((a!=b)<<i for i,(a,b) in enumerate(zip(r['input_hash'],g['input_hash'])))|((not output)<<3)|((bool(r['guard_error']))<<4)
   if r['mismatch']&31!=mismatch:errors.append('Mismatch flags disagree with fingerprints')
   if (not inputs or not output or r['guard_error']) and first is None:
    first=dict(inference=identity,id=id,name=r['name'],kind=r['kind'],inputs_match=inputs,output_matches=output,guard_error=r['guard_error'],views=r['views'],image_changed_on_recheck=bool(r['mismatch']&32),immutable_changed_on_recheck=bool(r['mismatch']&64))
   completed+=1
 if not ops:errors.append('No operation records')
 if mode==3 and bool(first)!=(cfg['error']==5):errors.append('First divergence and stopping status disagree')
 if cfg['error']==0 and any(v!=len(golden) for v in counts.values()):errors.append('Missing final operation checkpoint')
 post=data.get('post',[]);observations=[]
 if cfg.get('post_records',0)!=len(post):errors.append('Incomplete post-failure observations')
 if post and [r['id'] for r in post]!=[31,32]:errors.append('Invalid post-failure observation set')
 for r in post:
  id=r['id'];identity=r['inference']
  if not 1<=id<=len(golden) or identity!=max(counts,default=0):errors.append('Invalid post-failure identity');continue
  g=golden[id-1]
  sources=[o for o in ops if o['inference']==identity and o['id']==id]
  if len(sources)!=1:errors.append('Missing original post-failure descriptor')
  elif any(views_overlap(v,later['views'][3]) for v in sources[0]['views'] for later in ops if later['inference']==identity and later['id']>id):errors.append('Post-failure reference view was overwritten by a later operation')
  end=max((o['end_cycle'] for o in ops if o['inference']==identity),default=0)
  if (r['observation']!='after_failed_inference' or not r['drained'] or r['hart'] or r['end_hart'] or
      r['begin_cycle']<end or r['end_cycle']<r['begin_cycle']):errors.append('Post-failure snapshot observed before completion')
  inputs=r['input_hash']==g['input_hash'];output=r['output_hash']==g['output_hash']
  image=r['image_hash']==g['image_hash'];immutable=r['immutable_hash']==g['immutable_hash']
  mismatch=sum((a!=b)<<i for i,(a,b) in enumerate(zip(r['input_hash'],g['input_hash'])))|((not output)<<3)|((bool(r['guard_error']))<<4)|((not image)<<5)|((not immutable)<<6)
  if len(r['input_hash'])!=3 or mismatch!=r['mismatch']:errors.append('Post-failure flags disagree with fingerprints')
  observations.append(dict(inference=identity,id=id,name=g['name'],inputs_match=inputs,output_matches=output,image_matches=image,immutable_matches=immutable,guard_error=r['guard_error'],scope='Completed-inference state; does not establish what the accelerator read earlier'))
 capture=bytearray()
 for c in data['capture']:
  if c['offset']!=len(capture):errors.append('Missing/overlapping capture chunk');break
  capture.extend(bytes.fromhex(c['hex']))
 if len(capture)!=cfg['capture_size'] or len(capture)>4*1024*1024:errors.append('Capture size/bounds mismatch')
 difference=None
 if capture:
  id=cfg['capture_operation'];expected=(Path(reference)/f'op{id:02d}-output0.bin').read_bytes()
  if len(expected)!=len(capture):errors.append('Capture/reference shape mismatch')
  else:
   differing=[i for i,(a,b) in enumerate(zip(expected,capture)) if a!=b]
   if differing:
    i=differing[0];v=golden[id-1]['views'][3];width=v['element_bytes'];cols=v['cols'];index=i//width
    def number(data,at):return int.from_bytes(data[at:at+width],'little',signed=True)
    delta=[number(capture,k)-number(expected,k) for k in range(0,len(capture),width) if capture[k:k+width]!=expected[k:k+width]]
    difference=dict(mismatched_bytes=len(differing),mismatched_elements=len(delta),first_byte=i,row=index//cols,column=index%cols,expected=number(expected,index*width),actual=number(capture,index*width),minimum_delta=min(delta),maximum_delta=max(delta),mean_absolute_delta=sum(abs(d) for d in delta)/len(delta),delta_histogram=dict(sorted(Counter(delta).items())))
 post_bad=any(not all(r[k] for k in ('inputs_match','output_matches','image_matches','immutable_matches')) or r['guard_error'] for r in observations)
 return dict(passed=not errors and first is None and not post_bad,trace_valid=not errors,errors=errors,mode=mode,inferences=len(counts),completed_tensor_checks=completed,first_divergence=first,post_failure_observations=observations,capture_difference=difference,capture_sha256=hashlib.sha256(capture).hexdigest() if capture else None,overhead_cycles=sum(r['overhead_cycles'] for r in ops),numerical_coverage='completed tensors' if mode==3 else ('completed-inference snapshots only' if post else 'none: control mode'))

def main():
 p=argparse.ArgumentParser();p.add_argument('--trace',type=Path,required=True);p.add_argument('--reference',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();d=read_trace(a.trace);r=analyze(d,a.reference);a.output.write_text(json.dumps(r,indent=2)+'\n')
 if d['capture']:(a.output.parent/'captured-tensor.bin').write_bytes(b''.join(bytes.fromhex(x['hex']) for x in d['capture']))
 print(json.dumps(r,indent=2));return 0 if r['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
