# Rocket validation status

Status recorded on 2026-09-26 UTC. All three platform preflights passed on actual
Chipyard Verilator models. The user requested stopping Verilator after the new
FireSim workload matrix passed. The active single-core workload was gracefully
interrupted at 22:42 UTC and is recorded as incomplete; the remaining four
workloads were not run. The simulator, matrix runner, continuation process, and
report observer have exited. Logs and partial results are preserved in the
[cancellation record](/home/prashanth/illixr-rocket-work/results/user-stop-20260926T224223Z.json).
A preflight pass alone establishes startup, shared publication, and clock behavior.

The current [comparison report](/home/prashanth/illixr-rocket-work/results/comparison.md)
and [machine-readable summary](/home/prashanth/illixr-rocket-work/results/summary.json)
reflect the preserved cancellation state. The
[execution log](/home/prashanth/illixr-rocket-work/results/matrix-console.log)
records case transitions. No full workload pass is claimed by this snapshot.

| Harts | Observed startup mask | Atomic exchanges | Timer ticks | Core cycles | Host seconds | Result |
|---:|---|---:|---:|---:|---:|---|
| 1 | `0x1` | 2,048 | 5,000 | 4,999,434 | 717.8 | Pass |
| 2 | `0x3` | 2,048 | 5,000 | 4,999,388 | 617.1 | Pass |
| 4 | `0xf` | 4,096 | 5,000 | 4,999,459 | 851.8 | Pass |

Each timer measurement covered 10 ms: 5,000 ticks of the 500 kHz CLINT and
approximately 5 million cycles of the 500 MHz Rocket clock. Each preflight exited
normally through HTIF. The startup workers exercised every configured hart;
per-plugin placement requires the separate processing/publication counters from
the workload cases.

The workload order is single-core baseline, dual-core scheduler-managed,
dual-core pinned, quad-core scheduler-managed, then quad-core pinned. Every case
has a 24-hour host watchdog and a 100-billion-TestDriver-cycle limit. Interrupted
or timed-out cases remain incomplete. The initial standard-model dual preflight
was interrupted during performance investigation and is retained separately as
incomplete; the passing dual preflight is attempt 2.

All eight firmware images, original and optimized simulator binaries, generated
hardware, source snapshots, configurations, and hashes are retained under
`/home/prashanth/illixr-rocket-work`. Optimized models use the same generated RTL
and hardware clocks, with host compiler/code-generation improvements and an
unused testbench reference clock reduced from 1 GHz to 500 MHz. Thus their cycle
limit represents 200 simulated seconds. The initial short boot benchmark improved
from approximately 3,337 to 18,253 Rocket cycles per host second; full workload
performance is still to be measured.

All ten preserved Spike firmware images match their archived hashes. Estimator
math and calibration are unchanged, and Chipyard's existing source changes were
preserved. The validation tooling passes 52 tests. Native comparison uses this
port's estimator and the actual delivered input sequence at 1 mm / 0.001 rad
tolerances. Previously observed physical trajectory drift remains unresolved;
native agreement is a runtime-equivalence check, not an accuracy result.

See [the workflow](rocket-validation.md) for build/run commands and record formats.
