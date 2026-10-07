# Optional FireSim validation and recorded runs

The [README manual workflow](../README.md#run-the-built-image) is sufficient to
configure and launch FireSim. Use this guide when you also need hardware/firmware
identity checks, a reusable prebuilt driver bundle, watchdog/execution records,
and the complete native-comparison acceptance report. These helpers are project
validation tools, not additional FireSim prerequisites.

Start with the workspace, selected hardware, elaboration paths, and packaged
firmware from the README. The commands below use those `XRSIGHT_*` variables.
The native tools referenced at the end are built in the README's
[host reference tools](../README.md#build-the-host-reference-tools) section.
This helper workflow creates `case.json` and `execution.json`; the manual
`firesim runworkload` workflow does not create those project-specific files.

## Validate and package the built image

FireSim writes a proposed entry under `deploy/built-hwdb-entries/` and results under `deploy/results-build/`. The local build directory contains `cl_<deploy-quintuplet>/firesim.tar.gz`, `driver/`, and `vivado_proj/`. Locate it under the rendered build directory (for example with `find "$XRSIGHT_HW_WORK/builds" -name firesim.tar.gz`). Select that exact directory and its matching post-route bus-skew report:

```bash
export XRSIGHT_BUILD_OUTPUT=/absolute/path/to/cl_xilinx_alveo_u250-firesim-FireSim-FireSimILLIXRQuadRocketDualGemminiSaturnConfig-BaseXilinxAlveoU250Config
export XRSIGHT_BUS_SKEW_REPORT="$XRSIGHT_BUILD_OUTPUT/vivado_proj/firesim.runs/impl_1/overall_fpga_top_bus_skew_postroute_physopted.rpt"
export XRSIGHT_HARDWARE="$XRSIGHT_HW_WORK/hardware.json"
"$XRSIGHT_PYTHON" "$XRSIGHT_ROOT/scripts/check_firesim_platform.py" \
  --chipyard "$CHIPYARD_DIR" --config "$XRSIGHT_HW" \
  --staging-dir "$XRSIGHT_STAGING" --rtl "$XRSIGHT_RTL" \
  --elaboration-log "$XRSIGHT_HW_WORK/elaboration.log" \
  --build-output "$XRSIGHT_BUILD_OUTPUT" --bus-skew-report "$XRSIGHT_BUS_SKEW_REPORT" \
  --firmware "$XRSIGHT_FIRMWARE" --output "$XRSIGHT_HARDWARE"
python3 "$XRSIGHT_ROOT/scripts/setup_firesim.py" package-driver \
  --chipyard "$CHIPYARD_DIR" --hardware "$XRSIGHT_HARDWARE" \
  --output-dir "$XRSIGHT_HW_WORK/packages"
```

The checker requires final routed timing closure and complete routing; synthesis success or manager exit alone is insufficient. It also hashes the bitstream archive, driver, headers, boot ROM, and generated descriptions. The driver packager preserves the matching driver, its host shared libraries, and the tested runtime configuration. A FireSim bitstream archive is more than a raw `.bit` file.

`configure --hardware-manifest` then writes the HWDB entry automatically:

```yaml
illixr_u250_rocket_dual_gemmini_saturn_quad:
  bitstream_tar: file:///ABS/firesim.tar.gz
  driver_tar: file:///ABS/driver-bundle.tar.gz
  deploy_quintuplet_override: xilinx_alveo_u250-firesim-FireSim-FireSimILLIXRQuadRocketDualGemminiSaturnConfig-BaseXilinxAlveoU250Config
  custom_runtime_config: illixr-quad-runtime.conf
```

Keep bitstream and driver from the same build. The tested runtime configuration uses:

```text
+mm_readLatency_0=30
+mm_writeLatency_0=30
+mm_readMaxReqs_0=10
+mm_writeMaxReqs_0=10
+mm_useHardwareDefaultRuntimeSettings_0
+fesvr-step-size=10000
+idle-counts=1
+fesvr-wait-ticks=8
```

The value 16 is invalid for the compiled four-bit outstanding-request fields; it previously blocked memory traffic. FASED timing also differs from Verilator's DRAMSim2, so cross-simulator performance comparisons must identify the memory model.

## Prepare and record the run

Use a new run directory and unique workload name for every preflight or workload. First set `XRSIGHT_FIRMWARE` to the packaged **preflight**; repeat the same steps with the full workload only after the preflight passes.

```bash
export XRSIGHT_RUN="$XRSIGHT_WORK/runs/quad-eye-preflight-1"
"$XRSIGHT_PYTHON" "$XRSIGHT_ROOT/scripts/setup_firesim.py" configure \
  --chipyard "$CHIPYARD_DIR" --work "$XRSIGHT_RUN" --config "$XRSIGHT_HW" \
  --elf "$XRSIGHT_FIRMWARE/zephyr.elf" --fpga-db "$XRSIGHT_FPGA_DB" \
  --hardware-manifest "$XRSIGHT_HARDWARE" --workload-name quad-eye-preflight-1
```

This writes `config_runtime.yaml`, `config_hwdb.yaml`, the recipe files, `case.json`, and the bare-metal workload JSON. It installs conflict-checked workload links under FireSim's `deploy/workloads/`. The generated runtime selects:

- Externally provisioned localhost with one U250 and an explicit FPGA database.
- `default_simulation_dir: <run>/runfarm`, one no-network target, and the selected HWDB alias.
- `profile_interval: 1000000` and `+max-cycles=100000000000`.
- Tracing disabled, DRAM zeroing enabled, and synthesized assertions enabled.
- The generated workload JSON, whose `common_bootbinary` is `zephyr.elf`, `common_rootfs` is `null`, and simulation outputs include `uartlog` and `memory_stats0.csv`.

The complete [runtime template](../config/firesim/config_runtime.yaml.in) shows every field. No FireMarshal Linux disk image is involved: FireSim's TSI/loadmem path loads the ELF, including its embedded sensor samples.

Hold the same host-wide advisory lock across programming and execution. All users of the shared board should agree on its path. Also inspect the board's idle state; a lock alone cannot detect unrelated software that does not use it.

```bash
set -e
export XRSIGHT_FPGA_LOCK=/tmp/xrsight-u250.lock
exec 9>"$XRSIGHT_FPGA_LOCK"
flock -n 9
"$XRSIGHT_PYTHON" - "$XRSIGHT_ROOT" "$XRSIGHT_FPGA_DB" <<'PY'
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(sys.argv[1]) / 'scripts'))
from run_firesim_matrix import assert_board_free
print(assert_board_free(pathlib.Path(sys.argv[2])))
PY
cd "$XRSIGHT_DEPLOY"
firesim infrasetup -c "$XRSIGHT_RUN/config_runtime.yaml" \
  -a "$XRSIGHT_RUN/config_hwdb.yaml" -r "$XRSIGHT_RUN/config_build_recipes.yaml"
"$XRSIGHT_PYTHON" "$XRSIGHT_ROOT/scripts/collect_firesim_results.py" record \
  --manager-dir "$XRSIGHT_DEPLOY" --runtime-dir "$XRSIGHT_RUN" \
  --execution "$XRSIGHT_RUN/execution.json" --timeout 86400 -- \
  firesim runworkload -c "$XRSIGHT_RUN/config_runtime.yaml" \
    -a "$XRSIGHT_RUN/config_hwdb.yaml" -r "$XRSIGHT_RUN/config_build_recipes.yaml"
flock -u 9
exec 9>&-
```

Run this block in a shell with `set -e` so a failed lock, idle check, or programming step stops execution. The recorder preserves the manager result, watchdog outcome, and host runtime. Timeout cleanup targets only processes belonging to this run's private directory; it does not issue broad process-name kills. Never terminate unrelated FPGA work. A timeout or interruption remains incomplete, even if some poses were emitted.

In another terminal:

```bash
screen -ls
screen -r fsim0
# Detach with Ctrl-a, d. Or monitor the file directly:
tail -f "$XRSIGHT_RUN/runfarm/sim_slot_0/uartlog"
```

Trace records are buffered during processing and exported in batches at shutdown, so absence of continuously printed poses is not itself a hang. Run the result collector below before declaring either the preflight or workload a pass.


## Collect and validate a complete case

Use the collector for acceptance; it reuses the full FireSim analyzer, including exit/watchdog/cycle status, hardware and firmware identity, dataset hashes, FASED evidence, native estimator replay, independent prediction/transform checks, and enabled eye-stage checks:

```bash
"$XRSIGHT_PYTHON" "$XRSIGHT_ROOT/scripts/collect_firesim_results.py" collect \
  --runtime-dir "$XRSIGHT_RUN" --firmware-dir "$XRSIGHT_FIRMWARE" \
  --hardware-manifest "$XRSIGHT_HARDWARE" --dataset "$EUROC_MAV0" \
  --native "$XRSIGHT_NATIVE/estimator_replay" \
  --prediction-native "$XRSIGHT_NATIVE/prediction_reference" \
  --output "$XRSIGHT_RUN/collected"
```

The destination must be new or empty. The collector copies evidence before normalization and returns nonzero on failure/incompleteness. Do not edit execution metadata to convert an interrupted run into a pass. `case.json` comes from setup and `execution.json` from the recording wrapper; preserved older runs can use explicit `--case` and `--execution` paths.

