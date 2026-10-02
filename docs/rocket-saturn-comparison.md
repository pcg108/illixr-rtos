# Single-core Rocket/Saturn comparison

**Results update:** both images are built and timing-verified. Corrected Shuttle
passed scalar and RVV full workloads; Rocket passed scalar but its original RVV
attempt hit the cycle limit. See [the results and diagnostic status](saturn-corrected-results.md).
The following records the original setup and validation plan.

The Rocket/Saturn image is building alongside the corrected Shuttle/Saturn image.
Neither new image is accepted until generated-hardware checks, routed timing,
platform diagnostics and full workloads pass.

Workspace: `/scratch/prashanth_illixr_openblas_20260927T162930Z`.
Rocket snapshot: `hardware-rocket-saturn-single/`.
Corrected Shuttle snapshot: `hardware-single-fdivfix/`.

The [Rocket configuration](../config/FireSimILLIXRSingleRocketSaturnConfig.scala)
uses REFV256D128 Saturn: VLEN 256, ELEN 64, 128-bit execution/memory datapaths,
FP64 and a dedicated vector FP backend. Vector loads/stores use a separate
coherent TileLink path, as on Shuttle; the Rocket integration's default L1
vector path is explicitly disabled.

Both targets use a 128-bit system bus, 32 KiB instruction cache, 16 KiB data
cache, 64-byte cache lines and four outstanding data-cache misses. Rocket's
Huge-core baseline is adjusted to four D-cache ways and four MSHRs to match
these capacities. Rocket and Shuttle still have different scalar pipelines,
cache implementations, fetch behavior and branch prediction. This compares
complete core implementations with aligned resource sizes, not issue width
alone.

RAM remains 256 MiB. Generated clocks remain 500 MHz / 500 kHz, interpreted by
the unchanged firmware as modeled 1 GHz / 1 MHz with 10 kHz Zephyr ticks. Both
use identical FASED latency-bandwidth settings: 30-cycle read/write latency and
10 read/write requests. FPGA request: 30 MHz, NORETIMING. These modeled target
frequencies are not ASIC timing-closure claims.

The builds use separate source, Scala, Vivado and temporary directories, four
workers each on disjoint CPU masks and nice 10. The shared 48 GiB available-memory
reserve, 64 GiB per-process ceiling and new-OOM stop remain active. PSI is warning
only; a resource stop does not automatically restart a build.

## Validation and comparison

Each image must pass timing and packaging checks before programming. Validators
share the existing U250 runtime lock and check all task hardware recovery holds.
They never program the FPGA concurrently or terminate unrelated FPGA jobs.

Both run the identical immutable ELFs in this order:

1. Platform checks and the rejected-FDIV destination-register test.
2. Platform checks and 80,000 arithmetic checks with a competing FP thread.
3. The original scalar OpenBLAS full workload.
4. The original RVV OpenBLAS full workload, including vector preflight/self-tests.

These ELFs predate the new image bounds checks. A software bounds correction
therefore cannot conceal the hardware result. Any diagnostic or workload failure
blocks subsequent cases on that image. Each workload retains the 24-hour host
watchdog and 100-billion-cycle cap. Interrupted or timed-out cases are incomplete.

The application retains 50 stereo pairs, 501 IMUs, scheduler-managed placement,
the existing render/timewarp schedule and GPU delays, and batched trace export.
All delivery/accounting, native estimator replay, prediction, placement and
presentation acceptance checks remain enabled.

Compare scalar against scalar and RVV against RVV across cores, then compare
backends within each core. Report application time/cycles separately from trace
export and host runtime; include VIO poses, camera processed/skipped/dropped,
queue high-water marks, BLAS calls/cycles and display deadlines/freshness.
Different scheduling can change the delivered camera sequence and amount of
estimator work. Runtime ratios alone therefore do not establish computational
speedup; native agreement uses each run's actual delivered sequence. Trajectory
accuracy remains separate from runtime equivalence.

Live state and pinned firmware hashes are in each snapshot's
`control/validation.json`; build status and logs are in `build-runs/`.
All earlier hardware and firmware are preserved. The initial corrected Shuttle
attempt completed RTL generation but encountered a localhost SSH disconnect
before synthesis. After successful SSH checks it was resumed as a new attempt;
the failed attempt remains recorded, and no guard limits were changed.
