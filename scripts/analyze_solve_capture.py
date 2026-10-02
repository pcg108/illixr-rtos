#!/usr/bin/env python3
"""Decode the bounded full-fixture DTRSM capture and solve it independently."""
import argparse
import json
import math
from pathlib import Path
import re
import struct


def analyze(text):
    headers = re.findall(r'^ILLIXR_SOLVE_CAPTURE (.*)$', text, re.M)
    if len(headers) != 1:
        raise ValueError('Expected exactly one capture header')
    header = dict(re.findall(r'(\w+)=([^\s]+)', headers[0]))
    if header['present'] == '0':
        return {'captured': False}
    if text.count('ILLIXR_SOLVE_CAPTURE_END') != 1:
        raise ValueError('Incomplete capture')
    m, n, lda, ldb = (int(header[k]) for k in ('m', 'n', 'lda', 'ldb'))
    side, uplo, transpose, diagonal = header['flags']
    if m <= 0 or n <= 0 or lda < m or ldb < m or uplo not in 'UL' or transpose not in 'NT' or diagonal not in 'NU':
        raise ValueError('Invalid captured shape or flags')
    if side != 'L':
        raise ValueError('This independent diagnostic solver supports left solves only')
    sizes = [lda*m, ldb*n, ldb*n]
    arrays = [[None]*size for size in sizes]
    for array, offset, words in re.findall(r'^ILLIXR_SOLVE_DATA array=(\d+) offset=(\d+) ([0-9a-f ]+)\r?$', text, re.M):
        array, offset = int(array), int(offset)
        if array >= 3:
            raise ValueError('Invalid capture array')
        for i, word in enumerate(words.split()):
            index = offset+i
            if index >= sizes[array] or arrays[array][index] is not None or len(word) != 16:
                raise ValueError('Invalid, repeated or out-of-bounds capture element')
            arrays[array][index] = struct.unpack('>d', bytes.fromhex(word))[0]
    if any(x is None or not math.isfinite(x) for a in arrays for x in a):
        raise ValueError('Missing or non-finite capture elements')
    a, before, after = arrays
    def coefficient(i, j):
        if transpose == 'T':
            i, j = j, i
        if i == j and diagonal == 'U':
            return 1.
        if (uplo == 'U' and i > j) or (uplo == 'L' and i < j):
            return 0.
        return a[i+j*lda]
    upper = (uplo == 'U') != (transpose == 'T')
    expected = before.copy()
    for j in range(n):
        for i in (range(m-1, -1, -1) if upper else range(m)):
            value = before[i+j*ldb]*(1./.7)
            for k in (range(i+1, m) if upper else range(i)):
                value -= coefficient(i, k)*expected[k+j*ldb]
            expected[i+j*ldb] = value/coefficient(i, i)
    errors = []
    for index, (actual, reference) in enumerate(zip(after, expected)):
        if abs(actual-reference) > 1e-12+1e-10*abs(reference):
            errors.append({'row': index % ldb, 'column': index // ldb,
                           'actual': actual, 'reference': reference,
                           'absolute_error': abs(actual-reference)})
    def fixture(i):
        return ((i*17+11) % 41-20)/23.
    oracle_errors = sum(abs(x-fixture(i+2)) > 1e-12+1e-10*abs(fixture(i+2))
                        for i, x in enumerate(expected))
    residuals = []
    for j in range(n):
        for i in range(m):
            reconstructed = math.fsum(coefficient(i, k)*after[k+j*ldb] for k in range(m))
            rhs = before[i+j*ldb]*(1./.7)
            if abs(reconstructed-rhs) > 1e-12+1e-10*abs(rhs):
                residuals.append({'row': i, 'column': j, 'residual': reconstructed-rhs})
    return {'captured': True, 'metadata': header,
            'captured_input_host_solve_vs_original_fixture_errors': oracle_errors,
            'fpga_vs_host_solve_errors': len(errors),
            'max_absolute_error': max((e['absolute_error'] for e in errors), default=0.),
            'mismatches': errors, 'equation_residuals_above_tolerance': residuals}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = json.dumps(analyze(args.console.read_text()), indent=2)+'\n'
    if args.output:
        args.output.write_text(result)
    else:
        print(result, end='')
