#!/usr/bin/env python3
"""Run isolated dual/quad Spike validation, then gate quad FireSim on those passes.

No simulator or FPGA is started without --execute. All changing reports live
under --work; accepted hardware, preflights, and previous matrices are reused.
"""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import re
import signal
import sys
from types import SimpleNamespace

import run_firesim_matrix as firesim
import run_spike


REPO = Path(__file__).resolve().parents[1]
DEFAULT_FIRESIM = Path('/scratch/prashanth_illixr_firesim_20260926')
BASELINE_ANALYSIS = DEFAULT_FIRESIM / 'application-imu-values/results/firesim-imu-values-quad-scheduler-50-1/analysis.json'


def spike_cases(work, attempt=1, include_slow_render=False):
    if type(attempt) is not int or attempt < 1:
        raise ValueError('Spike attempt number must be a positive integer')
    cases = [{'name': f'gpu-spike-{mode}-50', 'elf': str(work / 'artifacts' / f'spike-{mode}-50/zephyr.elf'),
             'harts': harts, 'placement': 'unpinned', 'require_initialized': True, 'require_async': True,
             'require_gpu': True, 'isa': run_spike.DEFAULT_ISA, 'attempt': attempt,
             'output': str(work / 'results' / f'spike-{mode}-{attempt}')}
            for harts, mode in ((2, 'dual'), (4, 'quad'))]
    if include_slow_render:
        cases.append({**cases[0], 'name': 'gpu-spike-dual-slow-render-50', 'require_reuse': True,
                      'elf': str(work / 'artifacts/spike-dual-slow-render-50/zephyr.elf'),
                      'output': str(work / 'results' / f'spike-dual-slow-render-{attempt}')})
    return cases



def firesim_cases(work, hardware_work, preflight):
    # FireSim installs workload links in the shared deployment directory using
    # the result-directory basename. Namespace that basename by this task root
    # so accepted workloads from earlier rounds cannot collide or be replaced.
    root_label = re.sub(r'[^A-Za-z0-9_-]', '-', work.name)[:64] or 'gpu-pipeline'
    root_digest = hashlib.sha256(str(work.resolve()).encode()).hexdigest()[:10]
    namespace = f'{root_label}-{root_digest}'
    base = {'harts': 4, 'placement': 'unpinned',
            'hardware_manifest': str(hardware_work / 'control/hardware-4.json'),
            'timeout_seconds': firesim.WATCHDOG, 'max_cycles': firesim.MAX_CYCLES,
            'memory_profile_interval_cycles': firesim.MEMORY_PROFILE_INTERVAL, 'zero_out_dram': True}
    cases = [{**base, 'name': 'gpu-firesim-quad-preflight', 'platform_check': True,
             'elf': str(preflight), 'output': str(work / 'results' / f'{namespace}-firesim-quad-preflight-1')},
            {**base, 'name': 'gpu-firesim-quad-scheduler-50', 'platform_check': False, 'require_gpu': True,
             'elf': str(work / 'artifacts/rocket-quad-scheduler-50/zephyr.elf'),
             'output': str(work / 'results' / f'{namespace}-firesim-quad-scheduler-1')}]
    for case in cases:
        case.update(firesim.firmware_clock_settings(case['elf']))
    return cases


def check_latches(hardware_work):
    for name in ('STOPPED_NO_AUTORESTART.json', 'runtime-recovery-required.json'):
        path = hardware_work / 'control' / name
        if path.exists():
            raise ValueError(f'Shared execution latch blocks FireSim; no automatic restart: {path}')


def spike_pass(case, oracle):
    output = Path(case['output'])
    metadata = firesim.read_json(output / 'run.json') or {}
    analysis = firesim.read_json(output / 'analysis.json') or {}
    # Earlier runner revisions recorded the authoritative launch argv without
    # a separate ISA field. Accept that provenance only when exactly one ISA
    # argument exists and any explicit metadata field agrees with it.
    isa_flags = [argument for argument in metadata.get('command', [])
                 if isinstance(argument, str) and (argument == '--isa' or argument.startswith('--isa='))]
    isa_matches = (isa_flags == [f"--isa={case['isa']}"] and
                   ('isa' not in metadata or metadata['isa'] == case['isa']))
    reuse_matches = (not case.get('require_reuse') or
                     analysis.get('gpu_pipeline', {}).get('fresh_reuse_with_updated_prediction', 0) > 0)
    return bool(reuse_matches and metadata.get('elf_sha256') == run_spike.sha256(case['elf']) and
                metadata.get('prediction_native_sha256') == run_spike.sha256(oracle) and
                isa_matches and
                metadata.get('harts') == case['harts'] and metadata.get('returncode') == 0 and
                not any(metadata.get(key) for key in ('timed_out', 'interrupted', 'fatal')) and
                analysis.get('passed') is True and analysis.get('complete') is True and
                analysis.get('prediction_native', {}).get('passed') is True and
                analysis.get('gpu_pipeline', {}).get('fresh_warp_completed', 0) > 0)


def report_cell(value):
    if value is None:
        return '—'
    if isinstance(value, float):
        return f'{value:.9g}'
    if isinstance(value, list):
        return '[' + ', '.join(map(str, value)) + ']'
    return str(value).replace('|', '\\|').replace('\n', ' ')


def report_table(headers, rows):
    return ('| ' + ' | '.join(headers) + ' |\n| ' + ' | '.join('---' for _ in headers) + ' |\n' +
            ''.join('| ' + ' | '.join(report_cell(value) for value in row) + ' |\n' for row in rows) + '\n')


def milliseconds(value):
    return value / 1_000_000 if isinstance(value, (float, int)) else None


def report_metrics(row):
    analysis, metadata = row['analysis'], row['metadata']
    summary = analysis.get('summary', {})
    runtime_ns = summary.get('runtime_ns')
    target_seconds = analysis.get('simulated_runtime_seconds')
    if target_seconds is None and isinstance(runtime_ns, int) and runtime_ns > 0:
        target_seconds = runtime_ns / 1e9
    backend = 'firesim-u250' if metadata.get('backend') == 'firesim-u250' else (
        'spike' if row['name'].startswith('gpu-spike-') else 'firesim-u250')
    measured = [item for item in analysis.get('platform', [])
                if item.get('status') in ('ok', 'pass', 'passed', 'success') and item.get('ratio_checked') is True]
    frequencies = {item.get('core_hz') for item in measured}
    core_hz = next(iter(frequencies)) if (backend == 'firesim-u250' and len(frequencies) == 1
                                        and frequencies <= {500_000_000, 1_000_000_000}) else None
    application_cycles = (runtime_ns * core_hz // 1_000_000_000
                          if core_hz and isinstance(runtime_ns, int) and runtime_ns > 0 else None)
    camera_count = summary.get('cam_processed')
    metrics = {'backend': backend, 'target_seconds': target_seconds,
            'shared_timer_hz': analysis.get('clock', [{}])[0].get('timer_hz') if analysis.get('clock') else None,
            'host_seconds': metadata.get('host_elapsed_seconds', analysis.get('host_elapsed_seconds')),
            'verified_core_hz': core_hz, 'application_elapsed_cycles': application_cycles,
            'trace_export_seconds': summary.get('trace_export_ns', 0) / 1e9 if 'trace_export_ns' in summary else None,
            'trace_export_target_cycles': summary['trace_export_ns'] * core_hz // 1_000_000_000
                if core_hz and 'trace_export_ns' in summary else None,
            'firesim_total_target_cycles': analysis.get('firesim_target_cycles') if backend == 'firesim-u250' else None,
            'vio_camera_pairs_per_target_second': camera_count / target_seconds
                if isinstance(camera_count, int) and isinstance(target_seconds, (float, int)) and target_seconds > 0 else None}
    # A runner already executing when report fields were added can legitimately
    # preserve an older analysis schema. Derive only presentation accounting
    # from its immutable console; do not rewrite validation results or gates.
    fields = ('original_presentation_deadlines_missed', 'selected_target_deadlines_missed', 'retargeted_frames',
              'retargeted_target_deadlines_missed', 'maximum_original_presentation_lateness_ns', 'maximum_deadline_lateness_ns')
    stages = analysis.get('gpu_pipeline', {}).get('stages', {})
    metrics['gpu_deadlines'] = {stage: {key: stats.get(key) for key in fields} for stage, stats in stages.items()}
    if stages and any(stats.get(field) is None for stats in stages.values() for field in fields):
        try:
            events = firesim.records(Path(row['output']) / 'console.log')['gpu_events']
            origin = summary['origin_ns']
            for stage in stages:
                items = [event for event in events if event['stage'] == stage]
                original = [max(0, event['observed_complete_ns'] - event['presentation_ns']) for event in items]
                target = [max(0, event['observed_complete_ns'] -
                              (event['presentation_ns'] if stage == 'render' else event['target_ns'] - origin))
                          for event in items]
                metrics['gpu_deadlines'][stage] = {
                    'original_presentation_deadlines_missed': sum(delay > 0 for delay in original),
                    'selected_target_deadlines_missed': sum(delay > 0 for delay in target),
                    'retargeted_frames': sum(event.get('late_target') is True for event in items),
                    'retargeted_target_deadlines_missed': sum(event.get('late_target') is True and delay > 0
                                                             for event, delay in zip(items, target)),
                    'maximum_original_presentation_lateness_ns': max(original, default=0),
                    'maximum_deadline_lateness_ns': max(target, default=0)}
            metrics['gpu_deadline_source'] = 'derived from preserved console; original analysis retained'
        except (OSError, ValueError, KeyError, TypeError) as error:
            metrics['gpu_deadline_report_warning'] = str(error)
    return metrics


def report_case_status(analysis, metadata):
    if analysis.get('passed') is True:
        return 'pass'
    if analysis.get('complete') is True:
        return 'fail'
    if (not analysis and metadata.get('status') == 'running' and metadata.get('returncode') is None and
            not metadata.get('finished_utc') and
            not any(metadata.get(key) for key in ('timed_out', 'interrupted', 'fatal', 'fatal_markers'))):
        return 'running'
    return 'incomplete' if analysis or metadata else 'pending'


def preserved_history(work):
    history = []
    revisions = work / 'revision-history.json'
    if revisions.is_file():
        entries = json.loads(revisions.read_text())
        if not isinstance(entries, list):
            raise ValueError(f'Revision history must be a JSON list: {revisions}')
        for entry in entries:
            if not isinstance(entry, dict) or any(not isinstance(entry.get(key), str) or not entry[key].strip()
                                                  for key in ('name', 'description', 'evidence')):
                raise ValueError(f'Revision history entries require name, description, and evidence strings: {revisions}')
            history.append({**entry, 'status': entry.get('status', 'superseded'), 'history_source': str(revisions)})
    first_spike = work / 'results/spike-dual-1'
    metadata = firesim.read_json(first_spike / 'run.json') or {}
    isa = next((arg.split('=', 1)[1] for arg in metadata.get('command', [])
                if isinstance(arg, str) and arg.startswith('--isa=')), '')
    if metadata.get('interrupted') and isa and 'zicntr' not in isa.split('_'):
        history.append({'name': 'Initial dual-core Spike startup attempt', 'status': 'incomplete',
                        'description': 'Interrupted during startup with Zicntr omitted from the launch ISA. '
                                       'The corrected attempts explicitly enable the counter extension required by the platform check.',
                        'evidence': str(first_spike / 'run.json')})
    preparation_log = work / 'control/run-firesim.log'
    partial = work / 'results/firesim-quad-preflight-1'
    if (preparation_log.is_file() and 'Refusing to replace unrelated workload input:' in preparation_log.read_text(errors='replace') and
            partial.is_dir() and not (partial / 'manager-infrasetup.log').exists() and not (partial / 'run.json').exists()):
        history.append({'name': 'Initial FireSim preparation', 'status': 'configuration collision',
                        'description': 'Preparation stopped on an existing shared workload name before any FPGA operation. '
                                       'Task-specific workload names resolved the collision; the partial input directory is preserved.',
                        'evidence': str(preparation_log), 'partial_directory': str(partial)})
    return history


def comparison_markdown(rows, baseline, history=(), htif_directory=None):
    text = ('# Prediction and simulated GPU validation\n\n'
            'Render and timewarp delays are configured asynchronous target-time assumptions, not measured GPU performance. '
            'Prediction and rotational transforms use CPU math; images remain unchanged. '
            'The stages may overlap and do not model GPU resource contention. FireSim uses FASED memory timing.\n\n')
    overview, clocks, inputs, queues, consumers, placement, predictions, latencies, deadlines, native, physical = ([] for _ in range(11))
    for row in rows:
        name, analysis, metadata = row['name'], row['analysis'], row['metadata']
        metrics = row['metrics']
        summary = analysis.get('summary', {})
        pipeline = analysis.get('gpu_pipeline', {})
        gpu = pipeline.get('summary', {})
        overview.append([name, row['status'], analysis.get('pose_count', summary.get('vio_poses')),
                         gpu.get('render_completed'), gpu.get('timewarp_completed'), gpu.get('fresh_warp_completed'),
                         metrics['target_seconds'], metrics['host_seconds']])
        clocks.append([name, metrics['backend'], metrics['shared_timer_hz'], metrics['verified_core_hz'], metrics['application_elapsed_cycles'],
                       metrics['firesim_total_target_cycles'], metrics['trace_export_seconds'], metrics['trace_export_target_cycles']])
        if not summary:
            continue
        inputs.append([name, summary.get('imu_processed'), summary.get('imu_integrator_processed'),
                       summary.get('cam_processed'), summary.get('cam_skipped'), summary.get('cam_dropped'),
                       metrics['vio_camera_pairs_per_target_second']])
        queues.append([name, summary.get('imu_vio_highwater'), summary.get('imu_integrator_highwater'),
                       summary.get('cam_highwater'), summary.get('history_highwater'), gpu.get('mailbox_replaced'),
                       gpu.get('render_skipped_slots')])
        consumer = analysis.get('consumer', {})
        consumers.append([name, summary.get('probe_reads'), summary.get('probe_missed_deadlines'),
                          consumer.get('repeated_vio_pairs'), consumer.get('imu_advanced_on_repeated_vio_pairs'),
                          milliseconds(consumer.get('maximum_vio_age_ns'))])
        for item in analysis.get('placement', {}).get('plugins', []):
            mask = item.get('hart_mask')
            placement.append([name, item.get('plugin'), 'scheduler' if item.get('requested_hart') == -1 else item.get('requested_hart'),
                              hex(mask) if isinstance(mask, int) else None, item.get('work_counts'), item.get('publication_counts')])
        for item in pipeline.get('prediction_placement', []):
            caller = {0: 'render', 1: 'timewarp'}.get(item.get('caller'), 'unknown')
            mask = item.get('hart_mask')
            placement.append([name, f'pose_prediction({caller})', 'calling worker',
                              hex(mask) if isinstance(mask, int) else None, item.get('work_counts'), item.get('publication_counts')])
        for stage, stats in pipeline.get('stages', {}).items():
            status_counts = stats.get('prediction_status_counts', {})
            predictions.append([name, stage, stats.get('completed'), stats.get('completed_frames_per_target_second'),
                                status_counts.get('valid'), status_counts.get('fallback'), status_counts.get('stale'),
                                status_counts.get('invalid'), milliseconds(stats.get('maximum_source_age_ns')),
                                milliseconds(stats.get('maximum_horizon_ns'))])
            latencies.append([name, stage, milliseconds(gpu.get(stage + '_delay_ns')),
                              milliseconds(stats.get('minimum_observed_delay_ns')), milliseconds(stats.get('maximum_observed_delay_ns')),
                              milliseconds(stats.get('maximum_wakeup_delay_ns'))])
            timing = metrics['gpu_deadlines'][stage]
            deadlines.append([name, stage, timing.get('original_presentation_deadlines_missed'),
                              timing.get('selected_target_deadlines_missed'), timing.get('retargeted_frames'),
                              timing.get('retargeted_target_deadlines_missed'),
                              milliseconds(timing.get('maximum_original_presentation_lateness_ns')),
                              milliseconds(timing.get('maximum_deadline_lateness_ns'))])
        estimator = analysis.get('native_comparison', {})
        predictor = analysis.get('prediction_native', {})
        native.append([name, estimator.get('matching_poses'), estimator.get('max_position_error_m'), estimator.get('max_orientation_error_rad'),
                       predictor.get('predictions_compared'), predictor.get('max_position_error_m'), predictor.get('max_orientation_error_rad'),
                       predictor.get('transforms_compared'), predictor.get('max_transform_error')])
        trajectory = analysis.get('trajectory_sanity', {})
        physical.append([name, trajectory.get('estimated_displacement_m'), analysis.get('max_position_norm_m'),
                         trajectory.get('ground_truth_displacement_m'), trajectory.get('displacement_magnitude_difference_m')])
    text += report_table(['Case', 'Status', 'VIO poses', 'Rendered', 'Warped', 'Fresh warps', 'Target seconds', 'Host seconds'], overview)
    text += ('## Clock and cycle accounting\n\n'
             'Application elapsed cycles are derived from replay/drain runtime only when FireSim startup verifies the 500 MHz core clock. '
             'FireSim total target cycles also include startup and final trace output. '
             'Trace export is measured separately around the bulk trace dump, after worker shutdown; it excludes the final summary lines. '
             'These are elapsed cycles, not summed CPU busy cycles. Spike has no assumed 500 MHz core frequency; '
             'its target seconds come from the configured shared timer, and CPU cycle totals are unavailable here.\n\n')
    text += report_table(['Case', 'Backend', 'Shared timer Hz', 'Verified core Hz', 'Application elapsed cycles', 'FireSim total target cycles', 'Trace export target seconds', 'Trace export target cycles'], clocks)
    if htif_directory is not None:
        text += ('The preserved live FireSim driver uses `+fesvr-step-size=10000`. HTIF hands off one console character at a time; '
                 'the observed roughly 2.5 KB/s trace-output rate is consistent with host service at 10,000-target-cycle boundaries. '
                 'This throughput explanation is an inference from source and observed rates; exact handshake cycles per character '
                 'were not instrumented. The application `runtime_ns` is captured after worker shutdown and before trace dumping, '
                 'so application time excludes the dump while total simulator cycles and host runtime include its potentially long tail. '
                 f"[Diagnosis]({htif_directory / 'diagnosis.md'}), [live driver arguments]({htif_directory / 'driver-command.txt'}), "
                 f"[Zephyr HTIF source]({htif_directory / 'uart_htif.c'}), [TSI bridge source]({htif_directory / 'tsibridge.cc'}), "
                 f"[source hashes]({htif_directory / 'source-manifest.json'}).\n\n")
    text += ('## Input delivery and independent consumer progress\n\n'
             'Camera throughput uses total application time, including estimator drain. Queue high-water values are sample counts.\n\n')
    text += report_table(['Case', 'IMUs at VIO', 'IMUs at integrator', 'Camera pairs processed', 'Skipped', 'Dropped', 'VIO camera pairs/s'], inputs)
    text += report_table(['Case', 'IMU→VIO queue max', 'IMU→integrator queue max', 'Camera queue max', 'Integrator history max',
                          'Pending frames replaced', 'Render slots skipped'], queues)
    text += report_table(['Case', 'Probe reads', 'Probe deadline misses', 'Reads with unchanged VIO',
                          'IMU advances while VIO unchanged', 'Maximum VIO pose age (ms)'], consumers)
    text += ('## Observed processing placement\n\n'
             'Counter arrays are indexed by hart ID starting at zero. Masks reflect actual work/publication counters. '
             'Pose prediction executes on its caller; it has no dedicated thread. Scheduler-managed placement is reported as observed.\n\n')
    text += report_table(['Case', 'Worker/service caller', 'Requested placement', 'Observed hart mask', 'Work counts by hart', 'Publication counts by hart'], placement)
    text += ('## Prediction freshness and GPU stage timing\n\n'
             'Fresh warps require valid predictions at both rendering and timewarp. Fallback and stale output remain explicitly counted. '
             'Source age is measured at submission; the prediction horizon extends to the chosen display target. '
             'RK4 extrapolation is limited to 50 ms. Larger reported horizons identify frozen stale poses, whose ages keep growing '
             'after the bounded sensor stream ends while VIO drains.\n\n')
    text += report_table(['Case', 'Stage', 'Completed', 'Frames/s', 'Valid', 'Fallback', 'Stale', 'Invalid',
                          'Maximum source pose age (ms)', 'Maximum reported horizon including stale (ms)'], predictions)
    text += ('Requested GPU delay is modeled elapsed time. Observed latency includes scheduling before the worker resumes; '
             'wakeup delay is the excess beyond the scheduled GPU completion instant.\n\n')
    text += report_table(['Case', 'Stage', 'Requested delay (ms)', 'Observed minimum (ms)', 'Observed maximum (ms)',
                          'Maximum wakeup delay (ms)'], latencies)
    text += ('Legacy v1 separates the original render-frame deadline from a retargeted warp deadline. '
             'V2 gives every warp its own scheduled vsync and never retargets; both deadline columns refer to that original warp deadline. '
             'V2 misses use publication time, with GPU completion lateness also retained in analysis. Deadline misses are performance observations, '
             'separate from functional correctness.\n\n')
    text += report_table(['Case', 'Stage', 'Original presentation misses', 'Selected target misses', 'Retargeted frames',
                          'Retargeted frames missing new target', 'Maximum original lateness (ms)', 'Maximum selected-target lateness (ms)'], deadlines)
    text += ('## Native equivalence and trajectory limitations\n\n'
             'VIO and prediction tolerances are 0.001 m / 0.001 rad; transform tolerance is 1e-5 absolute plus 1e-5 relative. '
             'VIO replay follows the actual delivered input sequence. Prediction and transforms use the independent desktop reference.\n\n')
    text += report_table(['Case', 'VIO poses compared', 'Max VIO position error (m)', 'Max VIO orientation error (rad)',
                          'Predictions compared', 'Max prediction position error (m)', 'Max prediction orientation error (rad)',
                          'Transforms compared', 'Max transform element error'], native)
    text += ('Native agreement measures runtime equivalence. The following physical-motion quantities use endpoint displacement '
             'magnitudes without coordinate alignment and are not a trajectory accuracy pass/fail test.\n\n')
    text += report_table(['Case', 'Estimated displacement on matched endpoints (m)', 'Maximum VIO position norm (m)',
                          'Ground-truth endpoint displacement (m)', 'Displacement magnitude difference (m)'], physical)
    if baseline.get('available'):
        text += (f"The prior four-plugin quad-core scheduler-managed FireSim baseline produced **{baseline['vio_poses']} VIO poses** "
                 f"over **{baseline['target_seconds']:.9g} simulated seconds**. "
                 'This is context only: adding the downstream workload can change which camera samples reach VIO, '
                 f"so the comparison is not a controlled speedup measurement. [Baseline evidence]({baseline['path']}).\n\n")
    else:
        text += 'The prior four-plugin FireSim baseline analysis was unavailable when this report was generated.\n\n'
    if history:
        text += '## Preserved earlier attempts and revisions\n\nCurrent case statuses above describe the selected attempts.\n\n'
        for item in history:
            text += (f"- **{item['name']} — {item['status']}**: {item['description']} "
                     f"[Preserved evidence]({item['evidence']}).\n")
        text += '\n'
    scheduled = []
    for row in rows:
        pipeline = row['analysis'].get('gpu_pipeline', {})
        gpu = pipeline.get('summary', {})
        if gpu.get('version') != 2:
            continue
        scheduled.append([row['name'], gpu.get('distinct_selected'), gpu.get('never_selected'),
                          gpu.get('repeated_uses'), pipeline.get('fresh_reuse_with_updated_prediction'),
                          gpu.get('empty_opportunities'), gpu.get('missed_opportunities'),
                          gpu.get('new_outputs'), gpu.get('repeated_outputs'), gpu.get('no_outputs'),
                          gpu.get('fresh_on_time_presentations'), milliseconds(pipeline.get('maximum_frame_age_ns')),
                          milliseconds(pipeline.get('presentation', {}).get('maximum_observer_lateness_ns'))])
    if scheduled:
        text += ('## Absolute display scheduling (GPU trace v2)\n\n'
                 'Render starts 1 ms after vsync. Timewarp wakes GPU-time-plus-1-ms-margin before vsync, '
                 'retains its original deadline, and may reuse an image with a new prediction. '
                 'Presentation is modeled from timestamped publications at each nominal boundary; observer lateness is separate.\n\n')
        text += report_table(['Case', 'Distinct selected images', 'Never selected', 'Reused images', 'Fresh reuse with newer IMU state',
                              'Empty warp slots', 'Missed warp slots', 'New display output', 'Repeated display output',
                              'No display output', 'Fresh on-time displays', 'Max frame age (ms)', 'Max observer lateness (ms)'], scheduled)
    text += '## Preserved evidence\n\n'
    for row in rows:
        output = Path(row['output'])
        text += (f"- **{row['name']}**: [analysis]({output / 'analysis.json'}), "
                 f"[run provenance]({output / 'run.json'}), [console]({output / 'console.log'}).\n")
        if row['analysis'].get('errors'):
            text += '  Validation errors: ' + '; '.join(row['analysis']['errors']) + '\n'
    return text


def report(work, cases):
    rows = []
    for case in cases:
        output = Path(case['output'])
        analysis = firesim.read_json(output / 'analysis.json') or {}
        metadata = firesim.read_json(output / 'run.json') or {}
        rows.append({'name': case['name'], 'output': str(output), 'harts': case['harts'],
                     'status': report_case_status(analysis, metadata),
                     'analysis': analysis, 'metadata': metadata})
        rows[-1]['metrics'] = report_metrics(rows[-1])
    baseline_analysis = firesim.read_json(BASELINE_ANALYSIS) or {}
    baseline = {'available': baseline_analysis.get('passed') is True,
                'path': str(BASELINE_ANALYSIS), 'vio_poses': baseline_analysis.get('pose_count'),
                'target_seconds': baseline_analysis.get('simulated_runtime_seconds')}
    history = preserved_history(work)
    value = {'updated_utc': firesim.utcnow(), 'all_passed': all(row['status'] == 'pass' for row in rows),
             'baseline_context': baseline, 'preserved_history': history, 'cases': rows}
    firesim.write_json(work / 'results/pipeline-summary.json', value)
    htif_directory = work / 'provenance/htif-throughput'
    (work / 'results/pipeline-comparison.md').write_text(comparison_markdown(
        rows, baseline, history, htif_directory if (htif_directory / 'diagnosis.md').is_file() else None))
    return value


def execute_spike(args, cases):
    run_args = SimpleNamespace(spike=args.spike, isa=run_spike.DEFAULT_ISA, output=args.work / 'results',
                              timeout=firesim.WATCHDOG, native=str(args.native), dataset=str(args.dataset),
                              prediction_native=args.prediction_native)
    for case in cases:
        if (Path(case['output']) / 'run.json').exists():
            if spike_pass(case, args.prediction_native):
                print('REUSE', case['name'], flush=True)
                continue
            raise ValueError(f"Prior Spike attempt is not a matching pass; preserve and inspect it: {case['output']}")
        if not run_spike.run(run_args, case):
            raise ValueError(f"{case['name']} failed or is incomplete; sequence stopped")


def execute_firesim(args, cases, spike):
    for case in spike:
        if not spike_pass(case, args.prediction_native):
            raise ValueError(f"FireSim requires a complete matching Spike pipeline pass: {case['name']}")
    check_latches(args.firesim_work)
    run_args = SimpleNamespace(work=args.firesim_work, native=args.native, dataset=args.dataset,
                              prediction_native=args.prediction_native)
    with (args.firesim_work / 'control/runtime.lock').open('a+') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        preflight_passed = False
        for case in cases:
            check_latches(args.firesim_work)
            firesim.assert_board_free(args.firesim_work / 'control/fpga-db.json')
            prior = firesim.inspect_case(case)
            if prior.get('input_error'):
                raise ValueError(prior['input_error'])
            if prior['attempts'] and not prior['reusable']:
                raise ValueError(f"Prior FireSim attempt is not a matching pass; preserve and inspect it: {prior['output']}")
            if (prior['reusable'] and case.get('require_gpu') and
                    prior['metadata'].get('prediction_native_sha256') != run_spike.sha256(args.prediction_native)):
                raise ValueError('Prior FireSim comparison used a different prediction reference; preserve and inspect it')
            if not case['platform_check'] and not preflight_passed:
                raise ValueError('A completed matching platform preflight is required')
            if prior['reusable']:
                print('REUSE', case['name'], flush=True)
            else:
                passed, interrupted = firesim.run_case(case, run_args)
                if not passed or interrupted:
                    raise ValueError(f"{case['name']} failed or is incomplete; sequence stopped")
            check_latches(args.firesim_work)
            firesim.assert_board_free(args.firesim_work / 'control/fpga-db.json')
            preflight_passed = preflight_passed or case['platform_check']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--stage', choices=('spike', 'firesim', 'report'), default='report')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--include-slow-render', action='store_true',
                        help='Include two-period dual-core render case and require fresh reuse before FireSim')
    parser.add_argument('--firesim-work', type=Path, default=DEFAULT_FIRESIM)
    parser.add_argument('--preflight', type=Path, help='Matching preflight ELF; defaults to the current work artifacts')
    parser.add_argument('--spike', type=Path, default=Path('/home/prashanth/chipyard/.conda-env/riscv-tools/bin/spike'))
    parser.add_argument('--spike-attempt', type=int, default=1,
                        help='Explicit preserved attempt number for both Spike cases and the FireSim prerequisite gate')
    parser.add_argument('--native', type=Path, default=Path('/home/prashanth/illixr-spike-validation/native/estimator_replay'))
    parser.add_argument('--prediction-native', type=Path, default=REPO / 'tests/native/prediction_reference.py')
    parser.add_argument('--dataset', type=Path, default=Path('/home/prashanth/illixr-headless-reference/data/mav0'))
    args = parser.parse_args()
    args.work = args.work.resolve()
    if args.spike_attempt < 1:
        parser.error('--spike-attempt must be positive')
    spike = spike_cases(args.work, args.spike_attempt, args.include_slow_render)
    preflight = args.preflight or args.work / 'artifacts/rocket-quad-preflight/zephyr.elf'
    fpga = firesim_cases(args.work, args.firesim_work, preflight)
    all_cases = spike + fpga
    firesim.write_json(args.work / 'control/pipeline-cases.json', all_cases)
    if not args.execute or args.stage == 'report':
        print(json.dumps(report(args.work, all_cases), indent=2))
        return 0
    for executable in (args.native, args.prediction_native):
        if not executable.is_file():
            raise ValueError(f'Required native validation executable is absent: {executable}')
    if not args.dataset.is_dir():
        raise ValueError('EuRoC dataset is missing')
    state = {'status': 'running', 'stage': args.stage, 'spike_attempt': args.spike_attempt, 'started_utc': firesim.utcnow()}
    status_file = args.work / 'control/pipeline-status.json'
    def interrupt(signum, frame):
        raise KeyboardInterrupt
    previous = signal.signal(signal.SIGTERM, interrupt)
    acquired = False
    try:
        with (args.work / 'control/pipeline.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            acquired = True
            firesim.write_json(status_file, state)
            if args.stage == 'spike':
                execute_spike(args, spike)
            else:
                execute_firesim(args, fpga, spike)
            state['status'] = 'passed'
    except BaseException as error:
        state.update(status='stopped', error=str(error) or type(error).__name__)
        raise
    finally:
        state['finished_utc'] = firesim.utcnow()
        if acquired:
            firesim.write_json(status_file, state)
            report(args.work, all_cases)
        signal.signal(signal.SIGTERM, previous)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, RuntimeError, KeyboardInterrupt) as error:
        print(str(error) or type(error).__name__, file=sys.stderr)
        raise SystemExit(1)
