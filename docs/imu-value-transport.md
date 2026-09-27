# IMU transport without per-sample allocation

The IMU producer publishes a fixed-size `ImuSample` into two independent Zephyr
message queues. Each record contains the original integer nanosecond timestamp,
six unchanged double-precision sensor components, and the sample index. The
record is trivially copyable; Eigen objects are constructed locally in the VIO
and integrator workers after receiving a record.

This removes two allocations and two eventual frees per IMU sample: 1,002 of
each for the 501-sample workload. On RV64, each queue reserves 4096 × 64 bytes,
for 512 KiB total. This increases fixed queue storage by 448 KiB compared with
the former pointer queues. Queue depth remains 4096 samples per subscriber.

Both publications still use `K_NO_WAIT`, in the same order. Failure at either
subscriber still invalidates the run with `imu_queue_overflow`. Completion flags
remain independent of queue capacity. Consumers drain all samples on a normal
run; failure cleanup purges scalar records and retains ownership-based cleanup
for camera messages.

Dataset pacing, camera publication, thread priorities and affinities, estimator
math, calibration, initialization, and pose publication are unchanged. The
original Zephyr kernel is used for this experiment.

## Why this change is being tested

The failing dual-pinned and quad-scheduler FireSim captures show the high-priority
IMU worker blocked in allocation while boosting the allocator's VIO owner from
priority 5 to 3. The captured exception occurs during Zephyr ready-list removal.
Passing IMU values through queues removes this observed allocator interaction.
It does not establish that the underlying scheduler defect is fixed or that
other contended mutex paths cannot encounter it.

## Validation artifacts

New firmware, original-kernel checks, isolated FireSim runs, native comparisons,
and the application-only patch are kept under:

`/scratch/prashanth_illixr_firesim_20260926/application-imu-values`

The previous firmware, bitstreams, and matrix results are preserved. Hardware,
driver, dataset, queue capacities, and the existing 1 mm / 0.001 rad native
agreement thresholds remain the same. Each run must still complete all 501 IMUs
at both consumers and account for all 50 camera pairs. A timeout or truncated
trace remains incomplete.

The native `imu_transport` CTest checks scalar transport and message conversion;
the FireSim matrix exercises actual Zephyr queues, scheduling, and shutdown.
Saved matrix reports contain the acceptance status of completed runs. Numerical
agreement with the native port estimator is separate from physical trajectory
accuracy.

## Completed FireSim validation — 2026-09-26

All three platform preflights and all five complete workload cases passed on the
original kernel and the existing U250 bitstreams/drivers:

| Rocket harts | Placement | VIO poses | Cameras processed / skipped / dropped | Application seconds |
|---:|---|---:|---|---:|
| 1 | Scheduler-managed | 9 | 11 / 34 / 5 | 9.524646 |
| 2 | Scheduler-managed | 11 | 13 / 21 / 16 | 10.008686 |
| 2 | Pinned | 11 | 13 / 20 / 17 | 10.041666 |
| 4 | Scheduler-managed | 11 | 13 / 18 / 19 | 10.199384 |
| 4 | Pinned | 11 | 13 / 20 / 17 | 10.124260 |

Every case delivered all 501 IMUs to both consumers, accounted for all 50 camera
pairs, exited normally via HTIF, and produced complete traces with initialized,
finite, normalized VIO output. Native replay of each delivered input sequence
reported zero position and orientation error. No IMU overflow or missed probe
deadline was reported. Consumer progress remained independent.

The formerly incomplete dual-pinned and quad-scheduler cases now complete. In
the quad scheduler case, every plugin processed work on all four harts. Pinned
runs obeyed the requested mapping: VIO on hart 1 for dual; IMU/camera/VIO/integrator
on harts 0/1/2/3 for quad. This is evidence from actual sample processing and
publication counters, separate from startup hart checks.

The comparison report at the artifact root records the original results beside
these new runs, all placement counts, queue occupancy, pose ages, host times,
native comparisons, and separate physical-displacement diagnostics. It also
retains historical Spike and Verilator context; those binaries were not changed
or rerun as part of this application-fix validation.

These passes demonstrate recovery of the observed application stalls in this
matrix. They do not establish that the scheduler failure cannot recur through
another path. Existing trajectory-accuracy limitations also remain; estimator
math and calibration were preserved.
