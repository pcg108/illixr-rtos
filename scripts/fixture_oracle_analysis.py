"""Acceptance checks for standalone-only captured reference tables."""
import json


def analyze_oracle(text, expected_mode=None):
    found = []
    for line in text.splitlines():
        if line.startswith('ILLIXR_FIXTURE_ORACLE '):
            found.append(json.loads(line.split(' ', 1)[1]))
    if expected_mode is None:
        return found, ['Oracle trace requires its compiled manifest'] if found else []
    expected = {'mode': expected_mode, 'passed': True, 'products': 253728,
                'triangles': 80, 'errors': 0}
    errors = []
    if expected_mode not in ('capture', 'verify', 'table') or found != [expected]:
        errors.append('Standalone reference oracle is missing, incomplete, mismatched, or failed')
    if 'ILLIXR_FIXTURE_MISMATCH ' in text:
        errors.append('Standalone reference table differed from original calculations')
    return found, errors
