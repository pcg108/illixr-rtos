#!/usr/bin/env python3
"""Prepare pinned XRSight-RTOS software dependencies without changing Chipyard.

Fresh setup: --work DIR [--vector]
Offline reuse: add --reuse-deps EXISTING_DEPS --openblas-source EXISTING_OPENBLAS.
Dependencies remain outside the application checkout. The vector tree is private;
only its build-system multilib selection is patched, not Zephyr scheduling.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'config/dependencies.json'


def run(command, **kwargs):
    return subprocess.run([str(x) for x in command], check=True, **kwargs)


def output(command):
    return subprocess.check_output([str(x) for x in command], text=True).strip()


def revision(path, expected):
    actual = output(['git', '-C', path, 'rev-parse', 'HEAD'])
    if actual != expected:
        raise ValueError(f'{path}: expected revision {expected}, found {actual}')


def validate_source(path, metadata, patch=None, require_clean=False):
    revision(path, metadata['revision'])
    # Exclude build artifacts but do not silently accept a modified dependency.
    changes = output(['git', '-C', path, 'status', '--porcelain',
                      '--untracked-files=normal' if require_clean else '--untracked-files=no'])
    if patch:
        run(['git', '-C', path, 'apply', '--reverse', '--check', patch],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        actual = subprocess.check_output(['git', '-C', str(path), 'diff', 'HEAD'])
        if actual != patch.read_bytes():
            raise ValueError(f'{path}: changes differ from the supported patch')
    elif changes:
        raise ValueError(f'{path}: expected an unmodified pinned checkout')


def link_existing(source, destination):
    source = source.resolve(strict=True)
    if destination.is_symlink() or destination.exists():
        if destination.resolve() != source:
            raise ValueError(f'Refusing to replace {destination}')
    else:
        destination.symlink_to(source, target_is_directory=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--vector', action='store_true', help='prepare RVV-compatible Zephyr tree')
    parser.add_argument('--reuse-deps', type=Path,
                        help='reuse an existing pinned base dependency root without downloading')
    parser.add_argument('--openblas-source', type=Path,
                        help='reuse a clean pinned OpenBLAS checkout without downloading')
    args = parser.parse_args()
    spec = json.loads(MANIFEST.read_text())
    work = args.work.expanduser().resolve()
    work.mkdir(parents=True, exist_ok=True)
    deps = work / 'deps'
    if args.reuse_deps:
        link_existing(args.reuse_deps, deps)
    else:
        env = os.environ.copy()
        env['ILLIXR_RTOS_WORK'] = str(work)
        run(['bash', ROOT / 'scripts/setup_spike_deps.sh'], env=env)
    for name in ('zephyr', 'opencv', 'modules/lib/eigen'):
        meta = spec['sources'][name]
        validate_source(deps / name, meta, ROOT / meta['patch'] if 'patch' in meta else None)
    sdk = deps / ('zephyr-sdk-' + spec['zephyr_sdk']['version'])
    if (sdk / 'sdk_version').read_text().strip() != spec['zephyr_sdk']['version']:
        raise ValueError('Zephyr SDK version mismatch')
    cc = sdk / 'riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc'
    if output([cc, '-dumpfullversion']) != spec['zephyr_sdk']['compiler_version']:
        raise ValueError('Zephyr SDK compiler version mismatch')
    blas = work / 'openblas-source'
    meta = spec['sources']['openblas']
    if args.openblas_source:
        link_existing(args.openblas_source, blas)
    elif not blas.exists():
        run(['git', 'init', blas])
        run(['git', '-C', blas, 'remote', 'add', 'origin', meta['url']])
        run(['git', '-C', blas, 'fetch', '--depth=1', 'origin', meta['revision']])
        run(['git', '-C', blas, 'checkout', '--detach', meta['revision']])
    validate_source(blas, meta, require_clean=True)
    selected = deps
    if args.vector:
        selected = work / 'vector-deps'
        selected.mkdir(exist_ok=True)
        destination = selected / 'zephyr'
        if not destination.exists():
            run([sys.executable, ROOT / 'scripts/prepare_vector_zephyr.py',
                 '--source', deps / 'zephyr', '--destination', destination])
        validate_source(destination, spec['sources']['zephyr'],
                        ROOT / spec['sources']['zephyr']['vector_patch'])
        for name in ('modules', 'opencv', sdk.name):
            link_existing(deps / name, selected / name)
    report = {
        'format_version': 1,
        'dependency_manifest_sha256': hashlib.sha256(MANIFEST.read_bytes()).hexdigest(),
        'dependencies': str(selected), 'base_dependencies': str(deps),
        'zephyr_base': str(selected / 'zephyr'), 'zephyr_sdk': str(sdk),
        'openblas_source': str(blas),
        'sysroot': str(sdk / 'riscv64-zephyr-elf/riscv64-zephyr-elf'),
        'source_revisions': {k: v['revision'] for k, v in spec['sources'].items()},
        'patches': {name: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                    for name, path in [('opencv', spec['sources']['opencv']['patch'])] +
                    ([('zephyr', spec['sources']['zephyr']['vector_patch'])] if args.vector else [])},
    }
    (work / 'software-dependencies.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
