# Pinned local-U250 FireSim support

`manifest.json` records the audited source revisions, copied Scala files, patches,
configuration catalog, and accepted accelerator/boot-ROM hashes. This package
reconstructs the tested configuration from source; it does not redistribute
bitstreams or claim a new FPGA build has completed. The default is quad-core
Rocket with Saturn on every hart and independent FP32/custom3 and INT8/custom2
Gemmini accelerators on hart 0. Shuttle entries are experimental comparison
configurations, not the production Rocket baseline.

The containing Chipyard revision pins the corrected Rocket, Saturn and Shuttle
repositories. FireSim must be selected separately at the revision in the manifest.
The installer never changes Git revisions, fetches repositories, or modifies an
unrelated file. The published Chipyard, Rocket, Saturn, and Shuttle repositories support anonymous HTTPS access.

## Source installation

After cloning and initializing the pinned Chipyard/components and selecting the
manifest's FireSim revision:

```bash
python3 "$XRSIGHT/scripts/setup_firesim.py" install --chipyard "$CHIPYARD" --check
python3 "$XRSIGHT/scripts/setup_firesim.py" install --chipyard "$CHIPYARD"
```

The original Gemmini repository must contain stock commit
`8c3f9923a44a2fe2c7930587be297d6d4f8c09ca`. If it is in a separate clone, add
`--gemmini-source /absolute/path/to/gemmini`. The installer applies the packaged
host max-cycle/build-report patches and installs a reproducibly renamed stock
Gemmini namespace alongside the existing generator. An identical installation is
accepted; an unexpected revision or modified destination is rejected.

## Build preparation and generated parameters

```bash
python3 "$XRSIGHT/scripts/setup_firesim.py" configure \
  --chipyard "$CHIPYARD" --work "$WORK" --build-only \
  --config illixr_u250_rocket_dual_gemmini_saturn_quad
```

This writes fully expanded JSON-formatted YAML templates plus `commands.json`.
No ELF, FPGA database, or bitstream is needed at this stage. The `elaborate` entry
in `commands.json` is an argument array for `make ... verilog`; it produces the
DTS/FIRRTL/FireSim RTL and the Gemmini parameter headers. The headers are
`$CHIPYARD/gemmini_params_illixr.h` (FP32) and
`$CHIPYARD/gemmini_params_illixr_int8.h` (INT8), when those arrays are selected.
Different configurations require separate checkouts/build directories because
header names and FireSim intermediate paths are shared within a checkout.

Run elaboration/build commands under `scripts/firesim_resource_guard.py`:

```bash
python3 "$XRSIGHT/scripts/firesim_resource_guard.py" \
  --scratch "$WORK/builds" --run-dir "$WORK/guard-build" \
  --latch "$WORK/STOPPED_NO_AUTORESTART.json" --jobs 4 -- \
  python3 "$XRSIGHT/scripts/firesim_manager.py" --chipyard "$CHIPYARD" -- \
  buildbitstream -b "$WORK/config_build.yaml" \
  -r "$WORK/config_build_recipes.yaml" -a "$WORK/config_hwdb_build.yaml"
```

Use the sourced Chipyard/FireSim Python environment, Vivado 2022.1, and the normal
local U250 host prerequisites. The manager wrapper forwards the resource guard's
ownership tag and worker limits to localhost SSH workers. The guard requires
48 GiB available-memory reserve, stops at 64 GiB per-process RSS or a new OOM,
and treats memory PSI as warning-only. It records each process identity before
cleanup, never kills an unrelated job merely sharing a directory, preserves its
stop latch, and does not restart. Use a new `--run-dir` for each deliberate attempt.

Before synthesis, verify the generated description and parameters:

```bash
python3 "$XRSIGHT/scripts/check_firesim_platform.py" --elaboration-only \
  --chipyard "$CHIPYARD" --config "$HW_CONFIG" \
  --staging-dir "$STAGING_DIR" --rtl "$GENERATED_RTL" \
  --elaboration-log "$ELABORATION_LOG" --output "$WORK/platform.json"
```

`commands.json` records the exact staging and RTL paths for the selected target.
Capture the complete successful `make verilog` output as `ELABORATION_LOG` because
FASED's compiled outstanding-request capacities are verified from this evidence.
The resulting platform JSON is compatible with firmware artifact recording.
It explicitly has `timing_closed: false`; it cannot authorize FPGA programming.

## Review a completed build and package its driver

After a build completes, use the `cl_<quintuplet>` directory containing
`firesim.tar.gz`, `driver/`, and `vivado_proj/` as `BUILD_OUTPUT`. Supply the actual
final post-route bus-skew report from the build project's `firesim.runs/impl_1/`:

```bash
python3 "$XRSIGHT/scripts/check_firesim_platform.py" \
  --chipyard "$CHIPYARD" --config "$HW_CONFIG" \
  --staging-dir "$STAGING_DIR" --rtl "$GENERATED_RTL" \
  --elaboration-log "$ELABORATION_LOG" --build-output "$BUILD_OUTPUT" \
  --bus-skew-report "$BUS_SKEW_REPORT" --firmware "$FIRMWARE" \
  --output "$WORK/hardware.json"
python3 "$XRSIGHT/scripts/setup_firesim.py" package-driver \
  --chipyard "$CHIPYARD" --hardware "$WORK/hardware.json" \
  --output-dir "$WORK/drivers"
```

The checker verifies generated hart/ISA/memory/interrupt/clock/bridge evidence,
accelerator parameter headers and placement, ELF memory/HTIF/ABI compatibility,
FASED limits, final setup/hold/pulse-width/bus-skew/routing reports, and packaged
bitstream metadata. Driver packaging includes its shared libraries and the exact
runtime configuration, preserving their hashes. Boot and workload correctness
remain separate tests.

## Runtime preparation

```bash
python3 "$XRSIGHT/scripts/setup_firesim.py" configure \
  --chipyard "$CHIPYARD" --work "$WORK/run" --config "$HW_CONFIG" \
  --elf "$FIRMWARE/zephyr.elf" --fpga-db "$FPGA_DB" \
  --hardware-manifest "$WORK/hardware.json" --workload-name xrsight-quad
```

`FIRMWARE` must contain the recorded ELF, `.config`, CMake cache, DTS, dataset
manifest, and `build_manifest.json`. The helper installs uniquely named workload
links and emits `config_runtime.yaml`, `config_hwdb.yaml`, `workload.json`, and
`case.json`. It refuses to overwrite unrelated links or hand-edited outputs.
Run `infrasetup` and `runworkload` from the FireSim `deploy` directory using these
files, only after confirming exclusive ownership of the FPGA. Use the main README's
record/collect commands to preserve the watchdog/exit result and run analysis.

The retained runtime model is 30-cycle FASED read/write latency with 10 outstanding
requests each, zeroed DRAM, and explicit HTIF service cadence. Do not change the
request limit to 16: it does not fit this model's field/capacity. The 30 MHz FPGA
clock is separate from generated 500 MHz/500 kHz target clocks and the firmware's
explicit modeled 1 GHz/1 MHz interpretation.

## Optional TrafficGen trace dependency

The pinned FireChip sources contain optional TrafficGen tracing that references
`InclusiveCacheTrafficGenTraceCycles`. That parameter exists in a local
TrafficGen-modified inclusive-cache tree, but not in the pinned public
inclusive-cache revision. Scala resolves these names even when the selected
ILLIXR configuration never enables the feature.

The installer applies `chipyard-remove-trafficgen-trace-dependency.patch` to the
isolated XRSight checkout. It removes the FASED trace-print block, its imports,
and the TrafficGen-only parameter override. It leaves the FASED bridge and memory
model configuration intact. It does not add a dummy cache parameter or require
the TrafficGen cache modifications. This checkout should not be used to reproduce
the separate TrafficGen trace experiments.

If Scala compilation reports this missing symbol, update XRSight-RTOS and rerun
`setup_firesim.py install --chipyard "$CHIPYARD_DIR"` before retrying elaboration.
There is no Chipyard/FireSim revision change for this fix. Installation is
idempotent and rejects conflicting edits in the affected source files.
For the retry, use a new guard run directory and log filename (for example,
`elaboration-guard-trafficgen-fix` and `elaboration-trafficgen-fix.log`). Keep
the existing latch path: a resource-stop latch still requires investigation;
a normal Scala compilation failure is not a resource-stop event.

Validation on 2026-10-06 recompiled the pinned inclusive-cache sources after
removing all prior inclusive-cache and FireChip classes from the dependency
assembly. Original FireChip sources reproduced the three missing-symbol errors;
patched FireChip sources and all packaged configs compiled successfully. Fresh
quad-core dual-Gemmini Chisel elaboration completed in 34.84 seconds. The real
installer passed check/apply/repeat/conflict tests. Other dependency classes were
reused; this does not claim a fresh full dependency build, Golden Gate lowering,
Verilog compilation, synthesis or FPGA execution.

## Driver compilation without TrafficGen

The pinned FireChip driver makefile adds every bridge source, a TrafficGen DPI
model, and Boost serialization even for hardware with no TrafficGen bridge.
The XRSight installer applies `chipyard-xrsight-driver-without-trafficgen.patch`:
for `FireSimILLIXR...` target configurations it excludes `trafficgen.cc`,
`trafficgen_dpi.cc`, TrafficGen header dependencies, and `-lboost_serialization`.
Other target configurations retain their original TrafficGen source/link lists.
No bridge sources are deleted and no hardware configuration changes.

If `buildbitstream` fails compiling `trafficgen_socket_protocol.h` due to missing
Boost headers, update XRSight, rerun `setup_firesim.py install`, and retry the
build. Installing Boost is not required for this unused XRSight driver path.
Use a fresh resource-guard run directory and log for the retry while retaining
the existing stop-latch path. Do not discard completed build artifacts.

Validation compiled and linked complete single- and quad-core U250 host drivers
using accepted generated headers, with poison Boost archive headers that reject
any accidental include. The original TrafficGen source failed that negative
control. All 13 packaged target configurations exclude the dependency; a legacy
TrafficGen target retains byte-identical source/link flag lists. Installer
check/apply/repeat checks passed. This validates the host-driver change, not a new
synthesis, bitstream or FPGA workload.
