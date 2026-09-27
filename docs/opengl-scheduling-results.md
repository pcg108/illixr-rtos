# Native OpenGL scheduling validation

All three scheduler-managed Spike cases and the quad-core FireSim preflight/workload passed. The slow-render case uses a 16,666,666 ns GPU delay (two display periods). The standard cases use 6,944,445 ns rendering, 1 ms timewarp, 1 ms post-vsync render offset, and 1 ms timewarp scheduling margin at nominal 120 Hz.

Functional correctness passed; deadline performance remains limited. Images and GPU delays are modeled, and the unchanged estimator still has physical trajectory drift.

[Full comparison report](/scratch/prashanth_illixr_firesim_20260926/opengl-schedule-20260927T005455Z/measured-export/results/pipeline-comparison.md) · [Machine-readable results](/scratch/prashanth_illixr_firesim_20260926/opengl-schedule-20260927T005455Z/measured-export/results/pipeline-summary.json) · [Flow and implementation](gpu-pipeline.md)

| Case | VIO poses | Renders | Warps | Image reuses | Fresh on-time presentations | Application target s | Bulk export target s | Host s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Dual Spike | 23 | 885 | 885 | 0 | 40 | 7.392580 | 20.155525 | 253.016 |
| Quad Spike | 23 | 884 | 884 | 0 | 42 | 7.383805 | 20.130650 | 295.712 |
| Dual Spike, slow render | 23 | 295 | 884 | 589 | 79 | 7.392580 | 14.886135 | 211.805 |
| Quad FireSim | 11 | 1223 | 1229 | 6 | 4 | 10.258940 | 108.567306 | 2325.801 |

Host time is the recorded case duration; FireSim includes FPGA deployment. Its driver alone ran for 2,161.8 host seconds. Bulk-export time is measured on the target clock after worker shutdown and excludes the final summary lines. FireSim completed normally after 59,489,870,002 target cycles, below the 100-billion limit. Application time accounts for 5,129,470,000 of those cycles; bulk export accounts for 54,283,653,000.

## Input delivery and numerical checks

Every case delivered all 501 IMUs to both consumers and accounted for all 50 stereo pairs. Spike processed 25 camera pairs and dropped 25. FireSim processed 13, skipped 19, and dropped 18; it produced 11 initialized, finite VIO poses.

VIO replay of each actual delivered input sequence matched the native estimator exactly in reported position and orientation. All prediction/transform comparisons passed the 1 mm / 0.001 rad pose tolerances and 1e-5 absolute-plus-relative transform tolerance. FireSim compared 218 valid predictions and 1,229 transforms: maximum prediction orientation error was 5.9605e-8 rad and maximum transform-element error was 1.1963e-7.

The forced-reuse case produced 589 repeated-image warps, including 165 with valid predictions from newer IMU state. Saved render poses remained unchanged while each warp requested its own deadline prediction.

## Deadlines and modeled presentation

| Case | Render deadline misses | Warp deadline misses | Skipped render opportunities | Empty / missed warp opportunities | New / repeated / absent display output |
| --- | ---: | ---: | ---: | --- | --- |
| Dual Spike | 590 | 527 | 0 | 1 / 0 | 731 / 155 / 1 |
| Quad Spike | 590 | 694 | 0 | 1 / 0 | 728 / 156 / 1 |
| Dual Spike, slow render | 295 | 487 | 590 | 2 / 0 | 690 / 195 / 2 |
| Quad FireSim | 833 | 984 | 6 | 1 / 0 | 1022 / 206 / 2 |

Deadlines use the original target and publication time; none were retargeted. Fresh on-time presentation requires both predictions to be valid and the completed warp to be available at its intended boundary. FireSim achieved 4 such presentations out of 111 fresh warp completions. The full report records wake/completion lateness and observer lateness separately. These modeled events are not physical scanout measurements.

Zephyr uses 1 ms ticks; measured GPU completion includes scheduling delay beyond the configured latency. Stale prediction records during the long estimator drain remain explicitly labeled and frozen under the unchanged 50 ms policy. No scheduling margin, tick rate, or estimator math was changed to hide missed deadlines.

## Multicore execution

All six worker plugins had actual processing/publication observations on all four FireSim harts. Predictor calls also executed on all four harts; the service has no separate worker. Placement remained unrestricted. For the GPU workers, arrays below are ordered by hart 0/1/2/3:

| Worker | Processing counts | Publication counts |
| --- | --- | --- |
| render_loop | [276, 300, 349, 298] | [352, 287, 303, 281] |
| timewarp | [240, 282, 369, 338] | [334, 323, 288, 284] |

The supplemental audit found 1,222 overlapping modeled render/warp GPU intervals in FireSim. Its six repeated-image warps and 106 fresh warps using a newer source than the saved render pose demonstrate the independent consumers.

## Remaining accuracy limitations

Native agreement establishes runtime equivalence, not physical accuracy. In the available overlapping ground-truth interval, FireSim VIO estimated 0.201928 m endpoint displacement versus 0.000823 m ground truth. This comparison uses displacement magnitudes without coordinate alignment, over three matching VIO poses. The existing integrator/gravity behavior and estimator drift were preserved.

FireSim uses the accepted FASED memory model. Spike is a functional ISA simulator; neither backend measures real GPU performance.

## Verification and preserved evidence

- 140 Python tests passed, including v2 corruption rejection and legacy traces.
- Native GPU tests exercised schedule phase, future-slot advancement, retained concurrent snapshots, image reuse, startup, delayed presentation observation, late publication, final/no-frame shutdown, blocking waits, cancellation, 512 exact desktop transforms, and five rejected invalid configurations.
- 500 desktop prediction comparisons matched exactly; 120 angular-adapter continuity cases had maximum error 1.95153e-9 rad.
- Four isolated firmware builds passed under the unchanged resource guard. Prior firmware/results were preserved. A 119-file C/C++ boundary audit confirmed only the GPU/main/test files changed, and all 30 protected math/prior-artifact hashes remained unchanged.
- FireSim startup observed mask 0xF, 4,096 successful atomic exchanges, and the 500 kHz timer / 500 MHz target-core ratio. The preflight and workload both exited normally.
- The FPGA was idle afterward, the runtime lock was released, and Zephyr remained clean.

Artifacts, hardware/driver/firmware hashes, console logs, placement evidence, native comparisons, guard telemetry, and audits are under `/scratch/prashanth_illixr_firesim_20260926/opengl-schedule-20260927T005455Z/measured-export`. The parent directory preserves preliminary runs before export timing instrumentation.

[Prediction audit](/scratch/prashanth_illixr_firesim_20260926/opengl-schedule-20260927T005455Z/measured-export/provenance/final-prediction-audit.json) · [Independence audit](/scratch/prashanth_illixr_firesim_20260926/opengl-schedule-20260927T005455Z/measured-export/provenance/final-independence.json) · [Preservation audit](/scratch/prashanth_illixr_firesim_20260926/opengl-schedule-20260927T005455Z/measured-export/provenance/final-preservation-audit.json)
