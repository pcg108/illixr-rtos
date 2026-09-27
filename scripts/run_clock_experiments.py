#!/usr/bin/env python3
"""Compare quad FireSim timeout precision and explicitly modeled CPU frequency.

Builds are prepared separately. --execute runs each preflight and workload with
the unchanged shared hardware gates, runtime lock, watchdog, and cycle limit.
"""
import argparse
import fcntl
import json
from pathlib import Path
import signal
from types import SimpleNamespace

import run_firesim_matrix as fs
from run_gpu_pipeline import check_latches, report_table

REPO = Path(__file__).resolve().parents[1]
BASELINE = (fs.DEFAULT_WORK / 'opengl-schedule-20260927T005455Z/measured-export/results/'
            'measured-export-8c1116b6ea-firesim-quad-scheduler-1')
VARIANTS = (('tick10k', 1, 10000), ('cpu1ghz', 2, 1000))


def cases(work, hardware_work, attempt=1, combined=False):
    if type(attempt) is not int or attempt < 1:
        raise ValueError('Attempt must be a positive integer')
    rows = []
    for variant, scale, ticks in ((('combined', 2, 10000),) if combined else VARIANTS):
        for preflight in (True, False):
            name = variant + ('-preflight' if preflight else '-workload')
            rows.append({'name': name, 'harts': 4, 'placement': 'unpinned',
                'platform_check': preflight, 'require_gpu': not preflight,
                'modeled_clock_scale': scale, 'ticks_per_sec': ticks,
                'elf': str(work / 'artifacts' / name / 'zephyr.elf'),
                'hardware_manifest': str(hardware_work / 'control/hardware-4.json'),
                'output': str(work / 'results' / (work.name + '-' + name + '-' + str(attempt))),
                'timeout_seconds': fs.WATCHDOG, 'max_cycles': fs.MAX_CYCLES,
                'memory_profile_interval_cycles': fs.MEMORY_PROFILE_INTERVAL,
                'zero_out_dram': True})
    return rows


def collect(name, directory, scale, ticks):
    a = fs.read_json(directory / 'analysis.json') or {}
    run = fs.read_json(directory / 'run.json') or {}
    result = {'name': name, 'directory': str(directory), 'scale': scale,
              'core_hz': 500_000_000 * scale, 'timer_hz': 500_000 * scale,
              'ticks_per_sec': ticks, 'analysis': a, 'run': run}
    if a.get('complete') and (directory / 'console.log').is_file():
        # Same first 2.5 seconds of sensor replay in all cases; excludes drain.
        data = fs.records(directory / 'console.log')
        events = data['gpu_events']
        result['first_valid_prediction_seconds'] = {
            stage: min((p['computed_ns'] / 1e9 for p in data.get('predictions', [])
                        if p['status'] == 0 and p['caller'] == caller), default=None)
            for caller, stage in enumerate(('render', 'timewarp'))}
        result['first_2_5_seconds'] = {}
        origin = a['summary']['origin_ns']
        for stage in ('render', 'timewarp'):
            # For timewarp, presentation_ns belongs to its saved render frame.
            # Its own display deadline is target_ns in the dataset clock domain.
            selected = [e for e in events if e['stage'] == stage and e['target_ns'] - origin <= 2_500_000_000]
            missed = sum(e['publication_ns'] > e['target_ns'] - origin for e in selected)
            result['first_2_5_seconds'][stage] = {'completed': len(selected), 'missed': missed,
                'miss_percent': 100 * missed / len(selected) if selected else None}
    return result


def report(work, all_cases, baseline):
    rows = [collect('Baseline', baseline, 1, 1000)]
    rows += [collect(c['name'], Path(c['output']), c['modeled_clock_scale'], c['ticks_per_sec'])
             for c in all_cases if not c['platform_check']]
    fs.write_json(work / 'results/clock-comparison.json', rows)
    text = ('# Quad-core FireSim clock experiments\n\n'
        'All cases use scheduler-managed placement, 50 stereo pairs / 501 IMUs, 120 Hz display, '
        '6.944445 ms modeled render latency, 1 ms warp latency, and 1 ms offsets/margins. '
        'Estimator, integrator, prediction, dataset, queues, and application C/C++ are unchanged.\n\n'
        'The 1 GHz case reinterprets the existing 1000:1 CPU/mtime ratio using a 1 MHz timer declaration. '
        'The accepted bitstream, driver, 30 MHz FPGA request, and FASED settings remain unchanged. '
        'FASED latencies stay constant in cycles, so their modeled nanosecond durations also halve; '
        'this is a scaled target operating-point experiment, not evidence of physical 1 GHz timing closure. '
        'The independent 10 kHz tick case retains the original 500 MHz / 500 kHz timebase.\n\n')
    overview, deadlines, windows, displays, latency, native, queues, placement, consumers = ([] for _ in range(9))
    for row in rows:
        a, run, name = row['analysis'], row['run'], row['name']
        s = a.get('summary', {}); p = a.get('gpu_pipeline', {}); g = p.get('summary', {})
        status = 'pass' if a.get('passed') and a.get('complete') else run.get('status', 'pending')
        runtime = s.get('runtime_ns', 0) / 1e9
        overview.append([name, status, row['core_hz'] / 1e6, row['ticks_per_sec'], a.get('pose_count'),
            s.get('cam_processed'), s.get('cam_skipped'), s.get('cam_dropped'), runtime or None,
            s.get('runtime_ns', 0) * row['core_hz'] // 1_000_000_000 or None,
            s.get('trace_export_ns', 0) / 1e9 or None, run.get('host_elapsed_seconds'), a.get('firesim_target_cycles')])
        for stage in ('render', 'timewarp'):
            stats = p.get('stages', {}).get(stage, {})
            n = stats.get('completed', 0); misses = stats.get('original_presentation_deadlines_missed', 0)
            deadlines.append([name, stage, n or None, misses if n else None,
                              100 * misses / n if n else None,
                              stats.get('maximum_deadline_lateness_ns', 0) / 1e6 if n else None])
            win = row.get('first_2_5_seconds', {}).get(stage, {})
            windows.append([name, stage, win.get('completed'), win.get('missed'), win.get('miss_percent')])
            latency.append([name, stage] + [stats.get(k, 0) / 1e6 if n else None for k in (
                'maximum_scheduled_wake_lateness_ns', 'minimum_observed_delay_ns', 'maximum_observed_delay_ns')])
        displays.append([name] + [g.get(k) for k in ('render_skipped_slots', 'missed_opportunities',
            'repeated_uses', 'new_outputs', 'repeated_outputs', 'no_outputs', 'fresh_warp_completed', 'fresh_on_time_presentations')])
        vio = a.get('native_comparison', {}); pred = a.get('prediction_native', {})
        native.append([name, s.get('imu_processed'), s.get('imu_integrator_processed'), vio.get('matching_poses'),
            vio.get('max_position_error_m'), vio.get('max_orientation_error_rad'), pred.get('passed')])
        queues.append([name] + [s.get(k) for k in ('imu_vio_highwater', 'imu_integrator_highwater',
            'cam_highwater', 'history_highwater', 'probe_missed_deadlines')])
        consumer = a.get('consumer', {})
        consumers.append([name, s.get('cam_processed', 0) / runtime if runtime else None,
            a.get('pose_count', 0) / runtime if runtime else None,
            consumer.get('maximum_vio_age_ns', 0) / 1e6 if s else None,
            consumer.get('repeated_vio_pairs'), consumer.get('imu_advanced_on_repeated_vio_pairs'),
            row.get('first_valid_prediction_seconds', {}).get('render'),
            row.get('first_valid_prediction_seconds', {}).get('timewarp')])
        for plugin in a.get('placement', {}).get('plugins', []):
            placement.append([name, plugin.get('plugin'), plugin.get('hart_mask'), plugin.get('work_counts'), plugin.get('publication_counts')])
    text += report_table(['Case', 'Status', 'Modeled CPU MHz', 'Ticks/s', 'VIO poses', 'Cameras processed', 'Skipped', 'Dropped',
                         'Application s', 'Application cycles', 'Export s', 'Host s', 'Total target cycles'], overview)
    text += ('## Publication deadline misses\n\n'
             'Each completion is compared with its original vsync target; no retargeting. '
             'A late render/warp is distinct from a skipped opportunity or repeated display output. '
             'Counts include estimator drain; percentages account for different run lengths.\n\n')
    text += report_table(['Case', 'Stage', 'Completed', 'Missed deadline', 'Miss %', 'Max lateness ms'], deadlines)
    text += '\nSame sensor-time window: targets within the first 2.5 application seconds.\n\n'
    text += report_table(['Case', 'Stage', 'Completed', 'Missed deadline', 'Miss %'], windows)
    text += '\n## Presentation and wake timing\n\n'
    text += report_table(['Case', 'Skipped renders', 'Missed warp slots', 'Image reuse', 'New outputs', 'Repeated outputs',
                         'No output', 'Fresh warps', 'Fresh on-time displays'], displays)
    text += report_table(['Case', 'Stage', 'Max scheduled wake lateness ms', 'Min observed GPU wait ms', 'Max observed GPU wait ms'], latency)
    text += '\n## Functional checks and placement\n\n'
    text += report_table(['Case', 'IMUs at VIO', 'IMUs at integrator', 'Native VIO matches', 'Max error m', 'Max error rad', 'Prediction native pass'], native)
    text += report_table(['Case', 'VIO IMU queue max', 'Integrator queue max', 'Camera queue max', 'History max', 'Probe deadline misses'], queues)
    text += report_table(['Case', 'Cameras per application s', 'VIO poses per application s', 'Max VIO pose age ms',
                         'Repeated VIO probe pairs', 'IMU progresses while VIO unchanged',
                         'First valid render prediction s', 'First valid warp prediction s'], consumers)
    text += report_table(['Case', 'Plugin', 'Hart mask', 'Work by hart', 'Publication by hart'], placement)
    text += ('\nOne run per operating point, with an existing baseline; results include scheduler variation. '
             'Native agreement validates delivered-sequence runtime equivalence, not physical trajectory accuracy. '
             'The bounded input ends at 2.5 s; stale predictions during estimator drain are explicitly recorded.\n\n')
    for row in rows:
        text += f"- {row['name']}: [analysis]({row['directory']}/analysis.json), [run]({row['directory']}/run.json).\n"
    history = work / 'provenance/interrupted-attempt.json'
    if history.exists():
        text += f'\nAn earlier preflight was interrupted before startup by overlapping build-guard cleanup. '
        text += f'It remains incomplete and is excluded from the comparison. [Diagnosis]({history}).\n'
    (work / 'results/clock-comparison.md').write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--hardware-work', type=Path, default=fs.DEFAULT_WORK)
    parser.add_argument('--baseline', type=Path, default=BASELINE)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--attempt', type=int, default=1)
    parser.add_argument('--combined', action='store_true', help='Validate the combined 1 GHz / 10 kHz baseline')
    args = parser.parse_args()
    args.work = args.work.resolve()
    all_cases = cases(args.work, args.hardware_work, args.attempt, args.combined)
    fs.write_json(args.work / 'control/cases.json', all_cases)
    if not args.execute:
        report(args.work, all_cases, args.baseline)
        return
    # The resource guard also owns processes whose cwd is under its scratch
    # root. Complete every build and its cleanup before using that root to run.
    for guard in (args.work / 'control').glob('build-*/guard.json'):
        result = fs.read_json(guard.parent / 'guard-result.json') or {}
        if result.get('status') != 'command_completed':
            raise ValueError('All guarded builds must finish before FPGA execution: ' + str(guard.parent))
    for case in all_cases:
        fs.fingerprints(case)
    run_args = SimpleNamespace(work=args.hardware_work,
        native=Path('/home/prashanth/illixr-spike-validation/native/estimator_replay'),
        dataset=Path('/home/prashanth/illixr-headless-reference/data/mav0'),
        prediction_native=REPO / 'tests/native/prediction_reference.py')
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    try:
        with (args.hardware_work / 'control/runtime.lock').open('a+') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            preflight_passed = False
            for case in all_cases:
                check_latches(args.hardware_work)
                fs.assert_board_free(args.hardware_work / 'control/fpga-db.json')
                if case['platform_check']:
                    preflight_passed = False
                elif not preflight_passed:
                    raise ValueError('Matching experiment preflight must pass before the workload')
                prior = fs.inspect_case(case)
                if prior.get('input_error'):
                    raise ValueError(prior['input_error'])
                if Path(case['output']).exists():
                    raise ValueError('Preserve prior attempts; use report mode instead of restarting: ' + case['output'])
                passed, interrupted_run = fs.run_case(case, run_args)
                report(args.work, all_cases, args.baseline)
                if not passed or interrupted_run:
                    raise ValueError(case['name'] + ' failed or was incomplete; sequence stopped')
                preflight_passed = preflight_passed or case['platform_check']
    finally:
        report(args.work, all_cases, args.baseline)


if __name__ == '__main__':
    main()
