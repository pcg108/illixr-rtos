# Rocket/Saturn hardware interrupt correction: final validation

The corrected quad-core Rocket/Saturn image passes all five FPGA gates using
preserved firmware. The original uninstrumented ILLIXR RVV binary, which failed
on the previous image, now exits normally and agrees with native replay.

The [mechanism and patches](rocket-interrupt-fix.md) retain two separate Rocket
fixes: scalar exception-PC priority over stale vector writeback, and holding a
complete unissued instruction while Saturn defers interrupt acceptance. Incomplete
split instructions may still assemble. No estimator, OpenBLAS, Zephyr scheduler,
production interrupt masking, dataset or queue-policy change explains these results.

Workspace: `/scratch/prashanth_illixr_openblas_20260927T162930Z/rocket-interrupt-fix-v2-20260929T072508Z`.
The machine-readable audit is `completion-audit.json`; complete case records are
in `validation.json`, `results.json`, and the five `runtime/` subdirectories.

## Hardware and RTL

- Four Rocket harts, each with Saturn REFV256D128: VLEN 256, ELEN 64, 128-bit datapath.
- 256 MiB RAM; generated 500 MHz/500 kHz clock ratio; preserved firmware interprets
  this as modeled 1 GHz CPU/1 MHz timer with 10 kHz Zephyr ticks.
- U250 physical clock request 30 MHz, NORETIMING, four build workers and the
  unchanged resource guard. Build time: 13,677.4 seconds; no guard stop or new OOM.
- Final setup slack +0.090 ns, hold slack +0.002 ns, zero failing endpoints;
  all 859,957 routable nets connected, zero routing errors. All 28 bus-skew
  constraints pass, with minimum slack +2.763 ns.
- Fresh Scala-generated RTL: 8,640/8,640 cases pass. The original RTL fails 3,840
  and the rejected first candidate fails 2,407 of the same expanded cases.
  The tested generated RTL and Vivado input are byte-identical.

## FPGA gates

| Case | Result | Target cycles | Emulation host seconds |
|---|---|---:|---:|
| Platform/vector/BLAS preflight | Pass; mask `0xF`, 349,536 BLAS checks, 16 migrations | 678,430,002 | 40.7 |
| Copy, interrupts enabled first | Pass; 200,000 copies, 6,000,000 checks; normal exit | 357,220,002 | 29.5 |
| Copy, masked phase first | Pass; 150,000 copies, 4,500,000 checks; normal exit | 320,580,002 | 28.3 |
| Private assembly timer handler | Pass; three 50,000-copy phases, zero errors, 371 timer interrupts | 545,400,002 | 36.5 |
| Original ILLIXR RVV workload | Pass; normal exit, complete trace and native comparisons | 7,325,250,002 | 279.7 |

The private-handler fixture deliberately ends at breakpoint cause 3, PC
`0x8003b38a`, to export 452 raw words of history. Its intentional HTIF failure is
accepted only as this diagnostic's expected endpoint, after all phases pass.
All other cases require normal HTIF success. The complete full-workload host time,
including programming, loading and collection, was 462.1 seconds.

## Original workload results

| Metric | Result |
|---|---:|
| IMUs consumed by VIO / integrator | 501 / 501 |
| Camera pairs processed / skipped / dropped | 18 / 0 / 32 (50 total) |
| Initialized VIO poses | 16 |
| Maximum native position / orientation error | 0 m / 0 rad |
| Native prediction / transform comparisons | 426 / 680, pass |
| Render / timewarp completions | 680 / 680 |
| Render / timewarp deadline misses | 0 / 0 |
| Fresh on-time modeled presentations | 212 |
| Display slots: new / repeated / no output | 680 / 0 / 1 |
| Distinct frames selected / never selected | 680 / 0 |
| Repeated-image warps | 0 |
| Probe reads / missed deadlines | 681 / 0 |
| Probe reads with repeated VIO and advancing integration | 204 |
| Maximum observed VIO age | 3.416654128 s |
| Queue high-water marks: VIO IMU / integrator IMU / camera | 410 / 1 / 8 |
| IMU / application trace / GPU trace overflow | 0 / 0 / 0 |
| Application time | 5.675048 s |
| Trace-export target time | 0.955862 s |

Every plugin worker executes real sample or frame work on all four harts; the
prediction service likewise executes on all four through its render/timewarp callers.
These are processing/publication counters, not inferred placement from hart startup.

| Worker | Work counts on harts 0 / 1 / 2 / 3 |
|---|---|
| Offline IMU | 111 / 108 / 125 / 157 |
| Offline camera | 8 / 16 / 17 / 9 |
| OpenVINS | 146 / 123 / 109 / 141 |
| IMU integrator | 126 / 129 / 128 / 118 |
| Render | 166 / 166 / 166 / 182 |
| Timewarp | 132 / 153 / 186 / 209 |

OpenBLAS executes 1,005 DGEMM calls with dimensions up to 105, and 5,283 actual
RVV GEMM kernel entries distributed as 1,457 / 1,200 / 1,131 / 1,495 across harts.
The 32 MiB scratch arena has zero outstanding allocations after completion.
DGEMM elapsed time is 441.530 ms; the 372,185,784-cycle sum covers 887 calls whose
cycle measurements stayed on one hart. Another 118 calls migrated, so that cycle
sum must not be presented as total DGEMM time. Mutex wait time is 0.722 ms.

Batched trace transfer carries 1,977,293 binary bytes in 121 batches, yielding
2,923,008 decoded console bytes. Host decoding takes 0.717 seconds. Total target
cycles include boot, selftests and export, beyond the application interval above.

## Accuracy and scope limits

Native agreement uses the actual delivered sequence and unchanged 1 mm/0.001 rad
tolerances. It establishes runtime equivalence, not physical trajectory accuracy.
Only six VIO poses overlap the available ground-truth interval: their endpoint
displacement is 0.306819 m versus 0.001345 m ground truth, a magnitude difference
of 0.305474 m. This is an unaligned endpoint diagnostic, not an aligned trajectory
error metric. Maximum observed propagated-position norm is 43.221815 m.

Of 1,360 prediction requests, 426 are valid comparisons, 185 use startup fallback,
and 749 are stale under the unchanged policy. Zero missed GPU deadlines does not
mean every presentation contains a fresh tracking estimate. The run has 212 fresh
on-time presentations; startup and stale-pose behavior remain visible in the trace.
Maximum modeled presentation observer lateness is 8.380560 ms.

The previous original quad workload did not complete, so its timeout is not a
performance baseline. This validation covers the reproduced failures and full
workload, not every possible exception/interrupt combination. The separate
Shuttle TRSM numerical issue remains unresolved.

## Artifact identities

| Artifact | SHA-256 |
|---|---|
| Original full workload ELF | `d50cb20024043f0c74445e21b483804ef4606e6b68579c6402e022eb61870edc` |
| Generated/tested RTL | `0f0fa539c380b3f3f8597c768cb87774a1925c562c106c36d999e6bcbf731b4f` |
| Raw FPGA bitstream | `40ab3a16ae542901d1b94dd00c80fe4ecd6ee8224da2b74f73b5c55ce3f040aa` |
| Packaged bitstream archive | `97297cf4228aa25ead00672a9853b10257557255e8a45ca58ba3c0e547fe08ba` |
| FireSim driver | `82754fd3773a6723a203bdb89ff93784044c3f3bebdc823903cbbf0c59aa73d3` |
| Boot ROM | `5f6ae520d6612da2b31c2ee70bc872b743a77bbde0abba87bf96aa5fc249a216` |

Prior images, the rejected first candidate, failed runs, and the standalone RTL
reproducer remain preserved. No upstream repository has been modified or maintainer
message sent by this work.
