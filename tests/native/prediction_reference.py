#!/usr/bin/env python3
"""Compare target prediction/warp traces with the pinned desktop math oracle.

The helper uses independently retained desktop kernels, never RTOS kernel headers.
Compilation is cached beside the report and needs only a C++17 compiler and Eigen.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
DESKTOP_COMMIT = 'c9f4b6864d058211cb555a96bffa0e8c689001ca'
MAX_HORIZON_NS = 50_000_000


def norm(v):
    return math.sqrt(sum(x*x for x in v))


def qerror(a, b):
    a_norm, b_norm = norm(a), norm(b)
    if a_norm == 0 or b_norm == 0:
        return math.inf
    dot = abs(sum(x*y for x, y in zip(a, b))/(a_norm*b_norm))
    return 2*math.acos(min(1.0, max(-1.0, dot)))


def inverse(q):
    divisor = sum(x*x for x in q)
    return [q[0]/divisor] + [-x/divisor for x in q[1:]]


def finite_tree(value):
    if isinstance(value, dict):
        return all(finite_tree(v) for v in value.values())
    if isinstance(value, list):
        return all(finite_tree(v) for v in value)
    return not isinstance(value, float) or math.isfinite(value)


def build_helper(output_dir):
    sources = [HERE/'prediction_reference.cpp', HERE/'desktop_prediction_reference.hpp',
               HERE/'desktop_timewarp_reference.hpp']
    digest = hashlib.sha256(b''.join(p.read_bytes() for p in sources)).hexdigest()
    binary = output_dir/f'prediction-reference-{digest[:16]}'
    command = [os.environ.get('CXX', 'g++'), '-std=c++17', '-O2', '-ffp-contract=off',
               '-DEIGEN_DONT_VECTORIZE', '-DEIGEN_DONT_PARALLELIZE',
               '-I'+os.environ.get('EIGEN_INCLUDE_DIR', '/usr/include/eigen3'),
               str(sources[0]), '-o', str(binary)]
    if not binary.exists():
        subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)
    return binary, digest, command


def analyze(trace, output_dir):
    report = dict(passed=False, errors=[], predictions_compared=0, transforms_compared=0,
                  max_position_error_m=0.0, max_orientation_error_rad=0.0,
                  max_transform_error=0.0, max_raw_state_error=0.0,
                  fallback_predictions=0, stale_predictions=0,
                  desktop_commit=DESKTOP_COMMIT,
                  tolerances=dict(position_m=0.001, orientation_rad=0.001,
                                  transform_absolute=1e-5, transform_relative=1e-5))
    def error(message):
        if len(report['errors']) < 100:
            report['errors'].append(message)
    records, transforms, summaries, placements = [], [], [], []
    for lineno, line in enumerate(trace.read_text(errors='replace').splitlines(), 1):
        for marker, target in [('ILLIXR_PREDICTION ', records),
                               ('ILLIXR_PREDICTION_SUMMARY ', summaries),
                               ('ILLIXR_PREDICTION_PLACEMENT ', placements),
                               ('ILLIXR_GPU_EVENT ', transforms)]:
            if marker not in line:
                continue
            try:
                record = json.loads(line.split(marker, 1)[1])
                if not finite_tree(record):
                    error(f'nonfinite {marker.strip()} at line {lineno}')
                    continue
                if marker != 'ILLIXR_GPU_EVENT ' or record.get('stage') == 'timewarp':
                    target.append(record)
            except (ValueError, TypeError) as exc:
                error(f'malformed {marker.strip()} at line {lineno}: {exc}')
            break
    report['prediction_records'] = len(records)
    if not records:
        error('no prediction records')
    if len(summaries) != 1:
        error(f'expected one prediction summary, found {len(summaries)}')
    elif summaries[0].get('calls') != len(records) or summaries[0].get('overflow') or summaries[0].get('invalid'):
        error('prediction summary reports missing records, overflow, or invalid predictions')
    if not transforms:
        error('no timewarp transform records')
    requests, mappings = [], []
    previous_valid = {}
    expected_offset = [1.0, 0.0, 0.0, 0.0]
    first_valid = True
    work_counts, publication_counts = {}, {}
    for index, record in enumerate(records):
        try:
            caller, status = record['caller'], record['status']
            if caller not in (0, 1):
                error(f'prediction {index}: unknown caller {caller}')
            for field, counts in [('processing_hart', work_counts), ('publication_hart', publication_counts)]:
                hart = record[field]
                if hart < 0 or hart > 3:
                    error(f'prediction {index}: invalid {field}')
                counts[(caller, hart)] = counts.get((caller, hart), 0) + 1
            if abs(norm(record['orientation'])-1) > 1e-5:
                error(f'prediction {index}: nonunit orientation')
            if qerror(record['offset'], expected_offset) > 1e-5:
                error(f'prediction {index}: orientation offset differs from desktop policy')
            source = record['input']
            actual_horizon = record['target_ns']-source['timestamp_ns']
            if status == 0:
                if not 0 <= actual_horizon <= MAX_HORIZON_NS:
                    error(f'prediction {index}: valid prediction outside horizon')
                if record['source_seq'] != record['input_seq'] or record['source_ns'] != source['timestamp_ns']:
                    error(f'prediction {index}: source metadata differs from snapshot')
                if record['horizon_ns'] != actual_horizon:
                    error(f'prediction {index}: incorrect horizon')
                values = [actual_horizon*1e-9]
                for field in ('position', 'velocity', 'orientation', 'w_hat', 'a_hat', 'w_hat2', 'a_hat2'):
                    values.extend(source[field])
                values.extend(record['offset'])
                requests.append('P '+' '.join(format(v, '.17g') for v in values))
                mappings.append(('P', index, record))
                previous_valid[caller] = record
                if first_valid:
                    expected_offset = inverse(record['orientation'])
                    first_valid = False
            elif status == 1:
                report['fallback_predictions'] += 1
                if record['input_seq'] or norm(record['position']) or qerror(record['orientation'], [1,0,0,0]) > 1e-6:
                    error(f'prediction {index}: invalid initialization fallback')
            elif status == 2:
                report['stale_predictions'] += 1
                if actual_horizon <= MAX_HORIZON_NS:
                    error(f'prediction {index}: stale prediction within horizon')
                previous = previous_valid.get(caller)
                if previous:
                    if (record['position'] != previous['position'] or record['orientation'] != previous['orientation']
                        or record['source_seq'] != previous['source_seq'] or record['source_ns'] != previous['source_ns']):
                        error(f'prediction {index}: stale pose was not frozen per consumer')
                elif norm(record['position']) or qerror(record['orientation'], [1,0,0,0]) > 1e-6:
                    error(f'prediction {index}: stale-before-valid pose is not identity')
            else:
                error(f'prediction {index}: invalid status {status}')
        except (KeyError, TypeError, ValueError, ZeroDivisionError) as exc:
            error(f'prediction {index}: schema error: {exc}')
    for caller in (0, 1):
        matched = [p for p in placements if p.get('caller') == caller]
        if len(matched) != 1:
            error(f'caller {caller}: missing/duplicate prediction placement')
            continue
        placement = matched[0]
        mask = 0
        for field, counts in [('work_counts', work_counts), ('publication_counts', publication_counts)]:
            reported = placement.get(field, [])
            for hart, count in enumerate(reported):
                if count != counts.get((caller, hart), 0):
                    error(f'caller {caller}: {field} mismatch for hart {hart}')
                if count:
                    mask |= 1 << hart
            if sum(reported) != sum(v for (c, _), v in counts.items() if c == caller):
                error(f'caller {caller}: {field} total mismatch')
        if placement.get('hart_mask') != mask:
            error(f'caller {caller}: hart mask mismatch')
    # V2 correlates transforms by unique warp identity; repeated frame IDs are
    # valid and still produce independent native requests and comparisons.
    v2 = [record for record in transforms if record.get('version', 1) == 2]
    if v2 and (len(v2) != len(transforms) or [r.get('warp_id') for r in v2] != list(range(1, len(v2)+1))):
        error('v2 timewarp IDs are duplicate, missing, or mixed with legacy events')
    report['gpu_trace_version'] = 2 if v2 else 1
    report['distinct_render_images_warped'] = len({r.get('frame_id') for r in transforms})
    for index, record in enumerate(transforms):
        try:
            values = record['render_orientation']+record['orientation']
            if len(values) != 8 or len(record['transform']) != 16:
                raise ValueError('incorrect quaternion/matrix dimensions')
            requests.append('T '+' '.join(format(v, '.17g') for v in values))
            mappings.append(('T', index, record))
        except (KeyError, TypeError, ValueError) as exc:
            error(f'timewarp {index}: schema error: {exc}')
    try:
        binary, digest, command = build_helper(output_dir)
        report['reference_source_sha256'] = digest
        report['reference_binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
        report['reference_compile_command'] = command
        process = subprocess.run([str(binary)], input='\n'.join(requests)+'\n',
                                 capture_output=True, text=True, check=True, timeout=120)
        responses = process.stdout.splitlines()
        if len(responses) != len(mappings):
            error('native helper response count mismatch')
        for (kind, index, record), response in zip(mappings, responses):
            expected = list(map(float, response.split()))
            if not all(math.isfinite(x) for x in expected):
                error(f'{kind} {index}: nonfinite native result')
                continue
            if kind == 'P':
                raw, pos, quat = expected[:13], expected[13:16], expected[16:20]
                position_error = norm([a-b for a,b in zip(pos, record['position'])])
                orientation_error = qerror(quat, record['orientation'])
                raw_error = max(abs(a-b) for a,b in zip(raw, record['raw']))
                report['max_position_error_m'] = max(report['max_position_error_m'], position_error)
                report['max_orientation_error_rad'] = max(report['max_orientation_error_rad'], orientation_error)
                report['max_raw_state_error'] = max(report['max_raw_state_error'], raw_error)
                report['predictions_compared'] += 1
                if position_error > 0.001 or orientation_error > 0.001:
                    error(f'prediction {index}: desktop pose mismatch ({position_error} m, {orientation_error} rad)')
                if any(abs(a-b) > 1e-7*(1+abs(a)) for a,b in zip(raw, record['raw'])):
                    error(f'prediction {index}: desktop raw-state mismatch')
            else:
                difference = max(abs(a-b) for a,b in zip(expected, record['transform']))
                report['max_transform_error'] = max(report['max_transform_error'], difference)
                report['transforms_compared'] += 1
                if any(abs(a-b) > 1e-5*(1+abs(a)) for a,b in zip(expected, record['transform'])):
                    error(f'timewarp {index}: desktop transform mismatch ({difference})')
    except (subprocess.SubprocessError, OSError, ValueError) as exc:
        error(f'native helper failed: {exc}')
        if getattr(exc, 'stderr', None):
            report['native_stderr'] = exc.stderr
    if not report['predictions_compared']:
        error('no valid fresh predictions compared')
    report['passed'] = not report['errors']
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = analyze(args.trace, args.output.parent)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print(json.dumps({key: report[key] for key in ('passed', 'predictions_compared', 'transforms_compared',
          'max_position_error_m', 'max_orientation_error_rad', 'max_transform_error', 'errors')}))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    sys.exit(main())
