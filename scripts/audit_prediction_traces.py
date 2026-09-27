#!/usr/bin/env python3
"""Supplemental read-only audit of completed prediction/GPU console traces.

This does not replace the required native prediction or full trace analyzer.
It records array dimensions, actual returned-pose correspondence, timestamps,
and harts observed during valid RK4 evaluation (excluding fallback/stale calls).
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


def audit(path):
    predictions, events, summaries = [], [], []
    errors = []
    for lineno, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
        for marker, target in [('ILLIXR_PREDICTION ', predictions),
                               ('ILLIXR_GPU_EVENT ', events),
                               ('ILLIXR_RESULT ', summaries)]:
            if marker not in line:
                continue
            try:
                target.append(json.loads(line.split(marker, 1)[1]))
            except (ValueError, TypeError) as exc:
                errors.append(f'line {lineno}: malformed {marker.strip()}: {exc}')
            break
    complete = len(summaries) == 1
    if len(summaries) > 1:
        errors.append('multiple final result records')
    for n, record in enumerate(predictions):
        try:
            for field, count in [('position', 3), ('orientation', 4), ('offset', 4), ('raw', 13)]:
                if len(record.get(field, [])) != count:
                    errors.append(f'prediction {n}: dimension {field}')
            for field, count in [('position', 3), ('velocity', 3), ('orientation', 4),
                                 ('w_hat', 3), ('a_hat', 3), ('w_hat2', 3), ('a_hat2', 3)]:
                if len(record['input'].get(field, [])) != count:
                    errors.append(f'prediction {n}: input dimension {field}')
            if record['status'] == 0 and not record['input']['previous_timestamp_ns'] <= record['input']['timestamp_ns'] <= record['target_ns']:
                errors.append(f'prediction {n}: state time order')
        except (KeyError, TypeError) as exc:
            errors.append(f'prediction {n}: invalid schema {exc}')
    for stage in ('render', 'timewarp'):
        v2 = [event for event in events if event.get('stage') == stage and event.get('version', 1) == 2]
        identity = 'frame_id' if stage == 'render' else 'warp_id'
        if v2 and [event.get(identity) for event in v2] != list(range(1, len(v2)+1)):
            errors.append(f'{stage}: duplicate or missing {identity}')
    linked = 0
    per_caller = {}
    for caller, stage in [(0, 'render'), (1, 'timewarp')]:
        calls = [record for record in predictions if record.get('caller') == caller]
        stage_events = [record for record in events if record.get('stage') == stage]
        for call, event in zip(calls, stage_events):
            try:
                for field in ['position', 'orientation']:
                    if (len(call[field]) != len(event[field]) or
                        any(abs(a-b) > 1e-8*(1+abs(a)) for a,b in zip(call[field], event[field]))):
                        errors.append(f'{stage} frame {event["frame_id"]}: {field} differs from prediction service')
                if event.get('version',1)==2 and call['computed_ns'] < event.get('selection_ns',event['actual_wake_ns']):
                    errors.append(f'{stage}: prediction reused from an earlier opportunity')
                if call['computed_ns'] > event['submit_ns']:
                    errors.append(f'{stage} frame {event["frame_id"]}: computation timestamp follows submission')
            except (KeyError, TypeError) as exc:
                errors.append(f'{stage}: invalid schema {exc}')
            linked += 1
        if complete and len(calls) != len(stage_events):
            errors.append(f'{stage}: prediction/event count mismatch')
        valid = [record for record in calls if record.get('status') == 0]
        per_caller[stage] = {
            'valid_predictions': len(valid),
            'valid_processing_hart_counts': dict(Counter(record['processing_hart'] for record in valid)),
            'valid_publication_hart_counts': dict(Counter(record['publication_hart'] for record in valid)),
        }
    if complete and not predictions:
        errors.append('completed trace has no prediction records')
    return dict(complete=complete, passed=not errors if complete else None,
                console=str(path), console_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                prediction_records=len(predictions), linked_gpu_events=linked, errors=errors,
                valid_processing_hart_counts=dict(Counter(record['processing_hart'] for record in predictions if record.get('status') == 0)),
                valid_publication_hart_counts=dict(Counter(record['publication_hart'] for record in predictions if record.get('status') == 0)),
                status_counts=dict(Counter(record.get('status') for record in predictions)), per_caller=per_caller)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', action='append', required=True, metavar='NAME=CONSOLE_PATH')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = {'cases': {}}
    for item in args.case:
        name, path = item.split('=', 1)
        report['cases'][name] = audit(Path(path))
    report['completed_cases_passed'] = all(value['passed'] for value in report['cases'].values() if value['complete'])
    report['pending_cases'] = [name for name,value in report['cases'].items() if not value['complete']]
    report['passed'] = None if report['pending_cases'] else report['completed_cases_passed']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print(json.dumps(report, indent=2, allow_nan=False))
    return 0 if report['passed'] else 1

if __name__ == '__main__':
    raise SystemExit(main())
