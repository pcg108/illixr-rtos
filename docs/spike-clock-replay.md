# Clock-paced replay on Spike

> Historical first clock-paced implementation. Its 1 kHz tick and pointer-queue
> descriptions below predate the current 10 kHz ticks and value-based IMU
> transport. Use the [primary README](../README.md),
> [current clock settings](current-baseline.md), and
> [IMU transport](imu-value-transport.md) for new builds. Spike still has its
> own 10 MHz timer; the Rocket/FireSim factor-two model is platform-specific.

The active `imu` profile runs independently scheduled IMU and camera producers,
VIO, and IMU integration. A 120 Hz consumer probe reads current pose snapshots;
this configuration does not include rendering or a new pose-prediction algorithm.
The target reads its embedded data from the ELF and needs no filesystem or network.

## Clock and data flow

`RelativeClock` uses Zephyr's 64-bit cycle counter and configured conversion rate.
The Spike board maps that counter to the shared machine timer, `mtime`. Both board
overlays declare a 10 MHz timer, 256 MiB RAM, and the appropriate CPU/interrupt
nodes. Kernel wakeups use 1 kHz ticks, so storing nanoseconds does not imply
nanosecond scheduling precision.

After every worker completes setup, the runtime records one timer epoch and
releases a one-time startup barrier. The dataset origin is the earliest included
sensor timestamp:

```text
dataset_now_ns = dataset_origin_ns + elapsed_Spike_timer_ns
```

Measurement timestamps keep their original integer nanoseconds. They are not
replaced with delivery times; conversion to estimator seconds stays unchanged.
Default Spike CLINT time advances with simulator execution. The runner does not
pass `--real-time-clint`: simulated elapsed time and host watchdog time are distinct.
These tests do not measure cycle-accurate Chipyard hardware performance.

| Worker / consumer | Scheduling and transport |
| --- | --- |
| IMU producer, priority 3 | Absolute sample deadlines; publish every sample in order, catching up when late. |
| Camera producer, priority 5 | Select the latest pair at or before dataset time; skip superseded pairs; attempt each selected index once. Decode on this worker. |
| VIO, priority 5 | FIFO cameras and IMUs. Consume IMUs until the camera timestamp is covered, then call the existing estimator. |
| Integrator, priority 5 | Independent IMU queue. Read the latest VIO baseline and replay retained IMUs on its own worker. |
| Consumer probe | Absolute 120 Hz deadlines; read available immutable snapshots without requiring a fresh VIO result. |

Zephyr's smaller numerical priority runs first. Camera, VIO, and integration
share priority 5 with a 1 ms timeslice so a late decoder cannot starve downstream
processing. Normal workers are unpinned; a
separate startup check pins test workers to each requested hart to validate the
shared timer and atomic publication. OpenCV's internal parallel execution is
disabled; the application workers supply concurrency.

Each IMU subscriber has a 4,096-entry pointer queue. An IMU overflow invalidates
the run and triggers shutdown. Camera FIFO capacity is eight: a full queue drops
the newly arriving pair, counts the drop, and releases its image references.
Camera skips and queue drops are separate counters. Consumers own successfully
enqueued messages; producers own and free failed publications.

VIO and propagated poses use mutex-protected latest-value snapshots with validity
and sequence numbers. Publication never mutates another worker's estimator state.
The integrator retains 4,096 IMUs, rejects old baselines, and fails when a new
baseline requires history already discarded. Publication timestamps cannot rewind.

Producer completion uses atomic flags, not queue sentinels. VIO drains remaining
IMUs after the final camera; integration consumes all IMUs and the final baseline.
The runtime joins every worker before final queue reclamation and trace output.
Failures also release workers and produce an explicit final result where execution
can complete normally. Console output is buffered during sensor processing.

## Build and run

From the repository root, fetch isolated pinned dependencies and build the four
normal configurations:

```sh
export ILLIXR_RTOS_WORK=/home/prashanth/illixr-rtos-work
export CHIPYARD_DIR=/home/prashanth/chipyard
export ILLIXR_DATASET_DIR=/home/prashanth/illixr-headless-reference/data/mav0
scripts/setup_spike_deps.sh
scripts/build_spike.sh single 50
scripts/build_spike.sh dual 50
scripts/build_spike.sh single 200
scripts/build_spike.sh dual 200
```

The dependency pins are Chipyard's Zephyr revision
`cd45a528d3bf81f9c7aa2a63f9fe512eee0881b0`, OpenCV 4.5.4, and the matching
Zephyr-manifest Eigen revision. Setup also downloads the SHA-256-verified Zephyr
SDK 0.17.0 minimal package and its RISC-V toolchain. The isolated SDK supplies a
matched GCC 12.2, libstdc++, and Newlib with real retargetable locking; it is not
installed globally. Chipyard supplies the Python/build utilities and Spike.
Dependencies, generated data, and builds live under `ILLIXR_RTOS_WORK`; existing
Chipyard work is not modified. Set
`ILLIXR_BUILD_JOBS` to control build concurrency. The target uses floating-point
context sharing and disables the board's vector extension. The emitted ISA is
`rv64imafdc_zicsr_zifencei`, with `lp64d` ABI and `medany` code model. The SDK's
selected libc/libstdc++ multilib has matching attributes. A single byte-exchange
fallback uses a sequentially consistent word LR/SC loop and preserves adjacent
bytes; the cross-hart startup test exercises this fallback.

The generator validates matching stereo timestamps and increasing sensor times,
embeds the first 50 or 200 stereo pairs, and retains initial IMUs through the first
sample at or beyond 50 ms after the final camera. It writes metadata and input
SHA-256 hashes alongside generated headers in each build directory. The 50-pair
subset contains 501 IMUs and 34.5 MiB of compressed images. Memory is bounded by
the selected embedded prefix, configured queues/history, trace capacity, static
stacks, and a 64 MiB allocation arena. Inspect linker RAM usage and the saved
`size.txt` for each build; image compression size alone is not the RAM requirement.

Build the native comparison harness using the host OpenCV 4.5.4 and Eigen:

```sh
cmake -S tests/native -B /home/prashanth/illixr-spike-validation/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/prashanth/illixr-spike-validation/native -j2
python3 scripts/run_spike.py \
  --elf "$ILLIXR_RTOS_WORK/artifacts/single-200/zephyr.elf" --harts 1 \
  --output "$ILLIXR_RTOS_WORK/results-repeat/single-200-1" \
  --native /home/prashanth/illixr-spike-validation/native/estimator_replay \
  --require-initialized --require-async
```

Use the dual build with `--harts 2`. Run both 50-pair smoke cases and repeat both
200-pair cases three times in distinct output directories. The runner also accepts
`--matrix`; its case schema and native input contract are documented in
[the validation guide](../tests/native/README.md). Its default host watchdog is
3,600 seconds; a timeout means incomplete validation, never success.

Use distinct artifact names for fault scenarios so their snapshots cannot overwrite
normal results. These examples also use separate build caches:

```sh
ILLIXR_BUILD_DIR="$ILLIXR_RTOS_WORK/build-dual-50-delay" \
ILLIXR_ARTIFACT_NAME=dual-50-delay scripts/build_spike.sh dual 50 \
  -DILLIXR_VIO_DELAY_MS=250
ILLIXR_BUILD_DIR="$ILLIXR_RTOS_WORK/build-dual-50-camera-overflow" \
ILLIXR_ARTIFACT_NAME=dual-50-camera-overflow scripts/build_spike.sh dual 50 \
  -DILLIXR_CAM_QUEUE_CAPACITY=2 -DILLIXR_VIO_DELAY_MS=1000
ILLIXR_BUILD_DIR="$ILLIXR_RTOS_WORK/build-dual-50-imu-overflow" \
ILLIXR_ARTIFACT_NAME=dual-50-imu-overflow scripts/build_spike.sh dual 50 \
  -DILLIXR_IMU_QUEUE_CAPACITY=2 -DILLIXR_VIO_DELAY_MS=1000
```

Each command saves its ELF under `$ILLIXR_RTOS_WORK/artifacts/$ILLIXR_ARTIFACT_NAME/`.
Repeat the scenarios with `single` and corresponding `single-50-*` names. The build
script resets fault settings to normal defaults on each invocation before applying
explicit `-D` arguments; a separate `ILLIXR_BUILD_DIR` alone does not change the
artifact name.

The injected delay occurs once after one camera has been processed; change
`ILLIXR_VIO_DELAY_AFTER_CAM` if the test needs another point. Run the first with
`--require-delay`, the second with `--require-camera-drop`, and the third with
`--expect-failure imu_overflow`. A counted expected failure must still shut down
and emit a final result. Which scheduling conditions cause drops is established
by the recorded test, not assumed from queue capacity alone.

## Numerical boundaries and evidence

The estimator source and hardcoded EuRoC calibration are unchanged. Initial
implementation audit against repository `HEAD` confirmed byte-for-byte equality:

| Input | SHA-256 |
| --- | --- |
| `SLAMMath.cpp` | `5c194abe7388d208a8debd0ae8ee381dcb3b1ee4cab8d04bafab8f9d38b2a8c8` |
| `SLAMMath.hpp` | `a722c0f131554c8fa9d1d66da8f9ed8f337d23718b28421f98c4d031fc37ca48` |
| Exact `create_vio_config()` function text | `f791716e50eb38307a1852e4600ee948725e59b0faa2c5652be51066afcf0185` |

The native harness compiles those same sources and extracts that same calibration
function. Each target trace records the actual estimator call order and dataset
indices; native replay uses this sequence, including skipped cameras. It checks
finite poses, quaternion normalization, timestamp order, and discrepancies over
1 mm or 0.001 radians. Separately scheduled runs need not produce the same camera
sequence or trajectory.

The integrator's existing equations are also retained. Its gravity subtraction
has a known sign discrepancy relative to the estimator's gravity convention;
finite, normalized propagated poses alone do not establish physical correctness.
Tests report behavior and discrepancies rather than changing math or tuning
calibration. Ground-truth trajectory accuracy requires separate evaluation.

Build fingerprints are saved in `$ILLIXR_RTOS_WORK/artifacts/`. Completed execution
results are in each run's `console.log`, `run.json`, and `analysis.json` under
`/home/prashanth/illixr-rtos-work/results/`. See [the results report](spike-results.md)
for the 14 executed scenarios, camera overload behavior, native agreement, and
the unresolved physical-accuracy limitations.
