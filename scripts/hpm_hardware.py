"""Inspect generated Rocket CSR banks and pin the event selector meaning."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from hpm_analysis import EVENTS, SELECTORS


def inspect(fir, harts, rocket_source=None):
    modules={};current=None
    with Path(fir).open() as stream:
        for line in stream:
            m=re.match(r'  module (\w+)\s*:',line)
            if m:
                name=m[1]
                current=modules.setdefault(name,{'instances':{},'banks':{},'count':None,'register_widths':{},'counter_inputs':{}}) if re.fullmatch(r'(?:RocketTile|Rocket|CSRFile)(?:_\d+)?',name) else None
            if current is None:continue
            m=re.match(r'    inst (\w+) of (\w+)',line)
            if m:current['instances'][m[1]]=m[2]
            m=re.search(r'counters : \{ eventSel : UInt<64>, flip inc : UInt<1>\}\[(\d+)\]',line)
            if m:current['count']=int(m[1])
            m=re.match(r'    reg(?:reset)? ((?:small|large)(?:_\d+)?) : UInt<(\d+)>',line)
            if m:current['register_widths'][m[1]]=int(m[2])
            m=re.search(r'node \w+ = add\((small(?:_\d+)?), io.counters\[(\d+)\].inc\)',line)
            if m:current['counter_inputs'][int(m[2])]=m[1]
            m=re.match(r'    reg reg_hpmcounter_(\d+)(?:_(?:lo|hi))? : UInt<(\d+)>',line)
            if m:
                index=int(m[1]);current['banks'][index]=current['banks'].get(index,0)+int(m[2])
    rows=[]
    for hart in range(harts):
        tile='RocketTile'+('_'+str(hart) if hart else '')
        if tile not in modules:raise ValueError(f'Missing {tile}')
        cores=[m for m in modules[tile]['instances'].values() if re.fullmatch(r'Rocket(?:_\d+)?',m)]
        if len(cores)!=1:raise ValueError(f'Ambiguous core in {tile}')
        csrs=[m for m in modules[cores[0]]['instances'].values() if re.fullmatch(r'CSRFile(?:_\d+)?',m)]
        if len(csrs)!=1:raise ValueError(f'Ambiguous CSR bank in {cores[0]}')
        bank=modules[csrs[0]]
        for index,small in bank['counter_inputs'].items():
            large='large'+small[len('small'):]
            widths=bank['register_widths']
            if small in widths and large in widths:bank['banks'][index]=widths[small]+widths[large]
        if bank['count']!=13 or bank['banks']!={i:40 for i in range(13)}:
            raise ValueError(f'Hart {hart}: expected thirteen 40-bit HPM counters; found {bank}')
        rows.append({'hart':hart,'tile':tile,'core':cores[0],'csr':csrs[0],'width_bits':40})
    result={'programmable_counters':13,'event_map':'rocket-hpm-v1','width_bits':40,
            'basic_width_bits':64,'events':EVENTS,'selectors':SELECTORS,'harts':rows,
            'firrtl_sha256':hashlib.sha256(Path(fir).read_bytes()).hexdigest()}
    if rocket_source:
        source=Path(rocket_source);text=source.read_text()
        expected=['load-use interlock','long-latency interlock','csr interlock','I$ blocked','D$ blocked',
                  'branch misprediction','control-flow target misprediction','flush','replay','mul/div interlock','fp interlock',
                  'I$ miss','D$ miss']
        selected=text[text.index('("load-use interlock"'):text.index('("D$ release"')]
        names=re.findall(r'\("([^\"]+)", \(\) =>',selected)
        if names!=expected:raise ValueError(f'Rocket event source order changed: {names}')
        result['rocket_source_sha256']=hashlib.sha256(source.read_bytes()).hexdigest()
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--firrtl',required=True,type=Path)
    p.add_argument('--harts',type=int,choices=(1,2,4),required=True);p.add_argument('--rocket-source',type=Path)
    p.add_argument('--output',type=Path);a=p.parse_args()
    result=inspect(a.firrtl,a.harts,a.rocket_source);encoded=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(encoded)
    else:print(encoded,end='')
