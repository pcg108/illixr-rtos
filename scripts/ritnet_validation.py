"""Common numerical checks for RITNet FPGA/RTL standalone console records."""
import json,re
from pathlib import Path

def standalone_errors(data,text,harts,dual,vector_required=False,expected_inferences=2):
 from blas_analysis import vector_preflight_errors
 errors=[]
 if type(expected_inferences) is not int or not 1 <= expected_inferences <= 32:
  return ['Standalone inference count must be an integer from 1 to 32']
 reference=json.loads((Path(__file__).resolve().parents[1]/'third_party/ritnet/reference/manifest.json').read_text())
 if 'ILLIXR_RITNET_STANDALONE_END pass' not in text:errors.append('RITNet standalone did not pass')
 results=data.get('eye_results',[])
 if len(results)!=expected_inferences:errors.append(f'Expected {expected_inferences} standalone eye inferences')
 for i,r in enumerate(results):
  if not r.get('valid') or r.get('accelerator_hart')!=0 or r.get('output_hash')!=reference['output_hash'] or r.get('inference_id')!=i+1 or r.get('cycles',0)<=0:
   errors.append('RITNet standalone output, execution or hart mismatch')
 exact=re.findall(r'RITNET_ROUTE_PHASE int8_end (\d+) mismatches=0 valid=1 ',text)
 if list(map(int,exact))!=list(range(expected_inferences)):errors.append('Exact full-tensor comparisons missing or out of order')
 progress=re.findall(r'RITNET_INTERRUPT_PROGRESS (\d+) (\d+)',text)
 if [int(i) for i,n in progress]!=list(range(expected_inferences)) or any(int(n)<=0 for i,n in progress):errors.append('Missing timer preemption during inference')
 if dual:
  fp32_calls=expected_inferences+1+int(expected_inferences>1)
  if text.count('RITNET_ROUTE_PHASE fp32_end pass')!=fp32_calls:errors.append('FP32 interleaving/overlap fixtures missing')
  operations={r['name']:r for r in data.get('gemmini',[]) if r.get('phase')=='work'}
  for name in ['sgemm','sgemv']:
   r=operations.get(name,{})
   if r.get('submissions')!=fp32_calls or r.get('accelerator_hart_mask')!=1:errors.append('FP32 accelerator fixture execution missing: '+name)
 if vector_required:errors.extend(vector_preflight_errors(data,harts))
 return errors
