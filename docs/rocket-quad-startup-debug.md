# Quad Rocket/Saturn startup fault

Resolution: the refined instruction-buffer interrupt correction now passes the
preserved diagnostics and original uninstrumented workload on the corrected quad
FPGA image. See [the final validation](rocket-interrupt-fix-results.md).
The original failure observations below remain preserved for maintainer review.

2026-09-29 UTC. Workspace:
`/scratch/prashanth_illixr_openblas_20260927T162930Z/rocket-saturn-e2e-20260928T185203Z`.

The routed quad image passed timing (+0.150 ns setup, +0.010 ns hold), four-hart
startup/vector context tests, and platform preflight. The original workload ELF
(`d50cb20024043f0c74445e21b483804ef4606e6b68579c6402e022eb61870edc`)
stalled after vector checks and a partial Zephyr log prefix `[00:`. Its first
run reached the 100-billion-cycle limit. An unchanged repeat showed the same
prefix and was deliberately stopped after more than 15 billion cycles to free
the board for diagnostics; it remains incomplete, not a pass.

Two separately linked diagnostic ELFs passed the full workload. The fault-only
one produced 17 VIO poses, delivered all 501 IMUs to both consumers, processed
19 camera pairs and dropped 31, matched native VIO exactly, and had zero modeled
render/timewarp misses. No fault handler ran in that case. These builds change
code layout and are not evidence that the original failure is fixed.

## Direct fatal capture without relinking

`diagnostic-inplace-fatal-v1/patch.py` replaces only 650 bytes inside the original
`z_riscv_fatal_error_csf` function. ELF length and all other bytes are unchanged.
It prints through raw HTIF, without the logger or the HTIF driver's mutex, then
exits with failure. The immutable original ELF is retained.

A separate Spike positive control injects a breakpoint at `main` and verifies
cause 3, the expected PC, all 128 frame/stack words, and diagnostic termination.
Its initial header interleaves with the boot logger; the remaining trap fields
and dump are complete. That breakpoint ELF is never used on the FPGA.

The FPGA diagnostic (`runtime/quad-inplace-fatal-v1`) terminated after
281,030,002 target cycles with:

```
hart   = 0
mcause = 5 (load access fault)
mepc   = 0x800507b6 (dcopy_k+0x216)
mtval  = 0x104a04000
esf    = 0x863e3620
```

The faulting instruction is `vle64.v v24,(a1)` in the OpenBLAS RVV copy kernel,
before the BLAS startup self-test completes and before OpenVINS starts.
The saved register frame contains:

| Register | Value | Role at this instruction |
|---|---:|---|
| a0 | 15 | Element count |
| a1 | 0x104a04000 | Invalid vector-load address |
| a2 | 0x86467540 | Destination address already formed |
| a3 | 0x86467540 | Destination base |
| a4 | 2 | Destination stride |
| a5 | 8 | Current vector length |
| a6 | 16 | Loop index after increment |
| a7 | 16 | Destination stride in bytes |
| t4 | 0x82502000 | Original source base |

The invalid address equals twice the source base. Both the formed destination
address and loop index are consistent with part of a loop iteration being
repeated. This is a hypothesis, not yet a proven trap-return or context bug.
The earlier single-core trap-PC fix remains present in this hardware.

The raw capture establishes that the apparent startup hang hides a processor
fault. The standard error path uses deferred logging and a mutex in HTIF output;
its exact reason for stopping at `[00:` has not been independently isolated.

## Next diagnostic

`diagnostic-inplace-ring-v1` adds a bounded eight-entry trap history per hart,
recording cycle, cause, PC, trap value, mstatus and the relevant address/index
registers. It replaces a single `csrr t0,mepc` in the trap prologue with a jump
after caller registers have been saved. All application code remains at its
original addresses, but interrupt timing changes and can mask the failure.
The 2,592-byte history occupies the verified gap between rodata and bss, outside
both allocators and all section contents. This is a diagnostic, not production
firmware or a scheduler change.

## Repeated copy and trap-history evidence

The full workload with the trap recorder passed (16 poses, all IMUs, 18 cameras
processed/32 dropped, native agreement). Its changed trap timing masks the
original startup failure; it is not a production fix.

A focused fixture replaces the self-test call, retaining the original `dcopy_k`
address and instructions. It repeatedly copies 15 elements with source stride 1
and destination stride 2, checking all results and untouched padding. Four
50,000-copy phases (interrupts enabled/masked/enabled/masked) pass on Spike:
200,000 copies, six million checks. On FPGA, the first enabled phase fails near
copy 10,380, with copy-call registers but an execution PC in the diagnostic
printing helper. The handler frame is complete.

The masked-first variant changes only the phase initialization immediate and
same-length result text. Its first **50,000 masked copies pass**, then the
interrupt-enabled phase fails with an invalid load in the caller's check loop.
This establishes an interrupt-sensitive failure in the focused test; it does
not justify masking interrupts in production.

The repeated-copy fixture with a trap recorder also fails, so it captures the
preceding event (`diagnostic-dcopy-ring-v1/fault.json`):

| Event | Cycle | Cause | Hardware MEPC | a5 (VL) | a6 (index) |
|---|---:|---|---|---:|---:|
| Earlier timer in copy | 186,738,949 | timer | 0x800507c2 | 7 | 15 |
| Last timer | 188,738,966 | timer | 0x8003b2de (caller check loop) | 8 | 8 |
| Resulting fault | 188,745,132 | load access | 0x8003b2e4 | 8 | 8 |

The last timer's address registers still point to the first copy chunk:
`a1=0x82502000`, `a2=0x82503000`, `t4=0x82502000`. A completed 15-element copy
would leave `a6=15`, `a5=7`, and advanced addresses, as the earlier timer shows.
The eventual fatal frame also retains `a0=15`; a normal copy return sets a0=0.
The trap recorder captures MEPC before Zephyr's FPU/context or scheduling work.
The evidence therefore points to an incorrect hardware interrupt PC. A private
assembly timer-handler test is in progress to remove Zephyr entirely from the
interrupt/return path before making the final attribution.

## Private-handler reproduction: hardware attribution confirmed

`diagnostic-dcopy-private-irq-v2` leaves Zephyr's ISR bytes untouched and installs
its own aligned `mtvec` (`0x8003b3c4`) only for the focused fixture. The handler
saves integer temporaries, reads MEPC into the bounded history, rearms this hart's
CLINT comparator, restores temporaries, and executes `mret`. It never writes
MEPC, calls Zephyr, switches threads, or saves/restores vector state. Only the
timer interrupt is enabled on the test hart. The other harts remain running.

On Spike, all three phases pass: 50,000 masked, 50,000 private-timer, 50,000
masked copies, **4.5 million checks**, and 2,007 timer interrupts followed by
one deliberate breakpoint that exports the final history. The final diagnostic
HTIF failure exit is intentional in that successful Spike control.

On FPGA, the 50,000-copy masked phase passes, then the private-timer phase fails
near copy **5,779**. This is a genuine load-access fault (cause 5), before the
intentional end-of-test breakpoint (cause 3).

Immediately before failure, the private handler reads:

```
cause = 0x8000000000000007  (machine timer interrupt)
mepc  = 0x8003b2f8        (check_loop)
```

The caller's actual instruction sequence is:

```
0x8003b2f2: jalr ra,t0       # call dcopy_k; RA = 0x8003b2f6
0x8003b2f6: li   t0,0       # initialize checking index
0x8003b2f8: slli t1,t0,4
0x8003b2fc: add  t2,s5,t1
0x8003b300: ld   t3,0(t2)
```

At that timer boundary, the state is still the copy kernel's: the eventual
frame preserves `a0=15`, `t0=0x800505a0` (the kernel address), and
`t4=0x82502000` (source base). A completed kernel would set a0=0; executing the
caller's initialization would set t0=0. Instead, `mret` resumes at the shift,
uses the kernel address as the index, and the load faults at **0x882a08a00**:
`0x82503000 + (0x800505a0 << 4)`. The fatal PC is `0x8003b300` and RA remains
`0x8003b2f6`.

This independently reproduces incorrect hardware interrupt return-PC selection
on the accepted quad Rocket/Saturn image, with Zephyr removed from the relevant
path. It explains how the startup test can jump into a partly completed copy
iteration and double its source base. The exact RTL signal/condition responsible
is not yet isolated; the existing scalar/vector trap-PC priority patch is
insufficient for this case. No new hardware fix is claimed. Do not conclude this
requires multicore: the failing stream runs on hart 0, but this particular
focused test has not yet been run on the single-core image.

The production workload remains unaccepted on the quad image. No estimator,
OpenBLAS math, queue policy, scheduler policy, or production interrupt masking
was changed. Successful instrumented workloads are retained as diagnostic
results only.
