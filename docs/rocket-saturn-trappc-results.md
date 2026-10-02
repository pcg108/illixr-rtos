# Corrected Rocket/Saturn validation

The scalar-exception-PC priority patch passed validation on the single-core
Rocket/Saturn FPGA image. All six validation cases completed normally and passed:
private timer interrupt reproducer, full five-phase BLAS fixture, rejected-FDIV
fixture, competing FP-thread fixture, scalar full pipeline and RVV full pipeline.
The identical preserved firmware ELFs were used; no IRQ masking or internal-kernel
fence workaround was added to the production workload binaries.

The private timer reproducer improved from 14/64 failing rounds on the original
image to 0/64 on the patched image. The full five-phase BLAS fixture passed all
1,747,680 comparisons, including both normal interrupt-enabled baselines.
This validates the candidate trap-PC correction for these tests and workloads.
It does not constitute exhaustive verification of every vector exception case.

Both full workloads delivered 501 IMUs to each consumer, accounted for all 50
camera pairs (16 processed, 31 skipped, 3 dropped), and produced 14 VIO poses.
Each run's actual delivered inputs were replayed through the native harness:
maximum position and orientation differences were both zero. Both workloads had
zero render/timewarp deadline misses, zero probe deadline misses, 183 fresh
on-time presentations, complete traces and normal exit.

| Metric | Scalar OpenBLAS | RVV OpenBLAS |
|---|---:|---:|
| Application modeled time (s) | 6.425051 | 5.500016 |
| Trace-export modeled time (s) | 1.065028 | 0.925048 |
| VIO poses | 14 | 14 |
| DGEMM calls | 979 | 979 |
| Aggregate DGEMM cycles | 1,196,307,097 | 250,680,748 |
| Host runtime including setup (s) | 448.26 | 426.94 |

Application time was about 14.4% lower with RVV in these runs. Aggregate DGEMM
cycle intervals were about 4.77x shorter, but include preemption; this is not an
isolated kernel latency measurement. Clock interpretation remains modeled 1 GHz
CPU / 1 MHz timer, 10 kHz Zephyr ticks, with the existing display schedule.
Trajectory accuracy remains separate from native runtime equivalence.

Routed setup WNS is +0.108 ns and hold WHS +0.010 ns, with no failing endpoints.
Source, patch, firmware, driver and bitstream hashes and complete acceptance
records are preserved in:
`/scratch/prashanth_illixr_openblas_20260927T162930Z/hardware-rocket-saturn-single-trappcfix/control/validation.json`.
Run artifacts are under `runtime/rocket-saturn-single-trappcfix-*` in that workspace.

Shuttle's independent triangular-solve failure remains unresolved. Internal
kernel-return fences passed its two fenced test phases, but ordering versus delay
has not been separated and no production fence workaround has been validated.
These results apply to single-core Rocket/Saturn only.
