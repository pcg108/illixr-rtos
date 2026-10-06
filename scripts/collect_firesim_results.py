#!/usr/bin/env python3
"""Record one FireSim command, or collect one existing run without FPGA access.

Collection requires explicit firmware, hardware, dataset and native-reference
paths. It preserves input evidence and uses the matrix runner's full analyzer.
"""
import argparse
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys

import run_firesim_matrix as firesim
from firmware_profile import verified_pipeline


def read_object(path):
    value = json.loads(Path(path).read_text())
    if not isinstance(value, dict):
        raise ValueError(f'Expected a JSON object: {path}')
    return value


def runtime_slot(directory):
    """Accept a configured work directory, a runfarm, or one simulator slot."""
    for path in (directory / 'runfarm/sim_slot_0', directory / 'sim_slot_0', directory):
        if (path / 'uartlog').is_file():
            return path
    # Preserve incomplete runs even when execution produced no UART file.
    return directory / 'runfarm/sim_slot_0'


def execution_metadata(document):
    execution = dict(document.get('manager_execution', {}).get('runworkload', {}))
    fields = ('returncode', 'timed_out', 'interrupted', 'cycle_limit_reached',
              'fatal_markers', 'host_elapsed_seconds', 'stage')
    execution.update({key: document[key] for key in fields if key in document})
    required = fields[:-1]
    missing = [key for key in required if key not in execution]
    if missing:
        raise ValueError('Execution metadata must explicitly record: ' + ', '.join(missing))
    if execution['returncode'] is not None and type(execution['returncode']) is not int:
        raise ValueError('Execution returncode must be an integer or null')
    for key in ('timed_out', 'interrupted', 'cycle_limit_reached'):
        if type(execution[key]) is not bool:
            raise ValueError(f'Execution {key} must be a JSON boolean')
    if (not isinstance(execution['fatal_markers'], list) or
            any(not isinstance(marker, str) for marker in execution['fatal_markers'])):
        raise ValueError('Execution fatal_markers must be a list of strings')
    elapsed = execution['host_elapsed_seconds']
    if type(elapsed) not in (int, float) or not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError('Execution host_elapsed_seconds must be finite and nonnegative')
    execution.setdefault('stage', 'runworkload')
    return execution


def case_metadata(document, firmware, output, hardware_path):
    build = read_object(firmware / 'build_manifest.json')
    target = build['target']
    case = dict(document)
    expected = {'harts': target['harts'],
                'placement': 'pinned' if target['placement'] == 'pinned' else 'unpinned',
                'platform_check': target['platform_check_only'],
                'modeled_clock_scale': target.get('modeled_clock_scale', 1)}
    if 'ticks_per_sec' in target:
        expected['ticks_per_sec'] = target['ticks_per_sec']
    for key, value in expected.items():
        if key in case and case[key] != value:
            raise ValueError(f'Case {key} differs from firmware build manifest')
        case[key] = value
    if case['harts'] not in (1, 2, 4) or type(case['platform_check']) is not bool:
        raise ValueError('Unsupported hart count or invalid preflight mode')
    cache_path = firmware / 'CMakeCache.txt'
    cache = {}
    if cache_path.is_file():
        for line in cache_path.read_text().splitlines():
            if '=' in line and not line.startswith(('#', '//')):
                key, value = line.split('=', 1)
                cache[key.split(':', 1)[0]] = value
    enabled = lambda key: cache.get(key, '').upper() in ('1', 'ON', 'TRUE', 'YES')
    pipeline = verified_pipeline(firmware, build)
    # The caller cannot accidentally omit validation for compiled-in stages.
    case['require_hpm'] = bool(build.get('hpm', {}).get('enabled', False))
    case['require_eye'] = bool(case.get('require_eye') or
                               (not case['platform_check'] and (build.get('ritnet', {}).get('enabled') or
                                (pipeline and pipeline['require_eye']))))
    case['require_gpu'] = bool(case.get('require_gpu') or enabled('ILLIXR_GPU_PIPELINE') or
                               enabled('ILLIXR_GPU_PIPELINE_ENABLED') or case['require_eye'] or
                               (not case['platform_check'] and pipeline and pipeline['require_gpu']))
    if build.get('linalg', {}).get('backend'):
        backend = build['linalg']['backend']
        if case.get('linalg_backend', backend) != backend:
            raise ValueError('Case linear algebra backend differs from firmware')
        case['linalg_backend'] = backend
    for key in ('memory_profile_interval_cycles', 'max_cycles', 'timeout_seconds'):
        if type(case.get(key)) is not int or case[key] <= 0:
            raise ValueError(f'Case must record a positive integer {key}')
    case.update(elf=str(firmware / 'zephyr.elf'), hardware_manifest=str(hardware_path),
                output=str(output))
    case.setdefault('name', output.name)
    return case


def verify_dataset(dataset, manifest):
    files = manifest.get('files')
    if not isinstance(files, dict) or not files:
        raise ValueError('Dataset manifest lacks source-file hashes')
    for relative, identity in files.items():
        path = (dataset / relative).resolve()
        if not path.is_relative_to(dataset):
            raise ValueError(f'Dataset manifest path escapes dataset root: {relative}')
        if firesim.sha256(path) != identity['sha256']:
            raise ValueError(f'Native dataset differs from embedded input: {relative}')
    return len(files)


def resolve_hardware_paths(value, base):
    """Relative artifact paths in a portable manifest are relative to that file."""
    path_keys = {'path', 'dts', 'bitstream', 'driver', 'driver_tar', 'runtime_conf'}
    if isinstance(value, dict):
        return {key: str((base / item).resolve())
                if key in path_keys and isinstance(item, str) and item
                else resolve_hardware_paths(item, base) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve_hardware_paths(item, base) for item in value]
    return value


def collect(args):
    runtime = args.runtime_dir.resolve()
    firmware = args.firmware_dir.resolve()
    output = args.output.resolve()
    if output == runtime:
        raise ValueError('Collection output must differ from the runtime directory')
    if output.exists() and any(output.iterdir()):
        raise ValueError(f'Refusing to overwrite existing results: {output}')
    output.mkdir(parents=True, exist_ok=True)
    metadata = {'backend': 'firesim-u250', 'status': 'collecting',
                'collected_utc': firesim.utcnow(), 'runtime_dir': str(runtime),
                'firmware_dir': str(firmware), 'output': str(output),
                'collection_only': True, 'input_sha256': {}}
    result = {'passed': False, 'complete': False, 'errors': []}
    try:
        case_path = (args.case or runtime / 'case.json').resolve()
        execution_path = args.execution
        if execution_path is None:
            execution_path = runtime / 'execution.json'
            if not execution_path.is_file():
                execution_path = runtime / 'run.json'
        execution_path = execution_path.resolve()
        hardware_path = args.hardware_manifest.resolve()
        sources = {'case.input.json': case_path, 'execution.input.json': execution_path,
                   'hardware_manifest.json': hardware_path,
                   'firmware_build_manifest.json': firmware / 'build_manifest.json',
                   'dataset_manifest.json': firmware / 'dataset_manifest.json',
                   'zephyr.config': firmware / '.config', 'zephyr.dts': firmware / 'zephyr.dts'}
        if (firmware / 'profile.yaml').is_file():
            sources['profile.yaml'] = firmware / 'profile.yaml'
        for name, source in sources.items():
            shutil.copy2(source, output / name)
            metadata['input_sha256'][name] = firesim.sha256(output / name)
        slot = output / 'runfarm/sim_slot_0'
        slot.mkdir(parents=True)
        source_slot = runtime_slot(runtime)
        for source in [source_slot / 'uartlog', *sorted(source_slot.glob('memory_stats*.csv'))]:
            if source.is_file():
                shutil.copy2(source, slot / source.name)
                metadata['input_sha256'][str((slot / source.name).relative_to(output))] = firesim.sha256(slot / source.name)
        for source in sorted(runtime.glob('manager-*.log')):
            shutil.copy2(source, output / source.name)
        execution_document = read_object(execution_path)
        execution = execution_metadata(execution_document)
        case = case_metadata(read_object(case_path), firmware, output, hardware_path)
        metadata.update(case)
        metadata['elf_sha256'] = firesim.sha256(firmware / 'zephyr.elf')
        for key, actual in (('elf_sha256', metadata['elf_sha256']),
                            ('hardware_manifest_sha256', firesim.sha256(hardware_path))):
            if execution_document.get(key, actual) != actual:
                raise ValueError(f'Collected {key} differs from recorded execution')
        hardware = resolve_hardware_paths(read_object(hardware_path), hardware_path.parent)
        resolved = output / 'hardware_manifest.resolved.json'
        firesim.write_json(resolved, hardware)
        firesim.validate_hardware(resolved, case['harts'], require_bundle=False)
        firesim.validate_firmware(case, hardware)
        if not case['platform_check']:
            if not args.native.is_file():
                raise ValueError(f'Native estimator reference does not exist: {args.native}')
            args.dataset = args.dataset.resolve()
            metadata['dataset_files_verified'] = verify_dataset(args.dataset, read_object(firmware / 'dataset_manifest.json'))
        if execution['host_elapsed_seconds'] > case['timeout_seconds']:
            execution['timed_out'] = True
        firesim.write_json(output / 'case.json', case)
        result = firesim.analyze_case(case, hardware, output, execution, args)
        if result.get('firesim_target_cycles', 0) >= case['max_cycles']:
            result['errors'].append('Recorded target cycles reached the configured cycle limit')
            result.update(passed=False, complete=False)
        metadata.update(execution)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        result = {'passed': False, 'complete': False, 'errors': [str(error)]}
    metadata['status'] = 'pass' if result['passed'] else ('fail' if result['complete'] else 'incomplete')
    metadata['finished_utc'] = firesim.utcnow()
    firesim.write_json(output / 'run.json', metadata)
    firesim.write_json(output / 'analysis.json', result)
    print(json.dumps({'status': metadata['status'], 'analysis': str(output / 'analysis.json'),
                      'errors': result['errors']}, indent=2))
    return 0 if result['passed'] else 1


def manager_command(command, directory):
    """Keep manager cwd separate from the runtime monitored by capture_manager."""
    if not directory.is_dir():
        raise ValueError(f'FireSim deployment directory does not exist: {directory}')
    return [sys.executable, '-c',
            'import os,sys; os.chdir(sys.argv[1]); os.execvp(sys.argv[2], sys.argv[2:])',
            str(directory), *command]


def record(args):
    directory = args.runtime_dir.resolve()
    execution_path = args.execution.resolve()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        raise ValueError('record requires a manager command after --')
    launch = manager_command(command, args.manager_dir.resolve())
    if args.timeout <= 0:
        raise ValueError('Watchdog timeout must be positive')
    if execution_path.exists() or (directory / 'manager-runworkload.log').exists():
        raise ValueError('Refusing to overwrite an earlier execution record or manager log')
    directory.mkdir(parents=True, exist_ok=True)
    execution = {'command': command, 'launch_command': launch,
                 'manager_dir': str(args.manager_dir.resolve()),
                 'started_utc': firesim.utcnow(), 'stage': 'runworkload',
                 'timeout_seconds': args.timeout, 'returncode': None, 'timed_out': False,
                 'interrupted': False, 'cycle_limit_reached': False, 'fatal_markers': [],
                 'host_elapsed_seconds': 0.0}
    case_path = directory / 'case.json'
    if case_path.is_file():
        case = read_object(case_path)
        for key, field in (('elf_sha256', 'elf'), ('hardware_manifest_sha256', 'hardware_manifest')):
            if case.get(field):
                execution[key] = firesim.sha256(case[field])
    firesim.write_json(execution_path, execution)
    try:
        execution.update(firesim.capture_manager(launch, directory, 'runworkload', args.timeout))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        execution['fatal_markers'].append(str(error))
    execution['finished_utc'] = firesim.utcnow()
    firesim.write_json(execution_path, execution)
    return 0 if (execution['returncode'] == 0 and not any(execution[key] for key in
                ('timed_out', 'interrupted', 'cycle_limit_reached', 'fatal_markers'))) else 1


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] not in ('collect', 'record', '-h', '--help'):
        argv.insert(0, 'collect')
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    collect_parser = commands.add_parser('collect', help='Analyze preserved run evidence; never starts FireSim')
    for name in ('runtime-dir', 'firmware-dir', 'hardware-manifest', 'dataset', 'native', 'output'):
        collect_parser.add_argument('--' + name, type=Path, required=True)
    collect_parser.add_argument('--prediction-native', type=Path)
    collect_parser.add_argument('--case', type=Path, help='Default: runtime-dir/case.json')
    collect_parser.add_argument('--execution', type=Path, help='Default: runtime-dir/execution.json, then run.json')
    record_parser = commands.add_parser('record', help='Run the supplied manager command with a watchdog and record its outcome')
    record_parser.add_argument('--runtime-dir', type=Path, required=True)
    record_parser.add_argument('--manager-dir', type=Path, required=True, help='FireSim deploy directory containing workloads/')
    record_parser.add_argument('--execution', type=Path, required=True)
    record_parser.add_argument('--timeout', type=int, default=86400)
    record_parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args(argv)
    try:
        return record(args) if args.action == 'record' else collect(args)
    except (OSError, ValueError) as error:
        parser.exit(2, f'{parser.prog}: {error}\n')


if __name__ == '__main__':
    sys.exit(main())
