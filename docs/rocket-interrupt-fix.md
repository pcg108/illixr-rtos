# Rocket/Saturn deferred interrupt correction

Status: accepted for the reproduced defect. All 8,640 expanded cases pass on fresh
Scala-generated RTL. The isolated FPGA image passes routed timing, four-hart
preflight, all three preserved interrupt/copy diagnostics, and the original
uninstrumented full ILLIXR workload. See the [final results](rocket-interrupt-fix-results.md).
The first candidate built
and met timing, but its FPGA preflight stalled and is not accepted.

Workspace:
`/scratch/prashanth_illixr_openblas_20260927T162930Z/rocket-interrupt-fix-v2-20260929T072508Z`.
The prior image, firmware and [failure evidence](rocket-quad-startup-debug.md)
remain preserved.

## Mechanism

The Rocket decode path stops issuing instructions when `csr.io.interrupt` is
asserted (`ctrl_killd`). However, the instruction buffer's ready signal was only
`!ctrl_stalld`, allowing unissued instructions to be removed from the buffer.
Normally the first interrupt token reaches writeback promptly, preserving its
PC. Saturn changes that assumption: its iterative memory fault checker asserts
`mem.block_all`, and Rocket suppresses `wb_reg_xcpt` while that input is high.
During this delay, instruction-buffer PCs advance despite the instructions never
executing. A later accepted interrupt token can therefore save a PC beyond the
architectural resume point.

The earlier scalar/vector trap-PC priority correction does not prevent this
loss of unissued instructions. Both corrections are retained separately.

[rocket-vector-interrupt-ibuf.patch](../patches/rocket-vector-interrupt-ibuf.patch)
changes one ready expression:

```scala
ibuf.io.inst(0).ready := !ctrl_stalld &&
  !(usingVector.B && csr.io.interrupt && ibuf.io.inst(0).valid)
```

On vector-enabled cores, this holds the oldest complete unissued instruction while an
interrupt is pending, until the accepted trap redirects and flushes the frontend.
The valid condition matters: a 32-bit instruction may span two fetch words.
When only its first half is present, IBuf must accept more fetch data before it
can provide a valid instruction/PC to Rocket's interrupt pipeline. Unconditionally
holding ready during an interrupt deadlocks this case.
Non-vector Rocket configurations retain their existing ready logic. Software
interrupt enablement, Zephyr scheduling/context code, OpenBLAS kernels and
estimator math are unchanged.

## Reproducing the hardware source

The candidate uses Rocket revision
`885dd5966a49ce19512e3cc3275515926e729ba2` and Saturn revision
`2dae15d16bebe10f413ea39bdd18aa402eb419dd`. Apply both Rocket patches
to an isolated copy of that source, in this order:

1. `patches/rocket-vector-trap-pc.patch` (scalar/vector exception-PC priority).
2. `patches/rocket-vector-interrupt-ibuf.patch` (deferred interrupt handling).

Run `git apply --check` and then `git apply` from the copied
`generators/rocket-chip` directory, using absolute patch paths. If starting from
the previous accepted single-core or baseline quad snapshot, the first patch
is already present: verify it with `git apply --reverse --check` instead of
applying it twice. Keep the existing FireSim configuration and platform patches.

The current build's `hardware-quad/provenance/` directory records the complete
source revisions, source diffs, both patches and their hashes. Its private
`RocketCore.scala` contains both fixes; the original Chipyard checkout and prior
images remain unchanged. Newly generated RTL must pass the regression below,
and the resulting image must pass every hardware gate before replacing an
accepted image. Applying the patch alone is not hardware acceptance.

## RTL regression

[tests/rocket_irq](../tests/rocket_irq/README.md) compiles the generated Rocket
module with its real CSR and instruction-buffer logic, driving Saturn's actual
block-all interface. The expected resume PC comes from committed instructions,
not the instruction-buffer state. Each test also checks execution after MRET.

| RTL | Cases | Passed | Failed |
|---|---:|---:|---:|
| Existing quad image | 8,640 | 4,800 | 3,840 |
| First candidate, unconditional interrupt hold | 8,640 | 6,233 | 2,407 |
| Refined candidate, hold complete instructions | 8,640 | 8,640 | 0 |
| Fresh Scala-generated refined candidate | 8,640 | 8,640 | 0 |

Cases sweep 64 interrupt arrival cycles, nine 0–64-cycle deferrals, five
instruction layouts and three fetch-pause patterns. Layouts cover aligned 32-bit,
compressed, and mixed or straddled instructions. These are Rocket interface
regressions, not a replacement for vector-datapath and full-system hardware
validation. The refined expression was first tested as a generated-RTL edit;
the independent run on freshly emitted Scala RTL also passes all 8,640 cases.
Its source SHA-256 is
`0f0fa539c380b3f3f8597c768cb87774a1925c562c106c36d999e6bcbf731b4f`.
The validation runner checks that exact RTL hash again before FPGA acceptance.

The preserved first candidate is in
`/scratch/prashanth_illixr_openblas_20260927T162930Z/rocket-interrupt-fix-20260929T031224Z`.
Its earlier 432 aligned/compressed tests all passed, but did not exercise split
instructions. Its FPGA preflight printed the successful four-hart clock/atomic
check and then stopped progressing, beyond the original image's full preflight
duration. It was interrupted through scoped runner cleanup and recorded as
incomplete; no later workloads were run. The expanded RTL tests expose the
split-instruction deadlock in that candidate. The new image must demonstrate
that preflight and the original failures are both corrected on hardware.

## Hardware acceptance

A new independent quad Rocket/Saturn source/build snapshot retains the same
REFV256D128 configuration, 256 MiB RAM, generated clocks, FASED settings,
30 MHz FPGA clock request and NORETIMING strategy. Four build workers and the
existing resource guard remain in use. Resource stops are not automatically
restarted.

After routed timing and generated-hardware checks, validation executes these
preserved ELFs sequentially under the shared FPGA lock:

1. Four-hart platform/vector/BLAS preflight.
2. Repeated-copy fixture, interrupts-enabled phase first (200,000 copies).
3. Repeated-copy fixture, masked phase first (150,000 copies).
4. Private assembly timer-handler fixture (150,000 copies); its intentional
   final breakpoint exports history. Diagnostic acceptance requires all three
   phases to pass, the exact final breakpoint PC/cause, complete history and
   evidence of timer interrupts. A different failure is not accepted.
5. The original uninstrumented ILLIXR RVV ELF, SHA-256
   `d50cb20024043f0c74445e21b483804ef4606e6b68579c6402e022eb61870edc`.

The full workload retains all sample/accounting, native estimator/prediction,
placement, trace and presentation checks. The 24-hour watchdog and
100-billion-cycle cap remain; a timeout or interruption is incomplete.

### Refined image results (2026-09-29)

The build completed at 11:16:47 UTC. Post-route setup slack is +0.090 ns and
hold slack is +0.002 ns, with zero failing endpoints. All 28 bus-skew constraints
pass (minimum slack +2.763 ns). The finalized hardware manifest is
`hardware-quad/control/hardware-4.json` in the workspace above.

The preserved preflight ELF exited normally after 678,430,002 target cycles
(40.7 seconds of emulation; 220.6 seconds including setup and result collection).
The observed hart mask is `0xF`; the timer/core ratio, FP64 vector context checks,
and 16 forced migrations pass. Evidence is in `runtime/preflight/run.json`,
`analysis.json`, and `console.log`.

Both normal copy fixtures exit successfully (200,000 and 150,000 copies).
The private assembly timer fixture completes its three 50,000-copy phases with
zero errors, records 371 timer interrupts and reaches its exact intentional final
breakpoint. The unchanged full workload exits normally with 16 VIO poses, exact
native replay agreement, 501 IMUs at each consumer, and all 50 camera pairs
accounted for. All five cases pass; detailed artifacts are in `validation.json`,
`results.md` and `completion-audit.json`. This validates the reproduced failures
and workload, rather than exhaustive exception/interrupt correctness.
