#!/usr/bin/env python3
"""Reject incomplete, stale-only, reordered, or prematurely completed GPU traces."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import analyze_spike as analysis
import run_gpu_pipeline as pipeline


class GPUTraceValidation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'console.log'
        self.summary = {'status': 'pass', 'imu_overflow': 0, 'trace_overflow': 0, 'online_harts': 2,
            'origin_ns': 1_000_000_000, 'initialized': True, 'runtime_ns': 30_000_000,
            'imu_published': 2, 'imu_processed': 2, 'imu_integrator_processed': 2,
            'cam_published': 1, 'cam_processed': 1, 'cam_dropped': 0}
        self.gpu = {'render_submitted': 1, 'render_completed': 1, 'render_skipped_slots': 0,
            'timewarp_submitted': 1, 'timewarp_completed': 1, 'mailbox_replaced': 0, 'mailbox_pending': False,
            'render_deadlines_missed': 0, 'timewarp_deadlines_missed': 0, 'fresh_warp_completed': 1,
            'render_delay_ns': 6_944_445, 'timewarp_delay_ns': 1_000_000, 'period_ns': 8_333_333,
            'render_closed': True, 'timewarp_done': True, 'trace_overflow': 0}
        self.render = {'stage': 'render', 'frame_id': 1, 'slot': 0, 'submit_ns': 0,
            'scheduled_complete_ns': 6_944_445, 'observed_complete_ns': 7_000_000,
            'presentation_ns': 8_333_333, 'target_ns': 1_008_333_333, 'source_ns': 1_000_000_000,
            'source_sequence': 1, 'prediction_status': 'valid', 'prediction_horizon_ns': 8_333_333,
            'position': [0, 0, 0], 'orientation': [1, 0, 0, 0], 'hart': 0}
        self.warp = {**self.render, 'stage': 'timewarp', 'submit_ns': 7_000_000,
            'scheduled_complete_ns': 8_000_000, 'observed_complete_ns': 8_000_000,
            'render_source_sequence': 1, 'render_prediction_status': 'valid',
            'render_orientation': [1, 0, 0, 0], 'late_target': False, 'hart': 1,
            'transform': [1 if row == col else 0 for row in range(4) for col in range(4)]}
        self.events = [self.render, self.warp]
        self.prediction_count = 2

    def check(self):
        records = [
            'ILLIXR_CLOCK {"hart_mask":3,"atomic_exchanges":2048,"timer_hz":10000000,"monotonic":true}',
            'ILLIXR_TRACE IMU 0', 'ILLIXR_TRACE IMU 1', 'ILLIXR_TRACE CAM 0',
            'ILLIXR_POSE 0 1000000000 0 0 0 1 0 0 0',
            'ILLIXR_PROBE 100 1 1000000000 1 1000000000 0',
            'ILLIXR_PROBE 110 1 1000000000 2 1000000010 1']
        records += ['ILLIXR_GPU_EVENT ' + json.dumps(event) for event in self.events]
        calls = []
        for event in self.events:
            calls.append({'caller': 0 if event['stage'] == 'render' else 1,
                'processing_hart': event['hart'], 'publication_hart': event['hart'],
                'status': {'valid': 0, 'fallback': 1, 'stale': 2, 'invalid': 3}[event['prediction_status']],
                'source_ns': event['source_ns'], 'source_seq': event['source_sequence'],
                'target_ns': event['target_ns'], 'horizon_ns': event['prediction_horizon_ns']})
        records += ['ILLIXR_PREDICTION ' + json.dumps(call) for call in calls[:self.prediction_count]]
        records += ['ILLIXR_PREDICTION_SUMMARY ' + json.dumps({'calls': self.prediction_count, 'overflow': 0,
                    'invalid': 0, 'max_horizon_ns': 50_000_000})]
        for caller in (0, 1):
            counts = [sum(call['caller'] == caller and call['processing_hart'] == hart for call in calls)
                      for hart in (0, 1)]
            mask = sum(1 << hart for hart in (0, 1) if counts[hart])
            records += ['ILLIXR_PREDICTION_PLACEMENT ' + json.dumps({'caller': caller, 'hart_mask': mask,
                        'work_counts': counts, 'publication_counts': counts})]
        records += ['ILLIXR_GPU_RESULT ' + json.dumps(self.gpu), 'ILLIXR_RESULT ' + json.dumps(self.summary)]
        self.path.write_text('\n'.join(records))
        return analysis.analyze(self.path, harts=2, require_initialized=True, require_async=True, require_gpu=True)

    def test_valid_async_pipeline(self):
        result = self.check()
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['gpu_pipeline']['fresh_warp_completed'], 1)

    def test_early_completion_rejected(self):
        self.warp['observed_complete_ns'] -= 1
        self.assertFalse(self.check()['passed'])

    def test_missing_frame_rejected(self):
        self.warp['frame_id'] = 2
        self.assertFalse(self.check()['passed'])

    def test_corrupted_saved_render_orientation_rejected(self):
        self.warp['render_orientation'] = [0, 1, 0, 0]
        self.assertFalse(self.check()['passed'])

    def test_stale_only_pipeline_rejected(self):
        self.render['prediction_status'] = self.warp['prediction_status'] = 'stale'
        self.warp['render_prediction_status'] = 'stale'
        self.gpu['fresh_warp_completed'] = 0
        self.assertFalse(self.check()['passed'])

    def test_falsely_valid_long_horizon_rejected(self):
        self.warp['target_ns'] += 100_000_000
        self.warp['prediction_horizon_ns'] += 100_000_000
        self.assertFalse(self.check()['passed'])

    def test_missing_prediction_trace_rejected(self):
        self.prediction_count = 1
        self.assertFalse(self.check()['passed'])

    def test_pending_mailbox_rejected(self):
        self.gpu['mailbox_pending'] = True
        self.assertFalse(self.check()['passed'])

    def test_loss_without_accounting_rejected(self):
        self.gpu['mailbox_replaced'] = 1
        self.assertFalse(self.check()['passed'])

    def test_shader_transform_nan_rejected(self):
        self.warp['transform'][0] = float('nan')
        result = self.check()
        self.assertFalse(result['passed'])
        json.dumps(result, allow_nan=False)

    def test_earlier_warp_submission_rejected(self):
        for field in ('submit_ns', 'scheduled_complete_ns', 'observed_complete_ns'):
            self.warp[field] -= 2_000_000
        self.assertFalse(self.check()['passed'])

    def test_counted_mailbox_replacement_passes(self):
        earlier = copy.deepcopy(self.render)
        self.render['frame_id'] = self.warp['frame_id'] = 2
        self.render['slot'] = self.warp['slot'] = 1
        for event in (self.render, self.warp):
            for field in ('submit_ns', 'scheduled_complete_ns', 'observed_complete_ns',
                          'presentation_ns', 'target_ns', 'prediction_horizon_ns'):
                event[field] += 8_333_333
        self.events = [earlier, self.render, self.warp]
        self.gpu.update(render_submitted=2, render_completed=2, mailbox_replaced=1)
        self.prediction_count = 3
        result = self.check()
        self.assertTrue(result['passed'], result['errors'])

    def test_retargeted_deadline_does_not_hide_original_presentation_miss(self):
        self.warp.update(submit_ns=9_000_000, scheduled_complete_ns=10_000_000,
                         observed_complete_ns=10_000_000, target_ns=1_016_666_666,
                         prediction_horizon_ns=16_666_666, late_target=True)
        result = self.check()
        self.assertTrue(result['passed'], result['errors'])
        warp = result['gpu_pipeline']['stages']['timewarp']
        self.assertEqual(warp['original_presentation_deadlines_missed'], 1)
        self.assertEqual(warp['selected_target_deadlines_missed'], 0)
        self.assertEqual(warp['retargeted_frames'], 1)
        self.assertEqual(warp['retargeted_target_deadlines_missed'], 0)

    def test_retargeted_target_miss_is_reported_separately(self):
        self.warp.update(submit_ns=9_000_000, scheduled_complete_ns=10_000_000,
                         observed_complete_ns=17_000_000, target_ns=1_016_666_666,
                         prediction_horizon_ns=16_666_666, late_target=True)
        self.gpu['timewarp_deadlines_missed'] = 1
        result = self.check()
        self.assertTrue(result['passed'], result['errors'])
        warp = result['gpu_pipeline']['stages']['timewarp']
        self.assertEqual(warp['original_presentation_deadlines_missed'], 1)
        self.assertEqual(warp['selected_target_deadlines_missed'], 1)
        self.assertEqual(warp['retargeted_target_deadlines_missed'], 1)


class PipelineExecutionConfiguration(unittest.TestCase):
    def test_fixed_quad_case_reuses_hardware_and_preserves_outputs(self):
        work, hardware, preflight = Path('/scratch/new'), Path('/scratch/accepted'), Path('/accepted/preflight.elf')
        cases = pipeline.firesim_cases(work, hardware, preflight)
        self.assertEqual([case['platform_check'] for case in cases], [True, False])
        self.assertEqual(cases[0]['elf'], str(preflight))
        for case in cases:
            self.assertEqual(case['harts'], 4)
            self.assertEqual(case['placement'], 'unpinned')
            self.assertEqual(case['max_cycles'], 100_000_000_000)
            self.assertEqual(case['timeout_seconds'], 86400)
            self.assertTrue(Path(case['output']).is_relative_to(work))
            self.assertEqual(case['hardware_manifest'], str(hardware / 'control/hardware-4.json'))

    def test_shared_firesim_workload_basenames_are_unique_to_task_root(self):
        hardware, preflight = Path('/scratch/accepted'), Path('/accepted/preflight.elf')
        first = pipeline.firesim_cases(Path('/scratch/round-a/gpu-pipeline'), hardware, preflight)
        second = pipeline.firesim_cases(Path('/scratch/round-b/gpu-pipeline'), hardware, preflight)
        self.assertTrue(all(Path(case['output']).name.startswith('gpu-pipeline-') for case in first))
        self.assertEqual(len({Path(case['output']).name for case in first + second}), 4)
        self.assertTrue(all(Path(case['output']).name not in
                            ('firesim-quad-preflight-1', 'firesim-quad-scheduler-1') for case in first + second))
        self.assertEqual(first, pipeline.firesim_cases(Path('/scratch/round-a/gpu-pipeline'), hardware, preflight))

    def test_spike_sequence_is_dual_then_quad(self):
        cases = pipeline.spike_cases(Path('/scratch/new'))
        self.assertEqual([case['harts'] for case in cases], [2, 4])
        self.assertTrue(all(case['require_gpu'] and case['require_async'] for case in cases))

    def test_slow_render_case_requires_fresh_reuse(self):
        cases = pipeline.spike_cases(Path('/scratch/new'), include_slow_render=True)
        self.assertEqual([case['harts'] for case in cases], [2, 4, 2])
        self.assertTrue(cases[-1]['require_reuse'])
        self.assertIn('slow-render', cases[-1]['elf'])
        self.assertEqual(len({case['output'] for case in cases}), 3)

    def test_spike_counter_extension_is_enabled_in_every_case(self):
        self.assertIn('zicntr', pipeline.run_spike.DEFAULT_ISA.split('_'))
        self.assertTrue(all(case['isa'] == pipeline.run_spike.DEFAULT_ISA
                            for case in pipeline.spike_cases(Path('/scratch/new'))))

    def test_explicit_spike_retry_preserves_prior_result_directories(self):
        first = pipeline.spike_cases(Path('/scratch/new'))
        second = pipeline.spike_cases(Path('/scratch/new'), attempt=2)
        self.assertTrue(all(case['output'].endswith('-1') for case in first))
        self.assertTrue(all(case['output'].endswith('-2') for case in second))
        self.assertEqual([case['elf'] for case in first], [case['elf'] for case in second])
        for invalid in (0, -1, True, '2'):
            with self.assertRaises(ValueError):
                pipeline.spike_cases(Path('/scratch/new'), invalid)

    def test_prior_pass_without_counter_extension_cannot_gate_firesim(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            case = pipeline.spike_cases(root, attempt=2)[0]
            elf = Path(case['elf'])
            elf.parent.mkdir(parents=True)
            elf.write_bytes(b'firmware')
            oracle = root / 'oracle.py'
            oracle.write_text('reference')
            output = Path(case['output'])
            output.mkdir(parents=True)
            metadata = {'elf_sha256': pipeline.run_spike.sha256(elf),
                        'prediction_native_sha256': pipeline.run_spike.sha256(oracle),
                        'harts': 2, 'returncode': 0, 'isa': case['isa'],
                        'command': ['spike', '--isa=' + case['isa']]}
            (output / 'analysis.json').write_text(json.dumps({'passed': True, 'complete': True,
                 'prediction_native': {'passed': True}, 'gpu_pipeline': {'fresh_warp_completed': 1}}))
            (output / 'run.json').write_text(json.dumps(metadata))
            self.assertTrue(pipeline.spike_pass(case, oracle))
            # The live corrected run predates the separate ISA metadata field;
            # its unambiguous explicit launch argument remains authoritative.
            del metadata['isa']
            (output / 'run.json').write_text(json.dumps(metadata))
            self.assertTrue(pipeline.spike_pass(case, oracle))
            metadata['command'].append('--isa=' + case['isa'])
            (output / 'run.json').write_text(json.dumps(metadata))
            self.assertFalse(pipeline.spike_pass(case, oracle))
            metadata['command'] = ['spike', '--isa=' + case['isa']]
            metadata['isa'] = 'rv64imafdc_zicsr_zifencei'
            (output / 'run.json').write_text(json.dumps(metadata))
            self.assertFalse(pipeline.spike_pass(case, oracle))
            metadata['isa'] = 'rv64imafdc_zicsr_zifencei'
            metadata['command'] = ['spike', '--isa=' + metadata['isa']]
            (output / 'run.json').write_text(json.dumps(metadata))
            self.assertFalse(pipeline.spike_pass(case, oracle))

    def test_shared_latch_prevents_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'control').mkdir()
            (root / 'control/runtime-recovery-required.json').write_text('{}')
            with self.assertRaisesRegex(ValueError, 'no automatic restart'):
                pipeline.check_latches(root)


class PipelineReportMetrics(unittest.TestCase):
    def test_live_running_case_is_distinct_from_incomplete_execution(self):
        self.assertEqual(pipeline.report_case_status({}, {'status': 'running'}), 'running')
        for completed_state in ({'status': 'running', 'returncode': 0},
                                {'status': 'running', 'interrupted': True},
                                {'status': 'running', 'timed_out': True},
                                {'status': 'running', 'finished_utc': '2026-09-26T23:00:00Z'}):
            self.assertEqual(pipeline.report_case_status({}, completed_state), 'incomplete')
        self.assertEqual(pipeline.report_case_status({'passed': False, 'complete': True}, {'status': 'running'}), 'fail')
        self.assertEqual(pipeline.report_case_status({'passed': True, 'complete': True}, {'status': 'pass'}), 'pass')
        self.assertEqual(pipeline.report_case_status({}, {}), 'pending')

    def test_prior_failures_are_preserved_as_history(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            spike = root / 'results/spike-dual-1'
            spike.mkdir(parents=True)
            (spike / 'run.json').write_text(json.dumps({'interrupted': True,
                                                       'command': ['spike', '--isa=rv64imafdc_zicsr_zifencei']}))
            partial = root / 'results/firesim-quad-preflight-1'
            partial.mkdir()
            (root / 'control').mkdir()
            (root / 'control/run-firesim.log').write_text('Refusing to replace unrelated workload input: /shared/existing.json\n')
            history = pipeline.preserved_history(root)
            self.assertEqual(len(history), 2)
            self.assertEqual(history[0]['status'], 'incomplete')
            self.assertIn('before any FPGA operation', history[1]['description'])
            (partial / 'manager-infrasetup.log').write_text('programming started')
            self.assertEqual(len(pipeline.preserved_history(root)), 1)

    def test_optional_revision_history_links_superseded_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.assertEqual(pipeline.preserved_history(root), [])
            entry = {'name': 'Earlier adapter', 'description': 'Corrected gyro frame mapping.',
                     'evidence': '/preserved/results/pipeline-comparison.md'}
            (root / 'revision-history.json').write_text(json.dumps([entry]))
            history = pipeline.preserved_history(root)
            self.assertEqual(len(history), 1)
            self.assertEqual(history[0]['status'], 'superseded')
            self.assertEqual(history[0]['evidence'], entry['evidence'])
            text = pipeline.comparison_markdown([], {'available': False}, history)
            self.assertIn('Earlier adapter — superseded', text)
            self.assertIn(entry['evidence'], text)

    def row(self, backend):
        return {'name': 'gpu-spike-quad-50' if backend == 'spike' else 'gpu-firesim-quad-scheduler-50',
                'metadata': {'backend': backend, 'host_elapsed_seconds': 20.0},
                'analysis': {'summary': {'runtime_ns': 10_199_384_000, 'cam_processed': 13},
                             'firesim_target_cycles': 6_181_520_002,
                             'platform': [{'status': 'pass', 'ratio_checked': True, 'core_hz': 500_000_000}]}}

    def test_verified_firesim_clock_derives_only_application_elapsed_cycles(self):
        metrics = pipeline.report_metrics(self.row('firesim-u250'))
        self.assertEqual(metrics['application_elapsed_cycles'], 5_099_692_000)
        self.assertEqual(metrics['firesim_total_target_cycles'], 6_181_520_002)
        self.assertEqual(metrics['target_seconds'], 10.199384)
        self.assertAlmostEqual(metrics['vio_camera_pairs_per_target_second'], 13 / 10.199384)

    def test_spike_never_inherits_rocket_cycle_frequency(self):
        metrics = pipeline.report_metrics(self.row('spike'))
        self.assertIsNone(metrics['verified_core_hz'])
        self.assertIsNone(metrics['application_elapsed_cycles'])
        self.assertIsNone(metrics['firesim_total_target_cycles'])

    def test_modeled_1ghz_uses_verified_ratio_for_application_cycles(self):
        row = self.row('firesim-u250')
        row['analysis']['platform'][0]['core_hz'] = 1_000_000_000
        metrics = pipeline.report_metrics(row)
        self.assertEqual(metrics['application_elapsed_cycles'], 10_199_384_000)
        row['analysis']['platform'].append({'status': 'pass', 'ratio_checked': True, 'core_hz': 500_000_000})
        self.assertIsNone(pipeline.report_metrics(row)['application_elapsed_cycles'])

    def test_unverified_firesim_clock_cannot_produce_derived_cycles(self):
        row = self.row('firesim-u250')
        row['analysis']['platform'][0]['ratio_checked'] = False
        self.assertIsNone(pipeline.report_metrics(row)['application_elapsed_cycles'])

    def test_old_analysis_deadlines_are_derived_without_rewriting_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            row = self.row('spike')
            row['output'] = directory
            row['analysis']['summary']['origin_ns'] = 1_000
            row['analysis']['gpu_pipeline'] = {'stages': {'timewarp': {'maximum_deadline_lateness_ns': 0}}}
            console = Path(directory) / 'console.log'
            console.write_text('ILLIXR_GPU_EVENT ' + json.dumps({'stage': 'timewarp',
                'presentation_ns': 100, 'observed_complete_ns': 140, 'target_ns': 1_200, 'late_target': True}) + '\n')
            saved = Path(directory) / 'analysis.json'
            original = json.dumps(row['analysis'])
            saved.write_text(original)
            metrics = pipeline.report_metrics(row)
            timing = metrics['gpu_deadlines']['timewarp']
            self.assertEqual(timing['original_presentation_deadlines_missed'], 1)
            self.assertEqual(timing['selected_target_deadlines_missed'], 0)
            self.assertEqual(timing['retargeted_frames'], 1)
            self.assertEqual(timing['retargeted_target_deadlines_missed'], 0)
            self.assertEqual(saved.read_text(), original)


if __name__ == '__main__':
    unittest.main()
