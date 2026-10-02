#!/usr/bin/env python3
"""Extract an operation with verified matching inputs into a standalone C fixture.

Logical inputs come from the byte-repeatable CPU/Spike reference. Strides,
overlapping views, scales, and address alignment modulo 4096 are retained.
Absolute DRAM addresses and preceding accelerator history are deliberately not
claimed identical; a passing isolated fixture does not clear the full graph.
"""
import argparse
import hashlib
import json
from pathlib import Path
from analyze_ritnet_checkpoints import analyze, read_trace


def extract(record, reference, destination):
    reference, destination = Path(reference), Path(destination)
    golden = [json.loads(x) for x in (reference / 'operations.jsonl').read_text().splitlines()]
    op = golden[record['id'] - 1]
    if record['input_hash'] != op['input_hash'] or not record['drained']:
        raise ValueError('Fixture requires verified completed, matching inputs')
    # In-place residual inputs must have been captured before the write.
    # A matching trace hash alone does not validate an exported input file.
    for i, view in enumerate(op['views']):
        if not view['rows']:
            continue
        file = reference / (f'op{op["id"]:02d}-input{i}.bin' if i < 3 else f'op{op["id"]:02d}-output0.bin')
        data = file.read_bytes()
        value = 14695981039346656037
        for byte in data:
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        expected = op['input_hash'][i] if i < 3 else op['output_hash']
        if len(data) != view['rows'] * view['cols'] * view['element_bytes'] or value != expected:
            raise ValueError(f'Exported tensor {file} does not match its pre/post-operation fingerprint')
    destination.mkdir(parents=True, exist_ok=False)
    views = record['views']
    spans = sorted((v['address'], v['address'] + ((v['rows'] - 1) * v['stride'] + v['cols']) * v['element_bytes'])
                   for v in views if v['address'])
    groups = []
    for lo, hi in spans:
        if groups and lo < groups[-1][1]:
            groups[-1][1] = max(groups[-1][1], hi)
        else:
            groups.append([lo, hi])
    code = ['#include "include/gemmini.h"', '#include <string.h>', '#include <stdint.h>',
            '#ifdef RITNET_HOST_REFERENCE', '#define FIXTURE_EXECUTION CPU', '#else',
            '#define FIXTURE_EXECUTION WS', '#endif']
    for i, (lo, hi) in enumerate(groups):
        code.append(f'static unsigned char storage{i}[{hi-lo+lo%4096}] __attribute__((aligned(4096)));')
    for i, v in enumerate(views):
        if not v['address']:
            code.append(f'#define ptr{i} NULL')
            continue
        j = next(j for j, (lo, hi) in enumerate(groups) if lo <= v['address'] < hi)
        code.append(f'#define ptr{i} (storage{j}+{v["address"]-groups[j][0]+groups[j][0]%4096})')
    files = {}
    for i in range(4):
        if not views[i]['address']:
            continue
        source = reference / (f'op{op["id"]:02d}-input{i}.bin' if i < 3 else f'op{op["id"]:02d}-output0.bin')
        data = source.read_bytes()
        target = destination / f'tensor{i}.bin'
        target.write_bytes(data)
        files[target.name] = hashlib.sha256(data).hexdigest()
        code += [f'extern const unsigned char tensor{i}[];',
                 'asm(' + json.dumps(f'.pushsection .rodata\n.balign 64\n.global tensor{i}\ntensor{i}:\n.incbin "{target.resolve()}"\n.popsection\n') + ');']
    parameters = [repr(x) for x in op['parameters']]
    kind = op['kind']
    pointers = ['(const elem_t*)ptr0', '(const elem_t*)ptr1', '(const acc_t*)ptr2', '(elem_t*)ptr3']
    if kind == 'tiled_matmul_auto':
        args = parameters[:3] + pointers + parameters[3:]
    elif kind in ('tiled_conv_auto', 'tiled_conv_stride_auto'):
        kind = 'tiled_conv_stride_auto'
        args = parameters[:20] + pointers + parameters[20:]
    elif kind == 'tiled_conv_dw_auto':
        args = parameters[:9] + pointers + parameters[9:]
    elif kind == 'tiled_resadd_auto':
        args = parameters[:5] + [pointers[0], pointers[1], pointers[3]] + parameters[5:]
    else:
        raise ValueError('Unknown operation')
    code += ['unsigned ritnet_fixture_run(void) {', 'gemmini_fence();']
    code += [f'memset(storage{i},0,sizeof(storage{i}));' for i in range(len(groups))]
    for i, v in enumerate(views[:3]):
        if not v['address']:
            continue
        row, stride = v['cols'] * v['element_bytes'], v['stride'] * v['element_bytes']
        code.append(f'for(unsigned r=0;r<{v["rows"]};r++) memcpy(ptr{i}+r*{stride},tensor{i}+r*{row},{row});')
    code += ['#ifndef RITNET_HOST_REFERENCE', 'gemmini_flush(0);', '#endif',
             kind + '(' + ','.join(args + ['FIXTURE_EXECUTION']) + ');', 'gemmini_fence();',
             'unsigned differences=0;']
    out = views[3]
    row, stride = out['cols'] * out['element_bytes'], out['stride'] * out['element_bytes']
    code += [f'for(unsigned r=0;r<{out["rows"]};r++) for(unsigned c=0;c<{row};c++) differences+=ptr3[r*{stride}+c]!=tensor3[r*{row}+c];',
             'return differences;', '}']
    # Parenthesize the pointer macro when indexing it.
    code[-3] = code[-3].replace('ptr3[', '(ptr3)[')
    (destination / 'fixture.c').write_text('\n'.join(code) + '\n')
    manifest = dict(operation=op, observed=record, files=files, storage_groups=groups,
                    alignment_modulus=4096, absolute_addresses_preserved=False,
                    command_order='Original helper call; no preceding graph operations',
                    reference_inputs='Verified matching logical CPU/Spike inputs; stride gaps zeroed')
    (destination / 'fixture.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--trace', type=Path, required=True)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    data = read_trace(args.trace)
    result = analyze(data, args.reference)
    first = result.get('first_divergence')
    if not result.get('trace_valid') or not first or not first['inputs_match']:
        p.error('A valid first-divergence record with matching inputs is required')
    record = next(r for r in data['operations'] if r['inference'] == first['inference'] and r['id'] == first['id'])
    extract(record, args.reference, args.output)


if __name__ == '__main__':
    main()
