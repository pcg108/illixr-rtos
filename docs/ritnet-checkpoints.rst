RITNet operation diagnostics
===========================

The optional diagnostic interface preserves ``ritnet_infer()`` and the original
64 accelerator-helper calls. ``ILLIXR_RITNET_DIAGNOSTICS`` defaults to OFF. No
diagnostic buffers, completion drains, or hooks are linked into production when
it is disabled. Interrupts remain enabled in these experiments.

``scripts/instrument_ritnet.py --check`` verifies unique checkpoint coverage.
The catalog is ``third_party/ritnet/port/diagnostic_operations.json``. It covers
image copies, individual concatenation writes, residuals, convolution/pooling,
depthwise upsampling, and final output. A partial concatenation view hashes only
its written channels; intervening channels are reported separately as gaps.

Reference generation
--------------------

Build ``tests/ritnet/checkpoint_reference.c`` with the private RITNet C sources,
``RITNET_HOST_REFERENCE=1``, ``RITNET_DIAGNOSTICS=1``, ``-fno-fast-math`` and
``-ffp-contract=off``. Pass an empty output directory to the executable. It runs
two complete inferences, requires the established final output hash, and compares
every exported tensor byte on the second pass. It writes packed logical inputs
and outputs, ``operations.jsonl``, and ``ritnet_diagnostic_expected.h``.

Input tensors are captured before execution, including inputs overwritten by
residual operations. Output tensors are captured after completion. The fixture
extractor independently checks each input file against its pre-operation hash;
this prevents using an accidentally exported post-operation residual input.

Enable firmware with ``-DILLIXR_RITNET_DIAGNOSTICS=ON`` and
``-DILLIXR_RITNET_DIAGNOSTIC_REFERENCE=/path/to/reference``. The standalone test
and full eye-tracking profile both support diagnostics. Standalone tests enable
the existing batched HTIF trace transport by default; JSON formatting happens
on the host after inference has stopped.

Timing controls
---------------

The linked firmware exposes initialized selector words. Use
``scripts/ritnet_diagnostic_variant.py`` to copy an artifact directory and patch
only those words. It records every patch, original ELF hash, and new ELF hash.
Code and data addresses remain identical across modes made from the same ELF.

* Mode 1 records submissions/timestamps without additional completion drains or
  activation reads. Existing model-helper fences are not removed.
* Mode 2 adds completion drains after operations selected by the 64-bit mask;
  bit zero selects operation 1. No tensor hashing is performed.
* Mode 3 drains before input inspection and after output submission, checks
  logical fingerprints and workspace guards, and stops at the first mismatch.
  It verifies image and immutable-asset fingerprints each inference and checks
  them again on mismatch. A passing mode-3 run can mean the instrumentation
  prevents reproduction; it is not evidence that the original execution works.

``--inferences`` is bounded at 32. ``--capture`` selects an operation to capture;
by default the first differing output is captured. Oversized captures fail
explicitly. Capture capacity is 4 MiB; record capacity is 64 x 32 operations.
The full diagnostic workload built in the initial investigation occupies
208,176,440 bytes of the 256 MiB RAM region, including capture/record storage.

Records and comparison
----------------------

The version-1 records use stable operation IDs, input/output geometry and
strides, actual addresses, scalar arguments, execution harts, cycle timestamps,
logical FNV-1a fingerprints, separate gap fingerprints, and guard/mismatch flags.
``begin_cycle`` precedes input inspection and ``end_cycle`` follows completion;
``overhead_cycles`` includes hashing/draining in both hooks. The initial immutable
scan and structure-copy overhead are not included in that aggregate, so compare
whole-inference cycle counts as well. Spike counters are not hardware timings.

Run ``scripts/analyze_ritnet_checkpoints.py --trace console.log --reference REF
--output checkpoint-analysis.json`` after the existing batch decoder. It checks
ordering, geometry, parameters, completion status, captures, and first divergence.
Control-mode trace validity is distinct from final inference correctness; the
runner additionally requires the original exact final tensor/reference check.

Version 2 additionally supports ``ILLIXR_RITNET_POST`` observations after a
completed inference fails its existing final-output check. Successful
inferences perform no additional tensor reads or drains. On failure, the
diagnostic hook drains once, rechecks the image and immutable assets, and reads
the narrowed operation-31/32 handoff. A differing output uses the existing
bounded capture buffer. Records are exported only after inference stops.

These are completed-inference observations, not measurements taken at the
original operation boundary. The analyzer checks that later operations did
not overwrite their logical views and reports them separately from the first
per-operation divergence. Even matching final inputs do not prove what an
asynchronous accelerator read earlier. Version-1 traces remain supported.

``scripts/extract_ritnet_fixture.py`` extracts the first differing operation only
if its completed inputs match the reference. The generated C fixture retains
the helper call, strides, overlapping views, scalar parameters, and alignment
modulo 4096. It checks output bytes after a completion drain. Absolute addresses,
unwritten gaps, and preceding accelerator history are not reproduced: gaps are
zeroed and the command sequence contains the isolated helper. If it passes,
retain that result and extend the command history before claiming localization.
``tests/ritnet/fixture`` builds the extracted C file as a minimal Zephyr program
using ``-DRITNET_FIXTURE_DIR=DIR``. It attempts 32 executions on hart 0 with
interrupts enabled and exports results only after execution stops.

Execution orchestration
-----------------------

``scripts/run_ritnet_diagnostics.py CONFIG.json`` supports guarded follow-up
experiments with explicit artifact/reference/hardware paths. It waits for the
initial three-mode hardware matrix and the updated Spike gate, uses the global
FPGA lock plus each image's runtime lock, checks platform startup, and refuses
to continue after programming recovery, resource-stop, timeout, or trace errors.
It runs the full asynchronous application on dual-array and INT8-only images,
then tests empty/all drain masks and systematically reduces a passing set.

Reduction is a bounded sufficiency experiment, not a proof of a minimal fix.
Each trial attempts 32 identical inferences. A numerical failure is preserved;
an interrupted trial remains incomplete. No production fence or RTL change is
made by these tools. The ordinary numerical/runtime acceptance checks remain
enabled for full-workload cases, including unchanged native pose tolerances.

Host regression tests are in ``tests/native/test_ritnet_checkpoints.py`` and
registered with CTest. They cover logical ordering, partial concatenations,
in-place input export, capture/record bounds, malformed/duplicate/missing
records, first-divergence selection, and bounded drain-set reduction.

Corrected-Rocket preflight recovery (2026-10-03)
----------------------------------------------

The corrected-cache FPGA matrix initially selected the original two-inference
standalone preflight, which predates operation-boundary diagnostics and has no
operation-31 completion drain. Its second inference failed on the INT8-only
single-core image. Passing diagnostic workloads do not qualify that unfenced
firmware: their mode-2 drain masks explicitly select operation 31 or all 64
boundaries.

On the same corrected INT8-only image, the preserved post-failure diagnostic
firmware reproduced the known failure at inference 7 with no additional drains.
The captured operation-32 tensor was byte-identical to the earlier failing tensor
(SHA256 ``4e3ff154c49794d6bb30d16d4944da2ca859d43173918d984e6565b4d264e2f9``):
149 of 4800 bytes differed. Completed operation-31 inputs/output, operation-32
inputs, image, immutable assets, and guards matched. A same-layout ELF differing
only in one byte of ``ritnet_diag_drain_mask`` passed 32/32 exact full-output
comparisons when operation 31 was drained. Timer preemption and vector-context
checks remained enabled. These are completed-inference observations, not direct
traces of accelerator DMA reads.

FireSim cases may now explicitly declare ``ritnet_inferences`` (1 through 32).
The count must match the hashed firmware's diagnostic-variant manifest. Default
legacy cases still require two inferences. Every declared inference requires an
ordered result, an exact full-tensor comparison, hart-zero accelerator ownership,
and timer progress. Normal HTIF exit and vector checks remain required.

The remaining matrix uses explicit mode-2, operation-31, 32-inference diagnostic
preflights before running the unchanged paired full workloads. The failed legacy
preflight remains recorded as failed. This recovery changes host validation and
artifact selection; it does not add a production RITNet fence or change RTL.

Evidence directory:
``/scratch/prashanth_illixr_fixed_rocket_matrix_20261002T211704Z/preflight-debug``.
It preserves the ELF audit, same-layout comparison, unfenced capture, passing
control, isolated quad preflight build, and original matrix state before resume.

The corrected-hardware matrix subsequently completed all 12 full workloads and
all four platform preflights. See ``ritnet-fence-matrix-results.rst`` for the
final comparison and ``completion-audit.json`` in the evidence directory's
parent for the artifact and acceptance audit. The unfenced legacy failure is
retained; completion applies to the explicitly fenced diagnostic matrix.

Production completion policy
----------------------------

RITNet now unconditionally calls ``gemmini_fence()`` after each of the 64
catalogued tensor-producing operations. These boundaries apply with
``ILLIXR_RITNET_DIAGNOSTICS=OFF`` and complete output transfers before subsequent
consumers or workspace reuse. The original graph calls, weights, scales, and
quantization are unchanged. Eye inference remains asynchronous on hart 0;
timewarp continues reading the most recently published prediction.

Production has no diagnostic 32-inference cap. Normal end-of-stream and the
existing bounded trace storage still govern lifetime. Eye configuration records
identify ``completion_fence_policy=every_operation``, 64 operation fences,
``diagnostics=false`` and ``inference_limit=0`` (no diagnostic cap).

The optional diagnostic drain mask now adds drains on top of these mandatory
boundaries: clearing it no longer recreates unfenced execution. Use the preserved
historical ELFs and source snapshots for the earlier unfenced/op31-only controls.
``scripts/instrument_ritnet.py --check`` verifies unconditional fence coverage and
unchanged graph calls against the operation catalog.

Production validation (2026-10-03)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All six full-pipeline FireSim cases passed using normal firmware with diagnostics
disabled: RVV on single/quad INT8-only and dual-array Rocket+Saturn images, and
FP32 OpenBLAS on single/quad dual-array images. Quad-core cases produced 43 exact
reference eye predictions each, exceeding the former diagnostic cap; single-core
RVV cases produced 16 each and single-core FP32 produced 17. Every case delivered
501 IMUs to both consumers, accounted for all 50 camera pairs, passed the existing
native pose and prediction/transform checks, and reported zero render/timewarp
deadline misses with fresh presentations. There were 10 VIO poses in each
single-core case and 15 in each quad-core case.

The four firmware ELFs each contain 64 operation fences plus the original final
inference fence and no diagnostic runtime symbols. 86 native host tests passed.
The first INT8-only quad-core attempt was interrupted during host setup and is
retained as incomplete; the clean retry passed with the same firmware. No
resource limit was exceeded. Build supervisor cleanup scope and the recovery are
recorded with the artifacts.

Firmware, source manifests, disassembly, complete traces, comparisons and the
closing audit are preserved in
``/scratch/prashanth_illixr_ritnet_production_fences_20261003T173244Z/``;
see ``production-validation.rst`` and ``completion-audit.json``. Prior diagnostic
firmware/results remain unchanged. These runs validate production behavior;
they are not new paired measurements of incremental fence overhead.
