# Quad-core FireSim clock experiments

The combined **1 GHz / 10 kHz** default is now validated; see the [current baseline report](current-baseline.md). The results below preserve the two independent experiments that preceded it.

Two independent changes are compared with the accepted OpenGL-style scheduling baseline. Application C/C++, estimator/prediction math, dataset, queue capacities, display schedule, GPU delays, and scheduler-managed placement are identical.

| Case | Modeled CPU | Declared `mtime` frequency | Zephyr ticks/s | Timeout resolution |
| --- | ---: | ---: | ---: | ---: |
| Baseline | 500 MHz | 500 kHz | 1,000 | 1 ms |
| Finer ticks | 500 MHz | 500 kHz | 10,000 | 100 µs |
| Faster modeled target | 1 GHz | 1 MHz | 1,000 | 1 ms |

The faster target reinterprets the existing 1000:1 CPU/mtime cycle ratio. It uses the same accepted quad-core U250 bitstream and driver. The 30 MHz FPGA clock request and FASED latency settings in cycles are unchanged, so FASED latency in modeled nanoseconds halves in the 1 GHz case. This experiment does not establish physical silicon timing closure at 1 GHz.

These independent experiments preceded the new combined baseline: **1 GHz modeled CPU, 1 MHz declared timer, and 10 kHz Zephyr ticks**. The historical results below retain their original settings. Experiment configuration fragments and all four preflight/workload ELFs are isolated under `/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z`.

[Comparison report](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/results/clock-comparison.md) · [Machine-readable results](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/results/clock-comparison.json) · [Exact configuration differences](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/provenance/config-differences.json)

## Results

Both new preflights and workloads passed with complete traces, normal HTIF exits, initialized finite poses, and native equivalence. Increasing the tick rate eliminated observed GPU-stage deadline misses. Modeling 1 GHz increased VIO output and made valid predictions available earlier, but left substantial deadline misses with 1 ms ticks.

| Metric | Baseline: 500 MHz / 1 kHz ticks | 500 MHz / 10 kHz ticks | 1 GHz / 1 kHz ticks |
| --- | ---: | ---: | ---: |
| Render deadline misses / completions | 833 / 1,223 (68.1%) | 0 / 1,230 (0%) | 649 / 973 (66.7%) |
| Timewarp deadline misses / completions | 984 / 1,229 (80.1%) | 0 / 1,230 (0%) | 696 / 973 (71.5%) |
| VIO poses | 11 | 11 | 16 |
| Cameras processed / skipped / dropped | 13 / 19 / 18 | 13 / 20 / 17 | 18 / 0 / 32 |
| First valid warp prediction, application s | 1.624 | 1.681 | 0.782 |
| Fresh on-time modeled presentations | 4 | 103 | 25 |
| Repeated display outputs | 206 | 0 | 178 |
| Application time, modeled s | 10.258940 | 10.258406 | 8.125485 |
| Bulk export time, modeled s | 108.567306 | 107.842440 | 43.918069 |
| Full case host time, s | 2,325.801 | 2,303.225 | 2,054.021 |

The 1 GHz case produced 45.5% more VIO poses and completed in 20.8% less modeled application time. It processed five additional camera pairs; although its explicit drop count increased, combined skipped-plus-dropped pairs decreased from 37 to 32. Camera throughput over application time rose from 1.267 to 2.215 pairs/s. The delivered camera sequences differ, so this is a system-level throughput comparison, not identical-work estimator timing.

The finer tick reduced maximum scheduled-wake lateness from about 0.97–0.98 ms to 0.12 ms. The nominal 1 ms warp wait reached 2.032 ms in the baseline, 1.102 ms with 10 kHz ticks, and 2.014 ms at modeled 1 GHz. This supports timeout granularity as the main deadline limitation in these runs. There was no material application-duration change from finer ticks; interrupt overhead itself was not separately instrumented.

The conclusion also holds over the common first 2.5 seconds of sensor replay: baseline render/warp misses were 210/294 and 257/299; finer ticks yielded 0/300 and 0/299; modeled 1 GHz yielded 200/300 and 235/299. Both new cases had zero skipped render opportunities, versus six in the baseline.

All cases delivered all 501 IMUs to both consumers and accounted for all 50 camera pairs. VIO IMU queue high-water marks were 440 / 440 / 410; the camera queue reached eight in each case. All six workers processed/published on all four harts. Native VIO replay matched every pose exactly in reported position and orientation. Independent prediction/transform checks passed for 209 valid predictions and 1,230 warps in the tick experiment, and 425 valid predictions and 973 warps in the frequency experiment. The 1 mm / 0.001 rad tolerances were unchanged.

The runs finished below the 100-billion-cycle limit: 59,127,160,002 cycles for finer ticks and 52,124,570,002 for modeled 1 GHz. Total simulator and host times are dominated by trace export; they should not be interpreted as estimator execution time. The faster-frequency case used more application cycles (8.125 billion versus 5.129 billion) while processing more input, but fewer trace-export cycles because the application generated fewer display intervals.

145 native Python tests passed. Supplemental prediction and asynchronous-stage audits passed. All 42 protected prior artifacts remain unchanged, all four new firmware builds preserve the baseline application C/C++ sources, and Zephyr remains clean. The FPGA is idle and the runtime lock is released. [Preservation audit](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/provenance/final-preservation-audit.json) · [Prediction audit](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/provenance/prediction-audit.json) · [Independence audit](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/provenance/independence-audit.json)

## Method

Every workload uses 50 stereo pairs and 501 IMUs, nominal 120 Hz display boundaries, 6.944445 ms rendering, 1 ms timewarp, and 1 ms render offset/timewarp margin. Each new operating point requires a matching four-hart platform preflight before its workload. Native replay checks the actual delivered estimator input sequence; prediction and transform traces use the existing independent reference.

Report both absolute deadline-miss counts and percentages: run lengths and camera delivery may differ when the CPU has more cycles per application second. A second comparison restricts targets to the same first 2.5 seconds of sensor replay. Warp deadlines use each warp's own prediction target, not the saved render frame's deadline. Missed completion deadlines, skipped work opportunities, and repeated display output are separate measures.

Application time excludes bulk console export. Host runtime and total simulator cycles include deployment/export as recorded by the runner. The 24-hour host watchdog, 100-billion-cycle limit, existing hardware checks, and resource limits remain in effect.

## Reproduction and orchestration

The preserved `control/build.py` builds both configurations under the resource guard with eight workers and lower scheduling priority. Complete all builds before running FireSim: guard cleanup owns processes beneath its scratch root. `scripts/run_clock_experiments.py` checks build completion, artifact hashes, explicit clock-model agreement, board availability, and the shared runtime lock.

The initial 10 kHz preflight was interrupted before startup when a concurrent build's successful cleanup selected the FireSim process in the shared scratch tree. It remains incomplete. After all builds finished and the board was verified idle, explicit attempt 2 ran without build overlap. No hard resource threshold fired and no limits were relaxed. [Preserved diagnosis](/scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z/provenance/interrupted-attempt.json).

Refresh reports without executing hardware:

```sh
python3 scripts/run_clock_experiments.py \
  --work /scratch/prashanth_illixr_firesim_20260926/clock-experiments-20260927T040525Z \
  --attempt 2
```

One workload per operating point is a functional experiment, not a statistical characterization of scheduler variability. Native agreement establishes runtime equivalence; it does not resolve the previously observed physical trajectory drift.
