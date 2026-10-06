#!/usr/bin/env python3
"""Generate explicit diagnostic boundaries without changing any graph calls.
Run --check to verify checkpoint coverage; generation is intentionally one-time.
"""
import argparse,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def uncomment(s):return re.sub(r'/\*.*?\*/|//[^\n]*',lambda m:' '*len(m[0]),s,flags=re.S)
def calls(source):
 s=uncomment(source);result=[]
 for m in re.finditer(r'\b(tiled_\w+|memcpy|memset)\s*\(',s):
  start=m.end();depth=1;end=start
  while depth:
   depth+=(s[end]=='(')-(s[end]==')');end+=1
  args=[re.sub(r'\s+',' ',v.strip()) for v in s[start:end-1].split(',')]
  result.append((m.start(),end+1,m[1],args))
 return result

def check_coverage(source, catalog):
 ops=calls(source)
 ids=list(map(int,re.findall(r'const struct rd_operation rd_op = \{(\d+),',source)))
 assert ids==list(range(1,len(ops)+1)) and len(ops)==64, 'Missing or duplicate operation boundaries'
 assert source.count('if(rd_end()) return -3;')==len(ops)
 assert len(catalog)==len(ops)
 for (start,end,kind,args),expected in zip(ops,catalog):
  assert (kind,args)==(expected['kind'],expected['arguments']), 'Graph call changed'
  # No preprocessor directive may make the production completion conditional.
  assert re.match(r'\s*gemmini_fence\(\);\s*#ifdef RITNET_DIAGNOSTICS',
                  uncomment(source[end:])), 'Missing unconditional operation fence'
 return len(ops)

def view(pointer,rows,cols,stride,width=1):return '{(const void*)('+pointer+'),'+','.join(map(str,[rows,cols,stride,width]))+'}'
def symbol(p):return re.sub(r'^\([^)]*\)\s*','',p).strip()
def immutable(p,width=1):
 p=symbol(p)
 return view('NULL',0,0,0) if p=='NULL' else view(p,1,'sizeof('+p+')/'+str(width),'sizeof('+p+')/'+str(width),width)
def mul(*v):return '*'.join('('+str(x)+')' for x in v)
def descriptor(kind,a):
 assets=[]
 if kind=='tiled_matmul_auto':
  assert len(a)==24 and a[18:22]==['false']*4
  inputs=[view(a[3],a[0],a[2],a[7]),immutable(a[4]),immutable(a[5],4)]
  out=view(a[6],a[0],a[1],a[10]);name=symbol(a[6]);params=a[:3]+a[7:23];assets=a[4:6]
 elif kind in ('tiled_conv_stride_auto','tiled_conv_auto'):
  if kind=='tiled_conv_auto':a=a[:12]+[a[3],a[4],a[4]]+a[12:]
  assert len(a)==30 and a[15:20]==['false']*5
  def pool(x):return f'(({a[27]})==0?({x}):(({x})+2*({a[28]})-({a[26]}))/({a[27]})+1)'
  inputs=[view(a[20],mul(*a[:3]),a[3],a[12]),immutable(a[21]),immutable(a[22],4)]
  out=view(a[23],mul(a[0],pool(a[5]),pool(a[6])),a[4],a[14]);name=symbol(a[23]);params=a[:20]+a[24:29];assets=a[21:23]
 elif kind=='tiled_conv_dw_auto':
  assert len(a)==19
  def pool(x):return f'(({a[16]})==0?({x}):(({x})+2*({a[17]})-({a[15]}))/({a[16]})+1)'
  inputs=[view(a[9],mul(*a[:3]),a[3],a[3]),immutable(a[10]),immutable(a[11],4)]
  out=view(a[12],mul(a[0],pool(a[4]),pool(a[5])),a[3],a[3]);name=symbol(a[12]);params=a[:9]+a[13:18];assets=a[10:12]
 elif kind=='tiled_resadd_auto':
  assert len(a)==10
  inputs=[view(a[5],a[0],a[1],a[1]),view(a[6],a[0],a[1],a[1]),view('NULL',0,0,0)]
  out=view(a[7],a[0],a[1],a[1]);name=symbol(a[7]);params=a[:5]+[a[8]]
 else:raise ValueError('Unmapped tensor-producing call: '+kind)
 return inputs,out,name,params,[symbol(x) for x in assets if x!='NULL']

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');args=ap.parse_args()
 p=ROOT/'third_party/ritnet/port/ritnet.c';source=p.read_text();ops=calls(source)
 if args.check:
  check_coverage(source,json.loads((p.parent/'diagnostic_operations.json').read_text()))
  print('64 unchanged operations each have an unconditional production fence and diagnostic boundary');return
 assert 'rd_op' not in source,'Already instrumented'
 insertions=[];assets=set();catalog=[]
 for i,(start,end,kind,a) in enumerate(ops,1):
  inputs,out,name,params,immutable_assets=descriptor(kind,a);assets.update(immutable_assets)
  # Model helper scalar arguments use integer/float types. Preserve the actual
  # float rounding in the descriptor rather than an extra double expression.
  values=['(double)(float)('+v+')' for v in params]
  pre='\n/* RITNET_DIAG_BEGIN */\n#ifdef RITNET_DIAGNOSTICS\n    { const struct rd_operation rd_op = {'+str(i)+','+json.dumps(name)+','+json.dumps(kind)+',\n      {'+','.join(inputs)+'},'+out+',\n      {'+','.join(values)+'},'+str(len(params))+'};\n      rd_begin(&rd_op);\n#endif\n'
  post='\n/* Complete this operation before the next tensor consumer or buffer reuse. */\ngemmini_fence();\n#ifdef RITNET_DIAGNOSTICS\n      if(rd_end()) return -3;\n    }\n#endif\n/* RITNET_DIAG_END */\n'
  insertions.extend([(start,pre),(end,post)])
  catalog.append(dict(id=i,name=name,kind=kind,arguments=a,parameter_expressions=params))
 for at,text in sorted(insertions,reverse=True):source=source[:at]+text+source[at:]
 support='\n#ifdef RITNET_DIAGNOSTICS\n#include "diagnostics.h"\nvoid rd_drain(void) { gemmini_fence(); }\nint rd_guards(void) { return ritnet_workspace_check(); }\nuint64_t rd_immutable(void) {\n uint64_t hash=UINT64_C(14695981039346656037);\n'
 for name in sorted(assets):support+=' hash=(hash ^ rd_hash((struct rd_view){'+name+',1,sizeof('+name+'),sizeof('+name+'),1}))*UINT64_C(1099511628211);\n'
 support+=' return hash;\n}\n#endif\n'
 pos=source.index('__attribute__((weak))');source=source[:pos]+support+source[pos:]
 source=source.replace('    unsigned stage = 0;','    unsigned stage = 0;\n#ifdef RITNET_DIAGNOSTICS\n    if(rd_inference_begin(input,160*240)) return -3;\n#endif')
 source=source.replace('    if(ritnet_workspace_check()) return -2;','    if(ritnet_workspace_check()) return -2;\n#ifdef RITNET_DIAGNOSTICS\n    rd_inference_end();\n#endif')
 p.write_text(source)
 (p.parent/'diagnostic_operations.json').write_text(json.dumps(catalog,indent=2)+'\n')
 print('Instrumented',len(ops),'operations;',len(assets),'immutable assets')
if __name__=='__main__':main()
