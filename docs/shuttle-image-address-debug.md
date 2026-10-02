# Shuttle image-address failure: diagnostic results

**Validation update:** the corrected single-core image passed both architectural
diagnostics and the original scalar/RVV full workloads. See
[the hardware results](saturn-corrected-results.md). The investigation below
preserves the reasoning and evidence from before that validation completed.

The single-core Shuttle+Saturn FPGA has a confirmed floating-point exception
side-effect bug. An FP divide rejected because FP access is disabled still
changes its destination register. Spike executing the identical test ELF does
not. A candidate RTL correction is building in an isolated snapshot; the new
image has **not yet passed hardware validation**.

This investigation also confirmed signed overflow in the image bounds checks.
The application now rejects invalid coordinates without overflowing or converting
out-of-range floating-point values to integers. Those checks prevent this invalid
image access, but cannot make arbitrary hardware-corrupted arithmetic correct.
Estimator equations, precision, calibration and initialization are unchanged.
No Zephyr scheduler or FP context implementation was changed.

## Evidence chain

The workspace is `/scratch/prashanth_illixr_openblas_20260927T162930Z`.
All paths below without an absolute prefix are relative to that workspace.

1. Both scalar and RVV OpenBLAS passed their numerical preflights, then failed at
   an image load in `OpenVINS::ncc_match`. This was shared scalar FP/camera code.
2. A post-fault stack dump preserved the original NCC hot path. It showed the
   right template center `(365, INT_MAX)` and ROI origin `(358, 2147483640)`.
   The image itself was valid: base `0x86942a40`, 480 rows, stride 752.
   `0x86942a40 + 2147483640 * 752 + 358 = 0x17886941426`, exactly the bad pointer.
   The old `y + half` test overflows at this coordinate and admits the ROI.
3. The saved finite epipolar coefficients should produce rounded y = -2030,
   which should simply be rejected as outside the image. Instead, the caller
   had `INT_MAX`. A fixture using the exact coefficients and instruction sequence
   reproduced NaN/infinite results when a competing FP thread ran across timer
   interrupts. IRQ-masked phases did not reproduce the corruption.
4. A smaller architectural fixture disabled FP access, attempted `fdiv.d`, and
   handled its illegal-instruction exception by skipping the instruction.
   The destination was seeded to 9.0 and therefore must remain 9.0. On the FPGA
   it became 0.0, both in the handler and after returning. On Spike it stayed 9.0.

| Test | Original single-core FPGA | Same ELF on Spike |
|---|---|---|
| Rejected divide, destination sentinel | **Fail: 9.0 → 0.0**, cause 2 | Diagnostic passes: 9.0 → 9.0, cause 2 |
| Competing-thread arithmetic, 80,000 checks | 2 errors, unmasked tight sequence | 0 errors |
| FP-status-read barrier A/B, 400,000 checks | 3 errors without barrier, 2 with barrier | 0 errors |

These Spike runs intentionally used the unchanged FPGA ELF to compare instruction
semantics. Their platform clock-ratio check fails because Spike's timer differs
from the FPGA timer; they are **diagnostic arithmetic passes, not complete platform
or workload passes**. Earlier correctly configured full Spike validation remains
documented separately in [OpenBLAS Spike results](openblas-spike-results.md).

The barrier experiment did not fix the problem and remains disabled. An earlier
instrumented camera workload happened to finish with 15 poses and exact native
agreement, but added checks changed hot-path timing/layout. It is not evidence
that the original hardware is correct.

## RTL correction under validation

Shuttle revision: `337385f3634ad0489fa903d9152fcd29d2279f3b`.
In `generators/shuttle/src/main/scala/exu/Core.scala`, the divider launch used
`com_uops_reg(0).valid`. Unlike normal FP issue and the retirement scoreboard,
this did not exclude trapping, killed or replayed instructions. A rejected
instruction could launch the divider with stale operands and later write back
without an architectural retirement.

[The patch](../patches/shuttle-fdiv-retirement.patch) gates divide/square-root
launch with `com_retire(0)`, which already excludes those conditions. This is an
execution-control correction, not a change to division arithmetic. The direct
test establishes the architectural violation; the patched-image reruns are still
required to validate this correction and its effect on the full workload.

The original source snapshot, bitstreams and firmware remain preserved. The
corrected build is `hardware-single-fdivfix/`, with four workers, the established
memory guard, unchanged REFV256D128 hardware configuration, 30 MHz FPGA request
and NORETIMING strategy. A resource guard stop does not automatically restart it.

After routed timing and packaging checks, the queued validator acquires the shared
U250 lock and runs these immutable existing binaries in order:

1. Rejected-divide diagnostic and platform checks.
2. The 80,000-check competing-thread arithmetic diagnostic and platform checks.
3. The previously failing scalar OpenBLAS full pipeline.
4. The previously failing RVV OpenBLAS full pipeline.

Any failure blocks later cases. The full workloads retain the existing 24-hour
watchdog, 100-billion-cycle cap, delivered-input native replay comparison and
pipeline acceptance checks. Crucially, these ELFs predate the bounds correction;
that application change cannot mask the hardware comparison.

Live status: `hardware-single-fdivfix/control/validation.json` and
`hardware-single-fdivfix/build-runs/*/status.json`. The quad image shares the
affected Shuttle implementation and has not been validated with this correction.

## Application bounds correction and tests

`template_center_fits` checks the coordinate against image dimensions minus the
template radius, rather than adding a radius to an unchecked coordinate. Stereo
matching validates the input point before conversion and range-checks the same
rounded epipolar expression while it is still floating point. NaN, infinity and
out-of-image results are rejected before conversion to `int`.

The native `image_bounds` test passes with undefined-behavior and float-cast
sanitizers. It covers `INT_MIN`/`INT_MAX`, invalid template sizes, image edges,
NaN/infinite epipolar results, flat images and a known translated stereo match.
A separate preserved old/new comparison passed **3,068 exact comparisons** of
ordinary NCC/stereo results. The original extreme-coordinate reproductions
triggered signed-overflow diagnostics before the correction.

## Preserved diagnostic artifacts

| Evidence | Workspace path |
|---|---|
| Original scalar/RVV failures | See [original results](openblas-shuttle-single-results.md) |
| Stack-only fault reproduction | `runtime/shuttle-single-openblas_scalar-9d2aadbeaa-diagnostic/` |
| Decoded ROI, coefficients and exact bad pointer | `diagnostic-image-address/fault-evidence.json` |
| Fault decoding script and disassembly | `diagnostic-image-address/` |
| Competing-thread arithmetic failure | `runtime/shuttle-saturn-single-image-arithmetic-context/` |
| Rejected barrier A/B experiment | `runtime/shuttle-saturn-single-image-arithmetic-barrier-ab/` |
| Decisive rejected-divide failure | `runtime/shuttle-saturn-single-image-rejected-fdiv/` |
| Same-ELF Spike diagnostic output | `diagnostic-image-address/rejected-fdiv-spike.log` |
| Native exact-comparison result | `diagnostic-image-address/compare-valid.log` |
| Native sanitizer regression build | `software/native-image-bounds/` |
| Corrected hardware patch/provenance | `hardware-single-fdivfix/provenance/` |
| Pinned validation cases and ELF hashes | `hardware-single-fdivfix/control/validation.json` |

Diagnostic source and build switches are documented in
[tests/image_address](../tests/image_address/README.md).
