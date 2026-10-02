# Current Rocket/FireSim baseline

The default is **10 kHz Zephyr ticks (100 µs), a modeled 1 GHz CPU, and a 1 MHz declared timer**. Quad-core scheduler-managed execution passed the combined-setting validation below.

Subsequent [batched trace export](batched-traces.md) retains these clock defaults
and cuts the validated FireSim host case from 33.3 to 8.3 minutes. The table below
preserves the original clock-experiment results and character-console export.

The accepted U250 bitstream and generated 500 MHz / 500 kHz hardware description remain unchanged. Software scales both declarations by two, preserving the measured 1000:1 CPU/mtime ratio. FASED latencies stay fixed in cycles. This is a modeled operating point, not physical 1 GHz timing closure. Spike retains its own 10 MHz timer; its tick default is also 10 kHz.

| Case | VIO poses | Render misses / completions | Warp misses / completions | Fresh on-time presentations | Application s |
| --- | ---: | ---: | ---: | ---: | ---: |
| 500 MHz / 1 kHz ticks | 11 | 833 / 1223 | 984 / 1229 | 4 | 10.258940 |
| 500 MHz / 10 kHz ticks | 11 | 0 / 1230 | 0 / 1230 | 103 | 10.258406 |
| 1000 MHz / 1 kHz ticks | 16 | 649 / 973 | 696 / 973 | 25 | 8.125485 |
| 1000 MHz / 10 kHz ticks | 16 | 0 / 958 | 0 / 958 | 209 | 7.991723 |

Combined run: 501 IMUs reached VIO and 501 reached the integrator. Camera accounting: 18 processed, 0 skipped, 32 dropped. Native replay matched 16 VIO poses; independent prediction/transform comparisons also passed the existing 1 mm / 0.001 rad tolerances. Startup exercised all four harts, and HTIF exited normally with complete traces.

Presentation: 958 new outputs, 0 repeated outputs, and 1 empty startup slot. Of those outputs, 209 used fresh predictions; the bounded dataset still leads to stale predictions during estimator drain. Trace export took 42.831100 modeled seconds, separate from application execution. Full case host time was 1998.735 seconds, including deployment/export. Total target cycles were 50,903,900,002, below the unchanged 100-billion-cycle limit.

| Plugin | Observed hart mask |
| --- | ---: |
| offline_imu | 0xf |
| offline_cam | 0xf |
| openvins | 0xf |
| imu_integrator | 0xf |
| render_loop | 0xf |
| timewarp | 0xf |

147 native Python tests passed. Application C/C++, estimator initialization/math, prediction, dataset, queues, display schedule, and GPU delays are unchanged. Prior artifacts were hash-checked and preserved; Zephyr is unmodified. These checks establish runtime equivalence and scheduling behavior, not improved physical trajectory accuracy.

## Defaults and reproduction

- `prj.conf`: `CONFIG_SYS_CLOCK_TICKS_PER_SEC=10000`.
- `scripts/build_rocket.sh`: `ILLIXR_MODELED_CLOCK_SCALE` defaults to `2`, selecting `config/rocket_1ghz.conf` and a 1 GHz CPU declaration.
- `ILLIXR_MODELED_CLOCK_SCALE=1` selects the original 500 MHz / 500 kHz model. A separate configuration override restores 1,000 ticks/s for historical reproduction.
- Build the matching preflight and GPU workload with `profiles/gpu_pipeline.yaml`; complete all resource-guard cleanup before starting FPGA execution.
- FireSim and Verilator runners validate recorded firmware clock settings against compiled settings. Historical firmware retains its own clock interpretation.

[Combined report](/scratch/prashanth_illixr_firesim_20260926/baseline-1ghz-10khz-20260927T054345Z/results/clock-comparison.md) · [Analysis](/scratch/prashanth_illixr_firesim_20260926/baseline-1ghz-10khz-20260927T054345Z/results/baseline-1ghz-10khz-20260927T054345Z-combined-workload-1/analysis.json) · [Build commands](/scratch/prashanth_illixr_firesim_20260926/baseline-1ghz-10khz-20260927T054345Z/control/build.py) · [Preservation audit](/scratch/prashanth_illixr_firesim_20260926/baseline-1ghz-10khz-20260927T054345Z/provenance/final-preservation-audit.json)

Refresh the combined report without rerunning hardware:

```sh
python3 scripts/run_clock_experiments.py --work /scratch/prashanth_illixr_firesim_20260926/baseline-1ghz-10khz-20260927T054345Z --combined
```
