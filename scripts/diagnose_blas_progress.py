#!/usr/bin/env python3
"""Locate the last completed and outstanding diagnostic BLAS self-test regions."""
import argparse
import json
from pathlib import Path
import re


def inspect(text):
    active = None
    last_completed = None
    entries = 0
    errors = []
    for line in text.replace('\r', '').splitlines():
        match = re.search(r'ILLIXR_BLAS_PROGRESS (enter|exit) (.*)', line)
        if not match:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', match[2]))
        try:
            identifier = int(fields['id'])
            operation = fields['op']
        except (KeyError, ValueError):
            errors.append('Malformed progress record')
            continue
        if match[1] == 'enter':
            if active is not None or identifier != entries + 1:
                errors.append('Overlapping or out-of-order progress entry')
            entries += 1
            active = fields
        elif active is None or active['id'] != str(identifier) or active['op'] != operation:
            errors.append('Progress exit does not match an entry')
        else:
            last_completed = active
            active = None
    return {'entries': entries, 'last_completed': last_completed,
            'outstanding_region': active, 'record_errors': errors,
            'selftest_pass_record': bool(re.search(r'ILLIXR_BLAS_SELFTEST\s+\{[^\n]*"passed"\s*:\s*true', text)),
            'interpretation': 'An outstanding region localizes a stall to a call including its wrapper; it does not by itself identify an internal kernel or prove a cause. Diagnostic instrumentation changes timing.'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.console.read_text(errors='replace')), indent=2))
