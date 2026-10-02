# Image-address and FP exception diagnostics

These optional fixtures isolate the single-core Shuttle camera failure. All
diagnostic switches default OFF. See
[the investigation](../../docs/shuttle-image-address-debug.md) for exact artifacts
and the distinction between fixture results and complete workload acceptance.

| CMake switch | Purpose |
|---|---|
| `ILLIXR_DIAGNOSTIC_FAULTS` | Wrap the fatal handler; record CSRs and a bounded post-fault stack dump |
| `ILLIXR_DIAGNOSTIC_IMAGE_ADDRESS` | Print a bounded number of invalid camera coordinates/ROI addresses |
| `ILLIXR_DIAGNOSTIC_IMAGE_ARITHMETIC` | Replay captured coefficients using explicit FP instructions with a competing FP thread |
| `ILLIXR_DIAGNOSTIC_FPU_BARRIER` | Requires arithmetic fixture; compare a status-read barrier before Zephyr FP exception entry; **did not fix FPGA corruption** |
| `ILLIXR_DIAGNOSTIC_FPU_TRAP` | Test that an FP-disabled, rejected divide has no destination-register side effects |

`arithmetic.S` provides an intermediate-store version and a tight instruction
sequence. The C++ driver runs each with IRQs enabled/disabled. The current source
runs 200,000 checks per phase; the preserved earlier context-reproduction ELF runs
80,000. Correct rounded output is -2030 for every check. A passing record must
also have zero errors and actual competing-thread progress.

`trap.S` temporarily installs a private trap handler while interrupts are masked.
It seeds fa0 to 9.0, disables FP access and attempts `fdiv.d fa0, fs0, fs1`.
The handler records cause/PC/instruction, enables FP access, waits for any illicit
completion and skips the rejected instruction. Correct behavior is illegal
instruction (cause 2), with fa0 unchanged at 9.0 both in and after the handler.
The test restores mtvec, mstatus, fcsr and callee-saved registers before returning
to Zephyr. This deliberate exception test is separate from normal firmware.

The production bounds regression is `tests/native/test_image_bounds.cpp`:
build native tests and run `ctest -R '^image_bounds$' --output-on-failure`.
Its target enables undefined-behavior and float-to-int conversion sanitizers.

The FPGA binaries reused directly on Spike contain FPGA clock constants and
therefore fail the Spike platform clock-ratio check. Inspect the explicit fixture
record; do not count those processes as successful full platform tests.
