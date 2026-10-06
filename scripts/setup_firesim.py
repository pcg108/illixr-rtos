#!/usr/bin/env python3
"""Prepare the pinned XRSight FireSim sources and local U250 manager inputs.

Never clones, checks out revisions, builds, programs, or starts simulations.
JSON-formatted .yaml files are valid YAML and contain fully expanded paths.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

REPO = Path(__file__).resolve().parents[1]
DATA = REPO / 'config/firesim'


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1048576), b''):
            h.update(block)
    return h.hexdigest()


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def manifest():
    return json.loads((DATA / 'manifest.json').read_text())


def same_or_new(path, content):
    if path.is_symlink():
        raise ValueError(f'Refusing destination symlink: {path}')
    if path.exists() and path.read_bytes() != content:
        raise ValueError(f'Conflicting existing file: {path}')


def install(args):
    root = args.chipyard.resolve()
    info = manifest()
    for name, entry in info['components'].items():
        actual = git(root / entry['path'], 'rev-parse', 'HEAD')
        if actual != entry['revision']:
            raise ValueError(f'{name}: expected {entry["revision"]}, got {actual}. '
                             'Select the pinned revision in a separate checkout first.')
    plans = []
    for entry in info['patches']:
        patch = REPO / entry['file']
        if digest(patch) != entry['sha256']:
            raise ValueError(f'Package patch hash mismatch: {patch}')
        component = root / info['components'][entry['component']]['path']
        statuses = []
        for name, hashes in entry['files'].items():
            actual = digest(component / name)
            if actual not in hashes.values():
                raise ValueError(f'Unrecognized local changes: {component / name}')
            statuses.append('applied' if actual == hashes['after'] else 'pending')
        if len(set(statuses)) != 1:
            raise ValueError(f'Partially applied patch; preserve and resolve before setup: {patch}')
        applied = statuses[0] == 'applied'
        command = ['git', '-C', str(component), 'apply', '--check']
        if applied:
            command += ['--reverse']
        subprocess.run([*command, str(patch)], check=True)
        plans.append((component, patch, applied))
    copies = []
    for entry in info['scala_files']:
        source = REPO / entry['source']
        if digest(source) != entry['sha256']:
            raise ValueError(f'Package source hash mismatch: {source}')
        dest = root / entry['destination']
        if not dest.resolve().is_relative_to(root):
            raise ValueError(f'Configuration destination escapes checkout: {dest}')
        data = source.read_bytes()
        same_or_new(dest, data)
        copies.append((dest, data))
    source = (args.gemmini_source or root / 'generators/gemmini').resolve()
    with tempfile.TemporaryDirectory(prefix='xrsight-gemmini-') as tmp:
        exported = Path(tmp) / 'export'
        subprocess.run([sys.executable, str(REPO / 'scripts/prepare_gemmini_generator.py'),
                        '--source', str(source), '--destination', str(exported)], check=True)
        destination = root / 'generators/gemmini/src/main/scala/illixr_fp32_gemmini'
        if not destination.resolve().is_relative_to(root):
            raise ValueError(f'Gemmini destination escapes checkout: {destination}')
        expected = {p.relative_to(exported) for p in exported.rglob('*') if p.is_file()}
        if destination.exists():
            extra = {p.relative_to(destination) for p in destination.rglob('*') if p.is_file()} - expected
            if extra:
                raise ValueError(f'Unexpected files in generated namespace: {sorted(map(str, extra))}')
        for relative in sorted(expected):
            data = (exported / relative).read_bytes()
            same_or_new(destination / relative, data)
            copies.append((destination / relative, data))
    report = {'chipyard': str(root), 'checked_revisions': info['components'],
              'patches': [{'path': str(p), 'already_applied': a} for _, p, a in plans],
              'managed_files': {str(p.relative_to(root)): hashlib.sha256(b).hexdigest() for p, b in copies},
              'check_only': args.check}
    if (root / 'xrsight-firesim-install.json').is_symlink():
        raise ValueError('Refusing installation record symlink')
    if not args.check:
        for component, patch, applied in plans:
            if not applied:
                subprocess.run(['git', '-C', str(component), 'apply', str(patch)], check=True)
        for path, data in copies:
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        # Only this installer owns this record. Existing files were validated above.
        (root / 'xrsight-firesim-install.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


def render(name, values):
    def replace(value):
        if isinstance(value, str):
            if value.startswith('@') and value.endswith('@'):
                return values[value[1:-1]]
            return value
        if isinstance(value, list):
            return [replace(v) for v in value]
        if isinstance(value, dict):
            return {values.get(k[1:-1], k) if k.startswith('@') and k.endswith('@') else k: replace(v)
                    for k, v in value.items()}
        return value
    return replace(json.loads((DATA / (name + '.in')).read_text()))


def configure(args):
    root, work = args.chipyard.resolve(), args.work.resolve()
    info = manifest()
    if args.config not in info['configurations']:
        raise ValueError(f'Unknown config; choose from {list(info["configurations"])}')
    cfg = info['configurations'][args.config]
    target = cfg['target_config']
    deploy = root / 'sims/firesim/deploy'
    if not (deploy / 'firesim').is_file():
        raise ValueError('FireSim deployment checkout is not initialized')
    name = args.workload_name
    if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
        raise ValueError('Workload name must contain only letters, digits, hyphens, underscores')
    if not args.build_only and (args.elf is None or args.fpga_db is None):
        raise ValueError('Runtime preparation requires --elf and --fpga-db; use --build-only before firmware exists')
    elf = args.elf.resolve() if args.elf else None
    hardware = json.loads(args.hardware_manifest.read_text()) if args.hardware_manifest else None
    if hardware and (hardware.get('config') != args.config or not hardware.get('hardware_verified') or not hardware.get('timing_closed')):
        raise ValueError('Hardware manifest configuration or acceptance does not match')
    if hardware:
        from run_firesim_matrix import validate_hardware
        hardware = validate_hardware(args.hardware_manifest.resolve(), cfg['harts'])
    values = {'ALIAS': args.config, 'TARGET': target, 'DEPLOY': str(deploy),
              'MAKEFRAG': str(root / 'generators/firechip/chip/src/main/makefrag/firesim'),
              'BUILD_RECIPE': str(deploy / 'build-farm-recipes/externally_provisioned.yaml'),
              'BIT_BUILDER': str(deploy / 'bit-builder-recipes/xilinx_alveo_u250.yaml'),
              'RUN_RECIPE': str(deploy / 'run-farm-recipes/externally_provisioned.yaml'),
              'BUILD_DIR': str(work / 'builds'), 'SIM_DIR': str(work / 'runfarm'),
              'FPGA_DB': str(args.fpga_db.resolve()) if args.fpga_db else None, 'WORKLOAD': name,
              'WORKLOAD_JSON': name + '.json',
              'BITSTREAM': Path(hardware['bitstream']).resolve().as_uri() if hardware else None,
              'DRIVER_TAR': Path(hardware['driver_tar']).resolve().as_uri() if hardware else None,
              'QUINTUPLET': hardware['deploy_quintuplet'] if hardware else None,
              'RUNTIME_CONF': hardware['runtime_conf_bundle_name'] if hardware else None}
    outputs = {file: (json.dumps(render(file, values), indent=2) + '\n').encode() for file in
               ['config_build.yaml', 'config_build_recipes.yaml', 'config_hwdb.yaml', 'config_runtime.yaml', 'workload.json']}
    if args.build_only:
        outputs = {k:v for k,v in outputs.items() if k in ['config_build.yaml', 'config_build_recipes.yaml']}
    outputs['config_hwdb_build.yaml'] = b'{}\n'
    artifacts = elf.parent if elf else None
    build_path = artifacts / 'build_manifest.json' if artifacts else None
    case = None
    if build_path and build_path.exists():
        build = json.loads(build_path.read_text())
        t = build['target']
        if t['harts'] != cfg['harts']:
            raise ValueError('ELF hart count differs from hardware configuration')
        case = {'name': name, 'elf': str(elf), 'harts': cfg['harts'],
                'placement': 'pinned' if t['placement'] == 'pinned' else 'unpinned',
                'platform_check': t['platform_check_only'], 'modeled_clock_scale': t.get('modeled_clock_scale', 1),
                'ticks_per_sec': t['ticks_per_sec'], 'hardware_config': args.config,
                'timeout_seconds': 86400, 'max_cycles': 100000000000,
                'memory_profile_interval_cycles': 1000000, 'zero_out_dram': True,
                'output': str(work), 'linalg_backend': build['linalg']['backend'],
                'require_eye': build.get('ritnet', {}).get('enabled', False)}
        from firmware_profile import verified_pipeline
        pipeline = verified_pipeline(artifacts, build, required=True)
        case['require_eye'] = pipeline['require_eye'] and not case['platform_check']
        case['require_gpu'] = pipeline['require_gpu'] and not case['platform_check']
        if hardware:
            from run_firesim_matrix import validate_firmware
            case['hardware_manifest'] = str(args.hardware_manifest.resolve())
            validate_firmware(case, hardware)
        outputs['case.json'] = (json.dumps(case, indent=2) + '\n').encode()
    if hardware and not case and not args.build_only:
        raise ValueError('Runnable configuration requires a recorded build_manifest.json beside the ELF')
    quintuplet = 'xilinx_alveo_u250-firesim-FireSim-' + target + '-BaseXilinxAlveoU250Config'
    make = ['make', '-C', str(root / 'sims/firesim/sim'), 'PLATFORM=xilinx_alveo_u250',
            'TARGET_PROJECT=firesim', 'TARGET_PROJECT_MAKEFRAG=' + values['MAKEFRAG'],
            'DESIGN=FireSim', 'TARGET_CONFIG=' + target,
            'PLATFORM_CONFIG=BaseXilinxAlveoU250Config', 'verilog']
    commands = {'elaborate': make,
                'build': ['firesim', 'buildbitstream', '-b', str(work / 'config_build.yaml'), '-r', str(work / 'config_build_recipes.yaml'), '-a', str(work / 'config_hwdb_build.yaml')],
                'staging_dir': str(root / 'sims/firesim-staging/generated-src' / ('firechip.chip.FireSim.' + target)),
                'generated_rtl': str(root / 'sims/firesim/sim/generated-src/xilinx_alveo_u250' / quintuplet / 'FireSim-generated.sv'),
                'generated_fp32_header': str(root / 'gemmini_params_illixr.h') if cfg['fp32_gemmini'] else None,
                'generated_int8_header': str(root / 'gemmini_params_illixr_int8.h') if cfg['int8_gemmini'] else None}
    for task in ['infrasetup', 'runworkload']:
        commands[task] = ['firesim', task, '-c', str(work / 'config_runtime.yaml'), '-a', str(work / 'config_hwdb.yaml'), '-r', str(work / 'config_build_recipes.yaml')]
    outputs['commands.json'] = (json.dumps(commands, indent=2) + '\n').encode()
    stamp = work / 'prepare-manifest.json'
    if stamp.is_symlink():
        raise ValueError('Refusing preparation manifest symlink')
    previous = json.loads(stamp.read_text()) if stamp.exists() else {}
    for name_, data in outputs.items():
        p = work / name_
        if p.is_symlink():
            raise ValueError(f'Refusing managed output symlink: {p}')
        if p.exists() and p.read_bytes() != data and digest(p) != previous.get('files', {}).get(name_):
            raise ValueError(f'Refusing to replace modified/unowned configuration: {p}')
    links = {} if args.build_only else {deploy / 'workloads' / (name + '.json'): work / 'workload.json',
             deploy / 'workloads' / name: work / 'inputs', work / 'inputs/zephyr.elf': elf}
    for link, target_path in links.items():
        if (link.exists() or link.is_symlink()) and (not link.is_symlink() or link.resolve() != target_path):
            raise ValueError(f'Conflicting workload link: {link}')
    work.mkdir(parents=True, exist_ok=True)
    (work / 'inputs').mkdir(exist_ok=True)
    for name_, data in outputs.items():
        (work / name_).write_bytes(data)
    for link, target_path in links.items():
        link.parent.mkdir(parents=True, exist_ok=True)
        if not link.is_symlink():
            link.symlink_to(target_path)
    result = {'config': args.config, 'chipyard': str(root), 'work': str(work),
              'ready_for_runtime': bool(hardware and case), 'hardware_manifest': str(args.hardware_manifest.resolve()) if hardware else None,
              'files': {k: hashlib.sha256(v).hexdigest() for k, v in outputs.items()},
              'warning': None if hardware and case else 'Build preparation only: rerun configure with recorded firmware and accepted --hardware-manifest before infrasetup.'}
    stamp.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


def package_driver(args):
    # Reuse the existing immutable bundle format, without depending on a scratch env.sh.
    from run_firesim_matrix import validate_hardware
    hardware_path = args.hardware.resolve()
    h = json.loads(hardware_path.read_text())
    validate_hardware(hardware_path, h['harts'], require_bundle=False)
    root = args.chipyard.resolve()
    deploy = root / 'sims/firesim/deploy'
    sys.path.insert(0, str(deploy))
    from runtools.utils import get_local_shared_libraries
    contents = [(Path(h['driver']), Path(h['driver']).name)]
    contents += [(Path(src), name) for src, name in get_local_shared_libraries(h['driver'])]
    runtime_name = 'illixr-' + {1: 'single', 2: 'dual', 4: 'quad'}[h['harts']] + '-runtime.conf'
    contents += [(Path(h['runtime_conf']), runtime_name)]
    if len(set(name for _, name in contents)) != len(contents) or any(Path(name).name != name for _, name in contents):
        raise ValueError('Unsafe/duplicate driver archive member')
    out = args.output_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    inputs = {name: {'path': str(p.resolve()), 'sha256': digest(p)} for p, name in contents}
    key = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()[:16]
    archive = out / ('driver-' + key + '.tar.gz')
    if not archive.exists():
        import gzip
        with archive.open('xb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as gz:
            with tarfile.open(fileobj=gz, mode='w', dereference=True) as tar:
                for path, name in sorted(contents, key=lambda pair: pair[1]):
                    entry = tar.gettarinfo(str(path), arcname=name)
                    entry.uid = entry.gid = entry.mtime = 0
                    entry.uname = entry.gname = ''
                    with path.open('rb') as stream:
                        tar.addfile(entry, stream)
    with tarfile.open(archive) as tar:
        for name, item in inputs.items():
            stream = tar.extractfile(name)
            if stream is None or hashlib.sha256(stream.read()).hexdigest() != item['sha256']:
                raise ValueError(f'Cached driver archive changed: {name}')
    target = root / 'sims/firesim/sim/custom-runtime-configs' / runtime_name
    same_or_new(target, Path(h['runtime_conf']).read_bytes())
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(Path(h['runtime_conf']).read_bytes())
    h.update(driver_tar=str(archive), driver_tar_sha256=digest(archive), runtime_conf_bundle_name=runtime_name)
    hardware_path.write_text(json.dumps(h, indent=2) + '\n')
    (out / ('driver-' + key + '.json')).write_text(json.dumps(inputs, indent=2) + '\n')
    validate_hardware(hardware_path, h['harts'])
    print(archive)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    q = sub.add_parser('install', help='Install only audited sources into an explicitly pinned checkout')
    q.add_argument('--chipyard', type=Path, required=True)
    q.add_argument('--gemmini-source', type=Path)
    q.add_argument('--check', action='store_true', help='Validate revisions and conflicts without modifying checkout')
    q.set_defaults(func=install)
    q = sub.add_parser('configure', help='Render local U250 build/runtime/workload files')
    q.add_argument('--chipyard', type=Path, required=True)
    q.add_argument('--work', type=Path, required=True)
    q.add_argument('--config', default=manifest()['canonical_config'], choices=list(manifest()['configurations']))
    q.add_argument('--elf', type=Path)
    q.add_argument('--fpga-db', type=Path)
    q.add_argument('--build-only', action='store_true', help='Prepare build recipes before ELF/board discovery exists')
    q.add_argument('--hardware-manifest', type=Path)
    q.add_argument('--workload-name', default='xrsight-rtos')
    q.set_defaults(func=configure)
    q = sub.add_parser('package-driver', help='Package the reviewed matching host driver and runtime configuration')
    q.add_argument('--chipyard', type=Path, required=True)
    q.add_argument('--hardware', type=Path, required=True)
    q.add_argument('--output-dir', type=Path, required=True)
    q.set_defaults(func=package_driver)
    args = p.parse_args()
    try:
        args.func(args)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        p.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
