# RVV interrupt and triangular-solve investigation

Workspace: `/scratch/prashanth_illixr_openblas_20260927T162930Z`.
All new firmware is diagnostic-only. The accepted OpenBLAS archive, estimator
equations and tolerances remain unchanged. Original results are preserved.

## Rocket: observed failure and candidate mechanism

The first focused FPGA run reproduced incorrect arithmetic without OpenBLAS:
an explicit assembly loop sometimes accumulated 20,001 instead of 20,000.
Both unmasked phases failed, including the phase without a competing thread.
Both masked phases passed. Logged vector control state was correct. The same
diagnostic passed on corrected Shuttle and Spike.

A subsequent private-handler test disables FS, VS, or both and skips a rejected
vector FP instruction. Rocket passed all 32 cases: all destination sentinels
were preserved, and the expected illegal-instruction PC/cause was recorded.
This rules out a side effect of these rejected operations in the tested cases.
The accompanying unmasked arithmetic test still failed (69 and 89 output-element
errors), while both masked phases again passed.

Static inspection found a candidate trap-PC selection problem in the pinned
Rocket/Saturn integration. In `RocketCore.scala`, the vector block overrides
`csr.io.pc` when `v.wb.retire || v.wb.xcpt || wb_ctrl.vec`, without excluding
`wb_reg_xcpt`. Saturn's `PipelinedFaultCheck.scala` drives `io.s2.pc` from its
saved `s2_inst.pc`, which updates on `s1_valid` even though the separate `s2_valid`
controls whether an operation retires. The scalar interrupt pipeline can carry
an interrupt without issuing that interrupted instruction to Saturn. A stale
vector PC overriding the scalar exception PC could therefore resume at an
already executed vector instruction. This is a candidate, not a validated fix.

The next diagnostic uses a private CLINT timer handler during the arithmetic
loop. It records hardware MEPC, the remaining scalar loop count, and the vector
sum, then returns to that MEPC unchanged. It excludes Zephyr ISR/context code.
It passed all 64 rounds on Spike. On Rocket, 14 of 64 rounds failed without
any Zephyr exception handler or context restoration. In every failing round,
MEPC pointed at the vector add, but the vector sum was already one greater than
that PC and the scalar iteration counter permit. Returning to the recorded PC
produced 20,001 in all eight lanes. For example, remaining=18,162 implies 1,838
completed adds at the add instruction, but the handler observed 1,839; MEPC was
0x8000030c (add), rather than the subsequent decrement at 0x80000310.

This establishes a hardware precise-interrupt/resumption failure independent of
OpenBLAS and Zephyr context handling. Corrected Shuttle passed all 64 identical native timer rounds.
The stale-PC mux is the leading source-level explanation; the patch still needs hardware validation. A candidate preserves
`csr.io.pc` from the scalar writeback stage whenever `wb_reg_xcpt` is asserted,
matching the existing exception-cause priority. No existing image was changed.
An isolated build is running under
`hardware-rocket-saturn-single-trappcfix`, with the original guard limits, four
workers, low priority and unchanged Saturn/core parameters. On successful timing
and packaging review, its validator will run the unchanged native-IRQ reproducer,
full BLAS fixture, scalar diagnostics and scalar/RVV full workloads. Any failing
gate blocks subsequent cases.

## Shuttle: retain the broader reproducer

The smaller 324-case triangular fixture passed on corrected Shuttle, although
the earlier full fixture failed repeatedly. Its first mismatches were at row 0,
column 48 of a 47-by-49 solve. Narrowing the test changes preceding work, memory
placement and timing, so the smaller pass does not clear the earlier failure.

The full five-phase fixture now optionally captures the first failing 47-row
solve's coefficient matrix, incoming right-hand side and output. It also records
whether the preceding TRMM product passed. Data is exported after the phases;
comparisons and BLAS call ordering remain unchanged, but copies and added storage
still perturb execution. The host decoder independently solves the captured
triangular system and checks output and padding with the original tolerances.

The capture firmware passed all 1,747,680 checks on Spike. Its first attempt hit
a 2-billion-instruction diagnostic bound after two passing phases; that attempt
remains incomplete. A rerun of the same ELF with a 5-billion-instruction bound
completed successfully. This adjusts the expanded diagnostic's bound, not the
full-workload acceptance limits. The FPGA capture has completed on corrected Shuttle.

The full capture reproduced the failure with 39 / 0 / 0 / 40 / 0 mismatches
across baseline, IRQ-mask, outer-fence, both and repeated-baseline phases.
Its frozen case is a 47-by-49 LLTU solve (left, lower, transposed, unit diagonal)
at case 374 in the first phase. The preceding TRMM comparison had zero errors.
The independent host solve of the captured input reproduces the original expected
fixture with zero errors. The FPGA differs at 39 entries, with maximum absolute
error 0.019659735349716434.

Multiplying the FPGA result by the captured triangular matrix localizes the
underlying equation violation to **row 39, column 48 only**. The FPGA pivot there
is 0.19357277882797735, exactly the incoming RHS scaled by 1/0.7; the correct pivot
is 0.17391304347826086. Their difference equals the entire block-update sum from
columns 40 through 46. That missing update propagates through backward
substitution into the other wrong entries. This is much larger than rounding
noise and occurs in a unit-diagonal solve.

A new five-phase diagnostic tests `fence rw,rw` immediately after the internal
RVV DGEMM kernel returns, before the scalar triangular solve consumes its output.
The earlier outer-call fences do not cover this boundary. A vector-store / scalar-
load ordering issue is a hypothesis, not yet a proven cause. The comparison uses
one ELF with modes baseline, IRQ mask, kernel-return fence, both, baseline again.
The first version passed all five phases, including both unfenced baselines,
so it does not establish a fence fix. Inspection shows that even the unfenced
path introduced a post-kernel return sequence, which could itself hide a timing
hazard. Its results are under `diagnostic-rvv-kernel-fence/` and
`runtime/rvv-kernel-fence-ab-v1-shuttle-fixed/`.

A second version restores the direct tail call on the unfenced path. It is
queued alongside a small assembly store/load test: initialize a buffer, issue an
eight-lane vector store, then immediately load its last lane with a scalar load,
with and without an intervening fence. It varies buffer offsets across a cache
line and checks both the immediate read and final buffer contents with IRQs
masked. These follow-ups are under `diagnostic-rvv-memory-order/`.

Source inspection also confirms that the selected `RISCV64_ZVL256B` target uses
the generic TRSM kernels, which call the RVV DGEMM kernel for block updates.
The separate `trsm_kernel_*_rvv_v1.c` files are not the selected TRSM kernels for
this target. A tail-column failure must be traced through the actual generic
solve and its DGEMM calls rather than inferred from those unused files.

## Artifacts

- `diagnostic-rvv-resume-capture/`: immutable source snapshots, build and Spike logs.
- `diagnostic-rvv-resume-capture/capture-resume-state.json`: current capture queue.
- `runtime/rvv-rejected-vector-v1-{rocket,shuttle-fixed}/`: private rejected-op test.
- `runtime/rvv-full-trsm-capture-v1-shuttle-fixed/`: full-fixture capture.
- `diagnostic-rvv-native-irq/`: private timer-handler source, Spike results and queue.
- `runtime/rvv-native-irq-v1-{rocket,shuttle-fixed}/`: private timer-handler FPGA tests.

FPGA tests are sequential, with board availability checks, shared locks, existing
resource/recovery holds, 15-minute watchdogs and 10-billion-cycle diagnostic bounds.
Numerical failures remain failures; incomplete runs are never passes.

## Latest completed fence comparison and Rocket build

The second internal-kernel fence comparison completed with the unfenced direct
return restored. Each phase ran 349,536 checks in the same ELF:

| Mode | Errors |
|---|---:|
| Unfenced baseline | 0 |
| Unfenced, interrupts masked | 104 |
| Fence at internal GEMM return | 0 |
| Kernel-return fence plus IRQ masking | 0 |
| Unfenced baseline repeated | 183 |

This supports the kernel-return boundary as the relevant location; it does not
prove that the fence's ordering effect, rather than its delay/return path, is the
cause of improvement. The capture in the masked unfenced phase again has correct
input and a sole equation residual at row 39, column 48. Forty output entries
are wrong in that captured solve, with maximum error 0.042438563327032164.
The small vector-store/scalar-load fixture passed both 512-case modes on both
cores, so it has not isolated the full fixture's failure.

The Rocket trap-PC candidate bitstream finished and passed routed timing:
setup WNS +0.108 ns, hold WHS +0.010 ns, no failing endpoints. Initial driver
packaging stopped because a copied symlink pointed at the previous hardware
workspace. The old link and failed attempt were preserved; the private snapshot
now links to its own verified runtime configuration. Validation resumed without
rebuilding hardware or changing firmware. Native-IRQ, full-BLAS and full-workload
acceptance remain pending until their actual runs complete.

## Corrected Rocket validation completed

All six corrected-image cases passed, including 64/64 private timer rounds,
1,747,680 full-fixture BLAS comparisons, and the unchanged scalar/RVV full-pipeline
ELFs. Both pipelines generated 14 poses with zero native comparison error, full
sample accounting and zero render/timewarp deadline misses. This validates the
trap-PC correction for the reproduced failure and these workloads. See
[the corrected Rocket results](rocket-saturn-trappc-results.md) for details.
Shuttle's remaining issue has not been fixed by this Rocket-specific change.

## Shuttle kernel-return delay controls

The next diagnostic uses one ELF with ten full-fixture phases. In order, these
are baseline, masked baseline, masked 1-NOP delay, masked internal fence, masked
16-NOP delay, masked baseline, masked 64-NOP delay, masked internal fence,
masked 1-NOP delay, and baseline. Each phase retains the original 349,536 checks
and tolerances. All modes are diagnostic-only; production defaults are unchanged.

Spike passed all 3,495,360 checks. The corrected single-core Shuttle FPGA run is
in progress under `runtime/rvv-kernel-delay-ab-v1-shuttle-fixed/`. This longer
diagnostic has a 20-billion-cycle limit and 15-minute host watchdog. Its immutable
source snapshot and logs are under `diagnostic-rvv-kernel-delay/`.

The archived wrapper disassembly confirms a direct tail jump for the baseline,
and 1, 16 or 64 uncompressed NOPs without a hardware fence on the delay paths.
The fence path executes `fence rw,rw` immediately after GEMM returns. Every
nonbaseline path also adds an ordinary return through the wrapper, so a passing
delay mode would not establish which part of that additional time masks the bug.
Conversely, a passing fence and failing delays would strengthen, but would not
alone prove, the memory-ordering hypothesis.

RTL inspection is following Shuttle's commit-stage `scalar_check`, Saturn's
pending-store overlap checks and store acknowledgements. No change to these
paths has been made or validated yet.

The first seven FPGA phases reproduced the failure: baseline 118 errors,
masked baseline 79, masked 1-NOP delay 79, masked fence 0, masked 16-NOP delay
118, masked baseline 118, and masked 64-NOP delay 0. Remaining phases are still
running. The passing 64-NOP mode demonstrates that time alone can hide the
failure in this run; a passing fence is therefore insufficient evidence that
its ordering semantics, rather than its stall, are responsible.

A follow-up assembly sweep is built and queued behind this run. It varies the
delay between a vector store and a scalar read from 0 to 96 NOPs, checks eight
buffer offsets, and compares fenced and unfenced modes with interrupts masked.
All 24,832 added sweep cases passed Spike. Disassembly confirms exactly 96
four-byte NOPs in the delay table and the expected scalar-load target. Artifacts
are under `diagnostic-rvv-memory-delay/`; the FPGA case is
`runtime/rvv-memory-delay-v2-shuttle-fixed/`. The runner stops if its predecessor
is interrupted/incomplete or a resource/recovery hold is present.

## Completed delay comparison and store/load sweep

Both runs completed without interruption. The ten full-fixture phases reported
118, 79, 79, 0, 118, 118, 0, 0, 79 and 158 errors, respectively. Both internal
fence phases passed; both 1-NOP phases and the 16-NOP phase failed; the one
64-NOP phase passed. Each phase performed 349,536 checks. The overall full
fixture remains a numerical failure, with all records present.

The first captured failure again has correct inputs and one equation residual
at row 39, column 48. This case is LUNN (nonunit upper triangular): 40 output
entries differ from the host solve, maximum absolute error 0.021219281663516054,
and the sole residual is 0.04243856332703211. The independent host solve of the
captured inputs matches the original fixture. Results are in the run's
`solve-comparison.json`.

The smaller store/load sweep passed all 24,832 added cases on corrected Shuttle,
with every delay from 0 through 96 covered in both fence modes. The original
immediate-load tests, reduced TRSM tests and active-vector interrupt tests also
passed; the combined fixture completed 811,584 checks. Thus the direct
store/load timing experiment did not reproduce the full-kernel failure and
does not confirm the suspected completion-boundary hazard. Both diagnostic
processes have finished. The next useful localization is the actual RVV GEMM
block update and its scalar TRSM consumer, preserving the failing full-fixture
context as much as possible. No production workaround or Shuttle RTL correction
has been accepted for this remaining issue.
