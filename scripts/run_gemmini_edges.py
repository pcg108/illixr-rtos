#!/usr/bin/env python3
"""Run supplemental BLAS boundary fixtures; never substitute for the full gate."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

from run_rocket import capture, simulator_command
from blas_analysis import valid_gemmini_edge_record
from simulator_counter import counter_parameters


def analyze_edges(console, execution):
    errors = []
    def records(prefix):
        return [json.loads(line[len(prefix):]) for line in console.splitlines()
                if line.startswith(prefix)]
    tests = records('ILLIXR_GEMMINI_EDGE ')
    work = records('ILLIXR_GEMMINI ')
    headers = records('ILLIXR_BLAS ')
    if len(headers) != 1 or headers[0].get('backend') != 'openblas_gemmini_fp32':
        errors.append('Selected Gemmini backend did not initialize')
    if len(tests) != 1 or not valid_gemmini_edge_record(tests[0]):
        errors.append('Incomplete or failed supplemental numerical fixtures')
    if len(work) != 4 or {x.get('name') for x in work} != {'sgemm','dgemm','sgemv','dgemv'}:
        errors.append('Missing or duplicate accelerator dispatch records')
    for x in work:
        expected_submissions = 1 if x.get('name','').endswith('gemm') else 4
        if (x.get('phase') != 'edge' or x.get('precision') != 'fp32' or
                x.get('accelerator_hart_mask') != 1 or x.get('submissions') != expected_submissions):
            errors.append('Incorrect accelerator dispatch or ownership')
        if not 0 <= x.get('scratch_high_water', -1) <= 32*1024*1024:
            errors.append('Scratch bounds evidence failed')
    complete = (execution.get('returncode') == 0 and
                console.count('ILLIXR_GEMMINI_EDGE_END pass') == 1 and
                not any(execution.get(k) for k in ('timed_out','interrupted','cycle_limit_reached')))
    if not complete:
        errors.append('Supplemental execution is incomplete or did not exit normally')
    return dict(passed=not errors, complete=complete, errors=errors, tests=tests, work=work)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--platform', choices=['spike','verilator'], required=True)
    p.add_argument('--simulator', type=Path, required=True)
    p.add_argument('--extension', type=Path)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--harts', type=int, choices=[1,4], required=True)
    p.add_argument('--chipyard', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--counter-manifest', type=Path)
    a = p.parse_args()
    if a.platform == 'spike' and not a.extension:
        p.error('Spike requires the matching FP32 Gemmini extension')
    if a.platform == 'verilator' and not a.chipyard:
        p.error('Verilator requires its matching Chipyard tree')
    counter = counter_parameters(a.platform, a.simulator, a.counter_manifest)
    a.output.mkdir(parents=True, exist_ok=False)
    elf = a.output/'firmware.elf'
    shutil.copy2(a.elf, elf)
    if a.platform == 'spike':
        command = [str(a.simulator), '--extlib='+str(a.extension), '--extension=gemmini',
                   f'-p{a.harts}', '-m0x80000000:0x10000000',
                   '--isa=rv64imafdcv_zicsr_zifencei_zicntr_zvl256b',
                   '--instructions=100000000000', str(elf)]
    else:
        command = simulator_command(a.simulator, elf, a.chipyard, counter['counter_limit'])
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    record = dict(status='running', supplemental_only=True, harts=a.harts,
                  platform=a.platform, command=command, elf_sha256=digest(elf),
                  simulator_sha256=digest(a.simulator), timeout_seconds=86400,
                  **counter)
    if a.extension:
        record['extension_sha256'] = digest(a.extension)
    output = a.output/'run.json'
    def save():
        tmp = output.with_suffix('.tmp')
        tmp.write_text(json.dumps(record, indent=2)+'\n')
        tmp.replace(output)
    save()
    try:
        record.update(capture(command, a.output, 86400))
        record.update(analyze_edges((a.output/'console.log').read_text(), record))
        record['status'] = 'pass' if record['passed'] else 'fail' if record['complete'] else 'incomplete'
    except BaseException as error:
        record.update(status='incomplete', passed=False, error=repr(error))
        raise
    finally:
        save()
    print(json.dumps(record, indent=2))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
