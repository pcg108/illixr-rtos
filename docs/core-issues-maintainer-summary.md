# Rocket/Shuttle findings for maintainer review

**2026-09-29 update:** the quad Rocket/Saturn image exposes a remaining hardware
interrupt return-PC defect despite the earlier priority patch. The original
startup stall is an RVV `dcopy_k` load-access fault. A repeated-copy fixture
passes with interrupts masked but fails with a private assembly timer handler
that makes no Zephyr calls. Trap-entry records show an inconsistent hardware
MEPC before context restoration. See [the quad diagnostic evidence](rocket-quad-startup-debug.md).
The additional cause is now isolated: the instruction buffer consumes unissued
instructions while Saturn defers interrupt acceptance with `mem.block_all`.
Holding the oldest complete unissued instruction while an interrupt is pending
corrects all 8,640 expanded generated-RTL cases; the preserved original fails
3,840. The first candidate held incomplete instructions too: it passed the
earlier 432 tests but stalled in FPGA preflight and fails 2,407 expanded cases.
The refinement lets IBuf assemble split instructions before holding them. The new
quad image built and met routed timing, and fresh Scala-generated RTL independently
passes all 8,640 cases. Its preserved four-hart FPGA preflight, all three copy/interrupt
diagnostics, and original uninstrumented full workload now pass. The private
assembly handler observes 371 timer interrupts and completes all copy phases
without errors. The full workload produces 16 VIO poses with exact native replay
agreement, complete sample accounting and zero render/timewarp deadline misses.
This additional correction is **validated for the reproduced failures and workload**.
See the [final results and accuracy limitations](rocket-interrupt-fix-results.md) and
[the correction and hardware acceptance gates](rocket-interrupt-fix.md).

Prepared 2026-09-28 from local FPGA experiments. Two RTL corrections passed the
reproduced regressions and full workloads. A third numerical failure on corrected
Shuttle/Saturn remains unresolved; its responsible component is not established.
These findings concern the pinned revisions below, not a claim about current
upstream HEAD. Nothing has been submitted to maintainers by this agent.

| Component | Tested source revision |
|---|---|
| Shuttle | `337385f3634ad0489fa903d9152fcd29d2279f3b` |
| Rocket Chip | `885dd5966a49ce19512e3cc3275515926e729ba2` |
| Saturn | `2dae15d16bebe10f413ea39bdd18aa402eb419dd` |
| OpenBLAS fork | `6d12fcab91ebf4de5eb23c1218727d2551b8635e` |

Hardware validation used single-core FireSim on a local Alveo U250: Saturn
REFV256D128, VLEN 256, ELEN 64, 128-bit vector datapath and system bus, 256 MiB
RAM, generated 500 MHz CPU / 500 kHz timer, firmware factor-two interpretation
(modeled 1 GHz / 1 MHz), 10 kHz Zephyr ticks, and a 30 MHz FPGA request.
OpenBLAS was static and single-threaded, target RISCV64_ZVL256B, FP64, lp64d,
medany. The application used Zephyr SDK 0.17; the RVV library used GCC 13.2.
The existing archive includes an earlier eight-TRMM-kernel O0 compiler workaround;
the other library kernels use O2. That archive did not change during these tests.
Its SHA-256 is
`d19784dea78b7271b177f54567ecaff8d35be755c25a34a964ea4f1e3be7d2c1`.

## 1. Shuttle: rejected scalar divide still writes its destination — fixed

**Suggested issue title:** Scalar FDIV rejected with FS=Off can launch and later
modify its architectural destination.

**Application symptom.** Both scalar and RVV OpenBLAS ILLIXR builds faulted in
OpenVINS stereo matching (`ncc_match`). A saved image coordinate was INT_MAX,
although replaying the captured finite coefficients should round to -2030 and
be rejected as outside the image. The corrupted coordinate combined with an
overflowing application bounds check to produce an invalid image address. This
was shared scalar FP code, not evidence of a vector image-index instruction bug.

**Architectural reproducer.** With interrupts masked, a private trap handler is
installed, `fa0` is seeded to 9.0, and MSTATUS.FS is cleared. The fixture attempts
`fdiv.d fa0, fs0, fs1`. The illegal-instruction handler records cause and PC,
reenables FP access, waits for a possible late completion, and advances MEPC
past the rejected divide. Therefore the destination must remain 9.0.

- Original Shuttle FPGA: correct illegal-instruction cause 2, but fa0 changes
  **9.0 to 0.0**, observed inside the handler and after return.
- Spike: fa0 remains 9.0 in the same instruction-level experiment.
- No Zephyr FP handler, scheduler or OpenBLAS code is required for this violation.

Sources: `tests/image_address/trap.S` and `trap.cpp`. The assembly includes a
preceding divide to reproduce the FP operand-pipeline state. It restores machine
state and callee-saved registers before returning to its Zephyr caller.

**RTL correction.** In Shuttle `src/main/scala/exu/Core.scala`, around line 890,
the divide/square-root unit's `inValid` used pipeline occupancy
`com_uops_reg(0).valid`, which did not exclude a trapping, killed or replayed
instruction. Gate launch on `com_retire(0)` instead; retain the other existing
conditions. This changes launch control, not division arithmetic.

```diff
- divSqrt.io.inValid := com_uops_reg(0).valid && tag === typeTag(t).U && com_fp_divsqrt_valid && !divSqrt_val
+ divSqrt.io.inValid := com_retire(0) && tag === typeTag(t).U && com_fp_divsqrt_valid && !divSqrt_val
```

Patch: [shuttle-fdiv-retirement.patch](../patches/shuttle-fdiv-retirement.patch).

**Validation.** The corrected single-core FPGA preserves the 9.0 sentinel,
passes the preserved 80,000-check competing-FP-thread regression, and passes
both original scalar and RVV full-pipeline ELFs. Those workload ELFs predate the
application bounds correction, so it cannot explain their improvement. They
produce 14 and 15 VIO poses respectively, with exact native replay agreement,
501 IMUs at each consumer and all 50 camera pairs accounted for.

Scope: this validates the reproduced FDIV fault and these workloads. The same
launch gate covers square root, but we did not exhaustively test every rejected
FDIV/FSQRT, replay and exception combination. Corrected quad Shuttle is untested.

## 2. Rocket/Saturn: stale vector PC overrides the interrupt PC — fixed

**Suggested issue title:** Vector writeback PC overrides scalar exception PC,
causing an already executed vector instruction to execute again after MRET.

**Application symptom.** The original RVV numerical preflight did not complete
within the full-workload cycle bound. Instrumented BLAS fixtures completed but
reported wrong products. In a same-ELF test, masking interrupts removed Rocket's
numerical errors; fences around BLAS calls did not. The original timeout is an
observation, not a proven localization of the exact instruction that stalled.

**Architectural reproducer.** A hand-written loop performs exactly 20,000
`vfadd.vf` operations, adding 1.0 to eight initially zero FP64 lanes, with a
scalar decrement and branch. A private CLINT timer handler records MEPC, VSTART,
the scalar remaining-iteration counter and vector lanes, then returns to the
hardware-provided MEPC unchanged. It uses no Zephyr ISR/context-switch code and
no OpenBLAS. Expected final values are exactly representable 20,000.0.

- Original Rocket/Saturn: **14/64 rounds fail**, ending at 20,001.0.
- Example: remaining count 18,162 and MEPC pointing at the add imply 1,838
  completed additions, but the handler observes 1,839. VSTART is zero. MEPC is
  `0x8000030c` (add) instead of the subsequent decrement at `0x80000310` in this
  ELF. Returning reexecutes the already completed add.
- Corrected Shuttle and Spike pass all 64 equivalent private-handler rounds.

Source: `tests/rvv_focused/native_irq.S`; driver and assertions in `focused.cpp`.
The fixture assumes the tested hart-0 CLINT map (MTIMECMP 0x02004000, MTIME
0x0200bff8), temporarily installs MTVEC, and restores the original machine state.
This private-handler reproducer is single-hart only.

**RTL correction.** Rocket `src/main/scala/rocket/RocketCore.scala`, around line
906, unconditionally assigned `csr.io.pc := v.wb.pc` inside
`when (v.wb.retire || v.wb.xcpt || wb_ctrl.vec)`. Saturn can retain an older
vector instruction PC while the scalar pipeline carries an interrupt. Preserve
the scalar exception PC when `wb_reg_xcpt` is set, matching the existing priority
for the exception cause:

```diff
- csr.io.pc := v.wb.pc
+ when (!wb_reg_xcpt) { csr.io.pc := v.wb.pc }
```

Patch: [rocket-vector-trap-pc.patch](../patches/rocket-vector-trap-pc.patch).
The edit is in Rocket's vector integration, not in OpenVINS or OpenBLAS.

**Validation.** With unchanged diagnostic/workload ELFs, corrected Rocket passes
64/64 private interrupt rounds, all 1,747,680 comparisons in the five-phase BLAS
fixture, rejected-FDIV and FP-context diagnostics, and scalar/RVV full pipelines.
Both workloads produce 14 poses with zero native replay error, complete sample
accounting and zero render/timewarp misses. A subsequent RVV full rerun also
passes. No production IRQ masking or internal GEMM fence was added.

This is single-core validation, not exhaustive exception verification. The
subsequent four-core image passed platform preflight but failed the original
uninstrumented full workload, exposing the separate deferred-interrupt defect
described at the top of this report. The new quad build retains this priority
patch and adds the instruction-buffer correction; its five-case FPGA validation
now passes. See `rocket-saturn-trappc-results.md` for the earlier single-core metrics.

## 3. Corrected Shuttle/Saturn: timing-sensitive TRSM error — unresolved

**Suggested investigation title:** Full OpenBLAS fixture loses a triangular
block update on Shuttle/Saturn; internal fence or long return delay masks failure.

This remains after fix 1 and occurs on **one core**, including with interrupts
masked. It is not established as a Zephyr scheduler, context-save, OpenBLAS, or
specific RTL defect. Earlier passing full workloads remain valid for those
executions, but the failing diagnostics prevent declaring the backend robust.

**Localization.** The full deterministic fixture reproduces errors in 47-by-49
left triangular solves. Captured coefficients and incoming RHS pass an independent
host solve. Reconstructing A*x localizes the equation violation to zero-based
**row 39, column 48**; the error then propagates through backward substitution.
In one captured unit-diagonal case, the bad pivot equals the scaled incoming RHS
without the update from columns 40 through 46. This is consistent with a missing
block update; it does not prove which instruction or component lost it.

The latest nonunit-diagonal capture has 40 wrong output elements and maximum
absolute error 0.021219281663516054. The sole equation residual is
0.04243856332703211. These are matrix-fixture errors, not measured pose errors;
they exceed the unchanged tolerance `1e-12 + 1e-10 * abs(reference)` substantially.

The selected OpenBLAS target uses **generic TRSM calling an RVV DGEMM kernel**
for block updates, not the separate `trsm_kernel_*_rvv_v1.c` implementation.

**Same-ELF control experiment:** each phase checks 349,536 values.

| Phase | IRQs | Action at internal DGEMM return | Errors |
|---:|---|---|---:|
| 1 | Enabled | Baseline | 118 |
| 2 | Masked | Baseline | 79 |
| 3 | Masked | 1 NOP | 79 |
| 4 | Masked | `fence rw,rw` | 0 |
| 5 | Masked | 16 NOPs | 118 |
| 6 | Masked | Baseline | 118 |
| 7 | Masked | 64 NOPs | 0 |
| 8 | Masked | `fence rw,rw` | 0 |
| 9 | Masked | 1 NOP | 79 |
| 10 | Enabled | Baseline | 158 |

The diagnostic fence is in application integration code
`src/blas_backend.cpp::__wrap_dgemm_kernel`, **immediately after**
`__real_dgemm_kernel` returns and before its scalar TRSM caller consumes the
updated matrix. It is not in OpenVINS or the OpenBLAS kernel source. The baseline
retains a tail jump; nonbaseline modes return through the wrapper. The 64-NOP
pass shows that added execution time can mask the failure, so the fence pass
alone does not establish a memory-ordering fix. No production workaround is on.

**Negative reductions.** A smaller 324-case TRSM fixture passes. Direct
eight-lane vector-store/scalar-load tests pass, including a sweep of 0..96 NOPs,
eight buffer offsets, 16 repetitions and both fence modes: all 24,832 added
cases pass on Shuttle and Spike. The combined reduced FPGA fixture passes
811,584 checks. Thus these reductions do not reproduce the full-kernel failure.
All ten full-fixture phases pass on Spike (3,495,360 checks).

Next useful localization: retain the failing full-fixture context and inspect
the real RVV GEMM block update and scalar TRSM consumer. Scalar/vector memory
visibility, replay timing and other kernel interactions remain hypotheses;
there is no accepted RTL correction for this issue.

## Separate application correction and evidence package

We also corrected signed overflow and unsafe floating-point-to-integer conversion
in image bounds checks. It rejects invalid/NaN/infinite coordinates before
conversion and compares against dimension minus radius instead of overflowing
coordinate plus radius. Sanitized tests and 3,068 exact ordinary stereo/NCC
comparisons passed. This does not fix arbitrary arithmetic corruption and is
separate from the two hardware patches. No estimator equations, calibration,
initialization, precision or Zephyr scheduler policy were changed.

The accompanying archive contains these patches, assembly/C++ reproducer sources,
the captured full-fixture source, before/after diagnostic logs, acceptance JSON,
the captured-solve decoder/results and a SHA-256 manifest. It excludes the large
firmware/bitstream binaries and dataset. It is an evidence/source package, not
a standalone portable build; full reproduction uses the recorded local build
environment. The assembly fixtures can be adapted to a maintainer's bare-metal
test harness without OpenVINS. Diagnostic Spike runs that deliberately used FPGA
clock constants are instruction-level comparisons, not full platform passes.

Original artifacts remain under
`/scratch/prashanth_illixr_openblas_20260927T162930Z`. Useful subpaths are:

- `hardware-single-fdivfix/control/validation.json`
- `hardware-rocket-saturn-single-trappcfix/control/validation.json`
- `runtime/shuttle-saturn-single-image-rejected-fdiv/`
- `runtime/shuttle-single-fdivfix-rejected-fdiv/`
- `runtime/rvv-native-irq-v1-rocket/`
- `runtime/rocket-saturn-single-trappcfix-native-irq/`
- `runtime/rvv-kernel-delay-ab-v1-shuttle-fixed/`
- `runtime/rvv-memory-delay-v2-shuttle-fixed/`
