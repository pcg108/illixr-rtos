# Single-core Shuttle+Saturn OpenBLAS FPGA test

The actual U250 single-core REFV256D128 image passed platform preflight. Both
scalar OpenBLAS and RVV full-pipeline attempts then failed in the same OpenVINS
camera image read. Neither workload is an end-to-end pass.

**Investigation update:** the invalid ROI is now decoded, and a separate test
confirms a Shuttle FP exception side-effect bug. A corrected single-core image
is building; see [the diagnostic report](shuttle-image-address-debug.md).
The results below preserve the original failed attempts.

## Configuration and acceptance

Workspace: `/scratch/prashanth_illixr_openblas_20260927T162930Z`.
Hardware: dual-issue Shuttle, Saturn VLEN 256 / datapath 128, FP64, 256 MiB RAM.
Routed timing passed at the requested 30 MHz physical FPGA clock (setup slack
+0.152 ns, hold +0.010 ns). Firmware uses the accepted modeled 1 GHz CPU /
1 MHz timer interpretation and 10 kHz Zephyr ticks. The generated hardware clock
ratio remains 500 MHz / 500 kHz. Workload: 50 stereo pairs / 501 IMUs,
scheduler-managed placement, existing GPU scheduling and batched trace export.

Platform preflight passed with hart mask 0x1, 2,048 successful atomic exchanges,
monotonic timer progression, 9,999,870 core cycles per 10,000 timer ticks, and
normal HTIF exit. Its complete analysis is in
`runtime/shuttle-saturn-single-platform-preflight/analysis.json` under the workspace.

## Workload results

| Check | Scalar OpenBLAS | RVV OpenBLAS |
|---|---|---|
| BLAS numerical self-test | 349,536 checks passed | 349,536 checks passed |
| Vector preflight | Not applicable | Passed: VLEN 256, FP64, hart mask 0x1, zero errors |
| Full pipeline | Fatal load access fault | Fatal load access fault |
| Fault PC | 0x80008760 | 0x80008956 |
| Fault address (mtval) | 0x78865c284e | 0x7886a07f4b |
| Diagnostic host runtime, including setup | 225.24 s | 246.95 s |
| Complete trace / native replay | Unavailable / not run | Unavailable / not run |

RVV preflight exercised competing vector threads, timer interrupts and blocking,
with 32 rounds per worker; migration is inapplicable on one hart. Instrumented
RVV self-tests recorded 2,948 dgemm kernel entries and 28 entries each for dgemv_n
and dgemv_t. Together with preserved disassembly, these establish actual vector
kernel execution on Saturn, rather than merely vector ISA flags in an ELF.

An initial production scalar attempt failed first. A diagnostic-only fault
wrapper was then enabled for separate scalar and RVV runs to preserve mcause,
mtval, mepc and the complete Zephyr register dump. The wrapper only changes fatal
error reporting. The RVV attempt was explicitly diagnostic; it does not waive
the scalar acceptance gate. Estimator math and data remain unchanged.

## Failure evidence and limits

Both PCs resolve to `OpenVINS::ncc_match`, `plugins/openvins/SLAMMath.hpp:184`,
reading `row2[dx]` via `lbu a3,0(a4)`. mcause is 5 (load access fault). Register
a4 is 0x00000178865c284e for scalar and 0x0000017886a07f4b for RVV, outside
RAM [0x80000000, 0x90000000). The row stride in both dumps is 752 bytes.
This is evidence of a shared camera image-addressing failure, not an RVV-only
failure. Its root cause was not established at the time of these attempts.

A hypothesis at the time was invalid stereo candidate coordinates and
signed bounds-check overflow: 752 * 2^31 = 0x17800000000, matching the common
excessive pointer offset. Actual candidate coordinates and conversion inputs
were subsequently captured and the exact pointer reconstructed; see the updated
diagnostic report above. No bounds or math fix was included in these original
test binaries.

The runner detected the fatal markers and stopped each failed simulation. There
was no normal workload HTIF exit, final result, or complete trace. Consequently
VIO pose counts, delivery totals, render/timewarp deadlines, native estimator
agreement and backend speedup cannot be reported from these attempts. The host
times above are failed-attempt durations, not performance comparisons. Earlier
successful Spike results remain separate evidence.

## Preserved artifacts

All paths below are relative to the workspace:

- `hardware-single/control/hardware-1.json`: accepted hardware manifest.
- `runtime/shuttle-single-workloads.json`: original scalar acceptance attempt.
- `runtime/shuttle-single-diagnostic-workloads.json`: independent diagnostic results.
- `runtime/shuttle-single-openblas_scalar-522dc2d8d6/`: original scalar failure.
- `runtime/shuttle-single-openblas_scalar-d99266255c-diagnostic/`: complete scalar fault dump.
- `runtime/shuttle-single-openblas_rvv-04c718d686-diagnostic/`: complete RVV fault dump.
- `runtime/shuttle-single-{scalar,rvv}-fault-symbolization.txt`: saved PC symbolization.

Each case preserves firmware and hardware manifests, hashes, configuration,
console/UART logs, run metadata, and incomplete analysis. Immutable firmware is
under `software/artifacts/`. Both workload runner processes have exited.
The quad image also achieved routed timing closure, but its platform preflight
was interrupted to prioritize this single-core request and remains incomplete.
