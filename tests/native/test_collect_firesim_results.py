#!/usr/bin/env python3
"""Exercise portable collection against synthetic preserved FireSim evidence."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import collect_firesim_results as collector
import run_firesim_matrix as matrix
import test_firesim_matrix as fixtures


class CollectionTests(unittest.TestCase):
    def setUp(self):
        # Reuse the matrix's complete UART/platform/placement/hardware fixture.
        self.fixture = fixtures.FireSimValidation()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        case, runtime, args, execution = self.fixture.synthetic_workload()
        self.case = case
        self.runtime = runtime
        firmware = Path(case['elf']).parent
        args.dataset.mkdir()
        (args.dataset / 'input.csv').write_text('immutable dataset\n')
        manifest = matrix.read_json(runtime / 'dataset_manifest.json')
        manifest['files'] = {'input.csv': {'sha256': matrix.sha256(args.dataset / 'input.csv')}}
        matrix.write_json(firmware / 'dataset_manifest.json', manifest)
        build = matrix.read_json(firmware / 'build_manifest.json')
        build['artifact_sha256']['dataset_manifest.json'] = matrix.sha256(firmware / 'dataset_manifest.json')
        matrix.write_json(firmware / 'build_manifest.json', build)
        matrix.write_json(runtime / 'case.json', case)
        execution.update(timed_out=False, interrupted=False, cycle_limit_reached=False,
                         fatal_markers=[], stage='runworkload', elf_sha256=matrix.sha256(case['elf']))
        matrix.write_json(runtime / 'execution.json', execution)
        self.args = argparse.Namespace(runtime_dir=runtime, firmware_dir=firmware,
            hardware_manifest=Path(case['hardware_manifest']), dataset=args.dataset,
            native=args.native, prediction_native=None, case=None, execution=None,
            output=self.fixture.work / 'collected')

    @staticmethod
    def native(command, **kwargs):
        trace = Path(command[command.index('--trace') + 1]).read_text().splitlines()
        Path(command[-1]).write_text('\n'.join(line for line in trace
            if line.startswith(('ILLIXR_TRACE ', 'ILLIXR_POSE '))) + '\n')
        return subprocess.CompletedProcess(command, 0, '', '')

    def collect(self):
        with contextlib.redirect_stdout(io.StringIO()):
            return collector.collect(self.args)

    def analysis(self):
        return matrix.read_json(self.args.output / 'analysis.json')

    def test_collects_full_run_and_preserves_original_evidence(self):
        before = {str(p): matrix.sha256(p) for p in self.runtime.rglob('*') if p.is_file()}
        with patch.object(matrix.subprocess, 'run', side_effect=self.native) as native:
            self.assertEqual(self.collect(), 0)
        result = self.analysis()
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['firesim_target_cycles'], 50000000)
        self.assertEqual(result['dataset_accounting']['published_imu_samples'], 501)
        self.assertEqual(result['native_comparison']['max_position_error_m'], 0)
        self.assertEqual(native.call_count, 1)
        self.assertEqual(before, {str(p): matrix.sha256(p) for p in self.runtime.rglob('*') if p.is_file()})
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            self.collect()

    def test_interrupted_watchdog_and_cycle_limit_never_run_native(self):
        for flag in ('interrupted', 'timed_out', 'cycle_limit_reached'):
            with self.subTest(flag=flag):
                execution = matrix.read_json(self.runtime / 'execution.json')
                execution.update({key: key == flag for key in ('interrupted', 'timed_out', 'cycle_limit_reached')})
                matrix.write_json(self.runtime / 'execution.json', execution)
                self.args.output = self.fixture.work / flag
                with patch.object(matrix.subprocess, 'run') as native:
                    self.assertEqual(self.collect(), 1)
                    native.assert_not_called()
                self.assertFalse(self.analysis()['complete'])

    def test_missing_execution_outcome_cannot_be_inferred_from_success_uart(self):
        matrix.write_json(self.runtime / 'execution.json', {'host_elapsed_seconds': 1})
        self.assertEqual(self.collect(), 1)
        self.assertIn('explicitly record', self.analysis()['errors'][0])

    def test_no_htif_or_incomplete_transfer_fails_even_with_manager_success(self):
        uart = self.runtime / 'runfarm/sim_slot_0/uartlog'
        original = uart.read_bytes()
        for name, data in [('htif', original.replace(b'*** PASSED ***', b'no termination')),
                           ('transfer', b'ILLIXR_BATCH_BEGIN 1\n' + original)]:
            with self.subTest(name=name):
                uart.write_bytes(data)
                self.args.output = self.fixture.work / name
                with patch.object(matrix.subprocess, 'run') as native:
                    self.assertEqual(self.collect(), 1)
                    native.assert_not_called()
                self.assertFalse(self.analysis()['complete'])

    def test_changed_dataset_and_firmware_cannot_pass(self):
        (self.args.dataset / 'input.csv').write_text('different\n')
        self.assertEqual(self.collect(), 1)
        self.assertIn('Native dataset differs', self.analysis()['errors'][0])
        self.args.output = self.fixture.work / 'changed-firmware'
        (self.args.firmware_dir / 'zephyr.elf').write_text('different\n')
        self.assertEqual(self.collect(), 1)
        self.assertIn('elf_sha256', self.analysis()['errors'][0])

    def test_compiled_eye_requirement_cannot_be_disabled_by_case(self):
        build_path = self.args.firmware_dir / 'build_manifest.json'
        build = matrix.read_json(build_path)
        build['ritnet'] = {'enabled': True}
        matrix.write_json(build_path, build)
        case = collector.case_metadata({**self.case, 'require_gpu': False, 'require_eye': False},
            self.args.firmware_dir, self.args.output, self.args.hardware_manifest)
        self.assertTrue(case['require_eye'])
        self.assertTrue(case['require_gpu'])

    def test_eye_enabled_platform_preflight_does_not_require_workload_inference(self):
        build_path = self.args.firmware_dir / 'build_manifest.json'
        build = matrix.read_json(build_path)
        build['ritnet'] = {'enabled': True}
        build['target']['platform_check_only'] = True
        matrix.write_json(build_path, build)
        case = collector.case_metadata({**self.case, 'platform_check': True},
            self.args.firmware_dir, self.args.output, self.args.hardware_manifest)
        self.assertFalse(case['require_eye'])

    def test_preserved_run_json_and_direct_slot_are_supported(self):
        (self.runtime / 'execution.json').rename(self.runtime / 'run.json')
        self.args.execution = self.runtime / 'run.json'
        self.args.case = self.runtime / 'case.json'
        self.args.runtime_dir = self.runtime / 'runfarm/sim_slot_0'
        with patch.object(matrix.subprocess, 'run', side_effect=self.native):
            self.assertEqual(self.collect(), 0)

    def test_manager_wrapper_uses_deploy_directory_and_literal_arguments(self):
        command = collector.manager_command([sys.executable, '-c',
            'import os,sys; print(os.getcwd()); print(sys.argv[1])', 'literal $HOME; no shell'],
            self.fixture.work)
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        self.assertEqual(result.stdout.splitlines(), [str(self.fixture.work), 'literal $HOME; no shell'])

    def test_record_writes_explicit_interruption_and_never_overwrites(self):
        directory = self.fixture.work / 'new-runtime'
        args = argparse.Namespace(runtime_dir=directory, execution=directory / 'execution.json',
                                  timeout=86400, manager_dir=self.fixture.work, command=['--', 'firesim', 'runworkload'])
        outcome = dict(returncode=-15, timed_out=False, interrupted=True,
                       cycle_limit_reached=False, fatal_markers=[], host_elapsed_seconds=2.5)
        with patch.object(matrix, 'capture_manager', return_value=outcome) as capture:
            self.assertEqual(collector.record(args), 1)
        self.assertEqual(capture.call_args.args, (collector.manager_command(['firesim', 'runworkload'], self.fixture.work), directory, 'runworkload', 86400))
        self.assertTrue(matrix.read_json(args.execution)['interrupted'])
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            collector.record(args)


if __name__ == '__main__':
    unittest.main()
