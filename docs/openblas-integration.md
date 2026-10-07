# OpenBLAS integration and validation

> Historical integration record. Status statements below describe the initial
> scalar/RVV campaign, including investigations that have since completed.
> Use the [primary README](../README.md) for current dependencies, all four
> backends, and reproducible commands; see [FP32 Gemmini](gemmini-openblas.md)
> and [RVV packing results](rvv-gemmini-packing.rst) for subsequent acceptance.

The optional `ILLIXR_LINALG_BACKEND` CMake setting selects `eigen` (default),
`openblas_scalar`, or `openblas_rvv`. Scalar firmware has passed single- and quad-core Spike validation. The full RVV
pipeline has also passed on one and four Spike harts. See
[the production comparison](openblas-spike-results.md) for results and limitations.
This document does not claim that the full matrix has passed.

The single-core Shuttle+Saturn FPGA investigation has isolated a scalar FP
exception side-effect bug and an overflowing image bounds check. A corrected
hardware image is building; full FPGA acceptance remains pending. See the
[diagnostic report](shuttle-image-address-debug.md).

Build a pinned embedded archive with `scripts/build_openblas.py`, then supply its
`lib/libopenblas-zephyr.a` as `ILLIXR_OPENBLAS_ARCHIVE` when invoking the existing
Spike/Rocket build scripts. The adjacent manifest is checked against the selected
backend, pinned revision, and archive hash at configure and artifact-recording
steps. The source must be clean at OpenBLAS revision
`6d12fcab91ebf4de5eb23c1218727d2551b8635e`.

The embedded archive excludes OpenBLAS's desktop allocator, environment reader,
and error-handler objects. In this fork the embedded allocator object exports
libc stubs, so overriding only `blas_memory_alloc` is insufficient. Debug
name-printing/environment lookups are disabled. The application supplies a
32 MiB aligned scratch arena with an overflow sentinel, serialized entry-point
wrappers, and fatal handling of invalid BLAS arguments or allocator misuse.
OpenBLAS remains single-threaded; Zephyr still schedules the plugin workers.

The wrappers cover double/single GEMM, GEMV, TRMM, TRMV, TRSM, and AXPY. The final
application archive is audited for additional unwrapped BLAS references. Work
records count API entries on each hart, union entry/exit hart masks, maximum
matrix dimensions, call cycles, and mutex-wait cycles. Counter version 2 rejects
cross-hart cycle subtraction, records excluded samples, and also measures shared
clock elapsed/wait nanoseconds. Cycle intervals include preemption; they are not
exclusive CPU service time. Startup numerical checks
exercise transpose combinations, padded leading dimensions, positive/negative
vector strides, left/right triangular operations, and unit/non-unit diagonals.
Their calls are excluded from workload statistics. Products and triangular
multiplication use an Eigen-only oracle in a separate namespace and translation
unit, with a plain-pointer interface. This isolates its Eigen definitions from
the BLAS-enabled application; the link audit rejects any oracle BLAS reference.

New console records are `ILLIXR_BLAS`, `ILLIXR_BLAS_SELFTEST`,
`ILLIXR_BLAS_WORK`, and `ILLIXR_BLAS_MEMORY`. Work and memory summaries use the
existing batched trace transport. Historical traces without these records remain
readable. New OpenBLAS runs require successful self-tests, complete operation
records, real DGEMM activity, consistent hart counts, and no outstanding scratch
allocation.

`run_openblas_validation.py` provides scalar Spike, scalar Rocket FireSim, and
RVV Spike stage gates. Partial matrices, failed traces, and changed firmware do
not open the next gate. FireSim's shared runtime lock and idle-board inspection
remain in force. The Spike runner now passes its 100-billion instruction-step
budget explicitly; this is not a cycle-accurate hardware-cycle counter. Missing
normal completion remains incomplete even if Spike exits zero at that budget.

## Production scheduling fix and RVV setup

Render and timewarp now run at Zephyr priority 4, above camera/VIO/integrator
priority 5 and below IMU priority 3. Their simulated GPU waits still release the
CPU. The previous priority-5 workers could become ready before a display deadline
but remain queued behind camera/VIO processing. This is an application priority
change; Zephyr scheduler code, estimator math, and GPU timing remain unchanged.

Production scalar OpenBLAS at `-O2 -fno-fast-math` passed single/quad Spike:
19/23 VIO poses, 239/255 fresh presentations, all 501 IMUs at both consumers,
all 50 camera pairs accounted for, and exact native estimator replay agreement.
Earlier failed runs and the isolated scheduling diagnostic are preserved.

The SDK's RVV compiler ISA selects an incompatible default 32-bit runtime.
Use `scripts/prepare_vector_zephyr.py --source EXISTING_ZEPHYR --destination
NEW_PRIVATE_ZEPHYR` to apply the pinned build-system patch to a private checkout.
Place that checkout in a dependency root with the usual SDK/OpenCV/module links
and select the root with `ILLIXR_RTOS_DEPS`. Kernel/application C keeps the scalar
RV64/lp64d ISA; explicit vector assembly and `CONFIG_RISCV_ISA_EXT_V` still enable
vector context support. The separately compiled OpenBLAS archive carries RVV.
No context-switch algorithm is changed. Build manifests preserve dependency
patches as well as commit identities.

For RVV Spike, add `config/vector.conf` to the core configuration and use
`config/spike_single_rvv.overlay` or `config/spike_quad_rvv.overlay`. The runner
ISA is `rv64imafdcv_zicsr_zifencei_zicntr_zvl256b`. OpenBLAS uses Chipyard GCC 13.2
and the application's SDK Newlib headers/ABI. Build real FP32/FP64 only: unused
complex RVV GEMV intrinsics in this fork are incompatible with that compiler.
The OpenBLAS source remains pinned and unmodified.

GCC 13.2 at O1/O2 miscompiles odd-size TRMM tails: a discarded scalar vector-
length definition is subsequently used in pointer arithmetic. The isolated
`tests/rvv_trmm` fixture reproduces the fault with interrupts disabled. The same
kernel source passes at O0. The library builder therefore recompiles only the
eight real FP32/FP64 TRMM intrinsic kernels at O0 and records each command/object
hash in `rvv_trmm_compiler_workaround`. These remain RVV kernels; GEMM/GEMV and
other library code retain O2. This is an explicit compiler workaround, not a
scalar fallback. Disabling the RTL VSETVL pass also passed the reproducer, but is
not used in production. Changing the RVV compiler requires revalidating this
workaround. The full numerical preflight still checks 349,536 results.

The application vector preflight tests FP64 arithmetic, all 32 vector registers,
VL/VTYPE/VCSR/VSTART, competing threads, timer interrupts, blocking, and forced
migration. Standalone single/quad tests passed with masks 0x1/0xF, VLEN 256,
32 rounds per worker, and 16 successful migrations on quad. Every full RVV run
repeats these checks. Kernel wrappers count actual `dgemm_kernel`, `dgemv_n`, and
`dgemv_t` entries; artifact auditing also requires vector arithmetic disassembly.
The analyzer rejects absent/failed vector checks or missing kernel execution.

## Current experiment

Workspace: `/scratch/prashanth_illixr_openblas_20260927T162930Z`.

Single and quad REFV256D128 Shuttle/Saturn hardware builds remain independent,
four workers each, 30 MHz physical request, NORETIMING. Generated descriptions
confirm VLEN 256/FP64, hart counts, 256 MiB RAM, and 500 MHz / 500 kHz clocks.
Both images have passed routed timing and packaging. Single-core platform
preflight passed. Scalar and RVV full workloads both failed at the same OpenVINS
image read after passing BLAS checks; RVV also passed hardware vector preflight.
See [single-core FPGA results](openblas-shuttle-single-results.md). Quad platform
preflight remains incomplete.

The latest user instruction prioritizes RVV Spike after production scalar Spike.
Use `--rvv-prerequisite spike-scalar` explicitly for that order; the default
remains `rocket-scalar`. This does not waive or mark passed the separate Rocket
Eigen/scalar comparison. No complete Shuttle/Saturn FPGA workload has yet been accepted.
