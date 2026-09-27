#!/usr/bin/env python3
"""Run the bounded Rocket validation matrix on a gated local FireSim U250 build.

Preparation and reporting never program hardware. Execution requires a verified
hardware/timing manifest and an idle board, and uses only the isolated snapshot.
"""
import argparse
import csv
from datetime import datetime, timezone
import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shlex
import shutil
import signal
import subprocess
import sys
import tarfile
import time

from analyze_spike import analyze, records, startup_checks
from run_rocket import preserve_inputs
from run_rocket_matrix import attempts, markdown, next_output, read_json
from run_spike import git_revision, sha256, stop, prediction_reference


DEFAULT_WORK = Path('/scratch/prashanth_illixr_firesim_20260926')
DEFAULT_ARTIFACTS = Path('/home/prashanth/illixr-rocket-work/artifacts')
MODES = {1: 'single', 2: 'dual', 4: 'quad'}
MAX_CYCLES = 100_000_000_000
WATCHDOG = 86_400
MEMORY_PROFILE_INTERVAL = 1_000_000
ZERO_OUT_DRAM = True
ANSI = re.compile(r'\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))')
FATAL = ('ZEPHYR FATAL ERROR', 'Halting system', '*** FAILED ***', '%Error:')


def utcnow():
    return datetime.now(timezone.utc).isoformat()


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + '\n')
    temporary.replace(path)


def normalize_uart(raw):
    """Remove terminal framing while retaining every firmware trace record."""
    text = raw.decode('utf-8', errors='replace') if isinstance(raw, bytes) else raw
    return ANSI.sub('', text).replace('\r\n', '\n').replace('\r', '\n')


def cases_for(work, artifacts):
    cases = []
    for harts, mode in MODES.items():
        variants = [('preflight', 'unpinned', True), ('scheduler-50', 'unpinned', False)]
        if harts > 1:
            variants.append(('pinned-50', 'pinned', False))
        for suffix, placement, preflight in variants:
            name = f'firesim-{mode}-{suffix}'
            cases.append({'name': name, 'harts': harts, 'placement': placement,
                'platform_check': preflight,
                'elf': str(artifacts / f'rocket-{mode}-{suffix}' / 'zephyr.elf'),
                'hardware_manifest': str(work / 'control' / f'hardware-{harts}.json'),
                'output': str(work / 'results' / (name + '-1')),
                'timeout_seconds': WATCHDOG, 'max_cycles': MAX_CYCLES,
                'memory_profile_interval_cycles': MEMORY_PROFILE_INTERVAL,
                'zero_out_dram': ZERO_OUT_DRAM})
    return cases


def validate_firmware(case, hardware=None):
    elf = Path(case['elf'])
    build = json.loads((elf.parent / 'build_manifest.json').read_text())
    target = build['target']
    expected_placement = 'pinned' if case['placement'] == 'pinned' else 'scheduler'
    if (target['harts'] != case['harts'] or target['placement'] != expected_placement or
            target['platform_check_only'] is not case['platform_check']):
        raise ValueError('Firmware hart count, placement, or preflight mode does not match the case')
    # Existing firmware and embedded dataset must remain the exact approved build.
    for name in ('zephyr.elf', '.config', 'zephyr.dts', 'dataset_manifest.json'):
        if build['artifact_sha256'].get(name) != sha256(elf.parent / name):
            raise ValueError(f'Immutable firmware artifact differs from its build manifest: {name}')
    if hardware and target['timer_hz'] != hardware['timer_hz']:
        raise ValueError('Firmware timer frequency differs from verified FireSim hardware')
    if not case['platform_check']:
        dataset = json.loads((elf.parent / 'dataset_manifest.json').read_text())
        if dataset.get('camera_pairs') != 50 or dataset.get('imu_samples') != 501:
            raise ValueError('FireSim validation requires exactly 50 stereo pairs / 501 IMUs')
    return build


def validate_host_interface(hardware):
    """Validate host polling cadence without changing target clock settings."""
    interface = hardware.get('host_interface') or {}
    fields = {'fesvr_step_size_cycles': 'fesvr-step-size', 'idle_counts': 'idle-counts',
              'wait_ticks': 'fesvr-wait-ticks'}
    for field in fields:
        if type(interface.get(field)) is not int or not 1 <= interface[field] <= 2**31 - 1:
            raise ValueError(f'Host interface {field} must be a positive signed-32-bit value')
    evidence = interface.get('generated_header') or {}
    if not evidence.get('path') or evidence.get('sha256') != sha256(evidence['path']):
        raise ValueError('Host interface generated reset evidence is missing or changed')
    reset_default = interface.get('reset_default_cycles')
    reset_maximum = interface.get('reset_max_cycles')
    if (type(reset_default) is not int or type(reset_maximum) is not int or
            not 0 < reset_default <= reset_maximum):
        raise ValueError('Host interface reset limits are missing or invalid')
    startup_wait = interface['fesvr_step_size_cycles'] * interface['wait_ticks']
    if interface.get('startup_wait_cycles') != startup_wait or startup_wait <= reset_maximum:
        raise ValueError('Host interface startup wait must exceed the generated reset maximum')
    return [f'+{argument}={interface[field]}' for field, argument in fields.items()]


def validate_memory_timing(hardware):
    """Check requested FASED settings against the reviewed compiled model."""
    timing = hardware.get('memory_timing') or {}
    validation = timing.get('rtl_runtime_validation') or {}
    registers = validation.get('registers') or {}
    fields = {'readMaxReqs': 'max_reads', 'writeMaxReqs': 'max_writes',
              'readLatency': 'read_latency_cycles', 'writeLatency': 'write_latency_cycles'}
    if not hardware.get('runtime_conf') or set(registers) != set(fields):
        raise ValueError('Memory timing requires a runtime configuration and compiled register validation')
    for name in ('rtl', 'elaboration_log'):
        evidence = validation.get(name) or {}
        if not evidence.get('path') or evidence.get('sha256') != sha256(evidence['path']):
            raise ValueError(f'Memory timing {name} evidence is missing or changed')
    tokens = shlex.split(Path(hardware['runtime_conf']).read_text(), comments=True)
    expected = ['+mm_useHardwareDefaultRuntimeSettings_0'] + validate_host_interface(hardware)
    for register, field in fields.items():
        values = registers[register]
        names = ('requested_value', 'model_width_bits', 'hardware_default', 'supported_max')
        if any(type(values.get(name)) is not int for name in names):
            raise ValueError(f'Memory timing {register} has incomplete compiled limits')
        requested, width, default, maximum = (values[name] for name in names)
        if not 1 <= width <= 64 or not 1 <= maximum <= (1 << width) - 1:
            raise ValueError(f'Memory timing {register} has invalid compiled limits')
        if not 0 <= default <= maximum or not 1 <= requested <= maximum:
            raise ValueError(f'Memory timing {register} exceeds the compiled model capacity or is zero')
        if timing.get(field) != requested:
            raise ValueError(f'Memory timing {register} disagrees with manifest metadata')
        expected.append(f'+mm_{register}_0={requested}')
    if sorted(tokens) != sorted(expected):
        raise ValueError('Memory timing runtime configuration disagrees with verified register values')


def validate_hardware(path, harts, require_bundle=True):
    hardware = json.loads(path.read_text())
    if hardware.get('hardware_verified') is not True or hardware.get('timing_closed') is not True:
        raise ValueError('Hardware verification and timing closure must both pass before FPGA execution')
    if hardware.get('harts') != harts or hardware.get('hart_ids') != list(range(harts)):
        raise ValueError('Verified hardware hart count or IDs differ from requested configuration')
    if hardware.get('config') != f'illixr_u250_rocket_{MODES[harts]}':
        raise ValueError('Hardware manifest has an unexpected FireSim configuration')
    if (hardware.get('timer_hz') != 500_000 or hardware.get('core_hz') != 500_000_000 or
            hardware.get('memory_base') != 0x80000000 or hardware.get('memory_size') != 0x10000000):
        raise ValueError('Verified FireSim clocks/RAM do not match the preserved Rocket firmware')
    artifacts = ['dts', 'bitstream', 'driver'] + (['driver_tar'] if require_bundle else [])
    if hardware.get('runtime_conf'):
        artifacts.append('runtime_conf')
        if require_bundle and hardware.get('runtime_conf_bundle_name') != f'illixr-{MODES[harts]}-runtime.conf':
            raise ValueError('Driver bundle is missing its verified runtime configuration mapping')
    for key in artifacts:
        source = Path(hardware[key])
        if hardware.get(key + '_sha256') != sha256(source):
            raise ValueError(f'{key} hash differs from the verified hardware manifest')
    validate_memory_timing(hardware)
    if require_bundle:
        try:
            with tarfile.open(hardware['driver_tar']) as archive:
                for name, expected_hash in (
                        (Path(hardware['driver']).name, hardware['driver_sha256']),
                        (hardware['runtime_conf_bundle_name'], hardware['runtime_conf_sha256'])):
                    member = archive.extractfile(name)
                    if member is None or hashlib.sha256(member.read()).hexdigest() != expected_hash:
                        raise ValueError(f'Driver bundle member {name} differs from the verified input')
        except (tarfile.TarError, KeyError) as error:
            raise ValueError('Driver bundle is missing a verified executable or runtime configuration') from error
    return hardware


def package_driver(work, manifest_path, harts):
    """Bundle the verified executable using FireSim's own shared-library list.

    This is a local archive operation, without invoking a manager build or FPGA.
    Providing driver_tar makes FireSim skip both driver compilation and packaging.
    """
    hardware = validate_hardware(manifest_path, harts, require_bundle=False)
    if hardware.get('driver_tar'):
        if hardware.get('runtime_conf') and not hardware.get('runtime_conf_bundle_name'):
            archive = Path(hardware['driver_tar'])
            if sha256(archive) != hardware.get('driver_tar_sha256'):
                raise ValueError('Existing driver bundle hash differs from its manifest')
            name = f'illixr-{MODES[harts]}-runtime.conf'
            with tarfile.open(archive) as tar:
                member = tar.extractfile(name)
                if member is None or hashlib.sha256(member.read()).hexdigest() != hardware['runtime_conf_sha256']:
                    raise ValueError('Existing driver bundle does not contain the verified runtime configuration')
            hardware['runtime_conf_bundle_name'] = name
            symlink_input(work / 'chipyard/sims/firesim/sim/custom-runtime-configs' / name,
                          Path(hardware['runtime_conf']))
            write_json(manifest_path, hardware)
        validate_hardware(manifest_path, harts)
        return hardware
    deploy = work / 'chipyard/sims/firesim/deploy'
    script = (
        'import contextlib,json,sys\n'
        'sys.path.insert(0,sys.argv[1])\n'
        'from runtools.utils import get_local_shared_libraries\n'
        'with contextlib.redirect_stdout(sys.stderr):\n'
        '    libraries=get_local_shared_libraries(sys.argv[2])\n'
        'print(json.dumps(libraries))\n')
    invocation = ['python3', '-c', script, str(deploy), hardware['driver']]
    command = ['bash', '-c', 'source ' + shlex.quote(str(work / 'control/env.sh')) +
               '\nexec ' + shlex.join(invocation)]
    libraries = subprocess.run(command, capture_output=True, text=True, check=True)
    contents = [(Path(hardware['driver']), Path(hardware['driver']).name)]
    contents += [(Path(source), name) for source, name in json.loads(libraries.stdout)]
    if hardware.get('runtime_conf'):
        name = f'illixr-{MODES[harts]}-runtime.conf'
        contents.append((Path(hardware['runtime_conf']), name))
        hardware['runtime_conf_bundle_name'] = name
        symlink_input(work / 'chipyard/sims/firesim/sim/custom-runtime-configs' / name,
                      Path(hardware['runtime_conf']))
    inputs = {name: {'path': str(source.resolve()), 'sha256': sha256(source)} for source, name in contents}
    if len(inputs) != len(contents) or any(Path(name).name != name for _, name in contents):
        raise ValueError('Duplicate or unsafe driver-bundle member names')
    # Runtime configuration and shared libraries can change independently of the
    # executable. Preserve earlier bundles by identifying the complete inputs.
    inputs_hash = hashlib.sha256(json.dumps(inputs, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
    directory = work / 'control/driver-bundles' / hardware['config'] / (
        hardware['driver_sha256'][:16] + '-' + inputs_hash[:16])
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / 'driver-bundle.tar.gz'
    if archive.exists():
        old = read_json(directory / 'inputs.json')
        if old != inputs:
            raise ValueError('Existing driver bundle has different inputs; preserve it and select a new directory')
    else:
        with archive.open('xb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', filename='', mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w', dereference=True) as tar:
                for source, name in sorted(contents, key=lambda pair: pair[1]):
                    info = tar.gettarinfo(str(source), arcname=name)
                    info.uid = info.gid = info.mtime = 0
                    info.uname = info.gname = ''
                    with source.open('rb') as data:
                        tar.addfile(info, data)
        write_json(directory / 'inputs.json', inputs)
    (directory / 'library-discovery.log').write_text(libraries.stderr)
    hardware.update(driver_tar=str(archive), driver_tar_sha256=sha256(archive))
    write_json(manifest_path, hardware)
    return hardware


def fingerprints(case):
    path = Path(case['hardware_manifest'])
    hardware = validate_hardware(path, case['harts'])
    validate_firmware(case, hardware)
    return {'elf_sha256': sha256(case['elf']), 'hardware_manifest_sha256': sha256(path),
            'memory_profile_interval_cycles': case['memory_profile_interval_cycles'],
            'zero_out_dram': case['zero_out_dram'],
            **{key + '_sha256': hardware[key + '_sha256'] for key in ('bitstream', 'driver', 'driver_tar')}}


def symlink_input(path, target):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink() and path.resolve() == target.resolve():
        return
    if path.exists() or path.is_symlink():
        raise ValueError(f'Refusing to replace unrelated workload input: {path}')
    path.symlink_to(target.resolve())


def prepare_board_database(work, source):
    board = json.loads(source.read_text())
    if len(board) != 1 or board[0].get('device') != 'xcu250_0':
        raise ValueError('Select exactly one approved U250 in the FPGA database')
    destination = work / 'control/fpga-db.json'
    if destination.exists() and json.loads(destination.read_text()) != board:
        raise ValueError('Task FPGA identity differs from the supplied database; refusing to replace it')
    if not destination.exists():
        write_json(destination, board)


def runtime_config(case, work, sim_dir, workload_name):
    deploy = work / 'chipyard/sims/firesim/deploy'
    return {
        'run_farm': {'base_recipe': str(deploy / 'run-farm-recipes/externally_provisioned.yaml'),
            'recipe_arg_overrides': {'run_farm_tag': 'illixr-rtos-validation',
                'default_platform': 'XilinxAlveoU250InstanceDeployManager',
                'default_simulation_dir': str(sim_dir), 'default_fpga_db': str(work / 'control/fpga-db.json'),
                'run_farm_host_specs': [{'illixr_one_board': {'num_fpgas': 1, 'num_metasims': 0, 'use_for_switch_only': 0}}],
                'run_farm_hosts_to_use': [{'localhost': 'illixr_one_board'}]}},
        'metasimulation': {'metasimulation_enabled': False, 'metasimulation_host_simulator': 'verilator',
            'metasimulation_only_plusargs': '', 'metasimulation_only_vcs_plusargs': ''},
        'target_config': {'topology': 'no_net_config', 'no_net_num_nodes': 1, 'link_latency': 6405,
            'switching_latency': 10, 'net_bandwidth': 200,
            'profile_interval': case['memory_profile_interval_cycles'],
            'default_hw_config': f"illixr_u250_rocket_{MODES[case['harts']]}",
            'plusarg_passthrough': f"+max-cycles={case['max_cycles']}"},
        'tracing': {'enable': False, 'output_format': 0, 'selector': 1, 'start': 0, 'end': -1},
        'autocounter': {'read_rate': 0},
        'workload': {'workload_name': workload_name, 'terminate_on_completion': False, 'suffix_tag': None},
        'host_debug': {'zero_out_dram': case['zero_out_dram'], 'disable_synth_asserts': False},
        'synth_print': {'start': 0, 'end': -1, 'cycle_prefix': True},
    }


def prepare_case(case, work, directory, hardware=None):
    """Create manager configurations and workload links; never invoke the manager."""
    validate_firmware(case, hardware)
    deploy = work / 'chipyard/sims/firesim/deploy'
    if not (deploy / 'firesim').is_file():
        raise ValueError(f'Isolated FireSim snapshot is missing: {deploy}')
    directory.mkdir(parents=True, exist_ok=True)
    inputs = directory / 'inputs'
    inputs.mkdir(exist_ok=True)
    symlink_input(inputs / 'zephyr.elf', Path(case['elf']))
    name = directory.name
    workload = {'benchmark_name': name, 'common_bootbinary': 'zephyr.elf', 'common_rootfs': None,
                'common_outputs': [], 'common_simulation_outputs': ['uartlog', 'memory_stats0.csv'],
                'workloads': [{'name': name}]}
    write_json(directory / 'workload.json', workload)
    symlink_input(deploy / 'workloads' / (name + '.json'), directory / 'workload.json')
    symlink_input(deploy / 'workloads' / name, inputs)
    write_json(directory / 'runtime.yaml', runtime_config(case, work, directory / 'runfarm', name + '.json'))
    if hardware:
        hwdb = {hardware['config']: {'bitstream_tar': Path(hardware['bitstream']).resolve().as_uri(),
            'driver_tar': Path(hardware['driver_tar']).resolve().as_uri(),
            'deploy_quintuplet_override': hardware.get('deploy_quintuplet'),
            'custom_runtime_config': hardware.get('runtime_conf_bundle_name')}}
        write_json(directory / 'hwdb.yaml', hwdb)
    write_json(directory / 'case.json', case)


def process_snapshot(proc_root=Path('/proc')):
    found = []
    for entry in proc_root.iterdir():
        if not entry.name.isdecimal():
            continue
        try:
            command = (entry / 'cmdline').read_bytes().replace(b'\0', b' ').decode(errors='replace')
            # PID start time prevents killing a reused PID during scoped cleanup.
            start_time = (entry / 'stat').read_text().rsplit(')', 1)[1].split()[19]
            paths = {}
            for name in ('cwd', 'exe'):
                try:
                    paths[name] = str((entry / name).resolve(strict=True))
                except OSError:
                    paths[name] = None
            found.append({'pid': int(entry.name), 'start_time': start_time,
                          'cwd': paths['cwd'], 'command': command, 'executable': paths['exe'],
                          'comm': (entry / 'comm').read_text().strip()})
        except (OSError, IndexError):
            continue
    return found


def owned_processes(sim_dir, processes=None):
    sim_dir = sim_dir.resolve()
    return [p for p in (processes if processes is not None else process_snapshot())
            if p.get('cwd') and Path(p['cwd']).is_relative_to(sim_dir) and p['pid'] != os.getpid()]


def stop_owned(sim_dir):
    """Never use FireSim kill: this version implements it as broad name-based pkill."""
    stopped = []
    for signum in (signal.SIGTERM, signal.SIGKILL):
        for process in owned_processes(sim_dir):
            # Recheck identity and working directory immediately before signaling.
            current = next((p for p in process_snapshot() if p['pid'] == process['pid']), None)
            if current != process:
                continue
            try:
                os.kill(process['pid'], signum)
                stopped.append({**process, 'signal': signum.value})
            except ProcessLookupError:
                pass
        if signum == signal.SIGTERM:
            time.sleep(1)
    if owned_processes(sim_dir):
        raise RuntimeError('Owned simulation processes survived scoped cleanup; do not start another case')
    return stopped


def check_xdma_idle(module_count, has_devices):
    references = int(module_count.read_text().strip()) if module_count.exists() else None
    if references is not None and references != 0:
        raise ValueError(f'XDMA module has {references} active references; refusing to reprogram a busy board')
    if references is None and has_devices:
        raise ValueError('XDMA devices exist but the kernel module reference count cannot be verified')
    return references


def assert_board_free(fpga_db):
    board = json.loads(fpga_db.read_text())
    if len(board) != 1 or board[0].get('device') != 'xcu250_0':
        raise ValueError('Runtime FPGA database must select exactly the approved U250')
    # Include detached screens: the manager uses the global screen name fsim0.
    busy = [p for p in process_snapshot() if p['comm'].startswith('FireSim-') or
            (p['comm'].lower() == 'screen' and re.search(r'(?:^| )fsim\d(?: |$)', p['command']))]
    if busy:
        raise ValueError('An existing FireSim simulation is active; refusing to reprogram its board: ' +
                         ', '.join(str(p['pid']) for p in busy))
    bdf = board[0]['bdf']
    if not re.fullmatch(r'(?:[0-9a-fA-F]{4}:)?[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\.[0-7]', bdf):
        raise ValueError('Invalid PCI address in FPGA database')
    bdf = bdf if bdf.count(':') == 2 else '0000:' + bdf
    resource = Path('/sys/bus/pci/devices') / bdf / 'resource0'
    if not resource.exists():
        raise ValueError(f'Approved FPGA PCI resource is absent: {resource}')
    devices = [resource, *Path('/dev').glob('xdma*')]
    # XDMA file_operations.owner is THIS_MODULE. Every open character device
    # holds a module reference, including handles hidden by /proc permissions.
    references = check_xdma_idle(Path('/sys/module/xdma/refcnt'), len(devices) > 1)
    # Local U250 drivers run as this user. The host does not grant sudo fuser;
    # process names above also catch other users' identifiable FireSim drivers.
    check = subprocess.run(['fuser', *map(str, devices)], capture_output=True, text=True)
    if check.returncode != 1 or check.stdout.strip() or check.stderr.strip():
        raise ValueError('FPGA device handles are occupied or their idle state could not be verified: ' +
                         (check.stdout + check.stderr).strip())
    return {'checked_utc': utcnow(), 'fpga': board[0], 'device_paths': list(map(str, devices)),
            'xdma_module_references': references,
            'inspection_scope': 'all-user XDMA module references, system process names, and accessible PCI/device handles', 'free': True}


def manager_command(work, directory, task):
    command = ['python3', str(work / 'control/manager-entry.py'), task, '-c', str(directory / 'runtime.yaml'),
               '-a', str(directory / 'hwdb.yaml'), '-r', str(work / 'control/config_build_recipes.yaml')]
    # shlex protects all literal paths; only task-owned env.sh supplies shell code.
    # The build-only Vivado wrapper needs environment variables sudo strips.
    # FPGA programming must resolve the real executable, not that wrapper.
    shell = ('unset ILLIXR_BUILD_GUARD_TAG\nsource ' + shlex.quote(str(work / 'control/env.sh')) +
             '\nexport PATH="/ecad/tools/xilinx/Vivado/2022.1/bin:$PATH"\nexec ' + shlex.join(command))
    return ['bash', '-c', shell]


def capture_manager(command, directory, task, timeout):
    started = time.monotonic()
    execution = {'returncode': None, 'timed_out': False, 'interrupted': False,
                 'cycle_limit_reached': False, 'fatal_markers': [], 'cleanup': []}
    uart = directory / 'runfarm/sim_slot_0/uartlog'
    process = subprocess.Popen(command, cwd=directory, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        with (directory / f'manager-{task}.log').open('wb', buffering=0) as output:
            while True:
                for key, _ in selector.select(timeout=0.5):
                    block = os.read(key.fileobj.fileno(), 65536)
                    if block:
                        output.write(block)
                    else:
                        selector.unregister(key.fileobj)
                if uart.is_file():
                    with uart.open('rb') as stream:
                        stream.seek(max(0, uart.stat().st_size - 65536))
                        tail = normalize_uart(stream.read())
                    execution['fatal_markers'] = [marker for marker in FATAL if marker in tail]
                    execution['cycle_limit_reached'] = 'simulation timed out' in tail
                if process.poll() is not None and not selector.get_map():
                    break
                if time.monotonic() - started > timeout:
                    execution['timed_out'] = True
                if execution['timed_out'] or execution['fatal_markers']:
                    stop(process)
                    execution['cleanup'] += stop_owned(directory / 'runfarm')
    except KeyboardInterrupt:
        execution['interrupted'] = True
        stop(process)
        execution['cleanup'] += stop_owned(directory / 'runfarm')
    except BaseException:
        stop(process)
        stop_owned(directory / 'runfarm')
        raise
    finally:
        selector.close()
        process.stdout.close()
    execution.update(returncode=process.returncode, host_elapsed_seconds=time.monotonic() - started)
    if owned_processes(directory / 'runfarm'):
        execution['cleanup'] += stop_owned(directory / 'runfarm')
        execution['fatal_markers'].append('Manager exited with an owned simulation still running')
    return execution


def execution_errors(execution, console):
    errors = []
    if execution.get('timed_out'):
        errors.append('24-hour host watchdog expired: validation is incomplete')
    if execution.get('interrupted'):
        errors.append('Execution interrupted: validation is incomplete')
    cycle_limit = execution.get('cycle_limit_reached') or 'simulation timed out' in console
    if cycle_limit:
        errors.append('100-billion-cycle simulation limit reached: validation is incomplete')
    markers = sorted(set(execution.get('fatal_markers', [])) | {m for m in FATAL if m in console})
    if markers:
        errors.append('Fatal execution marker: ' + ', '.join(markers))
    if execution.get('returncode') != 0:
        errors.append(f"FireSim manager failed: return code {execution.get('returncode')}")
    passes = re.findall(r'\*\*\* PASSED \*\*\* after (\d+) cycles', console)
    if len(passes) != 1:
        errors.append(f'Expected one normal FireSim/HTIF success record, found {len(passes)}')
    return errors


def collect_memory_stats(directory, profile_interval):
    """Preserve FASED samples and reject observed physical DRAM AXI errors."""
    result = {'profile_interval_cycles': profile_interval, 'files': [], 'errors': [],
              'evidence_complete': True,
              'coverage': 'Periodic samples include cycle zero; the driver does not sample at exit, so the final interval is not covered.',
              'write_error_limitation': 'Generated brespError logic captures read-response bits when a write error occurs; zero brespError cannot establish absence of write errors.'}
    sources = sorted((directory / 'runfarm/sim_slot_0').glob('memory_stats*.csv'))
    if not sources:
        result['errors'].append('Missing FASED memory_stats CSV evidence')
        result['evidence_complete'] = False
    for source in sources:
        destination = directory / source.name
        shutil.copy2(source, destination)
        item = {'file': source.name, 'sha256': sha256(destination), 'samples': 0,
                'observed_rrespError': [], 'observed_brespError': [], 'axi_errors': []}
        result['files'].append(item)
        try:
            with destination.open(newline='') as stream:
                reader = csv.reader(stream)
                headers = next(reader)
                if headers and headers[-1] == '':
                    headers.pop()  # FASED writes a trailing comma.
                required = {'rrespError', 'brespError', 'totalReads', 'totalWrites'}
                if len(set(headers)) != len(headers) or not required.issubset(headers):
                    raise ValueError('Missing or duplicate AXI/traffic columns')
                for index, row in enumerate(reader):
                    if row and row[-1] == '':
                        row.pop()
                    if len(row) != len(headers) or any(not re.fullmatch(r'[0-9]+', value) for value in row):
                        raise ValueError(f'Malformed sample row {index + 1}')
                    values = dict(zip(headers, map(int, row)))
                    item['samples'] += 1
                    item['last_sample_nominal_cycle'] = index * profile_interval
                    item['last_traffic'] = {name: values[name] for name in
                        ('totalReads', 'totalReadBeats', 'totalWrites', 'totalWriteBeats') if name in values}
                    for signal in ('rrespError', 'brespError'):
                        observed = item['observed_' + signal]
                        if values[signal] not in observed:
                            observed.append(values[signal])
                            if values[signal] != 0:
                                event = {'signal': signal, 'value': values[signal],
                                         'sample_index': index, 'nominal_cycle': index * profile_interval}
                                item['axi_errors'].append(event)
                                result['errors'].append(f'FASED {source.name} reports {signal}={values[signal]} at sample {index}')
                if item['samples'] < 2:
                    raise ValueError('No FASED samples after initial cycle-zero sample')
        except (OSError, ValueError, StopIteration, csv.Error) as error:
            result['evidence_complete'] = False
            result['errors'].append(f'Incomplete FASED {source.name}: {error}')
    return result


def analyze_case(case, hardware, directory, execution, args):
    uart = directory / 'runfarm/sim_slot_0/uartlog'
    raw = uart.read_bytes() if uart.is_file() else b''
    (directory / 'uartlog.raw').write_bytes(raw)
    console = normalize_uart(raw)
    log = directory / 'console.log'
    log.write_text(console)
    extra_errors = execution_errors(execution, console)
    memory_stats = collect_memory_stats(directory, case['memory_profile_interval_cycles'])
    extra_errors.extend(memory_stats['errors'])
    native_log = None
    data = records(log)
    if case['platform_check']:
        result = {'clock': data['clocks'], 'platform': data['platforms'], 'errors': startup_checks(
            data, case['harts'], hardware['timer_hz'], hardware['core_hz'], True)}
    else:
        if len(data['summaries']) == 1 and not extra_errors:
            native_log = directory / 'native.log'
            command = [str(args.native.resolve()), '--dataset', str(args.dataset.resolve()),
                       '--trace', str(log), '--output', str(native_log)]
            native = subprocess.run(command, capture_output=True, text=True, timeout=600)
            (directory / 'native-console.log').write_text(native.stdout + native.stderr)
            execution.update(native_command=command, native_returncode=native.returncode, native_sha256=sha256(args.native))
            sources = args.native.parent / 'estimator_sources.json'
            if sources.exists():
                shutil.copy2(sources, directory / sources.name)
            if native.returncode:
                extra_errors.append('Native estimator replay failed')
                native_log = None
        else:
            extra_errors.append('Complete successful execution is required before native replay')
        result = analyze(log, native=native_log, harts=case['harts'], require_initialized=True,
            require_async=True, dataset=args.dataset, placement=case['placement'], require_platform=True,
            expected_timer_hz=hardware['timer_hz'], expected_core_hz=hardware['core_hz'], require_gpu=case.get('require_gpu', False))
        if case.get('require_gpu'):
            if not getattr(args, 'prediction_native', None):
                extra_errors.append('GPU pipeline requires the independent prediction/transform reference executable')
            elif len(data['summaries']) == 1 and not extra_errors:
                try:
                    result['prediction_native'], reference_errors = prediction_reference(args.prediction_native, log, directory, execution)
                    extra_errors.extend(reference_errors)
                except (OSError, ValueError, subprocess.SubprocessError) as error:
                    extra_errors.append(f'Desktop prediction/transform reference failed: {error}')
    result['errors'].extend(extra_errors)
    terminal = re.search(r'\*\*\* (?:PASSED \*\*\*|FAILED \*\*\* \(code = \d+\)) after \d+ cycles', console)
    trace_complete = (len(data['clocks']) == 1 and len(data['platforms']) == 1) if case['platform_check'] else len(data['summaries']) == 1
    result['complete'] = bool(execution.get('stage', 'runworkload') == 'runworkload' and terminal and trace_complete and
        not any(execution.get(k) for k in ('timed_out', 'interrupted', 'cycle_limit_reached')) and
        memory_stats['evidence_complete'])
    if 'simulation timed out' in console:
        result['complete'] = False
    if not result['complete']:
        result['errors'].append('Target execution lacks complete termination, final trace, or memory evidence')
    result['passed'] = not result['errors']
    cycle_matches = re.findall(r'\*\*\* PASSED \*\*\* after (\d+) cycles', console)
    if len(cycle_matches) == 1:
        result['firesim_target_cycles'] = int(cycle_matches[0])
    result.update(host_elapsed_seconds=execution['host_elapsed_seconds'], hardware_config=hardware['config'],
                  memory_stats=memory_stats)
    return result


def run_case(case, args):
    hardware_path = Path(case['hardware_manifest'])
    hardware = validate_hardware(hardware_path, case['harts'])
    identity = fingerprints(case)
    directory = next_output(case['output'])
    if directory.exists() and any(directory.iterdir()):
        raise ValueError(f'Refusing to overwrite existing results: {directory}')
    prepare_case(case, args.work, directory, hardware)
    root = Path(__file__).resolve().parents[1]
    metadata = {'status': 'running', 'started_utc': utcnow(), **case, **identity,
        'output': str(directory), 'backend': 'firesim-u250', 'repo_revision': git_revision(root),
        'chipyard_revision': git_revision(args.work / 'chipyard'),
        'instruction_log': False, 'waveforms': False, 'common_rootfs': None}
    metadata.update(preserve_inputs(directory, Path(case['elf']), hardware_path, Path(hardware['dts']), root))
    metadata['board_free_check'] = assert_board_free(args.work / 'control/fpga-db.json')
    write_json(directory / 'run.json', metadata)
    started = time.monotonic()
    execution = {}
    task = None
    try:
        for task in ('infrasetup', 'runworkload'):
            command = manager_command(args.work, directory, task)
            metadata.setdefault('manager_commands', {})[task] = command
            write_json(directory / 'run.json', metadata)
            remaining = max(0.01, case['timeout_seconds'] - (time.monotonic() - started))
            execution = capture_manager(command, directory, task, remaining)
            execution['stage'] = task
            metadata.setdefault('manager_execution', {})[task] = execution
            if execution['returncode'] or execution['timed_out'] or execution['interrupted'] or execution['fatal_markers']:
                break
            if task == 'infrasetup':
                # Verify what the manager actually copied before starting the DUT.
                slot = directory / 'runfarm/sim_slot_0'
                candidates = list(slot.glob('FireSim-*'))
                if not any(p.is_file() and sha256(p) == hardware['driver_sha256'] for p in candidates):
                    raise ValueError('Deployed driver differs from the verified hardware manifest')
                if sha256(slot / (directory.name + '-zephyr.elf')) != identity['elf_sha256']:
                    raise ValueError('Deployed ELF differs from the preserved firmware')
                metadata['post_setup_board_free_check'] = assert_board_free(args.work / 'control/fpga-db.json')
        execution['host_elapsed_seconds'] = time.monotonic() - started
        result = analyze_case(case, hardware, directory, execution, args)
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result = {'passed': False, 'complete': False, 'errors': [str(error)]}
        metadata['validation_error'] = str(error)
    except KeyboardInterrupt:
        execution['interrupted'] = True
        result = {'passed': False, 'complete': False, 'errors': ['Execution or analysis interrupted']}
    metadata.update(execution)
    recovery = task == 'infrasetup' and not result['passed']
    if recovery:
        # The programming helper may have root-owned descendants over localhost
        # SSH. Never infer that killing our manager also stopped those workers.
        metadata['requires_recovery'] = True
        write_json(args.work / 'control/runtime-recovery-required.json', {
            'created_utc': utcnow(), 'case': case['name'], 'attempt': str(directory),
            'reason': 'Infrastructure setup did not complete cleanly; verify programming helpers have stopped and the board is idle before any further FPGA operation',
            'manager_execution': metadata.get('manager_execution', {}), 'errors': result['errors']})
    metadata['status'] = 'pass' if result['passed'] else ('fail' if result['complete'] else 'incomplete')
    metadata['finished_utc'] = utcnow()
    write_json(directory / 'analysis.json', result)
    write_json(directory / 'run.json', metadata)
    write_json(directory / 'artifact_hashes.json', {str(p.relative_to(directory)): sha256(p)
        for p in sorted(directory.iterdir()) if p.is_file() and p.name != 'artifact_hashes.json'})
    print(f"{metadata['status'].upper()} {case['name']}: {directory}", flush=True)
    for error in result['errors']:
        print('  ' + error, flush=True)
    return result['passed'], execution.get('interrupted', False) or recovery


def inspect_case(case):
    item = {k: case[k] for k in ('name', 'harts', 'placement', 'platform_check')}
    item.update(status='pending', reusable=False, attempts=[])
    try:
        identity = fingerprints(case)
    except (OSError, ValueError, KeyError) as error:
        identity = None
        item['input_error'] = str(error)
    for _, directory in attempts(case['output']):
        metadata = read_json(directory / 'run.json') or {}
        analysis = read_json(directory / 'analysis.json') or {}
        status = metadata.get('status', 'incomplete')
        reusable = bool(identity and status == 'pass' and analysis.get('passed') is True and
            analysis.get('complete') is True and not analysis.get('errors') and metadata.get('returncode') == 0 and
            all(metadata.get(k) == v for k, v in identity.items()) and
            all(metadata.get(k) == case[k] for k in ('harts', 'placement', 'platform_check')))
        if status == 'pass' and not reusable:
            status = 'stale'
        item['attempts'].append({'directory': str(directory), 'status': status, 'reusable': reusable})
        item.update(status=status, reusable=reusable, output=str(directory), metadata=metadata, analysis=analysis)
    return item


def save_report(cases, work):
    items = [inspect_case(c) for c in cases]
    preflights = {item['harts']: item for item in items if item['platform_check']}
    for item in items:
        if not item['platform_check'] and item['status'] == 'pending' and not preflights[item['harts']]['reusable']:
            item['reason'] = 'A matching completed preflight is required before the workload'
    summary = {'updated_utc': utcnow(), 'all_passed': all(i['reusable'] for i in items), 'cases': items,
        'counts': {s: sum(i['status'] == s for i in items) for s in sorted({i['status'] for i in items})},
        'interpretation': 'FireSim uses the target clock; host FPGA MHz is not target GHz. Native agreement is runtime equivalence, not physical accuracy.'}
    write_json(work / 'results/summary.json', summary)
    report = markdown(summary).replace('# Rocket validation comparison', '# FireSim Rocket validation comparison', 1)
    report += ('\nFASED memory evidence is sampled every 1,000,000 target cycles. Nonzero AXI response latches fail validation; '
               'missing or incomplete samples make the case incomplete. The final sampling interval is not covered, and '
               'the generated write-error latch wiring means zero brespError is not proof of error-free writes.\n\n'
               '| Case | Memory file | Samples | Observed rrespError | Observed brespError |\n'
               '|---|---|---:|---|---|\n')
    for item in items:
        for stats in item.get('analysis', {}).get('memory_stats', {}).get('files', []):
            report += (f"| {item['name']} | {stats['file']} | {stats['samples']} | "
                       f"{stats['observed_rrespError']} | {stats['observed_brespError']} |\n")
    (work / 'results/comparison.md').write_text(report)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, default=DEFAULT_WORK)
    parser.add_argument('--artifacts', type=Path, default=DEFAULT_ARTIFACTS)
    parser.add_argument('--cores', type=int, choices=(1, 2, 4), nargs='+', default=[1, 2, 4])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--prepare-only', action='store_true')
    mode.add_argument('--report-only', action='store_true')
    mode.add_argument('--package-drivers', action='store_true', help='Create verified driver archives without touching the FPGA')
    parser.add_argument('--dataset', type=Path, default=Path('/home/prashanth/illixr-headless-reference/data/mav0'))
    parser.add_argument('--native', type=Path, default=Path('/home/prashanth/illixr-spike-validation/native/estimator_replay'))
    parser.add_argument('--prediction-native', type=Path)
    parser.add_argument('--fpga-db', type=Path, default=Path('/opt/firesim-db.json'))
    args = parser.parse_args()
    args.work, args.artifacts = args.work.resolve(), args.artifacts.resolve()
    if args.work == Path('/home/prashanth/chipyard') or not (args.work / 'control').is_dir():
        parser.error('Use the prepared isolated FireSim task workspace')
    all_cases = cases_for(args.work, args.artifacts)
    selected = [case for case in all_cases if case['harts'] in args.cores]
    if args.prepare_only:
        prepare_board_database(args.work, args.fpga_db)
        for case in selected:
            prepare_case(case, args.work, args.work / 'control/prepared' / case['name'])
        write_json(args.work / 'control/matrix.json', all_cases)
        save_report(all_cases, args.work)
        print(f'Prepared {len(selected)} cases; no FPGA operation was performed.')
        return 0
    if args.report_only:
        print(json.dumps(save_report(all_cases, args.work)['counts'], sort_keys=True))
        return 0
    if args.package_drivers:
        for harts in sorted(set(args.cores)):
            package_driver(args.work, args.work / 'control' / f'hardware-{harts}.json', harts)
        return 0
    if not args.native.is_file():
        parser.error('Native estimator harness is missing')
    recovery = args.work / 'control/runtime-recovery-required.json'
    if recovery.exists():
        raise ValueError(f'FPGA execution is held after incomplete programming; inspect and archive the recovery record only after confirming programmer completion and board idle: {recovery}')
    prepare_board_database(args.work, args.fpga_db)
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    # The lock covers every core count and both manager phases on this one board.
    with (args.work / 'control/runtime.lock').open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        initial = save_report(all_cases, args.work)
        if any(a['status'] == 'running' for i in initial['cases'] for a in i['attempts']):
            raise ValueError('An existing attempt is marked running; resolve its owned execution before resuming')
        platforms = {}
        for case in selected:
            if not case['platform_check'] and not platforms.get(case['harts']):
                print(f"BLOCKED {case['name']}: matching preflight did not pass", flush=True)
                continue
            existing = inspect_case(case)
            interrupted_run = False
            if existing['reusable']:
                passed = True
                print(f"RESUME {case['name']}: {existing['output']}", flush=True)
            elif existing.get('input_error'):
                passed = False
                print(f"BLOCKED {case['name']}: {existing['input_error']}", flush=True)
            else:
                passed, interrupted_run = run_case(case, args)
            if case['platform_check']:
                platforms[case['harts']] = passed
            save_report(all_cases, args.work)
            if interrupted_run:
                break
        final = save_report(all_cases, args.work)
    return 0 if all(i['reusable'] for i in final['cases'] if i['harts'] in args.cores) else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, RuntimeError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
