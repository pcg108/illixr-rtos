# Spike validation results

Completed on 2026-09-26 UTC against application base revision
`6610186ae276ab2580e4eff1bddc2c8710a5e23e` plus the working-tree changes described
in [Clock-paced replay on Spike](spike-clock-replay.md).

**All 14 runtime scenarios passed their checks. Pose accuracy remains unresolved.**
There were 12 successful firmware exits and two deliberately induced, correctly
reported IMU-overflow failures. Across the successful runs, all 281 published VIO
pose records exactly matched native replay of the same delivered input sequence.
The native harness compiles this port's existing `SLAMMath` estimator and exact
calibration. This agreement validates published output equivalence; it does not
establish physical accuracy or equality of every internal estimator variable.

## Executed matrix

All cases used the V1_02_medium EuRoC prefix embedded in the ELF. The 50-pair
prefix contains 501 IMU samples; the 200-pair prefix contains 2,001. Each successful
run delivered every embedded IMU to both VIO and the independent integrator.

| Case | Harts | Runs | Cameras processed / skipped / dropped per run | VIO poses per run | Result |
| --- | ---: | ---: | --- | ---: | --- |
| 50-pair smoke | 1 | 1 | 19 / 12 / 19 | 17 | Pass |
| 50-pair smoke | 2 | 1 | 25 / 0 / 25 | 23 | Pass |
| 200-pair extended | 1 | 3 | 29 / 48 / 123 | 27 | Pass, all three |
| 200-pair extended | 2 | 3 | 37 / 0 / 163 | 35 | Pass, all three |
| 50 pairs, 250 ms VIO delay | 1 | 1 | 18 / 11 / 21 | 16 | Pass |
| 50 pairs, 250 ms VIO delay | 2 | 1 | 24 / 0 / 26 | 22 | Pass |
| 50 pairs, camera FIFO 2, 1 s VIO delay | 1 | 1 | 8 / 7 / 35 | 6 | Pass |
| 50 pairs, camera FIFO 2, 1 s VIO delay | 2 | 1 | 13 / 0 / 37 | 11 | Pass |
| IMU FIFO 2 | 1 | 1 | Aborted on IMU overflow | 0 | Expected failure passed |
| IMU FIFO 2 | 2 | 1 | Aborted on IMU overflow | 0 | Expected failure passed |

Within each extended configuration, all three repetitions had identical input
order, camera outcomes, published poses, and probe counts. Single- and dual-hart
camera sequences differ. Every camera pair was accounted for as published,
skipped before decoding, or dropped on a full queue. The high skip/drop counts
show that VIO does not keep up with this replay clock; full camera consumption
and real-time throughput have not been demonstrated.

All successful cases initialized VIO, drained their queues, joined workers, and
exited through HTIF with status 0. They had no IMU loss, trace overflow, backwards
publication timestamps, invalid VIO quaternion, non-finite VIO pose, or missed
consumer-probe deadline. Both IMU-overflow cases emitted `imu_queue_overflow`, a
counted overflow, and final firmware status `fail`, then exited through HTIF with
status 1. Neither a timeout nor a killed simulator was counted as a pass.

## Clock and independence evidence

The executable was run with Chipyard's
`/home/prashanth/chipyard/.conda-env/riscv-tools/bin/spike`, using `-p1` or `-p2`,
256 MiB RAM at `0x80000000`, and ISA `rv64imafdc_zicsr_zifencei`. The shared
10 MHz simulated CLINT timer supplies runtime time; Zephyr ticks are 1 kHz.
`--real-time-clint` was absent. Host elapsed time is used only by the watchdog;
these results do not predict cycle-accurate Chipyard performance.

The startup tests actually executed on hart masks `0x1` and `0x3`, respectively,
and passed shared-clock monotonicity, latest-value publication, and 2,048 atomic
byte exchanges. Application workers remain unpinned. The dual-hart consumer
traces observed execution on both harts.

| Extended run | 120 Hz probe reads | Consecutive reads with unchanged VIO | Of those, propagated state advanced |
| --- | ---: | ---: | ---: |
| Single hart | 3,888 | 3,770 | 1,093 |
| Dual hart | 3,758 | 3,674 | 1,129 |

During the deliberate 250 ms VIO pause, the single-hart run published 51 IMUs
and five cameras while the probe read 31 times. The dual-hart run published
50 IMUs and five cameras while the probe read 30 times. Thus the producers,
integrator, and consumer do not require a fresh VIO pose before proceeding.
The consumer here is a probe; no renderer or new prediction algorithm was added.

The extended runs took approximately 456–460 host seconds on one hart and
548–550 host seconds on two harts on this machine. These are simulator execution
times, influenced by concurrent host work, not hardware speed comparisons.

## Numerical limitation

The maximum position and orientation difference from exact-input native replay
was **0 m and 0 rad** for all 281 published VIO poses. The checked tolerances were
1 mm and 0.001 rad. `SLAMMath.cpp`, `SLAMMath.hpp`, `create_vio_config()`, estimator
parameters, and the integrator's propagation equations were preserved. The
[implementation document](spike-clock-replay.md#numerical-boundaries-and-evidence)
records the estimator and calibration hashes.

Despite native agreement, the estimates are not physically acceptable:

| 200-pair run | Matching camera indices | Estimated endpoint displacement | Ground-truth endpoint displacement | Maximum observed propagated-position norm |
| --- | --- | ---: | ---: | ---: |
| Single hart | 24–178 | 22.656 m | 1.797 m | 512.004 m |
| Dual hart | 20–184 | 25.810 m | 1.699 m | 456.824 m |

Each displacement comparison uses the same timestamps for estimate and ground
truth within that run. Endpoint displacement magnitude is independent of initial
translation and rotation; this diagnostic does not claim a full aligned
trajectory benchmark. The propagated-position norm is a separate diagnostic,
not VIO error or an endpoint displacement.

Native controls establish that large VIO drift also occurs without asynchronous
camera drops. Feeding all 200 cameras to the unchanged estimator produced
35.090 m displacement versus 1.281 m ground truth over camera indices 20–199
(8.95 seconds). Reproducing the original revision's fixed ten-IMU window protocol,
extended to 200 cameras, produced 36.154 m versus the same 1.281 m ground truth.
The controls are saved as
[`native-200-sanity.json`](/home/prashanth/illixr-spike-validation/native-200-sanity.json)
and
[`native-legacy200-sanity.json`](/home/prashanth/illixr-spike-validation/native-legacy200-sanity.json).
They do not identify the root cause of the estimator's drift.

The existing integrator also subtracts gravity with a sign inconsistent with
the estimator's gravity convention. Its equations remain unchanged as requested.
Stale VIO baselines and continuing IMU propagation expose severe drift. Functional
clock/transport validation is complete; reasonable pose accuracy and suitability
for hardware operation remain unvalidated. No estimator tuning or math changes
were used to make these tests pass.

## Memory, artifacts, and reproduction

The largest image, dual-hart with 200 pairs, occupies 238,429,968 bytes of the
268,435,456-byte RAM region according to the linker: 88.82%, leaving 30,005,488
bytes outside the reserved image. This includes the embedded data, static stacks,
queues, trace buffer, and 64 MiB allocation arena. That spare region is not a
measurement of free space inside the allocator. The corresponding single-hart
image occupies 238,403,344 bytes. The 50-pair normal images occupy 129,671,696 and
129,698,320 bytes. Runtime allocation succeeded for all executed normal cases.

Builds used isolated Zephyr SDK 0.17.0 (GCC 12.2 with matching libstdc++ and
retargetable Newlib), the pinned Chipyard Zephyr revision, OpenCV 4.5.4 with the
documented Zephyr thread support patch, and the pinned Eigen dependency. Existing
Chipyard sources and configuration were left untouched.

A final provenance audit confirmed that every artifact's C/C++ sources and board
configuration match the final working tree. Eight older artifacts record an
earlier CMake fingerprint: subsequent edits confined replay-test macro overrides
to the two queue-owning plugins. The old CMake hashes were reconstructed and the
effective settings verified equivalent for all ten variants; estimator code and
runtime behavior were unaffected.

Saved evidence:

- [Build artifacts](/home/prashanth/illixr-rtos-work/artifacts): ten distinct ELF
  variants, configuration, device tree, dataset manifest, sizes, and build hashes.
- [Run results](/home/prashanth/illixr-rtos-work/results): 14 directories with
  `console.log`, `run.json`, `analysis.json`, source fingerprints, and native
  replay output for each successful firmware run.
- [Aggregated results](/home/prashanth/illixr-rtos-work/results/summary.json).
- [Executed matrix](/home/prashanth/illixr-rtos-work/matrix.json).

Follow [the build guide](spike-clock-replay.md#build-and-run) to rebuild. To repeat
the saved matrix with the existing artifacts, choose a fresh output directory:

```sh
python3 scripts/run_spike.py \
  --matrix /home/prashanth/illixr-rtos-work/matrix.json \
  --output /home/prashanth/illixr-rtos-work/results-repeat \
  --native /home/prashanth/illixr-spike-validation/native/estimator_replay
```

The host analysis suite also passed all 15 tests, covering missing output,
accounting failures, invalid poses, clock checks, native disagreement, and
expected overflow. Separate runner checks exercised wrong exit status, timeout,
and panic handling. `git diff --check` passed after implementation.
