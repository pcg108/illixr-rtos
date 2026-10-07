# HPM FireSim results — 2026-10-06

All eight selected workloads passed on the accepted single- and quad-core
Rocket+Saturn+FP32/INT8 Gemmini HPM images: RVV and FP32 Gemmini OpenBLAS, each
with profiling disabled and enabled. INT8 RITNet eye tracking was active in all
cases. These are existing-environment build/runtime results, not a fresh
README dependency bootstrap.

Every case exited normally, delivered 501 IMUs to both consumers, accounted for
50 camera pairs, produced initialized VIO and correct eye predictions, and passed
trace, prediction/transform and modeled-presentation validation. Native replay
of each actual delivered input sequence had zero position and orientation error.
All cases had zero render/timewarp deadline misses. This does not measure
physical trajectory accuracy or a real GPU/display.

## Measured workloads

Application and export times are modeled target seconds; host time includes FPGA
setup/programming and execution, excluding subsequent host analysis. Application
time excludes export. All cases use the modeled 1 GHz/1 MHz clock interpretation
and 10 kHz Zephyr ticks.

| Cores | BLAS | HPM | App s | Export s | Host s | VIO | Cameras processed/skipped/dropped | Eye | Fresh presentations |
|---:|---|---|---:|---:|---:|---:|---|---:|---:|
| 1 | gemmini_fp32 | off | 7.158431 | 1.215999 | 549.3 | 10 | 11/25/14 | 17 | 131 |
| 1 | gemmini_fp32 | on | 6.533367 | 1.149168 | 539.5 | 10 | 11/24/15 | 17 | 105 |
| 1 | rvv | off | 5.475035 | 0.938398 | 457.6 | 11 | 12/29/9 | 20 | 95 |
| 1 | rvv | on | 5.225041 | 0.916790 | 442.3 | 11 | 12/28/10 | 18 | 99 |
| 4 | gemmini_fp32 | off | 5.466771 | 0.952281 | 551.9 | 15 | 17/0/33 | 43 | 202 |
| 4 | gemmini_fp32 | on | 5.416766 | 0.977400 | 549.3 | 15 | 17/0/33 | 42 | 201 |
| 4 | rvv | off | 5.500027 | 0.952600 | 505.2 | 16 | 18/0/32 | 43 | 204 |
| 4 | rvv | on | 5.666747 | 1.005097 | 524.7 | 15 | 17/0/33 | 41 | 198 |

Profiling-on application-time differences were −4.57% (single RVV), −8.73%
(single Gemmini), +3.03% (quad RVV), and −0.91% (quad Gemmini). These are
single-run workload differences with scheduling and delivered-camera variation;
they do not establish isolated profiling cost or backend speedup.

All nine plugins have validated counters in profiled runs. Quad-core processing
records exercise all four harts; FP32 accelerator dispatch stays on hart 0.
For example, the profiled quad Gemmini run issued 1,002 workload DGEMMs from
callers on all four harts, with accelerator hart mask `0x1`. Their accumulated
packing/unpacking time was 110.476/43.389 ms, accelerator execution 40.625 ms,
and request queue time 128.281 ms. These phase durations are not whole-application
elapsed time. CPU counter attribution and cache-event limitations are described
in [hardware performance counters](hardware-performance-counters.md).

## Validation and provenance

Both hardware images passed routed timing and packaging. Six final firmware
artifacts passed build audits; profiler objects contain no FP/vector instructions.
Single/quad preflights passed 610,297/610,745 BLAS checks and HPM/vector-context
checks with expected hart masks. Focused RTL passed 1,513,588 checks covering all
13 programmable event selectors. Positive ISR accounting and a rejected
missing-callback negative control supplement full-workload acceptance.

The final profiler source SHA-256 is
`abcb46f186869e1d0ca4e2983e4ca773e368dabce2224c449b03c43d606ddca6`.
First-switch attribution now uses scheduler thread state before TLS initialization.
ISR hooks avoid redundant interrupt-mask and restore writes when already masked.
Earlier stalls and diagnostic controls are preserved; the underlying
microarchitectural stall mechanism is not established. No RTL or scheduler
change was made for this workaround.

Full evidence is under:

```text
/scratch/prashanth_illixr_hpm_20261006T011137Z/reports/final-r5/
```

`COMPARISON.md` and `summary.json` contain the consolidated report; `runs.csv`,
`plugins.csv`, `plugin_harts.csv`, `accelerators.csv`, `placement.csv`,
`display_eye.csv`, and `profiling_overhead.csv` contain detailed metrics.
Source/artifact hashes accompany the results. Earlier incomplete and diagnostic
runs are excluded from this eight-case production selection, not deleted.

To regenerate the general report from the same selection, run from the repository
root (use an output directory separate from the archived report):

```bash
results=/scratch/prashanth_illixr_hpm_20261006T011137Z/runtime
python3 scripts/summarize_performance.py \
  "$results"/eye-{single,quad}-{rvv,gemmini}-off \
  "$results"/eye-{single,quad}-{rvv,gemmini}-hpm-r5 \
  --output /tmp/xrsight-hpm-final-report
```

The final campaign acceptance audit additionally resides in
`reports/final-r5/acceptance-evidence.json`; preflight, build, focused-RTL, and
negative-control evidence remain in their original campaign directories.
