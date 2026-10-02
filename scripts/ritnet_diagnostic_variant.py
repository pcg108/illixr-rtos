#!/usr/bin/env python3
"""Make a same-layout diagnostic ELF by patching initialized selector words."""
import argparse,hashlib,json,shutil
from pathlib import Path

def make(source,destination,mode,inferences=32,mask=(1<<64)-1,capture=(1<<32)-1):
 from elftools.elf.elffile import ELFFile
 assert mode in range(4) and 1<=inferences<=32
 if destination.exists():raise ValueError('Refusing artifact overwrite')
 names={'ritnet_diag_mode':mode,'ritnet_diag_inferences':inferences,'ritnet_diag_drain_mask':mask,'ritnet_diag_capture_operation':capture}
 with (source/'zephyr.elf').open('rb') as f:
  elf=ELFFile(f);tab=elf.get_section_by_name('.symtab');patches=[]
  for name,value in names.items():
   syms=tab.get_symbol_by_name(name);assert syms and len(syms)==1,name
   sym=syms[0];sec=elf.get_section(sym['st_shndx']);assert sec['sh_type']!='SHT_NOBITS'
   patches.append(dict(symbol=name,value=value,address=sym['st_value'],offset=sec['sh_offset']+sym['st_value']-sec['sh_addr'],width=sym['st_size']))
 data=bytearray((source/'zephyr.elf').read_bytes());original=bytes(data)
 for p in patches:data[p['offset']:p['offset']+p['width']]=p['value'].to_bytes(p['width'],'little')
 shutil.copytree(source,destination);(destination/'zephyr.elf').write_bytes(data)
 path=destination/'build_manifest.json';m=json.loads(path.read_text());m['artifact_sha256']['zephyr.elf']=hashlib.sha256(data).hexdigest();m['diagnostic_variant']={'mode':mode,'inferences':inferences,'mask':mask,'patches':patches,'original_sha256':hashlib.sha256(original).hexdigest(),'same_layout':True};path.write_text(json.dumps(m,indent=2)+'\n')
 return m['diagnostic_variant']
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('destination',type=Path);p.add_argument('--mode',type=int,required=True);p.add_argument('--inferences',type=int,default=32);p.add_argument('--mask',type=lambda x:int(x,0),default=(1<<64)-1);p.add_argument('--capture',type=int,default=(1<<32)-1);a=p.parse_args();print(json.dumps(make(a.source,a.destination,a.mode,a.inferences,a.mask,a.capture),indent=2))
