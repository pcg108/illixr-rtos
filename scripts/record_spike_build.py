#!/usr/bin/env python3
"""Save immutable artifact and source fingerprints for a Spike build."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys

repo, deps, artifact = map(Path, sys.argv[1:])
def git(path, *args):
    return subprocess.check_output(['git', '-C', str(path), *args])
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

paths = git(repo, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')
dependency_patches = {}
for name in ('zephyr', 'opencv', 'modules/lib/eigen'):
    patch = git(deps / name, 'diff', '--binary', 'HEAD')
    if patch:
        path = artifact / (name.replace('/', '-') + '.patch')
        path.write_bytes(patch)
        dependency_patches[name] = {'file': path.name, 'sha256': sha(path)}
state = {
    'toolchain': {
        'sdk_version': (deps / 'zephyr-sdk-0.17.0/sdk_version').read_text().strip(),
        'compiler_version': subprocess.check_output([
            str(deps / 'zephyr-sdk-0.17.0/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-g++'),
            '--version']).decode().splitlines()[0],
        'runtime_package_sha256': 'cd97784c88de0207c93cf386f79d8d2606b46598dc74d4ad12cadd5617595964',
    },
    'application_commit': git(repo, 'rev-parse', 'HEAD').decode().strip(),
    'source_sha256': {p: sha(repo / p) for p in paths if p and (repo / p).is_file()},
    'dependency_commits': {p: git(deps / p, 'rev-parse', 'HEAD').decode().strip()
                           for p in ('zephyr', 'opencv', 'modules/lib/eigen')},
    'dependency_patches': dependency_patches,
    'artifact_sha256': {p.name: sha(p) for p in artifact.iterdir() if p.is_file()
                       and p.name != 'build_manifest.json'},
}
cache = {}
for line in (artifact / 'CMakeCache.txt').read_text().splitlines():
    if line.startswith(('#','//')) or '=' not in line: continue
    key,value=line.split('=',1);cache[key.split(':',1)[0]]=value
state['hpm']={'enabled':cache.get('ILLIXR_HPM_PROFILE','OFF').upper() in ('ON','1','TRUE','YES'),
              'event_map':'rocket-hpm-v1','programmable_counters':13,'attribution':'scheduled_context_exclusive'}
if 'ILLIXR_HPM_PROFILE' in cache:
    from hpm_build_audit import audit as audit_hpm_build
    audit=audit_hpm_build(Path(cache['CMAKE_CACHEFILE_DIR'])/'build.ninja',state['hpm']['enabled'])
    audit_path=artifact/'hpm-compile-audit.json'
    audit_path.write_text(json.dumps(audit,indent=2)+'\n')
    state['hpm']['compile_audit']={'file':audit_path.name,'sha256':sha(audit_path),'passed':True}
    state['artifact_sha256'][audit_path.name]=sha(audit_path)
backend=cache.get('ILLIXR_LINALG_BACKEND','eigen')
state['linalg']={'backend':backend,'gemmini_packing':cache.get('ILLIXR_GEMMINI_PACKING','scalar'),'gemmini_packing_traversal':cache.get('ILLIXR_GEMMINI_PACKING_TRAVERSAL','rows')}
state['linalg']['packing_saturn_compat']=cache.get('ILLIXR_PACKING_SATURN_COMPAT','OFF').upper() in ('ON','1','TRUE','YES')
if backend != 'eigen':
    archive=Path(cache['ILLIXR_OPENBLAS_ARCHIVE'])
    provenance=archive.parent.parent/'manifest.json'
    identity=json.loads(provenance.read_text())
    if identity['backend']!=backend or identity['sha256']!=sha(archive):
        raise ValueError('OpenBLAS archive identity mismatch')
    state['linalg'].update(identity)
(artifact / 'build_manifest.json').write_text(json.dumps(state, indent=2) + '\n')
