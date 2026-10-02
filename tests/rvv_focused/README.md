# Focused Saturn/RVV diagnostics

`ILLIXR_DIAGNOSTIC_RVV_FOCUSED=ON` replaces the ordinary BLAS numerical fixture
with two isolation tests. It is off by default, requires the RVV backend, and
does not change the OpenBLAS archive, estimator, or production interrupt policy.
Use a platform-check-only build. These results are not full-workload acceptance.

The triangular test executes 324 cases: a 47-row matrix, nine column widths
(1, 3, 7, 8, 9, 47, 48, 49, 50), three flag variants (LUNN, LLTN, LUNU), three
repetitions, masked/unmasked calls, and two input paths. One path prepares the
right-hand side with the existing Eigen oracle; the other uses TRMM and checks
it against that oracle before TRSM. This separates an incorrect incoming product
from an incorrect solve. Every result and leading-dimension padding element is
checked with the existing absolute/relative tolerances. Oracle preparation has
interrupts masked in both modes. Each case records its first mismatching matrix
coordinate and bit patterns, total errors, input errors and execution hart.

The active-vector test runs four phases: timer interrupts or timer interrupts
plus a higher-priority competing vector thread, each with masked/unmasked calls.
Each phase has 32 repetitions at VL 1, 3 and 8, using FP64/LMUL 2. An assembly
loop accumulates exactly representable integer sums while keeping vector data,
VL/VTYPE/VSTART/VCSR, FCSR and a scalar FP operand live. Checks require exact
results and CSR state. The competing thread overwrites all vector registers,
changes vector CSRs and a scalar FP register, then blocks for a tick.

A diagnostic timer records callbacks during the assembly-call window (including
the few instructions at its boundaries). Unmasked phases require callbacks in
that window and, when applicable, competing-thread progress. Masked phases
require zero callbacks in the window. This is coverage evidence, not an exact
interrupted-instruction trace. The final records report coverage and errors;
a passing phase does not exclude defects in untested vector instructions.

Two further optional diagnostics separate rejected operations and interrupt
resumption from Zephyr context handling:

- `ILLIXR_DIAGNOSTIC_RVV_REJECT`: temporarily installs a private trap handler
  with interrupts masked, disables FS, VS, or both, and attempts a vector FP add.
  The handler records cause/PC, enables access, and skips the rejected instruction.
  All eight destination lanes must retain their 9.0 sentinel. There are four
  modes and eight repetitions. Trap vector and machine status are restored.
- `ILLIXR_DIAGNOSTIC_RVV_NATIVE_IRQ`: single-hart CLINT test using a private
  timer handler. It schedules one timer interrupt during an exact 20,000-add
  vector loop, records MEPC, the scalar remaining-iteration counter and all eight
  vector lanes, then returns to the hardware-provided PC unchanged. It does not
  invoke Zephyr's ISR, floating-point handler or scheduler. The observed vector
  value must agree with the scalar counter and interrupted instruction, and the
  final value must remain 20,000. It restores MTIMECMP, MIE, MTVEC, MSTATUS and
  FCSR before releasing the caller's IRQ lock. This test uses the verified hart-0
  CLINT addresses and is not a multicore fixture.

`ILLIXR_DIAGNOSTIC_RVV_MEMORY` tests vector-store/scalar-load visibility with
interrupts masked. The original test reads the final lane immediately after an
eight-lane FP64 vector store, with and without a fence (512 cases each). A second
test sweeps 0 through 96 uncompressed NOPs between the store and scalar load,
with 16 repetitions at each of eight buffer offsets for each fence mode. It
computes the jump into the NOP sequence before issuing the store. Both modes
use the same jump and scalar load; only the fenced mode inserts `fence rw,rw`.
These additional 24,832 cases check the immediate scalar result and all eight
memory values after a fence. The 194 `ILLIXR_MEMORY_DELAY_RESULT` records report
errors per delay and mode. All expected values are exactly representable integers.

This sweep investigates a possible completion-boundary timing window; it does
not assume that the window is the cause of the full TRSM failure. Its v2 launch
uses Spike first, then corrected Shuttle, with outputs in
`diagnostic-rvv-memory-delay/` and `runtime/rvv-memory-delay-v2-shuttle-fixed/`.

Separately, `ILLIXR_DIAGNOSTIC_TRSM_CAPTURE` augments the original full five-phase
BLAS A/B fixture, rather than this reduced fixture. It snapshots inputs before
47-row TRSM calls until the first failing solve, freezes that case's inputs and
outputs, and exports them after all phases. This preserves the operation sequence
and original comparisons, but the added copies and storage still alter timing
and memory layout. `scripts/analyze_solve_capture.py` checks capture completeness
and independently solves the captured left-triangular system on the host.

Run the corresponding Spike build first to check the fixture. The FPGA runner
then uses one identical ELF on corrected Shuttle and Rocket sequentially, with
the existing board availability checks and locks. Each FPGA diagnostic has a
15-minute host watchdog and a 10-billion-cycle limit. All 324 tail records,
four active-vector summaries and the final marker are required for completeness.

Artifacts for this launch are under
`/scratch/prashanth_illixr_openblas_20260927T162930Z/diagnostic-rvv-focused`;
FPGA outputs are under `runtime/rvv-focused-tail-active-{shuttle-fixed,rocket}`
in the same workspace. Source snapshots, hashes, build logs and Spike output are
preserved. The sequence stops on a build/Spike failure, resource guard stop,
FPGA recovery hold or interrupted FPGA run; it does not retry automatically.
