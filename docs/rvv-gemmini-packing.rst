RVV conversion and packing for FP32 Gemmini
=========================================

Selection and numerical scope
-----------------------------

``ILLIXR_LINALG_BACKEND=openblas_gemmini_fp32`` supports the additional CMake
option ``ILLIXR_GEMMINI_PACKING=scalar|rvv``. The default remains ``scalar``
until the complete equivalence and hardware performance gates are accepted.
``ILLIXR_GEMMINI_PACKING_TRAVERSAL=rows|contiguous`` selects an experimental
traversal; its default is ``rows``. Both produce the same compact row-major
Gemmini operands and write results back to the caller's original layout.

The estimator still stores and computes its state in double precision. The
existing Gemmini adapter already combines scalar conversion with packing;
the new path vectorizes that combined operation. It does not remove the
required FP64-to-FP32 conversion or change Gemmini's mixed-precision compute.
No Gemmini command, scaling, accumulation, worker routing, arena ownership,
completion fence, or OpenVINS equation is changed.

Implementation
--------------

``src/gemmini_packing_rvv.c`` provides four C ABI kernels: double-to-float,
float-to-double, float copy, and zero fill. Strip lengths use e32/m1 and
matching e64/m2 lanes. Loads/stores support signed element strides; tails use
the actual VL. Narrowing uses the current rounding mode and widening is exact
for finite FP32 inputs. The kernel does not reset the caller's floating-point
status. Float copies preserve bit patterns, including NaN payloads.

``src/gemmini_packing_vector.hpp`` applies these kernels directly to caller
layouts and the existing aligned arena. Row traversal uses contiguous packed
rows. Contiguous traversal swaps loop axes for column-major inputs and
column-major output destinations. Beta-zero packing fills C without reading
the caller's C buffer. Zero dimensions return without accessing data.

Only the kernel object uses the pinned GCC 13.2 RVV toolchain, with lp64d,
medany, the recorded Zephyr/Newlib sysroot, no fast-math and no FP contraction.
The application remains on the accepted Zephyr SDK. Vector context support
must already be enabled. All packing occurs in the existing preemptible
Gemmini worker pinned to hart 0 while global BLAS serialization owns the
32 MiB arena.

Evidence and counters
---------------------

``ILLIXR_GEMMINI_PACKING`` records identify implementation and operation,
packed/unpacked elements, bytes read/written, packing/unpacking core cycles,
vectorized request count, and observed hart mask. These are aggregate
records exported after processing. ``vector_calls`` counts vectorized worker
requests, not individual vector instructions. Cycle measurements include
interrupt/preemption time on the pinned worker. Existing nanosecond counters
remain available and include surrounding bookkeeping. BLAS archive and
firmware manifests record backend, packing mode and traversal separately.

Correctness gates
-----------------

* ``tests/gemmini/packing_cases.cpp`` compares the actual RVV object against
  unchanged scalar packing bit for bit. Coverage includes all five rounding
  modes, floating-point flags, tails, signed strides, NaNs/subnormals,
  beta-zero no-read behavior, padded layouts, transposes, GEMV and dimensions
  through 135. The standalone app also retains the existing BLAS and vector
  context/interrupt/migration fixtures.
* ``ILLIXR_PACKING_TESTS=ON`` enables these tests. Adding
  ``ILLIXR_PACKING_BENCHMARK=ON`` runs paired timings on a separate worker
  pinned before startup. One warmup precedes eight alternating trials for
  each shape/layout/beta case. Benchmark failure fails target completion.
* ``scripts/analyze_packing_benchmark.py`` requires all 512 timing cases,
  rejects duplicates and malformed counters, and labels Spike timings as
  functional evidence only.
* ``ILLIXR_ESTIMATOR_REPLAY=ON`` replaces the application main only in a
  dedicated Spike build. Explicit input/output paths use host HTIF files.
  ``scripts/prepare_estimator_stream.py`` creates fixed ordered IMU/camera
  streams; raw images stay on the host rather than increasing target RAM.
  Snapshots include every state, camera covariance, clones and feature
  observations. The test-only feature accessor is absent in production.
* ``scripts/analyze_estimator_stream.py`` checks structure, finite values,
  quaternions and exact stream bytes. Scalar packing is the reference, not
  a different FP64 backend. The subsequent full pipeline still uses the
  independent native estimator and prediction/transform checks.

Current isolated campaign
-------------------------

Artifacts, commands, supervisors and result state are under::

  /scratch/prashanth_illixr_rvv_packing_20261004T010237Z

``control/estimator-state.json`` tracks repeated fixed-input comparisons for
50/501, 200/2001 and 1710/17100 samples on one hart, plus 50/501 on four harts.
``control/pipeline-state.json`` waits for those comparisons and the standalone
gate, then runs scalar/RVV single-hart GPU and quad-hart asynchronous-eye
pipelines. ``control/firesim-state.json`` waits for the complete Spike gate,
then runs preflight/paired benchmarks and three alternating scalar/RVV pairs
on each existing FPGA image. Failed gates stop the sequence.

The single-core image has Saturn and FP32 Gemmini. The quad-core image also
has INT8 Gemmini and uses production asynchronous eye prediction with a fence
after every RITNet operation. Images and earlier results are preserved. The
hardware's generated 500 MHz/500 kHz ratio is interpreted using the accepted
factor-two 1 GHz/1 MHz model; Zephyr uses 10 kHz ticks. No RTL rebuild is part
of this change.

The first timing matrix uses row traversal. Inspect the paired traversal
microbenchmarks before selecting a final per-layout policy or changing the
default. Compare both packing/unpacking and total BLAS latency, and report
live camera scheduling, VIO counts, eye activity and display deadlines
separately from fixed-operation speedups. A passing Spike comparison alone does not establish a speedup. Completed
FPGA measurements are recorded below.

Existing Saturn image compatibility (2026-10-03)
------------------------------------------------

The first single-core FPGA packing preflight failed although Spike passed.
Controlled diagnostics separated two behaviors:

* Reading fcsr immediately after vector conversion could observe stale flags.
  A fence before reading any output made all 180 diagnostic flag cases match.
* FPConvBlock in the image's Saturn source declares frm as two bits. Mode 4
  (nearest, ties to maximum magnitude) is therefore truncated to mode 0
  (nearest, ties to even). All 28 observed value-mismatch cases used mode 4.
  Exact examples include double bits 3ff0000010000000 producing float
  3f800000 instead of 3f800001, and 3690000000000000 producing zero instead
  of the minimum FP32 subnormal.

``ILLIXR_PACKING_SATURN_COMPAT=ON`` explicitly enables the tested software
candidate: return fences for narrowing/widening and scalar narrowing only
when frm is 4. Normal rounding remains vectorized. The option defaults OFF;
it is explicitly ON in new compatibility firmware artifacts. It changes
neither RTL nor Gemmini computation. ``ILLIXR_PACKING_COMPAT`` exposes the
scalar mode-4 call/element totals. Full-pipeline comparisons require zero
such calls, so the normal-workload RVV measurements cannot conceal scalar
mode-4 conversion. Firmware manifests record the option.

The candidate passed 20,709,896 packing checks on one- and four-hart Spike
and on the single-core FPGA, including the exact expected 32 mode-4 calls
and 1,700 scalar-converted elements in the special-value fixtures. The
single-core FPGA also passed the complete standalone BLAS/vector preflight
and all 512 timing trials. Across the 64 shape/layout/beta cases, median
packing-plus-unpacking speedup was 4.73x for row traversal and 5.11x for
contiguous traversal, including the return fences. These are medians of
per-case speedup ratios, not an OpenVINS throughput estimate.

The original failing artifacts, both diagnostics, and the successful
candidate remain under the campaign's runtime directory. The detailed
benchmark is ``firesim-single-compat-diagnostic/packing-benchmark.json``.
``control/compat-matrix-state.json`` now tracks the full-pipeline hardware
comparisons. The completed row-traversal results follow; the default remains
scalar and no automatic traversal-policy promotion was made.

Completed FPGA matrix (2026-10-04)
---------------------------------

All twelve full-pipeline cases passed: three alternating scalar/RVV pairs
on each of the single-core FP32 Gemmini image and quad-core FP32+INT8
Gemmini image. The latter includes asynchronous eye tracking with all
operation fences. Both standalone FPGA preflights passed 20,709,896 packing
checks and 512 benchmark records. Quad-core median standalone speedups were
4.75x (rows) and 5.08x (contiguous), measuring conversion/packing only.

.. list-table:: Full-pipeline results (identical reported timing across three repeats)
   :header-rows: 1

   * - Platform / packing
     - Pack + unpack (ms)
     - Application (s)
     - VIO poses
   * - Single / scalar
     - 240.503
     - 5.308431
     - 13
   * - Single / RVV rows
     - 100.304
     - 5.308336
     - 13
   * - Quad / scalar
     - 392.389
     - 5.708382
     - 15
   * - Quad / RVV rows
     - 149.279
     - 5.491741
     - 15

Every run delivered all 501 IMUs to both consumers, accounted for all 50
camera pairs, completed trace export, and passed native replay of its own
delivered input sequence with zero reported position/orientation error.
All render/timewarp deadline-miss counts were zero, with fresh on-time
presentations. Each quad run produced 43 valid eye results matching the
reference. All RVV selftest/work compatibility counters reported zero
scalar mode-4 calls. The explicit return fences remain enabled.

The quad application time decreased by 3.80%; single-core application time
was effectively unchanged. These are live paced workloads: scalar and RVV
runs have different delivered sequences and exported poses, even when
sample and BLAS call counts match. Aggregate conversion-time ratios (2.40x
single, 2.63x quad) therefore include scheduling/workload effects and are
not controlled equal-work kernel speedups. The standalone measurements
provide that separate evidence. Physical trajectory accuracy is unchanged
as an acceptance question; native agreement establishes runtime agreement
for each delivered sequence.

The user cancelled remaining long Spike runs after moving to FPGA. They
remain incomplete, not passes; completed earlier fixed-input comparisons
are retained. No RTL or estimator math was changed, and scalar packing
remains the default. The FPGA matrix supervisor exited and the U250 was
verified idle.

Campaign artifacts under
``/scratch/prashanth_illixr_rvv_packing_20261004T010237Z`` include
``analysis/hardware-matrix-progress.json`` (per-run timing, counts, placement,
eye freshness, export overhead and hashes),
``analysis/hardware-packing-benchmarks.json``,
``analysis/final-matrix-audit.json``, and all per-case manifests, console
logs, native comparisons and decoded traces in ``runtime``.
