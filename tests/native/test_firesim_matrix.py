#!/usr/bin/env python3
"""Check FireSim gates, evidence interpretation and task-scoped execution offline."""
import argparse
import json
from pathlib import Path
import sys
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import run_firesim_matrix as matrix


class FireSimValidation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        (self.work / 'control').mkdir()
        self.artifacts = self.work / 'artifacts'
        self.cases = matrix.cases_for(self.work, self.artifacts)
        self.hardware = {}
        for harts in (1, 2, 4):
            self.hardware[harts] = self.make_hardware(harts)
        for case in self.cases:
            self.make_firmware(case)

    def make_hardware(self, harts):
        hardware = {'harts': harts, 'hart_ids': list(range(harts)), 'hardware_verified': True,
            'timing_closed': True, 'config': f'illixr_u250_rocket_{matrix.MODES[harts]}',
            'timer_hz': 500_000, 'core_hz': 500_000_000, 'memory_base': 0x80000000, 'memory_size': 0x10000000}
        for field in ('dts', 'bitstream', 'driver', 'driver_tar'):
            path = self.work / 'control' / f'{field}-{harts}'
            path.write_text(f'{field} for {harts} harts\n')
            hardware[field] = str(path)
            hardware[field + '_sha256'] = matrix.sha256(path)
        runtime = self.work / 'control' / f'runtime-{harts}.conf'
        runtime.write_text('+mm_readMaxReqs_0=10\n+mm_writeMaxReqs_0=10\n'
                           '+mm_readLatency_0=30\n+mm_writeLatency_0=30\n'
                           '+mm_useHardwareDefaultRuntimeSettings_0\n'
                           '+fesvr-step-size=10000\n+idle-counts=1\n+fesvr-wait-ticks=8\n')
        hardware.update(runtime_conf=str(runtime), runtime_conf_sha256=matrix.sha256(runtime),
                        runtime_conf_bundle_name=f'illixr-{matrix.MODES[harts]}-runtime.conf')
        validation = {'registers': {}}
        for name in ('rtl', 'elaboration_log'):
            source = self.work / 'control' / f'{name}-{harts}'
            source.write_text(name + ' evidence')
            validation[name] = {'path': str(source), 'sha256': matrix.sha256(source)}
        for name in ('readMaxReqs', 'writeMaxReqs', 'readLatency', 'writeLatency'):
            request = name.endswith('MaxReqs')
            validation['registers'][name] = {'requested_value': 10 if request else 30,
                'model_width_bits': 4 if request else 32, 'hardware_default': 10 if request else 30,
                'supported_max': 10 if request else 2**32 - 1}
        hardware['memory_timing'] = {'max_reads': 10, 'max_writes': 10,
            'read_latency_cycles': 30, 'write_latency_cycles': 30, 'rtl_runtime_validation': validation}
        header = self.work / 'control' / f'generated-{harts}.h'
        header.write_text('reset bridge default50 maximum1023')
        hardware['host_interface'] = {'fesvr_step_size_cycles': 10000, 'idle_counts': 1, 'wait_ticks': 8,
            'generated_header': {'path': str(header), 'sha256': matrix.sha256(header)},
            'reset_default_cycles': 50, 'reset_max_cycles': 1023, 'startup_wait_cycles': 80000}
        with tarfile.open(hardware['driver_tar'], 'w') as archive:
            archive.add(hardware['driver'], arcname=Path(hardware['driver']).name)
            archive.add(runtime, arcname=hardware['runtime_conf_bundle_name'])
        hardware['driver_tar_sha256'] = matrix.sha256(hardware['driver_tar'])
        matrix.write_json(self.work / 'control' / f'hardware-{harts}.json', hardware)
        return hardware

    def make_firmware(self, case):
        directory = Path(case['elf']).parent
        directory.mkdir(parents=True)
        for name in ('zephyr.elf', '.config', 'zephyr.dts'):
            (directory / name).write_text(case['name'] + '\n')
        matrix.write_json(directory / 'dataset_manifest.json', {'camera_pairs': 50, 'imu_samples': 501})
        build = {'target': {'harts': case['harts'], 'placement': 'pinned' if case['placement'] == 'pinned' else 'scheduler',
            'platform_check_only': case['platform_check'], 'timer_hz': 500_000},
            'artifact_sha256': {name: matrix.sha256(directory / name) for name in
                               ('zephyr.elf', '.config', 'zephyr.dts', 'dataset_manifest.json')}}
        matrix.write_json(directory / 'build_manifest.json', build)

    def record(self, case, status='pass'):
        directory = Path(case['output'])
        directory.mkdir(parents=True)
        metadata = {**case, **matrix.fingerprints(case), 'status': status, 'returncode': 0}
        result = {'passed': status == 'pass', 'complete': status != 'incomplete', 'errors': []}
        matrix.write_json(directory / 'run.json', metadata)
        matrix.write_json(directory / 'analysis.json', result)
        return directory

    def test_exact_approved_cases_and_core_local_preflights(self):
        self.assertEqual(len(self.cases), 8)
        workloads = [(c['harts'], c['placement']) for c in self.cases if not c['platform_check']]
        self.assertEqual(workloads, [(1, 'unpinned'), (2, 'unpinned'), (2, 'pinned'), (4, 'unpinned'), (4, 'pinned')])

    def test_clock_scaling_requires_matching_explicit_firmware_and_case(self):
        case = dict(self.cases[-2], modeled_clock_scale=2, ticks_per_sec=1000)
        directory = Path(case['elf']).parent
        manifest = directory / 'build_manifest.json'
        build = json.loads(manifest.read_text())
        with self.assertRaisesRegex(ValueError, 'clock scale'):
            matrix.validate_firmware(case, self.hardware[4])
        build['target'].update(modeled_clock_scale=2, timer_hz=1_000_000, core_hz=1_000_000_000,
                               generated_timer_hz=500_000, generated_core_hz=500_000_000,
                               ticks_per_sec=1000)
        (directory / '.config').write_text('CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=1000000\nCONFIG_SYS_CLOCK_TICKS_PER_SEC=1000\n')
        (directory / 'CMakeCache.txt').write_text('ILLIXR_CORE_HZ:STRING=1000000000\n')
        for name in ('.config', 'CMakeCache.txt'):
            build['artifact_sha256'][name] = matrix.sha256(directory / name)
        matrix.write_json(manifest, build)
        matrix.validate_firmware(case, self.hardware[4])
        self.assertEqual(matrix.effective_clocks(case, self.hardware[4]),
                         {'timer_hz': 1_000_000, 'core_hz': 1_000_000_000})
        with self.assertRaisesRegex(ValueError, 'clock scale'):
            matrix.validate_firmware(self.cases[-2], self.hardware[4])
        with self.assertRaisesRegex(ValueError, 'tick rate'):
            matrix.validate_firmware(dict(case, ticks_per_sec=10000), self.hardware[4])
        build['target']['generated_core_hz'] = 1_000_000_000
        matrix.write_json(manifest, build)
        with self.assertRaisesRegex(ValueError, 'provenance'):
            matrix.validate_firmware(case, self.hardware[4])

    def test_clock_scaling_rejects_invalid_factors(self):
        for scale in (True, 0, 3, 2.0, '2'):
            with self.subTest(scale=scale), self.assertRaises(ValueError):
                matrix.effective_clocks({'modeled_clock_scale': scale}, self.hardware[4])
        for harts in (1, 2, 4):
            self.assertTrue(next(c for c in self.cases if c['harts'] == harts)['platform_check'])

    def test_unclosed_timing_never_launches(self):
        hardware = self.hardware[1]
        hardware['timing_closed'] = False
        path = self.work / 'control/hardware-1.json'
        matrix.write_json(path, hardware)
        with self.assertRaisesRegex(ValueError, 'timing closure'):
            matrix.validate_hardware(path, 1)

    def test_wrong_clock_and_hart_id_rejected(self):
        path = self.work / 'control/hardware-2.json'
        for change in ({'timer_hz': 10_000_000}, {'hart_ids': [0, 2]}):
            matrix.write_json(path, {**self.hardware[2], **change})
            with self.assertRaises(ValueError):
                matrix.validate_hardware(path, 2)

    def test_modified_bitstream_driver_and_bundle_rejected(self):
        path = self.work / 'control/hardware-1.json'
        for key in ('bitstream', 'driver', 'driver_tar'):
            source = Path(self.hardware[1][key])
            before = source.read_bytes()
            source.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, key + ' hash'):
                matrix.validate_hardware(path, 1)
            source.write_bytes(before)

    def test_modified_firmware_config_or_dataset_rejected(self):
        case = self.cases[1]
        for name in ('zephyr.elf', '.config', 'dataset_manifest.json'):
            path = Path(case['elf']).parent / name
            before = path.read_bytes()
            path.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Immutable firmware'):
                matrix.validate_firmware(case)
            path.write_bytes(before)

    def test_memory_request_limit_rejects_zero_truncation_and_capacity_overrun(self):
        path = self.work / 'control/hardware-1.json'
        for requested in (0, 11, 15, 16):
            with self.subTest(requested=requested):
                hardware = json.loads(json.dumps(self.hardware[1]))
                # 11 and 15 fit the four-bit register, but exceed the queue's
                # elaborated capacity of ten; 16 truncates to zero in the RTL.
                hardware['memory_timing']['max_reads'] = requested
                hardware['memory_timing']['rtl_runtime_validation']['registers']['readMaxReqs']['requested_value'] = requested
                matrix.write_json(path, hardware)
                with self.assertRaisesRegex(ValueError, 'capacity or is zero'):
                    matrix.validate_hardware(path, 1)

    def test_memory_timing_requires_unchanged_compilation_evidence(self):
        path = self.work / 'control/hardware-1.json'
        for key in ('rtl', 'elaboration_log'):
            evidence = self.hardware[1]['memory_timing']['rtl_runtime_validation'][key]
            source = Path(evidence['path'])
            before = source.read_bytes()
            source.write_bytes(b'different model')
            with self.assertRaisesRegex(ValueError, key + ' evidence'):
                matrix.validate_hardware(path, 1)
            source.write_bytes(before)
        hardware = self.hardware[1].copy()
        del hardware['memory_timing']
        matrix.write_json(path, hardware)
        with self.assertRaisesRegex(ValueError, 'compiled register validation'):
            matrix.validate_hardware(path, 1)

    def test_memory_runtime_overrides_must_match_reviewed_values_once(self):
        path = self.work / 'control/hardware-1.json'
        hardware = self.hardware[1].copy()
        runtime = Path(hardware['runtime_conf'])
        original = runtime.read_text()
        for content in (original.replace('readMaxReqs_0=10', 'readMaxReqs_0=16'),
                        original + '+mm_readMaxReqs_0=10\n'):
            runtime.write_text(content)
            hardware['runtime_conf_sha256'] = matrix.sha256(runtime)
            matrix.write_json(path, hardware)
            with self.assertRaisesRegex(ValueError, 'configuration disagrees'):
                matrix.validate_hardware(path, 1)

    def test_stale_embedded_runtime_rejected_even_with_valid_archive_hash(self):
        path = self.work / 'control/hardware-1.json'
        hardware = self.hardware[1]
        runtime = Path(hardware['runtime_conf'])
        runtime.write_text(runtime.read_text().replace('MaxReqs_0=10', 'MaxReqs_0=8'))
        hardware['runtime_conf_sha256'] = matrix.sha256(runtime)
        hardware['memory_timing']['max_reads'] = hardware['memory_timing']['max_writes'] = 8
        for name in ('readMaxReqs', 'writeMaxReqs'):
            hardware['memory_timing']['rtl_runtime_validation']['registers'][name]['requested_value'] = 8
        matrix.write_json(path, hardware)
        with self.assertRaisesRegex(ValueError, 'bundle member .* differs'):
            matrix.validate_hardware(path, 1)

    def test_host_cadence_is_explicit_bounded_and_after_reset(self):
        for field, value in (('idle_counts', 0), ('fesvr_step_size_cycles', 2**31),
                             ('wait_ticks', -1), ('startup_wait_cycles', 79_999)):
            with self.subTest(field=field):
                hardware = json.loads(json.dumps(self.hardware[1]))
                hardware['host_interface'][field] = value
                with self.assertRaisesRegex(ValueError, 'Host interface'):
                    matrix.validate_host_interface(hardware)
        hardware = json.loads(json.dumps(self.hardware[1]))
        hardware['host_interface'].update(fesvr_step_size_cycles=100, startup_wait_cycles=800)
        with self.assertRaisesRegex(ValueError, 'generated reset maximum'):
            matrix.validate_host_interface(hardware)

    def test_host_cadence_requires_matching_flags_and_reset_evidence(self):
        hardware = self.hardware[1]
        runtime = Path(hardware['runtime_conf'])
        original = runtime.read_text()
        for modified in (original.replace('step-size=10000', 'step-size=2004765'),
                         original + '+idle-counts=10\n'):
            runtime.write_text(modified)
            with self.assertRaisesRegex(ValueError, 'runtime configuration disagrees'):
                matrix.validate_memory_timing(hardware)
        runtime.write_text(original)
        Path(hardware['host_interface']['generated_header']['path']).write_text('different reset bridge')
        with self.assertRaisesRegex(ValueError, 'reset evidence'):
            matrix.validate_host_interface(hardware)

    def test_different_placement_firmware_rejected(self):
        with self.assertRaisesRegex(ValueError, 'placement'):
            matrix.validate_firmware({**self.cases[3], 'placement': 'pinned'})

    def test_prepare_does_not_execute_program_or_change_elf(self):
        deploy = self.work / 'chipyard/sims/firesim/deploy'
        deploy.mkdir(parents=True)
        (deploy / 'firesim').write_text('mock manager')
        case = self.cases[1]
        before = matrix.sha256(case['elf'])
        directory = self.work / 'control/prepared' / case['name']
        with patch.object(matrix.subprocess, 'Popen') as launch:
            matrix.prepare_case(case, self.work, directory)
            launch.assert_not_called()
        self.assertEqual(matrix.sha256(case['elf']), before)
        workload = json.loads((directory / 'workload.json').read_text())
        self.assertIsNone(workload['common_rootfs'])
        self.assertTrue((directory / 'inputs/zephyr.elf').is_symlink())
        self.assertFalse((directory / 'hwdb.yaml').exists())

    def test_runtime_uses_bounded_nontracing_task_local_one_board(self):
        directory = self.work / 'results/a'
        config = matrix.runtime_config(self.cases[0], self.work, directory, 'a.json')
        self.assertFalse(config['metasimulation']['metasimulation_enabled'])
        self.assertFalse(config['tracing']['enable'])
        self.assertEqual(config['target_config']['plusarg_passthrough'], '+max-cycles=100000000000')
        self.assertEqual(config['target_config']['profile_interval'], 1_000_000)
        farm = config['run_farm']['recipe_arg_overrides']
        self.assertEqual(farm['default_simulation_dir'], str(directory))
        self.assertEqual(farm['run_farm_hosts_to_use'], [{'localhost': 'illixr_one_board'}])
        self.assertEqual(self.cases[0]['timeout_seconds'], 86_400)

    def test_dram_initialization_is_consistent_recorded_and_invalidates_reuse(self):
        for case in self.cases:
            self.assertIs(case['zero_out_dram'], matrix.ZERO_OUT_DRAM)
            for enabled in (False, True):
                variant = {**case, 'zero_out_dram': enabled}
                config = matrix.runtime_config(variant, self.work, self.work / 'runtime', 'workload.json')
                self.assertIs(config['host_debug']['zero_out_dram'], enabled)
                self.assertIs(matrix.fingerprints(variant)['zero_out_dram'], enabled)
        case = {**self.cases[0], 'zero_out_dram': False}
        self.record(case)
        self.assertTrue(matrix.inspect_case(case)['reusable'])
        changed = matrix.inspect_case({**case, 'zero_out_dram': True})
        self.assertFalse(changed['reusable'])
        self.assertEqual(changed['status'], 'stale')

    def test_uart_crlf_ansi_preserve_json_and_compact_trace(self):
        raw = b'\x1b[32mILLIXR_RESULT {"status":"ok"}\x1b[0m\r\nILLIXR_TRACE IMU 0\rILLIXR_TRACE CAM 0\n'
        normalized = matrix.normalize_uart(raw)
        self.assertEqual(normalized.splitlines(), ['ILLIXR_RESULT {"status":"ok"}', 'ILLIXR_TRACE IMU 0', 'ILLIXR_TRACE CAM 0'])

    def test_manager_zero_exit_is_not_htif_success(self):
        errors = matrix.execution_errors({'returncode': 0}, 'ILLIXR_RESULT {"status":"ok"}')
        self.assertTrue(any('HTIF' in e for e in errors))

    def test_htif_success_requires_no_watchdog_failure_or_fatal(self):
        good = '*** PASSED *** after 123456 cycles\n'
        self.assertFalse(matrix.execution_errors({'returncode': 0}, good))
        self.assertTrue(matrix.execution_errors({'returncode': 0, 'timed_out': True}, good))
        self.assertTrue(matrix.execution_errors({'returncode': 0}, good + '*** FAILED *** simulation timed out'))
        self.assertTrue(matrix.execution_errors({'returncode': 0}, good + good))

    def test_scoped_cleanup_never_selects_other_runfarm_or_prefix(self):
        own = self.work / 'results/a/runfarm'
        candidates = [
            {'pid': 100, 'cwd': str(own / 'sim_slot_0')},
            {'pid': 101, 'cwd': str(self.work / 'results/b/runfarm/sim_slot_0')},
            {'pid': 102, 'cwd': str(own) + '-unrelated/sim_slot_0'},
        ]
        self.assertEqual([p['pid'] for p in matrix.owned_processes(own, candidates)], [100])

    def test_xdma_reference_blocks_even_when_process_handles_are_hidden(self):
        count = self.work / 'refcnt'
        count.write_text('1\n')
        with self.assertRaisesRegex(ValueError, 'active references'):
            matrix.check_xdma_idle(count, True)
        count.write_text('0\n')
        self.assertEqual(matrix.check_xdma_idle(count, True), 0)
        count.unlink()
        with self.assertRaisesRegex(ValueError, 'cannot be verified'):
            matrix.check_xdma_idle(count, True)
        self.assertIsNone(matrix.check_xdma_idle(count, False))

    def test_changed_inputs_make_saved_pass_stale(self):
        self.record(self.cases[0])
        self.assertTrue(matrix.inspect_case(self.cases[0])['reusable'])
        Path(self.hardware[1]['driver']).write_bytes(b'new driver')
        item = matrix.inspect_case(self.cases[0])
        self.assertFalse(item['reusable'])
        self.assertEqual(item['status'], 'stale')

    def test_pending_report_does_not_claim_passes_or_pose_counts(self):
        result = matrix.save_report(self.cases, self.work)
        self.assertEqual(result['counts'], {'pending': 8})
        self.assertFalse(result['all_passed'])
        report = (self.work / 'results/comparison.md').read_text()
        self.assertNotIn('0 / 0', report)
        self.assertIn('FireSim Rocket', report)

    def test_incomplete_run_cannot_resume_even_with_success_flag(self):
        directory = self.record(self.cases[0])
        matrix.write_json(directory / 'analysis.json', {'passed': True, 'complete': False, 'errors': []})
        self.assertFalse(matrix.inspect_case(self.cases[0])['reusable'])

    def test_manager_command_uses_task_environment_and_explicit_configs(self):
        directory = self.work / 'a path'
        command = matrix.manager_command(self.work, directory, 'runworkload')
        self.assertEqual(command[:2], ['bash', '-c'])
        self.assertIn(str(self.work / 'control/env.sh'), command[2])
        self.assertIn('control/manager-entry.py', command[2])
        self.assertLess(command[2].index('unset ILLIXR_BUILD_GUARD_TAG'), command[2].index('source '))
        self.assertNotIn('kill', command[2])

    def test_driver_archive_uses_verified_binary_and_named_libraries(self):
        path = self.work / 'control/hardware-1.json'
        hardware = self.hardware[1].copy()
        del hardware['driver_tar']
        del hardware['driver_tar_sha256']
        matrix.write_json(path, hardware)
        library = self.work / 'libtest.so.1.0'
        library.write_bytes(b'library')
        discovery = subprocess.CompletedProcess([], 0, json.dumps([[str(library), 'libtest.so.1']]), 'discovery evidence')
        with patch.object(matrix.subprocess, 'run', return_value=discovery):
            packaged = matrix.package_driver(self.work, path, 1)
        with tarfile.open(packaged['driver_tar']) as archive:
            self.assertEqual(set(archive.getnames()), {'driver-1', 'libtest.so.1', 'illixr-single-runtime.conf'})
            self.assertEqual(archive.extractfile('libtest.so.1').read(), b'library')
            self.assertEqual(archive.extractfile('driver-1').read(), Path(hardware['driver']).read_bytes())
        self.assertEqual(packaged['driver_tar_sha256'], matrix.sha256(packaged['driver_tar']))
        with patch.object(matrix.subprocess, 'run') as discovery_again:
            self.assertEqual(matrix.package_driver(self.work, path, 1), packaged)
            discovery_again.assert_not_called()
        incomplete_manifest = packaged.copy()
        del incomplete_manifest['runtime_conf_bundle_name']
        matrix.write_json(path, incomplete_manifest)
        with patch.object(matrix.subprocess, 'run') as discovery_again:
            self.assertEqual(matrix.package_driver(self.work, path, 1), packaged)
            discovery_again.assert_not_called()

    def test_watchdog_stops_only_its_detached_simulation(self):
        directory = self.work / 'capture'
        slot = directory / 'runfarm/sim_slot_0'
        slot.mkdir(parents=True)
        script = ('import subprocess,sys,time\n'
            f'p=subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"],cwd={str(slot)!r},start_new_session=True)\n'
            'print(p.pid,flush=True)\n'
            'time.sleep(60)\n')
        result = matrix.capture_manager([sys.executable, '-c', script], directory, 'runworkload', 0.1)
        self.assertTrue(result['timed_out'])
        self.assertTrue(result['cleanup'])
        self.assertFalse(matrix.owned_processes(directory / 'runfarm'))

    def test_runtime_only_change_preserves_old_bundle_and_creates_new_archive(self):
        path = self.work / 'control/hardware-1.json'
        hardware = self.hardware[1].copy()
        del hardware['driver_tar']
        del hardware['driver_tar_sha256']
        runtime = Path(hardware['runtime_conf'])
        runtime.write_text(runtime.read_text().replace('MaxReqs_0=10', 'MaxReqs_0=8'))
        hardware['runtime_conf_sha256'] = matrix.sha256(runtime)
        hardware['memory_timing']['max_reads'] = hardware['memory_timing']['max_writes'] = 8
        for name in ('readMaxReqs', 'writeMaxReqs'):
            hardware['memory_timing']['rtl_runtime_validation']['registers'][name]['requested_value'] = 8
        matrix.write_json(path, hardware)
        discovery = subprocess.CompletedProcess([], 0, '[]', '')
        with patch.object(matrix.subprocess, 'run', return_value=discovery):
            original = matrix.package_driver(self.work, path, 1)
        original_archive = Path(original['driver_tar'])
        original_bytes = original_archive.read_bytes()
        runtime.write_text(runtime.read_text().replace('MaxReqs_0=8', 'MaxReqs_0=10'))
        hardware['runtime_conf_sha256'] = matrix.sha256(runtime)
        hardware['memory_timing']['max_reads'] = hardware['memory_timing']['max_writes'] = 10
        for name in ('readMaxReqs', 'writeMaxReqs'):
            hardware['memory_timing']['rtl_runtime_validation']['registers'][name]['requested_value'] = 10
        matrix.write_json(path, hardware)
        with patch.object(matrix.subprocess, 'run', return_value=discovery):
            corrected = matrix.package_driver(self.work, path, 1)
        self.assertEqual(original['driver_sha256'], corrected['driver_sha256'])
        self.assertNotEqual(original['driver_tar'], corrected['driver_tar'])
        self.assertEqual(original_archive.read_bytes(), original_bytes)
        with tarfile.open(corrected['driver_tar']) as archive:
            self.assertEqual(archive.extractfile('illixr-single-runtime.conf').read(), runtime.read_bytes())
        with tarfile.open(original_archive) as archive:
            self.assertIn(b'MaxReqs_0=8', archive.extractfile('illixr-single-runtime.conf').read())

    def synthetic_workload(self):
        case = self.cases[-1]
        directory = self.work / 'workload-analysis'
        slot = directory / 'runfarm/sim_slot_0'
        slot.mkdir(parents=True)
        manifest = {'camera_pairs': 50, 'imu_samples': 501, 'dataset_origin_ns': 100}
        matrix.write_json(directory / 'dataset_manifest.json', manifest)
        lines = ['ILLIXR_CLOCK ' + json.dumps({'hart_mask': 15, 'atomic_exchanges': 4096, 'timer_hz': 500000, 'monotonic': True}),
            'ILLIXR_PLATFORM ' + json.dumps({'status': 'pass', 'online_harts': 4, 'timer_hz': 500000,
                'core_hz': 500000000, 'elapsed_timer_ticks': 5000, 'elapsed_core_cycles': 5000000, 'ratio_checked': True})]
        lines += [f'ILLIXR_TRACE IMU {i}' for i in range(501)]
        lines += [f'ILLIXR_TRACE CAM {i}' for i in range(50)]
        lines += ['ILLIXR_POSE 49 200 0 0 0 1 0 0 0',
                  'ILLIXR_PROBE 100 1 200 1 200 0', 'ILLIXR_PROBE 110 1 200 2 210 1']
        for hart, (plugin, work, published) in enumerate([('offline_imu', 501, 501), ('offline_cam', 50, 50),
                ('openvins', 551, 1), ('imu_integrator', 501, 501)]):
            work_counts, publications = [0] * 4, [0] * 4
            work_counts[hart], publications[hart] = work, published
            lines.append('ILLIXR_PLACEMENT ' + json.dumps({'plugin': plugin, 'requested_hart': hart,
                'hart_mask': 1 << hart, 'work_counts': work_counts, 'publication_counts': publications}))
        summary = {'status': 'pass', 'imu_overflow': 0, 'trace_overflow': 0, 'online_harts': 4,
            'origin_ns': 100, 'initialized': True, 'imu_published': 501, 'imu_processed': 501,
            'imu_integrator_processed': 501, 'cam_published': 50, 'cam_processed': 50,
            'cam_skipped': 0, 'cam_dropped': 0, 'runtime_ns': 100000000}
        lines += ['ILLIXR_RESULT ' + json.dumps(summary), '*** PASSED *** after 50000000 cycles']
        (slot / 'uartlog').write_bytes(('\x1b[32m' + '\r\n'.join(lines) + '\x1b[0m\r\n').encode())
        (slot / 'memory_stats0.csv').write_text('brespError,rrespError,totalReads,totalWrites,\n'
                                              '0,0,0,0,\n0,0,100,20,\n')
        native = self.work / 'fake-native'
        native.write_text('native executable fingerprint')
        args = argparse.Namespace(native=native, dataset=self.work / 'dataset')
        execution = {'returncode': 0, 'host_elapsed_seconds': 1.0}
        return case, directory, args, execution

    def test_full_uart_and_native_analysis_preserves_501_50_and_quad_placement(self):
        case, directory, args, execution = self.synthetic_workload()
        def native(command, **kwargs):
            trace = Path(command[command.index('--trace') + 1]).read_text().splitlines()
            Path(command[-1]).write_text('\n'.join(line for line in trace if line.startswith(('ILLIXR_TRACE ', 'ILLIXR_POSE '))) + '\n')
            return subprocess.CompletedProcess(command, 0, '', '')
        with patch.object(matrix.subprocess, 'run', side_effect=native):
            result = matrix.analyze_case(case, self.hardware[4], directory, execution, args)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['dataset_accounting']['published_imu_samples'], 501)
        self.assertEqual(result['dataset_accounting']['accounted_camera_pairs'], 50)
        self.assertEqual(result['pose_count'], 1)
        self.assertEqual(result['firesim_target_cycles'], 50000000)
        self.assertEqual(result['native_comparison']['max_position_error_m'], 0)
        self.assertEqual(len(result['placement']['plugins']), 4)
        self.assertTrue(result['memory_stats']['evidence_complete'])
        self.assertEqual(result['memory_stats']['files'][0]['last_traffic']['totalReads'], 100)

    def test_memory_axi_error_fails_even_with_complete_firmware_success(self):
        case, directory, args, execution = self.synthetic_workload()
        stats = directory / 'runfarm/sim_slot_0/memory_stats0.csv'
        stats.write_text(stats.read_text() + '0,2,105,21,\n')
        with patch.object(matrix.subprocess, 'run') as native:
            result = matrix.analyze_case(case, self.hardware[4], directory, execution, args)
            native.assert_not_called()
        self.assertFalse(result['passed'])
        self.assertTrue(result['complete'])
        self.assertTrue(any('rrespError=2' in error for error in result['errors']))
        self.assertEqual((directory / stats.name).read_bytes(), stats.read_bytes())
        self.assertEqual(result['memory_stats']['files'][0]['sha256'], matrix.sha256(stats))

    def test_missing_or_unsampled_memory_evidence_is_incomplete(self):
        case, directory, args, execution = self.synthetic_workload()
        stats = directory / 'runfarm/sim_slot_0/memory_stats0.csv'
        for content in (None, 'brespError,rrespError,totalReads,totalWrites,\n',
                        'brespError,rrespError,totalReads,totalWrites,\n0,0,0,0,\n'):
            if content is None:
                stats.unlink()
            else:
                stats.write_text(content)
            with patch.object(matrix.subprocess, 'run') as native:
                result = matrix.analyze_case(case, self.hardware[4], directory, execution, args)
                native.assert_not_called()
            self.assertFalse(result['passed'])
            self.assertFalse(result['complete'])
            self.assertFalse(result['memory_stats']['evidence_complete'])

    def test_collects_all_memory_files_and_preserves_write_latch_limitation(self):
        case, directory, args, execution = self.synthetic_workload()
        stats = directory / 'runfarm/sim_slot_0/memory_stats1.csv'
        stats.write_text('brespError,rrespError,totalReads,totalWrites,\n0,0,0,0,\n3,0,1,1,\n')
        result = matrix.collect_memory_stats(directory, case['memory_profile_interval_cycles'])
        self.assertEqual(len(result['files']), 2)
        self.assertTrue(result['evidence_complete'])
        self.assertTrue(any('brespError=3' in error for error in result['errors']))
        self.assertIn('zero brespError cannot', result['write_error_limitation'])

    def test_malformed_memory_row_or_missing_signal_never_passes(self):
        case, directory, args, execution = self.synthetic_workload()
        stats = directory / 'runfarm/sim_slot_0/memory_stats0.csv'
        for content in ('totalReads,totalWrites,\n0,0,\n',
                        'brespError,rrespError,totalReads,totalWrites,\n0,0,0,0,\n0,0,',
                        'brespError,rrespError,totalReads,totalWrites,\n0,0,0,0,\n0,nan,1,1,\n'):
            stats.write_text(content)
            result = matrix.collect_memory_stats(directory, case['memory_profile_interval_cycles'])
            self.assertFalse(result['evidence_complete'])
            self.assertTrue(result['errors'])

    def test_timed_out_uart_never_runs_native_or_passes(self):
        case, directory, args, execution = self.synthetic_workload()
        execution['timed_out'] = True
        with patch.object(matrix.subprocess, 'run') as native:
            result = matrix.analyze_case(case, self.hardware[4], directory, execution, args)
            native.assert_not_called()
        self.assertFalse(result['complete'])
        self.assertFalse(result['passed'])

    def test_no_htif_or_setup_failure_is_incomplete(self):
        case, directory, args, execution = self.synthetic_workload()
        uart = directory / 'runfarm/sim_slot_0/uartlog'
        uart.write_bytes(b'ILLIXR_CLOCK {"hart_mask":15}\r\n')
        execution.update(stage='infrasetup', returncode=1)
        with patch.object(matrix.subprocess, 'run') as native:
            result = matrix.analyze_case(case, self.hardware[4], directory, execution, args)
            native.assert_not_called()
        self.assertFalse(result['complete'])
        self.assertFalse(result['passed'])

    def test_failed_programming_latches_recovery_and_halts_matrix(self):
        deploy = self.work / 'chipyard/sims/firesim/deploy'
        deploy.mkdir(parents=True)
        (deploy / 'firesim').write_text('mock manager')
        args = argparse.Namespace(work=self.work, native=self.work / 'native', dataset=self.work / 'dataset')
        failed = {'returncode': 1, 'timed_out': False, 'interrupted': False,
                  'cycle_limit_reached': False, 'fatal_markers': [], 'host_elapsed_seconds': 0.1}
        with patch.object(matrix, 'capture_manager', return_value=failed), \
             patch.object(matrix, 'assert_board_free', return_value={'free': True}), \
             patch.object(matrix, 'preserve_inputs', return_value={}), \
             patch('builtins.print'):
            passed, halt = matrix.run_case(self.cases[0], args)
        self.assertFalse(passed)
        self.assertTrue(halt)
        recovery = json.loads((self.work / 'control/runtime-recovery-required.json').read_text())
        self.assertEqual(recovery['case'], self.cases[0]['name'])
        result = json.loads((Path(self.cases[0]['output']) / 'analysis.json').read_text())
        self.assertFalse(result['complete'])


if __name__ == '__main__':
    unittest.main()
