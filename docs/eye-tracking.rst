INT8 eye tracking integration
============================

Select ``profiles/eye_tracking.yaml`` to enable the eye plugins alongside the
existing VIO, prediction, render and timewarp pipeline. The default profile is
unchanged. ``offline_eye`` publishes a descriptor for one embedded, already
quantized 240 x 160 sample at absolute 120 Hz boundaries. It skips expired
publication opportunities. No additional sample images are stored.

``eye_tracking`` is an independent publisher. Its priority-5 worker pinned to
hart 0 snapshots the latest image and runs RITNet, then publishes a retained eye
position with image/inference IDs and timestamps. Image notifications coalesce:
updates arriving during inference replace the pending image, so the next
inference uses the newest image without a backlog. Each image sequence is
processed at most once. INT8 uses custom2; FP32 OpenBLAS retains its separate
custom3 worker and scratch arena. Neither path uses ReRoCC.

Timewarp reads the latest completed eye prediction through a short snapshot
lock, then requests its existing pose prediction and submits modeled GPU work.
It never starts or waits for eye inference. Successive warps may reuse the same
eye prediction; before the first publication they record an unavailable result
and continue. The eye position is traced but does not yet alter the warp.
Inference is interruptible and holds no snapshot lock while using the accelerator.

Shutdown finishes and publishes any in-flight inference, stops new work and
joins the worker before exporting traces. Storage remains bounded. Trace version
2 separates image publications, inference completions and timewarp reads, with
reuse, age, availability and actual harts. Readers retain support for version-1
synchronous traces. The old exemption for zero fresh presentations applies only
to those legacy traces; asynchronous runs require fresh on-time presentation.

Model and memory
----------------

The original supplied files and their hashes are retained under
``third_party/ritnet/original``. The adapted graph retains weights, scales,
operations and INT8/INT32 computation. Two supplied application buffer-shape
errors were corrected: the final up-block output needs 160 x 240 x 32 storage,
and a down-block residual operation needs 9600 rows with a 96-element stride,
not the preceding block's 38400 rows and 65-element stride.

Activation arrays have guard words and are reset before inference. They are
statically allocated independently of the existing 32 MiB BLAS arena. Completion
fences precede buffer inspection and publication. Empty foreground, guard damage,
wrong-hart execution, reference mismatch and trace overflow fail explicitly.
The reported position is the unweighted centroid of non-background argmax
pixels; ties select the lowest class. Coordinates are image pixels, not a
calibrated gaze vector.

The CPU reference executes the supplied CPU helpers and compares every output
byte over two repeated inferences. Its current result is 743 foreground pixels,
centroid (128.94616419919245, 111.95289367429341), and FNV-1a tensor hash
96ed2b1eb697a218. This is a CPU reference, not a hardware-validation claim.

Hardware and gates
------------------

The four configurations combine single/quad Rocket+Saturn with either INT8
alone or INT8 plus FP32 Gemmini. INT8 is 16 x 16, 256 KiB scratchpad, 64 KiB
accumulators, attached only to hart 0. FP32 remains 4 x 4 custom3 on hart 0.
Every core retains Saturn REFV256D128 and the accepted interrupt/page fixes.
Generated parameter headers, tile instances, memory and clocks are checked
before synthesis. FPGA builds require routed timing closure.

The revised Verilator gate is a small bare-metal dual-array program in
``tests/gemmini_dispatch``. It interleaves individual flush, configuration,
DMA, preload, compute and output commands across custom2/custom3. Eight rounds
alternate which array issues first, reuse the same array-local addresses, check
output guards, and compare exact signed INT8 and fractional FP32 matrix products.
No fence separates commands to the two arrays; completion is fenced before
checking output or reusing buffers. RTL counters and opcode assertions at each
array's active command input verify actual routing and command sequences.

The user retired the full Zephyr Verilator tests. The INT8 full-graph run is
preserved as interrupted/incomplete; its queued dual-array counterpart never
started. Full RITNet, interrupt, vector-context and worker integration are tested
with Zephyr on FireSim. FPGA builds continue independently; workloads wait for
the revised bare-metal routing gate and routed timing acceptance. Original RTL
counter instrumentation was accidentally inserted inside a commented legacy
frontend; the replacement model moves it before the active command queue and
requires both counter strings in the compiled simulator before running.

FPGA execution is sequential: platform/vector/RITNet preflight, then full
50-camera/501-IMU workloads. Combined-array images run RVV and FP32 BLAS;
INT8-only images run RVV BLAS. Native VIO tolerances remain 1 mm / 0.001 rad.
Runs retain a 24-hour watchdog and 100-billion-target-cycle limit. Interrupted
or timed-out runs are incomplete. Shared-board locking and idle checks prevent
programming over other work. Guard stops do not restart automatically.

Trace interfaces
----------------

``ILLIXR_EYE_CONFIG`` identifies the model, accelerator, static workspace,
publication rate and counts. ``ILLIXR_EYE_IMAGE`` records scheduled/actual
publication and hart. ``ILLIXR_EYE_RESULT`` records snapshot, request, queue,
inference and decision times; input/render/display identities; centroid/hash;
caller and execution harts; cycles; final/expired state. Records use the existing
batched trace transport. Host validators correlate them with GPU version-2
records and independently check numerical results and display outcomes.

Active validation workspace
---------------------------

``/scratch/prashanth_illixr_ritnet_20260930T161851Z`` contains isolated hardware,
RTL and software trees, immutable firmware artifacts, runtime output and
coordinator state files under ``control``. The single and quad build lanes each
build the combined-array image first and the INT8-only image second. Read
``baremetal-dispatch-verified-state.json``, ``build-single-state.json``,
``build-quad-state.json`` and
``fpga-matrix-state.json`` for actual progress; build completion alone is not
runtime acceptance.

Dual-Gemmini Spike validation
----------------------------

``scripts/build_dual_gemmini_spike.py`` builds two separately named models in
one shared library: ``illixr_gemmini_int8`` (custom2) and
``illixr_gemmini_fp32`` (custom3). Each translation unit has its own generated
parameter header, C++ class names, scratchpad, accumulator and configuration
state. Both reject accelerator instructions outside hart 0. RVV remains
available on all simulated harts. Exit records report actual model dispatch
counts and hart masks.

``scripts/run_spike.py`` accepts ``named_extensions`` entries in matrix cases,
each containing ``name`` and ``library``. A shared library is loaded once; each
named extension is registered separately. Firmware, library and source hashes
are retained. Eye-enabled cases set ``require_eye`` as well as the existing
VIO/prediction/GPU checks. The Spike timer remains 10 MHz and the Zephyr tick
rate 10 kHz; instruction counts are not hardware cycle measurements.

The active Spike workspace is ``spike-dual`` under the validation workspace.
Its sequence is interleaving, standalone RITNet with vector/interrupt checks,
and full eye-enabled pipelines with RVV and FP32 BLAS on one and four harts.
A failed stage stops the sequence and preserves its output.

This functional extension does not validate mstatus.XS context semantics. The
initial dynamically loaded XS-checking model rejected the first accelerator
instruction, so this test uses the previously accepted functional model; that
limitation is explicit in the library manifest. DMA/cache ordering, AXI errors,
accelerator latency and physical scheduling performance remain RTL/FPGA checks.
