"""Common numerical checks for RITNet FPGA/RTL standalone console records."""
import json,re
from pathlib import Path

def standalone_errors(data,text,harts,dual,vector_required=False):
 from blas_analysis import vector_preflight_errors
 errors=[]
 reference=json.loads((Path(__file__).resolve().parents[1]/'third_party/ritnet/reference/manifest.json').read_text())
 if 'ILLIXR_RITNET_STANDALONE_END pass' not in text:errors.append('RITNet standalone did not pass')
 results=data.get('eye_results',[])
 if len(results)!=2:errors.append('Expected two standalone eye inferences')
 for i,r in enumerate(results):
  if not r.get('valid') or r.get('accelerator_hart')!=0 or r.get('output_hash')!=reference['output_hash'] or r.get('inference_id')!=i+1 or r.get('cycles',0)<=0:
   errors.append('RITNet standalone output, execution or hart mismatch')
 if len(re.findall(r'RITNET_ROUTE_PHASE int8_end [01] mismatches=0 valid=1 ',text))!=2:errors.append('Exact full-tensor comparisons missing')
 progress=re.findall(r'RITNET_INTERRUPT_PROGRESS ([01]) (\d+)',text)
 if len(progress)!=2 or {i for i,n in progress}!={'0','1'} or any(int(n)<=0 for i,n in progress):errors.append('Missing timer preemption during inference')
 if dual:
  if text.count('RITNET_ROUTE_PHASE fp32_end pass')!=4:errors.append('FP32 interleaving/overlap fixtures missing')
  operations={r['name']:r for r in data.get('gemmini',[]) if r.get('phase')=='work'}
  for name in ['sgemm','sgemv']:
   r=operations.get(name,{})
   if r.get('submissions')!=4 or r.get('accelerator_hart_mask')!=1:errors.append('FP32 accelerator fixture execution missing: '+name)
 if vector_required:errors.extend(vector_preflight_errors(data,harts))
 return errors
