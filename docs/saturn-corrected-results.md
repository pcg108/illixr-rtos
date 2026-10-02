# Corrected Shuttle and Rocket/Saturn: first hardware results

Corrected single-core Shuttle/Saturn passed both architectural diagnostics and
both original scalar/RVV full workloads. The same firmware that previously
faulted now completes without the application bounds correction. This validates
the divider retirement gate for the reproduced failure and these workloads.
Rocket/Saturn passed the scalar diagnostics and scalar workload, but its original
RVV workload exhausted the 100-billion-cycle limit during numerical preflight.
Rocket RVV remains incomplete and is not accepted.

Results workspace: `/scratch/prashanth_illixr_openblas_20260927T162930Z`.
Routed timing: corrected Shuttle +0.097 ns setup / +0.010 ns hold; Rocket
+0.152 ns setup / +0.010 ns hold. Both images have packaged drivers and bitstreams.
All below use one core, 10 kHz ticks and modeled 1 GHz / 1 MHz clocks.

| Workload | Rocket scalar | Corrected Shuttle scalar | Corrected Shuttle RVV |
|---|---:|---:|---:|
| Acceptance | Pass | Pass | Pass |
| Application time (s) | 6.425051 | 6.866732 | 5.250036 |
| VIO poses | 14 | 14 | 15 |
| Cameras processed / skipped / dropped | 16 / 31 / 3 | 16 / 22 / 12 | 17 / 25 / 8 |
| IMUs at each consumer | 501 | 501 | 501 |
| Render / timewarp deadline misses | 0 / 0 | 0 / 0 | 0 / 0 |
| Fresh on-time presentations | 183 | 190 | 190 |
| Native position / orientation error | 0 / 0 | 0 / 0 | 0 / 0 |
| DGEMM calls | 979 | 979 | 982 |
| Aggregate DGEMM cycles | 1,196,307,097 | 1,613,483,830 | 314,724,887 |
| Trace export (s) | 1.096678 | 1.064288 | 0.821058 |
| Host time including setup (s) | 450.78 | 462.45 | 417.91 |

BLAS cycle intervals include preemption. The delivered camera sequences differ;
these are complete application observations, not identical-operation microbenchmarks.
Native replay uses each actual delivered sequence. Exact runtime equivalence
does not establish physical trajectory accuracy.

The rejected-FDIV test now leaves its 9.0 destination unchanged on both images.
Both also passed the 80,000-check competing-thread arithmetic fixture without
errors. These tests and the unchanged full-workload ELFs are pinned in each
hardware snapshot's `control/validation.json`.

## Rocket RVV investigation

The original Rocket RVV run passed VLEN/FP64/vector-context preflight but emitted
no completed BLAS self-test record before reaching 100 billion target cycles.
It did not reach full replay; pose counts and backend speedup are unavailable.

After corrected Shuttle finished, a diagnostic build added entry/exit logging
around each numerical self-test operation. Both cores returned from all 520
BLAS/reference regions, but both reported numerical self-test failure. Therefore
this instrumentation changes the outcome and does not exonerate either hardware
or library path. It also does not identify an outstanding call as the cause of
the original stall.

A second diagnostic records the first mismatching result bit patterns and the
associated operation, shape and flags. Its Spike build passed all 349,536
numerical checks. On FPGA it identified these first mismatches:

| Core | First failing operation | Shape and flags | First absolute error |
|---|---|---|---:|
| Corrected Shuttle | DTRSM | m=47, n=49; left, upper, no transpose, non-unit diagonal | 0.0001552896 |
| Rocket | DGEMM | m=47, n=49, k=48; both inputs transposed | 0.0105860113 |

Both diagnostic runs finished all 349,536 numerical comparisons and failed the
unchanged tolerance. The next diagnostic repeats the checks with IRQs masked
only around each reference/BLAS call; logging occurs outside the masked region.
This is an isolation experiment, not a production workaround or performance run.
That IRQ-masked diagnostic passed all 349,536 checks on Rocket, but corrected
Shuttle repeated the same DTRSM mismatches bit-for-bit. The Shuttle failure
therefore does not require an IRQ during the reference/BLAS call. Rocket's
improvement suggests interrupt or timing sensitivity, but those builds differ
in code layout as well; it is not yet proof of a context-switch defect.

The same-ELF runtime experiment has now completed on both cores. Each phase
ran all 349,536 comparisons with the original tolerances:

| Mode, in execution order | Corrected Shuttle errors | Rocket errors |
|---|---:|---:|
| Baseline | 79 | 64 |
| IRQs masked during calls | 78 | 0 |
| Memory fences around calls | 39 | 280 |
| IRQ masking plus fences | 117 | 0 |
| Baseline repeated | 118 | 718 |

Rocket passed both masked phases and failed every unmasked phase in one ELF.
This strengthens the evidence for interrupt sensitivity, but does not yet
identify whether the defect is in architectural trap handling, context state,
or another timing-sensitive interaction. Fences alone were insufficient.
IRQ masking is diagnostic only and is not a proposed production fix.

Corrected Shuttle failed in every mode. Mapping comparison indices back to
matrix coordinates places the first mismatch of every phase at row 0, column 48
(zero-based) of a 47-by-49 DTRSM result: the last column. The affected variants
are LUNN, LLTN and LUNU. This narrows the next investigation to the triangular
solve's tail-column path; it does not establish that all errors share one cause.
Only the first 16 mismatches per phase were logged. The localization is saved in
`diagnostic-rvv-after-shuttle/runtime-ab-localization.json`.

The earlier full-workload pass on corrected Shuttle remains valid for that ELF
and run, but these diagnostic failures prevent declaring the RVV backend robust
across builds. No estimator equations or numerical tolerances were changed.

The library archive is unchanged
(`d19784dea78b7271b177f54567ecaff8d35be755c25a34a964ea4f1e3be7d2c1`).
The runtime experiment used a 10-billion-cycle / 15-minute bound; earlier
single-phase diagnostics used 5 billion cycles / 15 minutes. Both runtime
experiments finished all five phases and exited with numerical failure, rather
than timing out. Original workload limits and preserved artifacts are unchanged.

## Focused diagnostic follow-up

Both FPGA runs completed all focused tests. The matching Spike build passed all
578,880 checks. Both FPGA runs used the identical ELF.

| Focused result | Corrected Shuttle | Rocket |
|---|---:|---:|
| Tail cases failing / executed | 0 / 324 | 3 / 324 |
| Active arithmetic errors: IRQs enabled, no competitor | 0 | 78 |
| Active arithmetic errors: IRQs masked, no competitor | 0 | 0 |
| Active arithmetic errors: IRQs enabled, vector competitor | 0 | 82 |
| Active arithmetic errors: IRQs masked, vector competitor | 0 | 0 |
| Diagnostic outcome | Pass | Numerical failure |

The active arithmetic test is a small assembly loop independent of OpenBLAS.
All 11 logged Rocket error examples contain 20,001.0 where exactly 20,000.0 is
expected. Their logged VL, VTYPE, VSTART, VCSR and FCSR values are correct. The
78/82 totals count failing output elements, not necessarily distinct interrupts.
Unmasked phases observed timer callbacks during the assembly-call window;
competing-thread progress was verified. Masked phases observed no callbacks
inside that window. This reproduces Rocket's corruption without the OpenBLAS
library or an OpenVINS calculation. It suggests interrupted instruction execution
or resumption is worth examining; it does not yet prove instruction replay or
exclude a runtime context-handling defect.

Rocket's three failed tail cases all had interrupts enabled and TRMM-prepared
input. Two already had incorrect products before TRSM; one had a correct product
and an incorrect solve. No masked tail case failed.

Corrected Shuttle passed all narrowed cases, so its earlier full-fixture failure
has not been reproduced in isolation. The smaller fixture changes preceding work,
memory placement and timing; this pass does not erase the earlier failures.
The next Shuttle step is to preserve the full fixture while instrumenting the
first divergent operation. Rocket now has a library-independent reproducer for
investigating precise interrupt handling and context restoration.

Both runs ended with complete diagnostic output, without a watchdog or cycle
limit. Shuttle passed HTIF exit. Rocket signaled the expected test-failure exit;
its manager returned -15, and the analyzer records the run as complete but failed.
These are diagnostic results, not full-workload acceptance.

Details are in `tests/rvv_focused/README.md`. Source snapshots, firmware hashes,
build logs, Spike output and localized results are in `diagnostic-rvv-focused/`.
FPGA output is under `runtime/rvv-focused-tail-active-{shuttle-fixed,rocket}`.

## Follow-up localization and candidate fix

Further diagnosis reproduced Rocket's extra vector add with a private timer
handler, excluding Zephyr ISR/context handling: 14 of 64 rounds failed on Rocket,
while all 64 passed on corrected Shuttle. A candidate Rocket trap-PC fix is being
built in an isolated image. It is not yet hardware-validated.

Shuttle's complete captured solve independently checks out on the host. One
missing block update at row 39, column 48 propagates into 39 wrong output entries.
Internal kernel-return fence tests and a direct vector-store/scalar-load fixture
are investigating this boundary. See [the investigation report](rvv-interrupt-and-trsm-debug.md)
for evidence, limitations and current artifact paths.

## Artifact locations

- `runtime/shuttle-single-fdivfix-{rejected-fdiv,arithmetic-context,scalar,rvv}/`
- `runtime/rocket-saturn-single-{rejected-fdiv,arithmetic-context,scalar,rvv}/`
- `runtime/rvv-selftest-progress-{shuttle-fixed,rocket}/`
- `runtime/rvv-selftest-errors-{shuttle-fixed,rocket}/` (second diagnostic)
- `runtime/rvv-selftest-runtime-ab-{shuttle-fixed,rocket}/` (same-ELF comparison)
- `diagnostic-rvv-after-shuttle/runtime-ab-localization.json`
- `diagnostic-rvv-after-shuttle/errors-spike.log`
- `diagnostic-rvv-after-shuttle/error-runs.json` and `error-runs.log`

The corrected quad-core Shuttle image has not been built or validated in this
round; these results apply to the single-core configurations only.

## Rocket trap-PC fix accepted for the tested single-core cases

The new Rocket/Saturn image passed the native interrupt reproducer, the full
BLAS diagnostic and both scalar/RVV pipelines using preserved firmware. See
[the corrected Rocket results](rocket-saturn-trappc-results.md). The original
Rocket timeout and earlier diagnostic failures above remain historical records;
Shuttle's distinct remaining failure is still unresolved.
