#!/usr/bin/env bash
# Usage: build_rocket.sh [single|dual|quad] [scheduler|pinned] [extra -D arguments ...]
# ILLIXR_PLATFORM_CHECK_ONLY=1 produces a startup-only preflight ELF.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-single}
placement=${2:-scheduler}
if (($#)); then shift; fi
if (($#)); then shift; fi
case "$mode" in
    single) harts=1; rocket_config=RocketConfig ;;
    dual) harts=2; rocket_config=DualRocketConfig ;;
    quad) harts=4; rocket_config=QuadRocketConfig ;;
    *) echo 'Expected single, dual, or quad' >&2; exit 2 ;;
esac
case "$placement" in
    scheduler) pin_plugins=0 ;;
    pinned) pin_plugins=1 ;;
    *) echo 'Expected scheduler or pinned' >&2; exit 2 ;;
esac
work=${ILLIXR_ROCKET_WORK:-/home/prashanth/illixr-rocket-work}
deps=${ILLIXR_RTOS_DEPS:-/home/prashanth/illixr-rtos-work/deps}
chipyard=${CHIPYARD_DIR:-/home/prashanth/chipyard}
build=${ILLIXR_BUILD_DIR:-$work/build-$mode}
data=${ILLIXR_DATASET_DIR:-/home/prashanth/illixr-headless-reference/data/mav0}
generated=${ILLIXR_DATA_INCLUDE_DIR:-$work/generated-50}
platform=${ILLIXR_ROCKET_PLATFORM:-$work/simulators/$rocket_config-platform.json}
core_hz=500000000
clock_scale=${ILLIXR_MODELED_CLOCK_SCALE:-2}
case "$clock_scale" in 1|2) ;; *) echo 'ILLIXR_MODELED_CLOCK_SCALE must be 1 or 2' >&2; exit 2 ;; esac
extra_conf="$repo/config/rocket_$mode.conf"
if [[ "$clock_scale" == 2 ]]; then extra_conf+=";$repo/config/rocket_1ghz.conf"; fi
preflight=${ILLIXR_PLATFORM_CHECK_ONLY:-0}
case "$preflight" in 0|1) ;; *) echo 'ILLIXR_PLATFORM_CHECK_ONLY must be 0 or 1' >&2; exit 2 ;; esac
name="rocket-$mode-$placement-50"
if [[ "$preflight" == 1 ]]; then name="rocket-$mode-preflight"; fi
artifact="$work/artifacts/${ILLIXR_ARTIFACT_NAME:-$name}"

export ZEPHYR_BASE="$deps/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$deps/zephyr-sdk-0.17.0"
unset CROSS_COMPILE
export PATH="$chipyard/.conda-env/bin:$PATH"
size_tool="$ZEPHYR_SDK_INSTALL_DIR/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-size"
readelf_tool="$ZEPHYR_SDK_INSTALL_DIR/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-readelf"

if [[ "${ILLIXR_CONFIGURE_ONLY:-0}" != 1 ]]; then
    [[ ! -e "$artifact" ]] || { echo "Refusing to overwrite $artifact" >&2; exit 2; }
    python "$repo/scripts/record_rocket_build.py" --validate-platform "$platform" --harts "$harts"
    core_hz=$(python -c 'import json,sys; print(json.load(open(sys.argv[1]))["core_hz"])' "$platform")
fi
core_hz=$((core_hz * clock_scale))
cmake -S "$repo" -B "$build" -G Ninja \
    -DBOARD=chipyard_riscv64 -DCMAKE_BUILD_TYPE=Release \
    -DPYTHON_EXECUTABLE="$chipyard/.conda-env/bin/python" -DZEPHYR_MODULES= \
    -DEXTRA_CONF_FILE="$extra_conf" \
    -DDTC_OVERLAY_FILE="$repo/config/rocket_$mode.overlay" \
    -DOPENCV_SRC_DIR="$deps/opencv" -DYAML_FILE="$repo/profiles/imu.yaml" \
    -DILLIXR_DATASET_DIR="$data" -DILLIXR_DATASET_FRAMES=50 \
    -DILLIXR_DATA_INCLUDE_DIR="$generated" \
    -DILLIXR_CAM_QUEUE_CAPACITY=8 -DILLIXR_IMU_QUEUE_CAPACITY=4096 \
    -DILLIXR_VIO_DELAY_MS=0 -DILLIXR_VIO_DELAY_AFTER_CAM=1 \
    -DILLIXR_PIN_PLUGINS="$pin_plugins" -DILLIXR_PLATFORM_CHECK_ONLY="$preflight" \
    -DILLIXR_CORE_HZ="$core_hz" "$@"
if [[ "${ILLIXR_CONFIGURE_ONLY:-0}" == 1 ]]; then exit 0; fi
cmake --build "$build" --parallel "${ILLIXR_BUILD_JOBS:-6}"
mkdir -p "$artifact"
cp "$build/zephyr/zephyr.elf" "$build/zephyr/.config" "$build/zephyr/zephyr.dts" \
   "$generated/dataset_manifest.json" "$build/CMakeCache.txt" "$artifact/"
"$size_tool" "$artifact/zephyr.elf" > "$artifact/size.txt"
"$readelf_tool" -h -A "$artifact/zephyr.elf" > "$artifact/elf-attributes.txt"
python "$repo/scripts/audit_blas.py" --build "$build" --artifact "$artifact" --nm "$ZEPHYR_SDK_INSTALL_DIR/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-nm"
python "$repo/scripts/record_rocket_build.py" \
    --repo "$repo" --deps "$deps" --artifact "$artifact" --platform "$platform" \
    --harts "$harts" --placement "$placement" --preflight "$preflight" --modeled-clock-scale "$clock_scale"
printf 'ELF: %s/zephyr.elf\n' "$artifact"
