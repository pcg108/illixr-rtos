# Production scheduling fix and RVV Spike validation

Updated 2026-09-27T18:11:33.688875+00:00.

Production render/timewarp priority is 4; camera/VIO/integrator remain 5 and IMU remains 3. The original failure was ready-to-run delay behind camera/VIO work. GPU waits still release the CPU. No estimator math, queue policy, dataset, display schedule, Zephyr scheduler, or vector context algorithm was changed.

All four runs below exited normally, passed complete-trace validation, delivered all 501 IMUs to each consumer, accounted for all 50 camera pairs, passed 349,536 BLAS numerical checks, and matched native estimator replay with zero reported position/orientation difference. Prediction/transform comparisons passed their unchanged tolerances.

| Backend | Harts | VIO poses | Cameras processed/skipped/dropped | Render/warp misses | Fresh on-time presentations | App seconds | Trace export seconds | Host seconds |
|---|---:|---:|---|---|---:|---:|---:|---:|
| openblas_scalar | 1 | 19 | 21/28/1 | 0/0 | 239 | 5.350015 | 0.209480 | 70.835 |
| openblas_scalar | 4 | 23 | 25/0/25 | 0/0 | 255 | 7.116710 | 0.278555 | 133.230 |
| openblas_rvv | 1 | 20 | 22/28/0 | 0/0 | 239 | 4.166765 | 0.163840 | 62.282 |
| openblas_rvv | 4 | 25 | 27/0/23 | 0/0 | 255 | 4.566700 | 0.179890 | 106.901 |

Spike is an instruction-level functional simulator. These application times are not hardware speedup measurements, and camera delivery differs between runs. All use 10 kHz ticks; Spike retains its verified 10 MHz timer interpretation. FireSim uses the separately accepted modeled 1 GHz CPU / 1 MHz timer interpretation.

## RVV integration and compiler workaround

Zephyr SDK 12.2 continues to compile the application. A private, recorded Zephyr CMake patch selects its scalar RV64/lp64d runtime while retaining explicit vector context assembly. The separately compiled RVV library uses Chipyard GCC 13.2, the SDK Newlib headers, medany/lp64d, the pinned OpenBLAS source, real FP32/FP64, single-thread BLAS, and no fast-math. Unused complex kernels were excluded because their intrinsics do not compile with GCC 13.2.

The first full RVV attempts failed in the triangular-multiply self-test. The same fault reproduces in tests/rvv_trmm with timer interrupts disabled. Disassembly uses a missing scalar vector-length definition in a tail-path address calculation. O1/O2 and disabled instruction scheduling reproduce it; O0 and disabling the RTL VSETVL pass eliminate it. Production uses only the narrower O0 workaround for eight FP32/FP64 RVV TRMM kernels; GEMM/GEMV and other BLAS code retain O2. No kernel equations/source were changed, no scalar fallback was used, and tolerances were not relaxed. The numerical/canary regression fails with the original archive and passes with the workaround. Manifests include the replacement object hashes and exact compiler commands.

Vector preflight passed on one/four harts with VLEN 256, FP64, all vector registers and VL/VTYPE/VCSR/VSTART preserved across timer interrupts, competing threads and blocking. Quad also passed 16 forced migrations. The full workloads repeat this preflight. Kernel disassembly and entry counters prove RVV execution.

| Harts | Workload DGEMM API calls | RVV GEMM kernel entries by hart | API entry hart counts | Same-endpoint-hart cycles | Excluded migrated calls |
|---:|---:|---|---|---:|---:|
| 1 | 997 | [5000, 0, 0, 0] | [997, 0, 0, 0] | 263215280 | 0 |
| 4 | 1032 | [355, 418, 7447, 1370] | [29, 22, 647, 334] | 544444587 | 14 |

API and kernel counts differ because one API call can dispatch several blocked kernels. Cycle aggregates include preemption and exclude calls ending on another hart; shared-clock elapsed time is retained separately in JSON.

## Placement, queues, freshness and accuracy

### 1-hart RVV

Scheduler-managed placement: offline_imu mask 0x1, processing [501]; offline_cam mask 0x1, processing [22]; openvins mask 0x1, processing [523]; imu_integrator mask 0x1, processing [501]; render_loop mask 0x1, processing [499]; timewarp mask 0x1, processing [499].

IMU VIO/integrator queue high-water marks 207/2; camera queue 8. Maximum observed VIO age 1.708365 s. Probe missed deadlines 0. Completed render/warp 499/499, repeated images 0, startup empty displays 1. Fresh on-time presentations are a subset of completed warps: startup fallback and stale predictions remain explicitly recorded.

Trajectory diagnostic: estimated endpoint displacement 0.376965 m versus ground-truth magnitude 0.001437 m over the matched segment. This unaligned short-segment diagnostic is separate from native runtime equivalence; substantial physical drift remains.

### 4-hart RVV

Scheduler-managed placement: offline_imu mask 0x3, processing [400, 101, 0, 0]; offline_cam mask 0xD, processing [1, 0, 33, 16]; openvins mask 0xF, processing [5, 18, 339, 166]; imu_integrator mask 0x3, processing [288, 213, 0, 0]; render_loop mask 0x1, processing [547, 0, 0, 0]; timewarp mask 0x1, processing [547, 0, 0, 0].

IMU VIO/integrator queue high-water marks 254/1; camera queue 8. Maximum observed VIO age 2.158395 s. Probe missed deadlines 0. Completed render/warp 547/547, repeated images 0, startup empty displays 1. Fresh on-time presentations are a subset of completed warps: startup fallback and stale predictions remain explicitly recorded.

Trajectory diagnostic: estimated endpoint displacement 0.375134 m versus ground-truth magnitude 0.000723 m over the matched segment. This unaligned short-segment diagnostic is separate from native runtime equivalence; substantial physical drift remains.

## Evidence and remaining work

- [openblas_scalar 1 harts](/scratch/prashanth_illixr_openblas_20260927T162930Z/runtime/spike-single-openblas_scalar-displayprio4-o2-1/analysis.json); ELF SHA-256 `6635fe4ca032b87d5b898edcd797b8b462f75ad2d12d41f64686091c8fd6209d`.
- [openblas_scalar 4 harts](/scratch/prashanth_illixr_openblas_20260927T162930Z/runtime/spike-quad-openblas_scalar-displayprio4-o2-1/analysis.json); ELF SHA-256 `ef7993d04056497c5cdf06e2d0481cf7bd214c32f5cd8ab857f3375328dcb49b`.
- [openblas_rvv 1 harts](/scratch/prashanth_illixr_openblas_20260927T162930Z/runtime/spike-single-openblas_rvv-25928f1de9/analysis.json); ELF SHA-256 `25928f1de9aabddc1155d8d56ac828c22c7dcd3d5062cd8bbfbe421fcf6ae21b`.
- [openblas_rvv 4 harts](/scratch/prashanth_illixr_openblas_20260927T162930Z/runtime/spike-quad-openblas_rvv-a5bb637dd4/analysis.json); ELF SHA-256 `a5bb637dd43109da495228e27fddf41887c2671617a5d8702e7a32ca9810f7df`.

Compiler fault/reproduction artifacts: `/scratch/prashanth_illixr_openblas_20260927T162930Z/diagnostic-rvv-trmm`. Original failed full workloads and all earlier scalar results remain preserved. The initial quad RVV attempt stalled during fatal logging and was explicitly interrupted; it remains incomplete, never a pass.

175 host-side tests passed. The user explicitly prioritized RVV Spike after the successful production scalar Spike gate. The Rocket Eigen/scalar FireSim comparison remains required separately and has not passed yet. Both new Shuttle/Saturn FPGA builds continue; routed timing, programming and FPGA runtime acceptance remain pending.
