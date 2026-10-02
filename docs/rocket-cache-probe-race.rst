Rocket nonblocking-cache probe/metadata race
============================================

Status, 2026-10-02
------------------

Root cause is confirmed by the full ILLIXR FPGA trace and an independent
Verilator cache reproducer. The narrow RTL correction passes focused
regressions. The corrected FPGA image passes timing, platform preflight, and
all three full ILLIXR regressions, including the original unmodified failing
ELF. The cache fix is validated on this isolated quad-core diagnostic image.
Default hardware images have not been replaced; the broader SoC/fence
comparison matrix is a separate, unfinished task.

Failure mechanism
-----------------

Zephyr increments hart 1's IRQ nesting counter at 0x84dc8790 from zero to one.
The register arithmetic and submitted store are correct. The line is initially
exclusive and clean, so Rocket's NonBlockingDCache queues the store while its
MSHR updates the line metadata to dirty.

An invalidating coherence probe overlaps this transition. In NBDcache.scala,
MSHR.io.probe_rdy excludes several refill/writeback states but does not exclude
s_meta_write_req. The post-write meta_hazard register only starts after a
metadata write fires. A probe can therefore accept a previously read clean
state while the dirty-state write is still pending, including the cycle in
which that write finally occurs.

The probe replies without dirty data and invalidates the line before the
queued store replays. The cache acknowledges the replay and attempts a data
write with a zero way-enable mask. No cache way receives the value one.
A later refill/read returns zero. IRQ exit decrements that zero to 0xffffffff,
takes the nested-interrupt return path, restores the wrong stack frame, and
faults at PC zero.

Hart 0 writes its own FPU/vector state fields at 0x84dc8788/0x84dc878c during
the failing interval. These share the counter's cache line but do not overlap
its bytes. The trace contains no direct other-hart scalar overwrite of the
counter. The cache-only reproducer needs no Zephyr, Saturn, Gemmini, RITNet,
or OpenVINS computation.

FPGA evidence
-------------

These are global target cycles, not hart mcycle values:

* 1001000369: counter load returns zero; line state is exclusive-clean.
* 1001000371: invalidating probe accepted; CPU submits store data one.
* 1001000372: store retires and enters the metadata miss path, without kill/nack.
* 1001000374: MSHR writes dirty metadata for the counter line.
* 1001000376: probe replies as clean, without data.
* 1001000377: probe invalidates the line.
* 1001000380: store replay responds with way=0, although its data is one.
* 1001000381: data-array write has value one and way enable zero.
* 1001000411: refill brings back zero in the counter's word.
* 1001000644: first subsequent observed counter read returns zero.
* 1001001968: committed IRQ exit decrement underflows.

The firmware preserves original normal-path instructions and layout; only
fatal reporting is instrumented. All four harts have trace coverage.
The cache-internal observer changes no cache control outputs.

The trace does not log metadata writes to unrelated indices. The small RTL
reproducer separately demonstrates the arbitration condition: completion of
an unrelated miss delays the counter's metadata write while a probe has
already read the old clean state.

Correction
----------

Patch: ../patches/rocket-nbdcache-probe-metadata-interlock.patch

Block same-index probes in s_meta_write_req as well as the existing excluded
states. Retain the existing post-write metadata hazard delay. The resulting
source condition is::

  io.probe_rdy := !idx_match ||
    (!state.isOneOf(states_before_refill :+ s_meta_write_req) && meta_hazard === 0.U)

The patch applies cleanly to the accepted baseline and reverse-checks against
the isolated source used to build the accepted corrected image. Application math, IRQ assembly,
Zephyr scheduling, interrupt masking, and operating-point settings are unchanged.

Validation
----------

* Minimal single-case reproduction: the uncorrected cache acknowledges a store
  of one but subsequently reads zero; the corrected cache reads one. This
  fixture uses read-only internal observations and no forced RTL state.
* Probe/unrelated-grant overlap: 26,880 cases; baseline loses 11 stores,
  corrected RTL loses none.
* Follow-up invalidation, including shared downgrades: 26,880 cases;
  baseline loses 22 stores, corrected RTL loses none.
* Deliberately missing-store controls: all 26,880 detected on corrected RTL.
* Existing cache arbitration regression: 6,720 cases pass, including 107,520
  synthetic vector-side requests. This is cache-request traffic, not a test
  of the actual Saturn arithmetic datapath.
* Existing backpressure regression: 9,600 cases pass, including four miss
  sources, C-channel stalls, and D-channel gaps.
* Diagnostic FPGA platform preflight: pass, hart mask 0xF, timer/core progression
  and atomics checked. Uncorrected ILLIXR reproduces the original fault at the
  same target cycle with the cache mechanism visible.
* Corrected FPGA: final setup slack +0.069 ns, hold slack +0.010 ns, no
  failing endpoints, all routable nets routed, and all 28 bus-skew constraints
  pass. Packaging and four-hart platform preflight pass.
* Corrected compact diagnostic workload: normal exit at 7,969,410,002 target cycles;
  all 501 IMUs reach both consumers, 18 cameras processed and 32 dropped,
  16 VIO poses with zero reported native-replay error, and 32 valid eye results.
  Prediction/transform checks pass; render and timewarp report no missed
  deadlines and 204 fresh on-time modeled presentations.
* The corrected workload's bounded hardware trace covers all four harts and
  observes 16,553 hart-1 interrupt entries and exits, with zero underflows.
  Diagnostic checkpoint accounting passes in drain-only mode; it does not
  compare intermediate tensor values. Final eye outputs are checked separately.
* Timing-variation control: normal exit at 8,008,210,002 target cycles,
  matching native/eye/prediction checks, and 17,240 observed IRQ entries/exits
  with no underflows.
* Original unmodified failing ELF: normal exit at 7,974,100,002 target cycles,
  all workload gates pass. Its bounded hardware trace is byte-for-byte
  identical to the passing compact diagnostic trace.
* Every full workload uses RVV OpenBLAS and INT8 eye inference. The presence
  of FP32 Gemmini in this image does not establish a new FP32-backend test.

Earlier synthetic-manager tests incorrectly required an empty D-beat queue
before issuing any probe. This unnecessarily serialized probes with grants
for unrelated lines and hid this race. Removing that restriction, while
retaining the same-line outstanding-grant constraint, exposed the failures.

Evidence and build locations
----------------------------

Investigation root::

  /scratch/prashanth_illixr_quad_fault_20261001T204248Z

Relative to that directory:

* root-cause-confirmed.json: diagnosis, exact events, and provenance.
* runtime/trace-v3-compact-context/hardware-trace.log: FPGA raw trace.
* runtime/trace-v3-compact-context/cache-store-audit.json: store/metadata audit.
* software/cache-probe-minimal-rtl: single-case baseline/corrected reproducer.
* software/cache-probe-grant-overlap-rtl: failing full-cache test matrix.
* software/cache-probe-grant-fixed-rtl: corrected RTL and matching tests.
* software/cache-probe-eviction-rtl: baseline/corrected eviction comparison.
* software/probe-fix-regressions: arbitration and backpressure results.
* hardware-irq-trace-v4: isolated corrected FPGA source/build snapshot.
* control/trace-v4-sequence-state.json: guarded build state.
* control/trace-v4-acceptance-state.json: timing/packaging/runtime follower.

FPGA trace SHA-256::

  8fd415bcb0ed413e42b86fe8cafdab456c0949af0a861afa5380f658085669c1

Firmware SHA-256::

  ba9257cb2c6daa3eece8f7e1b037e97d9fbaa2892cfc5b694f82d801d422baea

Portable reproducer
-------------------

The investigation root contains rocket-cache-probe-reproducer.tar.gz and its
SHA-256 sidecar. It includes the generated RTL, observation wrapper, one-case
fixture, source correction, hashes, expected logs and fresh validation results.
Extract it and run::

  python3 run.py --work /tmp/cache-race-check --verilator /path/to/verilator

The work directory must be new. The runner compiles baseline and corrected
models separately, then requires the exact lost-store failure on baseline and
success on the candidate. It needs Python 3, Verilator, make and a C++ toolchain;
no FPGA, Zephyr firmware or dataset. Fresh local validation with Verilator 5.022
took about 32 seconds with two build workers. This validates the reproducer,
independent reproducibility of the RTL failure and correction.

Final full-workload comparison
------------------------------

All three runs use the corrected quad-core Rocket+Saturn+dual-Gemmini image,
scheduler-managed plugins, the modeled 1 GHz core/1 MHz timer, 10 kHz Zephyr
ticks, and the existing all-operation drain-only RITNet diagnostic firmware.
The original ELF was not rebuilt or modified for the final regression.

========================  ================  =============  ===============
Case                      Target cycles     Application s  Trace export s
========================  ================  =============  ===============
Compact diagnostic        7,969,410,002      5.525104       1.744912
Startup timing variation  8,008,210,002      5.550073       1.758730
Original failing ELF      7,974,100,002      5.525104       1.749582
========================  ================  =============  ===============

Each case delivers all 501 IMUs to both consumers, processes 18 stereo pairs,
drops 32, and generates 16 VIO poses. All 16 match the native replay with zero
reported position/orientation error, within unchanged 1 mm / 0.001 rad limits.
All 32 final eye predictions match the reference. BLAS numerical selftests
and vector-context checks pass. Actual processing/publication records show
all four harts used by each of the six measured plugin workers.

Compact/original runs complete 662 renders and 662 warps; the timing variation
completes 665 of each. All report zero render/timewarp deadline misses and 204
fresh on-time modeled presentations. Startup fallback and stale predictions
remain explicitly reported; zero deadline misses does not mean every
presentation carries a fresh tracking estimate. The workload continues beyond
the tracking input interval for the repeated-eye diagnostic.

Host runworkload durations are 571.469 s, 584.175 s, and 573.921 s, respectively;
these include host management/collection work and exclude the separately
recorded FPGA infrastructure setup. These instrumented runs are regression
evidence, not an uninstrumented performance benchmark.

Native agreement is runtime equivalence, not trajectory accuracy. For the
original case's six ground-truth-matched poses, estimated endpoint displacement
is 0.370236 m versus 0.000301 m in ground truth; no coordinate alignment or
physical-accuracy acceptance threshold is applied. The cache correction does
not change estimator equations or address that pre-existing accuracy limit.

Final evidence and identities
------------------------------

The investigation root's corrected-fpga-validation.json consolidates the
acceptance checks, actual artifact hashes, per-plugin placement, BLAS counters,
GPU/prediction/eye checks, numerical comparisons, and focused RTL results.
control/final-root-cause-audit.py independently checks those acceptance records
and hashes. control/root-cause-completion-audit.json records goal completion
scope. Historical test records retain their original at-the-time status.

Original firmware SHA-256::

  647d8c2c0bb72987049aa08af9da8af4631b7047e2f272f283521531474e5131

Corrected bitstream package SHA-256::

  5d116f11c5983e9a765269e36614d2f0a9b30bdab04aacabcffd2d627e7503be

Corrected FireSim driver SHA-256::

  12a06b90ef3f575ad28baf21c3ffa6f2aca4f43783fc44972a7abf25264f0106

Original/compact passing bounded hardware trace SHA-256::

  ecd5e3b8f1ee264c8121cabe0a876fe1e8bcb87c389f5c8316f5850785e50698

These results validate the narrow cache interlock without Zephyr scheduler,
interrupt masking, estimator, or eye-inference math changes. FPGA cache
observers remain in this accepted diagnostic image. No claim is made that
the broader configuration/backend matrix has been rerun or that production
default bitstreams have been replaced.
