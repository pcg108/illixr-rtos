#!/usr/bin/env python3
"""Decode buffered FP64 mismatch records from a diagnostic phase sweep.

This is diagnostic evidence, never a substitute for the normal acceptance gate.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def decode_mismatch(line):
    data = fields(line)
    for key in ('check', 'case', 'm', 'n', 'k', 'inc'):
        data[key] = int(data[key])
    for key in ('actual', 'expected'):
        word = data[key]
        if not re.fullmatch('[0-9a-fA-F]{16}', word):
            raise ValueError('Invalid FP64 bit pattern')
        data[key + '_bits'] = word
        value = struct.unpack('>d', bytes.fromhex(word))[0]
        data[key] = value if math.isfinite(value) else str(value)
    if isinstance(data['actual'], float) and isinstance(data['expected'], float):
        error = abs(data['actual'] - data['expected'])
        bound = 1e-12 + 1e-10 * abs(data['expected'])
        data.update(absolute_error=error, tolerance=bound,
                    error_over_tolerance=error / bound)
        if error <= bound:
            raise ValueError('Recorded mismatch satisfies the FP64 tolerance')
    return data


def analyze(text, phases=16, checks=610745):
    records, active, errors = [], None, []
    for line in text.replace('\r', '').splitlines():
        if line.startswith('ILLIXR_FIXTURE_PHASE_BEGIN '):
            f = fields(line)
            if active is not None:
                errors.append('New phase before previous phase ended')
            active = {'phase': int(f['phase']), 'delay_us': int(f['delay_us']),
                      'mismatches': [], 'failure_summaries': [], 'checks': [],
                      'oracle': []}
            if active['phase'] != len(records):
                errors.append('Duplicate or out-of-order phase')
        elif line.startswith(('ILLIXR_BLAS_MISMATCH ', 'ILLIXR_BLAS_FAILURES ',
                              'ILLIXR_BLAS_SELFTEST ', 'ILLIXR_FIXTURE_ORACLE ',
                              'ILLIXR_FIXTURE_PHASE_END ')):
            if active is None:
                errors.append('Diagnostic record outside an active phase')
                continue
            if line.startswith('ILLIXR_BLAS_MISMATCH '):
                try:
                    active['mismatches'].append(decode_mismatch(line))
                except (ValueError, KeyError) as exc:
                    errors.append(str(exc))
            elif line.startswith('ILLIXR_BLAS_FAILURES '):
                active['failure_summaries'].append({k: int(v) for k, v in fields(line).items()})
            elif line.startswith('ILLIXR_BLAS_SELFTEST '):
                active['checks'].append(json.loads(line.split(' ', 1)[1]))
            elif line.startswith('ILLIXR_FIXTURE_ORACLE '):
                active['oracle'].append(json.loads(line.split(' ', 1)[1]))
            else:
                f = fields(line)
                if int(f['phase']) != active['phase']:
                    errors.append('Phase completion ID mismatch')
                active['passed'] = f['passed'] == '1'
                active['oracle_passed'] = f['oracle_passed'] == '1'
                summaries = active['failure_summaries']
                if len(summaries) != 1:
                    errors.append('Missing or duplicate buffered failure summary')
                else:
                    summary = summaries[0]
                    if summary['stored'] != min(16, summary['errors']) or summary['stored'] != len(active['mismatches']):
                        errors.append('Incomplete buffered mismatch records')
                    if active['passed'] and summary['errors']:
                        errors.append('Phase hides numerical failures')
                if len(active['checks']) != 1 or active['checks'][0]['checks'] != checks:
                    errors.append('Incorrect full-fixture check count')
                elif active['checks'][0]['passed'] != active['passed']:
                    errors.append('Phase result differs from BLAS result')
                oracle = active['oracle']
                if len(oracle) != 1 or oracle[0] != {
                        'mode': 'table', 'passed': True, 'products': 253728,
                        'triangles': 80, 'errors': 0} or not active['oracle_passed']:
                    errors.append('Oracle validation did not complete successfully')
                records.append(active)
                active = None
    if active is not None or len(records) != phases:
        errors.append('Incomplete phase sweep')
    return {'diagnostic_only': True, 'acceptance_eligible': False,
            'complete_records': not errors, 'record_errors': errors,
            'phases': records, 'failed_phases': [r['phase'] for r in records if not r['passed']],
            'interpretation': 'Completeness describes diagnostic records only. Instrumentation and timing offsets can change reproduction; normal acceptance still requires the unmodified gate.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.console.read_text()), indent=2, allow_nan=False) + '\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')
