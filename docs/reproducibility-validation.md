# README reproducibility checks

This document separates validation of the packaged setup/build instructions from
historical FPGA runtime acceptance. The README packaging changes do not change
estimator, plugin, accelerator, or Zephyr scheduler behavior.

## Software setup contract

`config/dependencies.json` records the firmware source revisions, SDK archives,
RVV compiler version and dataset selection. Hardware/component pins are in
`config/firesim/manifest.json`.

```sh
python3 scripts/setup_xrsight.py --work "$WORK" --vector
```

This calls the existing pinned dependency bootstrap, fetches the pinned OpenBLAS
source, and creates a private vector-compatible Zephyr checkout. Set up the
Python environment first, as described in the README. The helper does not install
Python packages, Chipyard, Vivado, a dataset, or global toolchains. Its output is
`$WORK/software-dependencies.json`, containing resolved paths and patch hashes.

When the dependencies have already been downloaded, this equivalent form checks
and reuses them without changing the existing checkout:

```sh
python3 scripts/setup_xrsight.py --work "$WORK" --vector \
  --reuse-deps "$EXISTING_DEPS" --openblas-source "$EXISTING_OPENBLAS"
```

The base dependencies must have the exact pinned revisions, the included OpenCV
thread patch and no unsupported tracked modifications. The OpenBLAS source must
be clean. The vector directory adds only the recorded Zephyr SDK multilib patch;
OpenCV, Eigen and SDK are linked from the base dependency root. This directory
layout is required because plugin CMake files locate Eigen relative to Zephyr.
Re-running setup with the same arguments validates and reuses the result; it
refuses to replace a directory or link pointing at another dependency root.

The Zephyr SDK compiles the application and supplies its Newlib/C++ runtime.
Chipyard GCC 13.2.0 compiles the RVV library objects using that SDK's headers and
ABI. Do not replace the complete OpenBLAS build directory with a standalone
archive: CMake checks the adjacent manifest and, for Gemmini, generated headers.
The manifest also identifies the RVV compiler/sysroot used for packing kernels.

## Isolated firmware build validation

Validation workspace:

```text
/scratch/prashanth_xrsight_readme_validation_20261005
```

A new application export copies current tracked and untracked source files, with
an SHA-256 manifest in `control/application-export.json`. Build directories start
empty. Downloaded dependencies and previously built, provenance-checked BLAS
archives are reused; a compiler cache may reuse matching object files. This is a
clean application-build check, not a claim that dependencies were downloaded or
that OpenBLAS was rebuilt during the check.

The representative matrix builds single-core Eigen/GPU, quad-core scalar
OpenBLAS/eye tracking, single-core RVV OpenBLAS/GPU, and quad-core FP32
Gemmini/eye tracking. A separate configure-only check covers single-core FP32
Gemmini with its default scalar packing. The target is `chipyard_riscv64`, with matching one-/four-hart overlays,
10 kHz ticks and the documented factor-two 1 GHz / 1 MHz timer interpretation.
RVV and FP32 Gemmini use the vector overlay and context configuration. FP32
Gemmini uses RVV row packing with explicit Saturn compatibility enabled.
Each case embeds the normal 50 stereo pairs / 501 IMUs.

The sequential build supervisor uses at most four compiler workers, lower
priority, a 48 GiB available-memory reserve, a 64 GiB process address-space limit
and a stop-on-new-OOM guard. No bitstream build, board programming or simulator
workload is part of this documentation check.

For each completed case, validation checks:

- ELF link success and the existing BLAS symbol/disassembly audit.
- Requested core count, vector support, timer frequency and tick rate in `.config`.
- Exactly 50 camera pairs and 501 IMUs in the generated dataset manifest.
- Linked text/data/BSS section total below the 256 MiB target memory capacity.
- Saved ELF/configuration/devicetree/CMake cache, source and ELF hashes, command,
  host duration, and audit results.

`control/representative-validation-state.json` is the authoritative build-status
file. The earlier eight-case queue was reduced to these representative cases;
its completed single-core Eigen artifact is retained. A briefly started
quad-core Eigen configuration is incomplete and is not counted as a pass.
Each completed case has `software/artifacts/<case>/validation.json`; build logs
are in `control/<case>.log`. An incomplete build is not a successful validation.
All four representative ELF builds and their BLAS audits passed. The single-core
FP32 Gemmini default-scalar-packing configure-only check also passed; it is not
counted as an additional linked or executed workload.
The setup helper passed an offline setup, an idempotent repeat, and a negative
check rejecting an OpenBLAS checkout at an unsupported revision.

## Firmware build results (2026-10-05)

| Cores | Backend / profile | Result | Text + data + BSS | Host build/audit time |
|---:|---|---|---:|---:|
| 1 | `eigen / gpu_pipeline` | Pass | 139,133,660 bytes | 238.2 s |
| 4 | `openblas_scalar / eye_tracking` | Pass | 202,406,690 bytes | 236.7 s |
| 1 | `openblas_rvv / gpu_pipeline` | Pass | 173,257,628 bytes | 232.0 s |
| 4 | `openblas_gemmini_fp32 / eye_tracking` | Pass | 203,153,477 bytes | 234.0 s |

All cases used fresh application build directories, the recorded dependency
cache and an available compiler cache. These host durations describe this build
validation; they are not target application-performance measurements.

ELF SHA-256 identities:

- `single-eigen`: `28c136c370f6d17c97b47df7aaeb501057b9098d5367218dc7130ab14c2a4225`
- `quad-openblas_scalar`: `6e91cc92e752a63fcd4e872b25d895bede3bd4bd2a03779b1a3ab4344cf09d91`
- `single-openblas_rvv`: `ca0ce6ade3c642d3927c45b4cf0e2c030d9652345cc2549991b8c4a28e0914c8`
- `quad-openblas_gemmini_fp32`: `376ab05af0d09a5167fa962899d6869a3ff451961cec00652277322cd81b1193`

The guard exited normally with no resource stop. The configure-only case
verified one hart, vector context enabled, and scalar Gemmini packing. All full
builds verified their requested core/vector/timer settings, dataset accounting,
symbol coverage and linked memory footprint. No target workload was executed.

## Runtime evidence and limits

Historical runtime results are described in
[RVV Gemmini packing](rvv-gemmini-packing.rst) and
[RITNet fence matrix results](ritnet-fence-matrix-results.rst). The twelve accepted
packing FPGA pipeline runs are a separate experiment from this setup validation.
Building an ELF does not establish a passing workload on a newly generated
bitstream. Run platform preflight and the normal trace/native comparisons for
each new hardware/firmware combination.

The default linear-algebra backend remains `eigen`; the default Gemmini packing
implementation remains `scalar`. RVV row packing is selected explicitly. Current
Saturn images require the documented conversion compatibility option for the
validated rounding/flag behavior. Render/timewarp GPU execution remains a model,
and RITNet CPU reference execution is a test path rather than a selectable
production eye-tracking backend.

## Hardware packaging checks

The package was validated in `/tmp/xrsight-firesim-package-a1ov3fzw` using new
local clones at the exact recorded Chipyard/component revisions. Installer
check/install/repeat operations passed. Modified pre-existing Scala was rejected.
The seven packaged configuration sources and 55 namespaced Gemmini Scala files
were byte-identical to their accepted source-snapshot counterparts
(`source-equivalence.json`).

Both build-only configuration and full runtime configuration generated valid
FireSim build, HWDB, runtime, workload, command, and case manifests. Repeating
configuration preserved the results. The full case enabled GPU and eye checks,
scale-two timing, and 10 kHz ticks. Six host packaging/resource-guard tests passed.

The hardware inspector was exercised against the existing accepted quad-core
dual-Gemmini image's actual generated DTS/FIRRTL/RTL, generated accelerator
headers, clock ratios, boot ROM, firmware ELF, routed timing/bus-skew reports,
bitstream and matching driver. Its elaboration-only result explicitly leaves
`timing_closed=false`; its full result verifies routed timing and packaged image
identity. Driver packaging produced a manifest-checked bundle containing the
matching runtime configuration. Evidence is in `elaborated-platform.json`,
`hardware-4.json`, `drivers/`, and the per-command logs.

The checks above validate source installation, compatibility inspection and
existing artifact packaging. They do not themselves establish a new hardware
build. The additional fresh Chisel elaboration below covers configuration
compilation and generation; new hardware must still complete Golden Gate,
synthesis, FPGA preflight and workload validation.

## Collector, documentation, and preservation checks

The host validation suite passed **22 tests**: ten collector tests, six hardware
packaging/resource-guard tests, and six profile tests. README checks covered all
24 code blocks, Bash syntax, YAML examples, local links, SVG XML and the editable
OmniGraffle property list.

The new collector was also run on preserved real FireSim outputs:

| Preserved case | Result |
|---|---|
| Complete GPU pipeline | Complete/pass; native replay passes |
| Interrupted workload | Incomplete/fail, with missing final result/normal exit correctly rejected |
| Quad eye tracking with RVV packing | Complete/pass; 15 VIO poses with zero native VIO position/orientation error, 43 correct eye results, 405 prediction comparisons and 658 transform comparisons |

Prediction/transform comparisons in the eye case passed their tolerances; their
floating-point residuals are recorded separately from the exactly matching VIO
poses. Re-analyzing saved outputs validates the collector and does not represent
a newly executed target workload. The firmware artifact packager verified all
22 saved artifact hashes on an existing quad build and rejected overwrite and
clock-mismatch cases.

The 29 initially dirty/untracked user files retained their original hashes.
Lightweight JSON and log evidence from the setup, collector and hardware-package
checks is preserved under the validation workspace's `control/host-evidence/`,
with a SHA-256 manifest. Bulk trace files, source clones and driver archives are
not duplicated there. The original temporary validation directories and prior
runtime campaigns remain available.


## Fresh configuration compilation and Chisel elaboration

All seven packaged configuration Scala sources were freshly compiled in 4.117 s.
`FireSimILLIXRSingleRocketDualGemminiSaturnConfig` then completed fresh Chisel
elaboration in 22.421 s under the four-worker memory/OOM guard, with normal exit.
It produced new FIRRTL, device tree and accelerator headers. The device tree and
both headers matched the accepted corrected single-core image byte for byte:

| New artifact | SHA-256 |
|---|---|
| Device tree | `645c7f98f04b97d0be8500142234542c4e178f60bb38f291135920285dcf70ca` |
| FP32 Gemmini header | `4e45a12fddba04fcc9eeac9fa554c7282ec55e14526d2efcd661ffabf8f371f9` |
| INT8 Gemmini header | `b56e2e464f26c6e3b93f08b4c1fc41fc1f8a4ba44057ab751101c4bdc25c3901` |

Checks confirmed one hart, two Gemmini arrays, one Saturn unit, the 500 MHz /
500 kHz hardware clock ratio, 256 MiB RAM and expected bridges. Configuration
classes were newly compiled; the dependency assembly was reused after matching
its accepted Rocket core/cache, Saturn memory and LegacyFirrtl2 source inputs.
This is fresh configuration/Chisel elaboration, not a fresh dependency rebuild,
Golden Gate transformation, Verilog compilation, Vivado run or FPGA workload.
Commands, state and guard results are preserved in
`control/host-evidence/fresh-chisel/` within the validation workspace.
