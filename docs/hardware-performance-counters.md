# Rocket hardware performance counters

XRSight-RTOS can attribute Rocket HPM events to scheduled plugin execution,
including preemption, migration, synchronous prediction, and FP32 Gemmini worker
requests. Enable firmware profiling with `-DILLIXR_HPM_PROFILE=ON`. The default is
OFF; previous firmware settings and bitstreams remain usable without profiling.
Profiling requires a newly built Rocket image with thirteen programmable counters
on **every hart**. Old bitstreams cannot gain counters from a firmware update.

All packaged Rocket configurations now compose `WithNPerfCounters(13)`. Shuttle
configurations are unchanged. The hardware inspector checks the actual generated
CSR banks and pinned Rocket event ordering before accepting HPM hardware.

## Events

`mcycle` and `minstret` are the first two exported values. The following thirteen
40-bit counters use event map `rocket-hpm-v1`:

| CSR | Event | Selector |
|---|---|---|
| mhpmcounter3 | Load-use interlock | 0x101 |
| mhpmcounter4 | Long-latency/scoreboard interlock | 0x201 |
| mhpmcounter5 | CSR interlock | 0x401 |
| mhpmcounter6 | Instruction cache blocked | 0x801 |
| mhpmcounter7 | Data cache blocked | 0x1001 |
| mhpmcounter8 | Branch-direction misprediction | 0x2001 |
| mhpmcounter9 | Control-flow target misprediction | 0x4001 |
| mhpmcounter10 | Pipeline flush | 0x8001 |
| mhpmcounter11 | Pipeline replay | 0x10001 |
| mhpmcounter12 | Multiply/divide interlock | 0x20001 |
| mhpmcounter13 | Floating-point interlock | 0x40001 |
| mhpmcounter14 | Instruction cache acquire | 0x102 |
| mhpmcounter15 | Data cache acquire | 0x202 |

These selectors are specific to the pinned Rocket implementation. Interlock and
blocked events count cycles with the corresponding hardware predicate asserted;
they overlap and must not be summed as a stall percentage. Branch-direction and
target mispredictions, replays, and Rocket's explicit flush event remain separate.
Cache events are Rocket L1 acquire requests, not L2 misses or all Saturn/Gemmini
memory traffic. Use the existing FASED statistics for shared memory traffic.

## Attribution and measurement boundaries

Per-hart accumulators use Zephyr's thread-switch and ISR tracing callbacks. Each
thread has a registered logical owner. Counters are accumulated only while that
context is scheduled; blocked intervals are not charged to it. Each counter's
successive snapshots are subtracted on the same hart with its actual width.
No subtraction combines raw values read on different harts.

- Plugin workers normally own their own execution.
- `pose_prediction` temporarily owns its caller's execution and records whether
  render or timewarp requested the service. Exclusive totals are additive;
  separately reported inclusive caller totals are not.
- The FP32 accelerator worker inherits the requester's identity and records
  packing, accelerator submission/waiting while scheduled, and unpacking phases.
- The asynchronous INT8 worker belongs to `eye_tracking`. Reading its latest
  result belongs to timewarp; inference is not retroactively charged to a reader.
- Main/runtime, idle, ISR body, switch gaps, and measured profiler hook bodies
  have separate accounting categories.

The window brackets replay release and orderly completion, with each hart's
start/end timer timestamps exported. Bounded rendezvous and lifecycle work can
appear under main/system/profiler. Trace export occurs after accounting stops.
GPU model sleeps and blocked accelerator waits remain elapsed-time measurements,
not plugin CPU execution or GPU internal performance counters.

Existing ISR hooks surround the handler body, not the entire trap prologue and
epilogue. Kernel work outside the hooks remains in the surrounding scheduled
context. Ordered CSR reads have sampling skew; the exported maximum read span
and hook-body overhead expose part of that cost. Hook-body overhead is a lower
bound, not an exact correction. Events delayed in hardware can cross attribution
boundaries. These statistics measure scheduled contexts, not instruction-perfect
causal ownership of every memory event.

Rocket's cycle counter can stop during CSR/WFI stalls; it is not a replacement
for CLINT time or the FireSim target-cycle count. Summed plugin/core cycles must
not be presented as elapsed application cycles. Hardware performance must be
measured on RTL/FPGA, not inferred from Spike instruction timing.

## Export

The existing checksummed batched transport carries these version-1 records:

- `ILLIXR_HPM_PREFLIGHT`: CSR selector/readback and progress checks for each hart.
- `ILLIXR_HPM_SELFTEST`: blocked-time exclusion, prediction service ownership,
  first-switch ownership before any application scope, forced migration on SMP,
  observed hart mask, ISR instruction attribution, and conservation checks.
- `ILLIXR_HPM_CONFIG`: event order, selectors, widths, and attribution policy.
- `ILLIXR_HPM_WORK`: exclusive totals keyed by hart, plugin, phase, and caller.
- `ILLIXR_HPM_HART`: measurement boundaries, total counters, sampling cost, errors.
- `ILLIXR_HPM_THREAD`: registered owner and migration counts.

Counters are aggregated in bounded RAM. There is no per-switch UART logging.
Switch hooks query the scheduler's current thread directly: Zephyr's optional
thread-local `k_current_get()` cache is not initialized until thread entry, after
the first switch-in callback. Using that cache in the hook loses initial thread
ownership and can undercount migrations.
ISR hooks read the incoming interrupt state and skip redundant `mstatus` mask
and restore writes when interrupts were already disabled, retaining compiler
ordering. Callers entering with interrupts enabled still acquire and release the
interrupt lock. In the FPGA investigation, removing only redundant restore
writes passed preflight but did not resolve a full-workload startup stall. An
original-layout control replacing the two remaining ISR mask writes with reads
passed the complete single-core Gemmini workload. The source-built conditional
lock revision passed the complete single-/quad-core RVV/Gemmini matrix below. These controls do not
establish the underlying microarchitectural mechanism.
New self-tests require positive `isr_instructions`; the host still accepts legacy
self-test records without that field. A missing-ISR-callback negative control
must fail validation even if its BLAS tests succeed.
The host rejects duplicate identities, missing required records, unsupported
maps, failed preflights, and totals that do not conserve each hart's counters.
Profiling firmware must match a hardware manifest with verified HPM support.
Packaging audits the actual Ninja compile definitions for application and plugin
object libraries; enabled firmware without a matching audit is rejected.
Absent data in a legacy run is unavailable, never zero.

## One-command report

From the repository root:

```sh
python3 scripts/summarize_performance.py \
  /path/to/results /path/to/another/matrix \
  --output /path/to/performance-report
```

Inputs may be run directories or matrix roots. The command preserves original
results, decodes available batched logs in temporary storage, and writes:

- `summary.md` and `summary.json`: performance and recorded acceptance results.
- `runs.csv`: clocks, runtimes, VIO/sample counts, queues, and profiling availability.
- `profiling_overhead.csv`: matched profiling-on/off workload deltas and ratios;
  pairing requires identical bitstream, backend, dataset, and clock settings.
- `plugins.csv` and `plugin_harts.csv`: cycles, instructions, IPC/CPI, event counts,
  per-thousand-instruction rates, overlapping event-active fractions, and placement.
- `accelerators.csv`: BLAS, Gemmini, packing/unpacking, scratch and latency records.
- `placement.csv` and `display_eye.csv`: plugin placement, eye inference/reuse,
  render/timewarp deadlines, and presentation outcomes.

Existing numerical comparisons are preserved. Reporting does not fabricate a
native pass when native replay was not run. Interrupted or timed-out cases remain
incomplete even when partial metrics are available. It neither programs an FPGA
nor starts a workload.

Diagnostic runs stay visible but are excluded from automatic profiling-overhead
comparisons. Set `"diagnostic": true` in a run's `case.json` or metadata, or add a
`classification.json` sidecar containing `{"diagnostic": true, "reason": "..."}`
without changing the original records. The reporter includes classification
hashes in its provenance. Classification does not turn a failed run into a pass.

## Validation status

The final source-built profiler (`r5`) passed all four profiled FPGA workloads:
single- and quad-core Rocket+Saturn with RVV or FP32 Gemmini OpenBLAS, including
INT8 eye tracking. Matching unprofiled controls also passed, for eight accepted
full workloads. All delivered 501 IMUs to both consumers, accounted for 50
cameras, matched native VIO replay exactly, produced correct eye predictions,
and passed prediction, presentation, complete-trace and orderly-exit checks.
All nine plugins have valid HPM records in each profiled case. Quad-core work
counters confirm processing on all four harts; accelerator workers remain pinned
to hart 0. See [the final results](hpm-firesim-results.md).

All six final firmware artifacts passed build/configuration/symbol and memory
audits. Profiler objects contain no FP/vector instructions. Single- and quad-core
HPM bitstreams passed routed timing and packaging checks. Final preflights passed
610,297 and 610,745 BLAS checks respectively, including counter attribution and
vector-context checks. The self-tests observed hart masks `0x1` and `0xF` and
positive ISR instruction counts. The missing-ISR-callback negative control was
correctly rejected despite passing its BLAS checks.

The focused Rocket RTL test passed 1,513,588 checks and positively exercised all
thirteen selected events using external frontend/cache/FPU stimuli. This tests
counter wiring and selector behavior, not full-SoC cache performance. See
[the harness](../tests/hpm/README.md).

Evidence is preserved under `/scratch/prashanth_illixr_hpm_20261006T011137Z`;
`reports/final-r5/` contains the accepted eight-case comparison, detailed JSON,
CSV exports and provenance. Earlier stalls and diagnostic controls remain
preserved separately and are excluded from production comparisons. The earlier
restore-only candidate was insufficient; the final conditional mask/restore
implementation passed the matrix, but the underlying microarchitectural stall
mechanism remains unproven. No RTL or scheduler change was made for this workaround.

These are firmware-build and FPGA-runtime results using the existing dependency
environment, not verification of a fresh README dependency bootstrap.
