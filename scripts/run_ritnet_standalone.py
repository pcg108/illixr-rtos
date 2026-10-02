#!/usr/bin/env python3
"""Run either required RTL RITNet gate, preserving numerical and routing evidence."""
import argparse,hashlib,json,re,shutil,sys
from pathlib import Path
from run_rocket import simulator_command,capture
from trace_batches import decode_console
from analyze_spike import records,startup_checks

ROUTE=re.compile(r'RITNET_ROUTE array=(int8|fp32) count=\s*(\d+) opcode=\s*(\d+) funct=\s*(\d+)\r?\n')
def routing(text,mode):
    events=[];chunks=[];cursor=removed=0
    for m in ROUTE.finditer(text):
        chunks.append(text[cursor:m.start()]);cursor=m.end()
        array,count,opcode,funct=m.groups()
        events.append(dict(array=array,count=int(count),opcode=int(opcode),funct=int(funct),console_offset=m.start()-removed))
        removed+=m.end()-m.start()
    chunks.append(text[cursor:]);clean=''.join(chunks);errors=[];counts={'int8':0,'fp32':0}
    for e in events:
        counts[e['array']]+=1
        if e['count']!=counts[e['array']]:errors.append('Missing/duplicate accelerator command counter')
        if e['opcode']!={'int8':0x5b,'fp32':0x7b}[e['array']]:errors.append('Command accepted by wrong array')
    if not counts['int8'] or (mode=='dual' and not counts['fp32']) or (mode=='int8' and counts['fp32']):errors.append('Missing/unexpected array execution')
    # First inference is deliberately exclusive; second overlaps FP32 requests.
    begin=clean.find('RITNET_ROUTE_PHASE int8_begin 0\n');end=clean.find('RITNET_ROUTE_PHASE int8_end 0 ')
    if begin<0 or end<=begin:errors.append('Missing exclusive RITNet phase')
    else:
        selected=[e for e in events if begin<=e['console_offset']<=clean.find('\n',end)]
        if not selected or any(e['array']!='int8' for e in selected):errors.append('Exclusive RITNet phase routed incorrectly')
    if mode=='dual':
        first=clean.find('RITNET_ROUTE_PHASE fp32_begin\n');last=clean.find('RITNET_ROUTE_PHASE fp32_end pass\n')
        selected=[e for e in events if first<=e['console_offset']<=clean.find('\n',last)]
        if first<0 or last<=first or not selected or any(e['array']!='fp32' for e in selected):errors.append('Exclusive FP32 phase routed incorrectly')
    return clean,{'counts':counts,'commands':events,'errors':list(dict.fromkeys(errors))}

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--mode',choices=['int8','dual'],required=True);p.add_argument('--simulator',type=Path,required=True);p.add_argument('--elf',type=Path,required=True);p.add_argument('--chipyard',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=False);shutil.copy2(a.elf,a.output/'firmware.elf')
    result={'status':'running','mode':a.mode,'elf_sha256':sha(a.elf),'simulator_sha256':sha(a.simulator),'counter_semantics':'Verilator target cycles','cycle_limit':100_000_000_000,'watchdog_seconds':86400}
    def save():(a.output/'run.json').write_text(json.dumps(result,indent=2)+'\n')
    cmd=simulator_command(a.simulator,(a.output/'firmware.elf').resolve(),a.chipyard,100_000_000_000);result['command']=cmd;save()
    try:
        execution=capture(cmd,a.output,86400);result.update(execution)
        raw=(a.output/'console.log').read_text(errors='replace');clean,route=routing(raw,a.mode)
        (a.output/'routing.json').write_text(json.dumps(route,indent=2)+'\n')
        clean,transfer=decode_console(clean);(a.output/'decoded.log').write_text(clean)
        data=records(a.output/'decoded.log');errors=list(route['errors']);errors+=startup_checks(data,1,1_000_000,1_000_000_000,True)
        complete=execution['returncode']==0 and 'ILLIXR_RITNET_STANDALONE_END pass' in clean and not any(execution[k] for k in ['timed_out','interrupted','cycle_limit_reached'])
        if not complete:errors.append('No complete normal successful HTIF exit')
        from ritnet_validation import standalone_errors
        errors.extend(standalone_errors(data,clean,1,a.mode=='dual'))
        reference=json.loads((Path(__file__).resolve().parents[1]/'third_party/ritnet/reference/manifest.json').read_text())
        results=data['eye_results']
        if len(results)!=2:errors.append('Expected two completed inferences')
        for r in results:
            if not r['valid'] or r['accelerator_hart']!=0 or r['output_hash']!=reference['output_hash']:errors.append('Inference result/reference/hart mismatch')
        if len(re.findall(r'RITNET_ROUTE_PHASE int8_end [01] mismatches=0 valid=1 ',clean))!=2:errors.append('Missing exact full-tensor comparisons')
        if execution['fatal_markers']:errors.append('Simulator fatal assertion')
        result.update(status='pass' if not errors else 'fail' if complete else 'incomplete',passed=not errors,complete=complete,errors=errors,route_counts=route['counts'],trace_transfer=transfer)
    except BaseException as e:
        result.update(status='incomplete',passed=False,complete=False,error=repr(e));save();raise
    save();return 0 if result['passed'] else 1
if __name__=='__main__':sys.exit(main())
