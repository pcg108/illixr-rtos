# Spike validation and exact-input native replay

This harness builds the port's existing `SLAMMath.cpp` directly and copies the exact
`create_vio_config()` function from the plugin into the **build directory**. Neither
math nor calibration is edited. `estimator_sources.json` records both math-source
hashes and the extracted calibration hash. The host must have OpenCV **4.5.4** and
Eigen 3.4. A scalar, single-threaded native build reduces backend differences;
numerical discrepancies are reported, never corrected by tuning.

```sh
cmake -S tests/native -B /home/prashanth/illixr-spike-validation/native -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /home/prashanth/illixr-spike-validation/native -j2
python3 tests/native/test_analysis.py
```

Native-only smoke (50 stereo pairs, every camera, IMUs through camera coverage):

```sh
/home/prashanth/illixr-spike-validation/native/estimator_replay \
  --dataset /home/prashanth/illixr-headless-reference/data/mav0 \
  --frames 50 --output /home/prashanth/illixr-spike-validation/native-50.log
```

The functional comparison must use the **actual estimator call sequence** from a
Spike run, not a presumed clock or lockstep order. Indices are zero-based rows in
the embedded data / EuRoC CSV. IMUs retain every original row through the selected
end timestamp. Camera gaps are allowed and represent intentionally skipped or
dropped frames. Original integer nanoseconds are converted to estimator seconds
in precisely the same way as the plugin. Float position and normalized float
quaternion publication also match the plugin.

```sh
python3 scripts/run_spike.py \
  --elf /path/to/single-200/zephyr/zephyr.elf --harts 1 \
  --output /home/prashanth/illixr-spike-validation/runs/single-200-1 \
  --native /home/prashanth/illixr-spike-validation/native/estimator_replay \
  --require-initialized --require-async
```

Use `--harts 2` with the dual-hart ELF. The command selects 256 MiB at `0x80000000`,
RV64IMAFDC+Zicsr+Zifencei, and default simulated CLINT time. It never requests real
wall-clock CLINT. The 3,600-second host watchdog marks a timeout incomplete and
terminates only its own simulator process group. A final target record is
necessary but insufficient: the simulator must also terminate normally via HTIF.
Output directories cannot overwrite an existing `run.json`.

Each output directory preserves `console.log`, `run.json` (exact command, ELF and
Spike SHA-256, source revision/diff hash, host duration), available generated
configuration/device tree/dataset manifest, `native.log`, native source hashes,
and `analysis.json`. Native comparison checks equal input order, equal pose indices,
finite poses, quaternion norm within 1e-5, and flags translation above 1 mm or
rotation above 0.001 rad. Quaternion sign is irrelevant. Differently scheduled
asynchronous runs are not expected to yield identical trajectories.

A matrix is a JSON array accepted by `--matrix cases.json --output RUN_ROOT`:

```json
[
  {"name":"single-50", "elf":"/path/single-50/zephyr/zephyr.elf", "harts":1},
  {"name":"dual-50", "elf":"/path/dual-50/zephyr/zephyr.elf", "harts":2},
  {"name":"single-200", "elf":"/path/single-200/zephyr/zephyr.elf", "harts":1,
   "repeat":3, "require_initialized":true, "require_async":true},
  {"name":"dual-200", "elf":"/path/dual-200/zephyr/zephyr.elf", "harts":2,
   "repeat":3, "require_initialized":true, "require_async":true},
  {"name":"dual-delay", "elf":"/path/dual-delay/zephyr/zephyr.elf", "harts":2,
   "require_initialized":true, "require_async":true, "require_delay":true},
  {"name":"dual-camera-overflow", "elf":"/path/camera-overflow/zephyr/zephyr.elf", "harts":2,
   "require_camera_drop":true},
  {"name":"dual-imu-overflow", "elf":"/path/imu-overflow/zephyr/zephyr.elf", "harts":2,
   "expect_failure":"imu_overflow"}
]
```

Both harts' dedicated clock/publication startup checks run inside the target.
The ordinary plugin schedule remains unpinned. The consumer probe must show the
same VIO sequence across multiple reads while the IMU-derived sequence advances.
`--require-async` checks actual recorded probes rather than merely a summary claim.
Overload cases use separately built queue/delay configurations. An expected IMU
failure passes only when the target explicitly reports failure and a counted IMU
overflow; an unqualified crash or missing final result cannot pass. The explicit IMU-overflow
case must terminate with HTIF exit status 1 and `imu_queue_overflow` as its diagnostic.
Other successful cases require exit status 0.

## Console record contract

Records can follow a console prefix but must each end in a newline. Trace/probe
buffers are flushed after processing so serial output cannot pace the estimator.

```text
ILLIXR_TRACE IMU index [timestamp_ns]
ILLIXR_TRACE CAM index [timestamp_ns]
ILLIXR_POSE camera_index timestamp_ns x y z qw qx qy qz
ILLIXR_PROBE runtime_ns vio_sequence vio_timestamp_ns propagated_sequence propagated_timestamp_ns hart
ILLIXR_RESULT {"status":"pass", ...}
```

Pose values use 17 significant digits. Required successful-run summary fields:
`status`, `imu_overflow`, `trace_overflow`, `online_harts`, `imu_published`,
`imu_processed`, `cam_published`, and `cam_processed`. Extended tests also require
`initialized`. `origin_ns` enables pose-age calculation; `imu_integrator_processed`
and `clock_test_passed`, when present, are verified. Other counters are retained
verbatim in the analysis. Any IMU loss, truncated trace, backwards timestamp,
invalid quaternion, unmet expected overload, or native disagreement fails.

## Native baseline finding

An initial all-camera, 200-pair native replay of the unchanged estimator produced
198 initialized poses, all finite and normalized, but it did **not** produce an
accurate trajectory. Across exactly matching ground-truth timestamps at camera
indices 20 through 199 (8.95 seconds), estimated endpoint displacement was
35.0904 m versus 1.28081 m in EuRoC ground truth. This comparison uses displacement
magnitudes, which are independent of initial translation and rotation; it does
not claim a full trajectory alignment or benchmark result.

Evidence: `/home/prashanth/illixr-spike-validation/native-200-sanity.json`, with
exact timestamp bounds, source hashes, ground-truth row indices, and log hash.
The runner reports the same motion comparison for actual asynchronous Spike
input sequences. Runtime correctness and native numerical agreement can pass
while this preexisting estimator-accuracy problem remains unresolved. No math,
calibration, or estimator parameter was changed to conceal it.

A second diagnostic reproduced the original repository's ten-IMU-window input
ordering, extending its original 50-frame stop limit to 200 solely for this
native comparison. It yielded **36.1537 m** estimated endpoint displacement over
the same camera-20-to-199 timestamps, versus the same **1.28081 m** ground truth.
Initialization occurred at camera 1 (199 published poses); the coverage-based
ordering initialized at camera 2 (198 poses). Both runs remained finite with
normalized quaternions. This confirms that substantial drift also occurs with
the original input ordering; it does not identify its mathematical cause.

`/home/prashanth/illixr-spike-validation/native-legacy200-sanity.json` includes
checksums proving estimator source and calibration equality to git HEAD,
original scheduling provenance, ground-truth indices/timestamps, and the native
result hash. Input order is recorded in `native-legacy200-input.log`; reproduce
with `estimator_replay --dataset MAV0 --trace native-legacy200-input.log
--output native-legacy200.log`.

The analyzer reads `ILLIXR_PROPAGATION` JSON separately from VIO pose records.
Its `max_observed_position_norm_m` is an integrator diagnostic, not a claim of
physical accuracy. Finite magnitudes are reported without an arbitrary radius
pass/fail threshold. A successful run also accounts for the complete embedded
dataset whenever `dataset_manifest.json` is beside the log: published + skipped
+ dropped camera pairs must equal the embedded total, and every embedded IMU must
have been published. Deliberate overflow failures are exempt from full-dataset
completion because they terminate early by design.
