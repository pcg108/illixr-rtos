#!/usr/bin/env python3
"""Build the pinned BLAS-only embedded archive in an isolated output directory."""
import argparse, hashlib, json, os, pathlib, subprocess, shutil, shlex
from prepare_gemmini_openblas import adapt
PIN = '6d12fcab91ebf4de5eb23c1218727d2551b8635e'
def gcc13_rvv_trmm_workaround(build, archive, cc):
 # GCC 13.2's optimized VSETVL pass loses a scalar VL definition on TRMM
 # odd-size tail paths. Keep these eight intrinsic kernels at O0; they still
 # execute RVV. GEMM/GEMV and the rest of BLAS retain O2. No source/math edits.
 version = subprocess.check_output([cc, '-dumpfullversion'], text=True).strip()
 if version != '13.2.0':
  raise ValueError('RVV compiler changed: revalidate the TRMM compiler workaround')
 names = {p+'trmm_kernel_'+side+trans+'.c' for p in 'ds' for side in 'LR' for trans in 'NT'}
 commands = json.loads((build/'compile_commands.json').read_text())
 records = []
 directory = archive.parent/'gcc13-trmm-workaround'
 directory.mkdir(exist_ok=True)
 ar = subprocess.check_output([cc, '-print-prog-name=ar'], text=True).strip()
 for item in commands:
  source = pathlib.Path(item['file'])
  if source.name not in names: continue
  command = shlex.split(item['command'])
  obj = directory/(source.name+'.obj')
  command[command.index('-o')+1] = str(obj)
  command += ['-O0']
  subprocess.run(command, cwd=item['directory'], check=True)
  subprocess.run([ar, 'r', str(archive), str(obj)], check=True)
  records.append({'source':str(source),'command':command,'object_sha256':hashlib.sha256(obj.read_bytes()).hexdigest()})
 if len(records) != len(names): raise ValueError('Missing RVV TRMM workaround kernels')
 subprocess.run([ar, 's', str(archive)], check=True)
 return records

def main():
 p=argparse.ArgumentParser();p.add_argument('--source',type=pathlib.Path,required=True);p.add_argument('--build',type=pathlib.Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--sysroot',required=True);p.add_argument('--backend',choices=['openblas_scalar','openblas_rvv','openblas_gemmini_fp32'],required=True);p.add_argument('--jobs',type=int,default=2);p.add_argument('--gemmini-params',type=pathlib.Path);a=p.parse_args()
 if subprocess.check_output(['git','-C',str(a.source),'rev-parse','HEAD'],text=True).strip()!=PIN: raise SystemExit('OpenBLAS revision mismatch')
 if subprocess.check_output(['git','-C',str(a.source),'status','--porcelain'],text=True).strip(): raise SystemExit('OpenBLAS source must be clean')
 a.build.mkdir(parents=True,exist_ok=True)
 vector=a.backend in ('openblas_rvv','openblas_gemmini_fp32');gemmini=a.backend=='openblas_gemmini_fp32'
 target='RISCV64_ZVL256B' if vector else 'RISCV64_GENERIC';march='rv64gcv_zvl256b' if vector else 'rv64imafdc_zicsr_zifencei'
 adaptation={}
 if gemmini:
  if not a.gemmini_params or not a.gemmini_params.is_file(): raise SystemExit('Generated Gemmini parameters required')
  source=a.build/'source'
  if source.exists(): raise SystemExit('Use a fresh Gemmini build directory')
  shutil.copytree(a.source,source,ignore=shutil.ignore_patterns('.git'))
  adaptation=adapt(source)
  a.source=source
  headers=a.build/'include/gemmini';headers.mkdir(parents=True)
  for h in (source/'gemmini').glob('*.h'): shutil.copy2(h,headers/h.name)
  shutil.copy2(a.gemmini_params,headers/'gemmini_params.h')
 t=a.build/'toolchain.cmake';t.write_text(f'''set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER "{a.cc}")
set(CMAKE_SYSROOT "{a.sysroot}")
set(CMAKE_C_FLAGS_INIT "-march={march} -mabi=lp64d -mcmodel=medany -DOS_EMBEDDED -DnDEBUG_PRINT_NAME -ffunction-sections -fdata-sections -fno-fast-math {'-DGEMMINI_BACKEND=1' if gemmini else ''}")
''')
 command=['/usr/bin/cmake','-S',str(a.source),'-B',str(a.build),'-G','Ninja','-DCMAKE_TOOLCHAIN_FILE='+str(t),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG -fno-fast-math','-DTARGET='+target,'-DDYNAMIC_ARCH=OFF','-DUSE_THREAD=OFF','-DUSE_OPENMP=OFF','-DNOFORTRAN=ON','-DBUILD_WITHOUT_LAPACK=ON','-DBUILD_WITHOUT_CBLAS=ON','-DBUILD_TESTING=OFF','-DBUILD_BENCHMARKS=OFF','-DBUILD_SINGLE=ON','-DBUILD_DOUBLE=ON','-DBUILD_COMPLEX=OFF','-DBUILD_COMPLEX16=OFF','-DBUILD_STATIC_LIBS=ON','-DBUILD_SHARED_LIBS=OFF','-DINTERFACE64=OFF','-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
 subprocess.run(command,check=True)
 subprocess.run(['/usr/bin/cmake','--build',str(a.build),'--target','openblas_static','--parallel',str(a.jobs)],check=True)
 lib=a.build/'lib/libopenblas.a'
 if not lib.exists():
  libs=list(a.build.glob('lib/*.a'))
  if len(libs)!=1: raise SystemExit('Cannot identify static archive')
  lib=libs[0]
 # Embedded memory.c also exports libc stubs (puts/getenv/printf). Remove
 # desktop runtime objects entirely; the application supplies checked hooks.
 original=lib
 lib=a.build/'lib/libopenblas-zephyr.a'
 shutil.copyfile(original,lib)
 ar=subprocess.check_output([a.cc,'-print-prog-name=ar'],text=True).strip()
 removed=['memory.c.obj','xerbla.c.obj','openblas_env.c.obj']
 subprocess.run([ar,'d',str(lib),*removed],check=True)
 subprocess.run([ar,'s',str(lib)],check=True)
 workaround=gcc13_rvv_trmm_workaround(a.build,lib,a.cc) if vector else []
 (a.build/'manifest.json').write_text(json.dumps(dict(backend=a.backend,revision=PIN,compiler=a.cc,compiler_version=subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],sysroot=a.sysroot,target=target,archive=str(lib),removed_runtime_objects=removed,sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),configure_command=command,rvv_trmm_compiler_workaround=workaround,gemmini_adaptation=adaptation,gemmini_params_sha256=hashlib.sha256(a.gemmini_params.read_bytes()).hexdigest() if gemmini else None,gemmini_include=str(a.build/'include') if gemmini else None,computation_precision='fp32-gemm-gemv' if gemmini else 'native'),indent=2)+'\n')
 print('OpenBLAS archive:',lib)
if __name__=='__main__': main()
