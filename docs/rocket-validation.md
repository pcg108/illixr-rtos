# Rocket RTL validation

This workflow runs the Zephyr ILLIXR image in Chipyard's Verilator models of
`RocketConfig`, `DualRocketConfig`, and `QuadRocketConfig`. It uses the same
embedded 50 stereo pairs and 501 IMU samples as the Spike smoke workload.
Estimator equations, calibration, queue capacities, and replay policy are unchanged.
The existing Spike artifacts remain under `/home/prashanth/illixr-rtos-work`.

This is an execution and portability check. Matching the native build of this
port's estimator does not establish agreement with desktop OpenVINS or physical
trajectory accuracy; the previously observed drift remains a separate diagnostic.

## Hardware and firmware

Task artifacts live under `/home/prashanth/illixr-rocket-work`. The simulator build
produces `simulators/{RocketConfig,DualRocketConfig,QuadRocketConfig}-platform.json`
and an immutable simulator binary for each configuration. The manifest records
hardware DTS and simulator hashes, hart IDs, ISA, memory, interrupts, and clock
frequencies derived from generated hardware.

These standard configurations have 256 MiB RAM at `0x80000000`, a 500 kHz CLINT
timer, and 500 MHz Rocket core clocks. The generated CPU DTS may contain a zero
placeholder clock frequency; the hardware inspection resolves the core clock from
the generated RTL clock source. The runtime checks measured timer/core progression
within 5%, in addition to checking the declared frequency and all configured harts.

Use the matching Zephyr firmware, not the old Spike ELF with its 10 MHz timer.
From the repository root, build the three preflight images after their hardware
manifests exist:

```sh
ILLIXR_PLATFORM_CHECK_ONLY=1 scripts/build_rocket.sh single scheduler
ILLIXR_PLATFORM_CHECK_ONLY=1 scripts/build_rocket.sh dual scheduler
ILLIXR_PLATFORM_CHECK_ONLY=1 scripts/build_rocket.sh quad scheduler
```

A preflight starts every configured hart, tests shared publication and byte-atomic
operations, checks monotonic clocks, measures at least 10 ms of timer/core
progression, and exits before plugin replay. The clock worker count is
`max(2, configured_harts)` and each worker performs 1,024 atomic exchanges. Required
hart masks are `0x1`, `0x3`, and `0xF`.

Run each preflight sequentially, substituting the configuration and hart count:

```sh
python3 scripts/run_rocket.py \
  --elf /home/prashanth/illixr-rocket-work/artifacts/rocket-single-preflight/zephyr.elf \
  --hardware-manifest /home/prashanth/illixr-rocket-work/simulators/RocketConfig-platform.json \
  --harts 1 --platform-check \
  --output /home/prashanth/illixr-rocket-work/results/single-preflight
```

Build the five workload variants:

```sh
scripts/build_rocket.sh single scheduler
scripts/build_rocket.sh dual scheduler
scripts/build_rocket.sh dual pinned
scripts/build_rocket.sh quad scheduler
scripts/build_rocket.sh quad pinned
```

The builder writes immutable artifacts named
`artifacts/rocket-{single,dual,quad}-{scheduler,pinned}-50/`. Each includes the ELF,
Zephyr configuration/DTS, dataset manifest, hardware description, memory report,
and source/dependency fingerprints.

## Worker placement and evidence

In scheduler-managed mode all plugin workers retain an unrestricted CPU mask.
In pinned mode workers are created suspended, assigned their CPU mask, then
started with the following mapping:

| Plugin | Single baseline | Dual pinned | Quad pinned |
|---|---:|---:|---:|
| `offline_imu` | 0 | 0 | 0 |
| `offline_cam` | 0 | 0 | 1 |
| `openvins` | 0 | 1 | 2 |
| `imu_integrator` | 0 | 0 | 3 |

The consumer probe remains scheduler-managed. The single baseline needs no
explicit affinity because only hart 0 exists.

`ILLIXR_PLACEMENT` JSON records provide each plugin's requested hart (`-1` for
unrestricted), observed hart mask, and per-hart `work_counts` and
`publication_counts`. Hart observations occur during actual work, not only at
thread startup:

- IMU producer work counts successful fanouts, once per sample; publication counts
  the same fanouts to the two independent consumer queues.
- Camera work counts decoded selected pairs, including an attempted enqueue that
  drops because its queue is full; publication counts successful enqueues.
- VIO work counts estimator IMU and stereo calls; publication counts each
  initialized pose/baseline pair.
- Integrator work counts newly consumed IMUs, excluding repeated history replay;
  publication counts propagated poses.

The analyzer checks those totals against the input/output trace and final
counters. Pinned execution must remain on its assigned hart and collectively
cover all assigned harts. Scheduler-managed execution reports the observed
placement; online-hart count alone is not evidence that plugins used every core.

## Run the workload matrix

Run the following five cases **sequentially**. The builder calls unrestricted
placement `scheduler`; the runner's corresponding flag is `--placement unpinned`.

| Firmware artifact directory | Hardware manifest | Harts | Runner placement |
|---|---|---:|---|
| `rocket-single-scheduler-50` | `RocketConfig-platform.json` | 1 | `unpinned` |
| `rocket-dual-scheduler-50` | `DualRocketConfig-platform.json` | 2 | `unpinned` |
| `rocket-dual-pinned-50` | `DualRocketConfig-platform.json` | 2 | `pinned` |
| `rocket-quad-scheduler-50` | `QuadRocketConfig-platform.json` | 4 | `unpinned` |
| `rocket-quad-pinned-50` | `QuadRocketConfig-platform.json` | 4 | `pinned` |

For example:

```sh
python3 scripts/run_rocket.py \
  --elf /home/prashanth/illixr-rocket-work/artifacts/rocket-quad-pinned-50/zephyr.elf \
  --hardware-manifest /home/prashanth/illixr-rocket-work/simulators/QuadRocketConfig-platform.json \
  --harts 4 --placement pinned \
  --output /home/prashanth/illixr-rocket-work/results/quad-pinned-50
```

The runner invokes the simulator directly using the same flags as Chipyard's
`run-binary-fast` with `LOADMEM=1`: `+dramsim`, `+dramsim_ini_dir`, `+max-cycles`,
and `+loadmem=<ELF>`, enclosed by the standard permissive flags. It emits no
instruction trace or waveform and runs with its working directory set to the
result directory so incidental DRAMSim files stay with the case.
The runner also raises the child simulator's host stack allowance to 64 MiB,
subject to the host hard limit; this does not change Zephyr thread stacks.

Defaults are a **24-hour host watchdog** and **100 billion simulation cycles**.
The latter counts TestDriver cycles, not retired instructions or necessarily
Rocket core cycles. The original simulator's TestDriver runs at 1 GHz while
Rocket cores run at 500 MHz. The optimized simulator uses a 500 MHz TestDriver;
the independent DUT clock sources remain unchanged. The platform manifest records
both frequencies, so the cycle limit corresponds to 100 or 200 simulated seconds,
respectively. Host time and simulated time are reported separately.
A timeout or interruption is incomplete and cannot pass. Existing result
directories are never overwritten; use a fresh directory for a rerun.

### Host simulation performance

The original dual-core model measured approximately 3,337 Rocket cycles per host
second over a deliberately limited 100 microsecond simulation. An optimized model
measured approximately 18,253 cycles per second over the same interval. These are
short boot benchmarks, not full workload results. Their deliberate cycle-limit
exits are retained as incomplete executions and never count as functional passes.

The optimized build retains assertions, hardware clocks, generated RTL, and
firmware. It uses four host simulator threads, serializes DPI callbacks, enables
native host compilation and link-time optimization, and increases generated
function sizes. Its 2 ns TestDriver reference period removes extra evaluations
of an input clock unused by the generated harness. Host simulator threads do not
establish RTOS plugin placement; that still requires the firmware work counters.

Build into a separate directory to preserve the original models:

```sh
ILLIXR_SIM_OUTPUT_DIR=/home/prashanth/illixr-rocket-work/simulators-optimized-wide \
ILLIXR_SIM_THREADS=4 ILLIXR_TESTDRIVER_PERIOD_NS=2.0 \
ILLIXR_SIM_NATIVE_OPTIMIZATION=1 ILLIXR_SIM_SPLIT_CFUNCS=2000 \
scripts/build_rocket_sim.sh DualRocketConfig
```

Repeat sequentially for the other configurations. Each matrix case selects its
simulator through `hardware_manifest`; preflights use the same manifest as their
workload variants. The original matrix is retained as `matrix-standard.json` and
the selected optimized matrix is `matrix.json` in the task work directory.

## Sequential matrix and comparison report

The prepared `/home/prashanth/illixr-rocket-work/matrix.json` lists the five
workloads and their immutable artifact paths. The matrix runner derives the
single-, dual-, and quad-core preflight cases, runs those first, and then runs
eligible workloads sequentially:

```sh
python3 scripts/run_rocket_matrix.py
```

It recognizes an existing `results/preflight-dual-1` result. A case is reused only
when its latest attempt completed successfully and its firmware, simulator, and
hardware-manifest hashes still match. A failed or stale attempt is retained; the
next run receives a fresh numbered sibling directory. A running attempt blocks
matrix launch so a separate preflight and the matrix cannot accidentally execute
models concurrently. A failed preflight blocks the workloads for that hardware
configuration. An interrupted matrix stops before launching another case.

Each case retains the same 24-hour watchdog and 100-billion-simulation-cycle
limits as the standalone runner. Workload entries can explicitly record those
values using `timeout_seconds` and `max_cycles`. Use `--matrix` and `--work` to
select another prepared matrix/work directory.

The runner writes `results/summary.json` and `results/comparison.md` from actual
saved metadata and analysis. The comparison includes case status, placement,
sensor counts, native differences, queue occupancy, pose age, missed deadlines,
simulated/host elapsed time, and physical motion diagnostics when available.
Unknown values remain blank; running or incomplete cases are never summarized as
passes. Refresh the report at any time without starting a model:

```sh
python3 scripts/run_rocket_matrix.py --report-only
```

The matrix resume and platform-gating rules have a separate tooling test:

```sh
python3 tests/native/test_rocket_matrix.py
```

## Acceptance and retained evidence

Every full run requires a normal simulator exit, one successful final firmware
result, complete trace, initialized VIO output, all 501 IMUs consumed by both
workers, and all 50 camera pairs accounted as processed, skipped, or dropped.
It checks finite poses, normalized quaternions, timestamp monotonicity, probe
progress while VIO is unchanged, startup/platform clocks, and actual work
placement. The native harness replays the exact estimator event sequence with
limits of 1 mm positional difference and 0.001 rad orientation difference.

Each case saves `console.log`, `run.json`, `analysis.json`, native replay and
console output, firmware/hardware descriptions and fingerprints, and the dataset
manifest. The final firmware `runtime_ns` reports simulated elapsed time before
trace printing; legacy Spike traces fall back to their last probe timestamp.
Final counters retain camera throughput, queue high-water marks, and missed probe
deadlines. Analysis includes maximum VIO age, observed plugin harts, trajectory
motion versus ground truth, and propagated-pose magnitudes. No physical accuracy
threshold is silently replaced by native equivalence.

The runner and evidence checks can be exercised without a simulator build:

```sh
python3 tests/native/test_analysis.py
```

These tests cover incomplete/fatal process exits, four-hart clock evidence,
frequency mismatch, missing/malformed placement, wrong affinity, inconsistent
work/publication counts, and all existing Spike trace checks. They validate the
test tooling; only real model runs establish Rocket execution results.
