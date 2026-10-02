#!/usr/bin/env python3
"""Run controlled RITNet follow-ups on exclusively locked, accepted FireSim images.

Configuration supplies preserved firmware/reference/hardware paths. Results and
ELF variants are never overwritten. An interrupted case or malformed trace stops
the sequence. A numerical failure is retained as diagnostic evidence.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import time
from types import SimpleNamespace

import run_firesim_matrix as fs
import ritnet_validation
from analyze_ritnet_checkpoints import analyze, read_trace
from analyze_spike import records, startup_checks
from blas_analysis import vector_preflight_errors
from extract_ritnet_fixture import extract
from ritnet_diagnostic_variant import make


def execution_is_incomplete(run, analysis):
    """A target-reported numerical failure is evidence; a crash is not a trial.

    The general FireSim monitor terminates its manager on the FAILED marker,
    even after a complete HTIF failure exit. Preserve the failed verdict while
    permitting the next diagnostic control when the trace is complete.
    """
    return (not analysis.get('complete') or
            any(run.get(k) for k in ('timed_out', 'interrupted', 'cycle_limit_reached')) or
            any(marker != '*** FAILED ***' for marker in run.get('fatal_markers', [])))


def narrow(candidates, trial, budget=24):
    """Keep a passing subset; failures mean not sufficient in this bounded trial.

    Returns a sufficient observed set, not a proof of a globally minimal fix.
    The caller must establish a failing empty set and passing full set first.
    """
    calls = 0
    partitions = 2
    while len(candidates) > 1 and calls < budget:
        chunks = [candidates[i::partitions] for i in range(partitions)]
        choices = chunks + [[x for x in candidates if x not in c] for c in chunks]
        seen = set()
        reduced = False
        for choice in choices:
            key = tuple(choice)
            if not choice or len(choice) == len(candidates) or key in seen:
                continue
            seen.add(key)
            calls += 1
            if trial(choice):
                candidates = choice
                partitions = 2
                reduced = True
                break
            if calls >= budget:
                break
        if not reduced:
            if partitions >= len(candidates):
                break
            partitions = min(len(candidates), partitions * 2)
    return candidates, calls


class Runner:
    def __init__(self, config):
        self.c = config
        self.out = Path(config['output'])
        self.out.mkdir(parents=True, exist_ok=False)
        self.state = dict(status='waiting', pid=os.getpid(), completed=[])
        self.save()
        self.serial = 0
        self.reference = Path(config['reference'])

    def save(self):
        fs.write_json(self.out / 'state.json', self.state)

    def guards(self):
        if Path(self.c['guard_latch']).exists():
            raise RuntimeError('Resource guard latch: no automatic restart')

    def wait_for(self, path, expected):
        while True:
            self.guards()
            if Path(path).exists():
                state = json.loads(Path(path).read_text())
                if state['status'] == expected:
                    return
                if state['status'] in ('failed_or_incomplete', 'resource_stopped'):
                    raise RuntimeError('Prerequisite stopped: ' + str(path))
            time.sleep(10)

    def run(self, hardware, label, mode, mask=(1 << 64) - 1, full=False):
        self.guards()
        root = Path(self.c['hardware'][hardware])
        if (root / 'control/runtime-recovery-required.json').exists():
            raise RuntimeError('FPGA programming recovery required')
        self.serial += 1
        name = f'{self.serial:02d}-{hardware}-{label}'
        artifact = self.out / 'artifacts' / name
        artifact.parent.mkdir(exist_ok=True)
        source = Path(self.c['full_artifact'] if full else self.c['standalone_artifact'])
        make(source, artifact, mode, 32, mask)
        case = json.loads(Path(self.c['full_case'] if full else self.c['standalone_case']).read_text())
        directory = self.out / 'runtime' / name
        case.update(name='ritnet-diagnostic-' + name, elf=str(artifact / 'zephyr.elf'),
                    hardware_manifest=str(root / 'control/hardware-1.json'),
                    output=str(directory), zero_out_dram=True,
                    timeout_seconds=86400, max_cycles=100000000000,
                    linalg_backend='openblas_rvv' if full else 'eigen')
        args = SimpleNamespace(work=root, dataset=Path(self.c['dataset']), native=Path(self.c['native']),
                               prediction_native=Path(__file__).resolve().parents[1] / 'tests/native/prediction_reference.py')
        self.state.update(status='running', current=name)
        self.save()
        with (root / 'control/runtime.lock').open('a+') as own:
            fcntl.flock(own, fcntl.LOCK_EX | fcntl.LOCK_NB)
            passed, interrupted = fs.run_case(case, args)
        if interrupted:
            raise RuntimeError('Interrupted FPGA diagnostic remains incomplete')
        run = json.loads((directory / 'run.json').read_text())
        general = json.loads((directory / 'analysis.json').read_text())
        if execution_is_incomplete(run, general):
            raise RuntimeError('Incomplete or crashed FPGA diagnostic')
        data = records(directory / 'console.log')
        startup = startup_checks(data, 1, 1000000, 1000000000, False)
        if not full:
            startup += vector_preflight_errors(data, 1)
        if startup:
            raise RuntimeError('Platform startup failed: ' + str(startup))
        trace = read_trace(directory / 'console.log')
        report = analyze(trace, self.reference)
        fs.write_json(directory / 'checkpoint-analysis.json', report)
        result = dict(case=name, passed=passed, trace_valid=report.get('trace_valid', False),
                      first_divergence=report.get('first_divergence'), mask=mask, mode=mode, full=full,
                      output=str(directory))
        self.state['completed'].append(result)
        self.save()
        if not report.get('trace_valid'):
            raise RuntimeError('Malformed/incomplete checkpoint stream: ' + str(report['errors']))
        if trace['capture']:
            (directory / 'captured-tensor.bin').write_bytes(b''.join(bytes.fromhex(c['hex']) for c in trace['capture']))
        first = report.get('first_divergence')
        if first and first['inputs_match']:
            record = next(r for r in trace['operations'] if (r['inference'], r['id']) == (first['inference'], first['id']))
            extract(record, self.reference, directory / 'isolated-fixture')
        return passed

    def execute(self):
        try:
            if not self.c.get('prioritize_narrowing', False):
                self.wait_for(self.c['initial_matrix_state'], 'diagnostics_completed')
            self.wait_for(self.c['spike_gate_state'], 'passed')
            # The external runner's production two-inference preflight checker
            # is replaced only in this diagnostic process, never in production.
            def standalone(data, text, harts, dual, vector_required=False):
                errors = []
                if 'ILLIXR_RITNET_STANDALONE_END pass' not in text:
                    errors.append('Standalone diagnostic numerical failure')
                eye = data.get('eye_results', [])
                if len(eye) != 32 or any(not r['valid'] or r['output_hash'] != 10875396086082019864 for r in eye):
                    errors.append('Incomplete or differing RITNet outputs')
                if any(r['accelerator_hart'] != 0 for r in eye):
                    errors.append('Incorrect accelerator ownership')
                if vector_required:
                    errors += vector_preflight_errors(data, harts)
                return errors
            ritnet_validation.standalone_errors = standalone
            with Path(self.c['global_lock']).open('a+') as board:
                fcntl.flock(board, fcntl.LOCK_EX | fcntl.LOCK_NB)
                if not self.c.get('prioritize_narrowing', False):
                    for hardware in ('dual', 'int8'):
                        self.run(hardware, 'full-metadata', 1, full=True)
                        self.run(hardware, 'full-tensor', 3, full=True)
                self.state['phase'] = 'fence_controls'
                self.save()
                empty = self.run('dual', 'drain-none', 2, 0)
                all_boundaries = self.run('dual', 'drain-all', 2)
                if empty or not all_boundaries:
                    self.state['narrowing'] = 'Control separation did not reproduce in this linked firmware; no sufficiency conclusion'
                else:
                    self.state['phase'] = 'fence_boundary_narrowing'
                    self.save()
                    def trial(ids):
                        return self.run('dual', 'drain-subset', 2, sum(1 << (i - 1) for i in ids))
                    ids, count = narrow(list(range(1, 65)), trial, self.c.get('narrow_budget', 24))
                    self.state['narrowing'] = dict(observed_sufficient_boundaries=ids, trials=count,
                                                  qualification='32-inference bounded trials; not an established fix')
                    mask = sum(1 << (i - 1) for i in ids)
                    self.run('int8', 'localized-drain-none', 2, 0)
                    self.run('int8', 'localized-drain-subset', 2, mask)
                    self.run('dual', 'full-localized-drain', 2, mask, full=True)
                if self.c.get('prioritize_narrowing', False):
                    self.state['phase'] = 'deferred_confirmation'
                    self.save()
                    self.run('int8', 'deferred-tensor', 3)
                    for hardware in ('dual', 'int8'):
                        self.run(hardware, 'full-metadata', 1, full=True)
                        self.run(hardware, 'full-tensor', 3, full=True)
            self.state['status'] = 'diagnostics_completed'
            self.save()
        except BaseException as error:
            self.state.update(status='failed_or_incomplete', error=repr(error))
            self.save()
            raise


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('config', type=Path)
    args = parser.parse_args()
    Runner(json.loads(args.config.read_text())).execute()
