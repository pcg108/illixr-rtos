#!/usr/bin/env bash
# Usage: build_spike.sh [single|dual|quad] [50|200] [additional -D CMake arguments ...]
# ILLIXR_PROFILE=gpu_pipeline selects isolated pipeline build/artifact defaults.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mode=${1:-single}
frames=${2:-50}
if (($#)); then shift; fi
if (($#)); then shift; fi
case "$mode" in single|dual|quad) ;; *) echo 'Expected single, dual, or quad' >&2; exit 2;; esac
[[ "$frames" =~ ^[1-9][0-9]*$ ]] || { echo 'Invalid frame count' >&2; exit 2; }
work=${ILLIXR_RTOS_WORK:-/home/prashanth/illixr-rtos-work}
chipyard=${CHIPYARD_DIR:-/home/prashanth/chipyard}
deps=${ILLIXR_RTOS_DEPS:-$work/deps}
profile=${ILLIXR_PROFILE:-imu}
case "$profile" in
    */*) profile_file=$profile ;;
    *) profile_file="$repo/profiles/$profile.yaml" ;;
esac
[[ -f "$profile_file" ]] || { echo "Missing profile: $profile_file" >&2; exit 2; }
profile_name=$(basename -- "$profile_file" .yaml)
suffix="$mode-$frames"
if [[ "$profile_name" != imu ]]; then suffix="$profile_name-$suffix"; fi
build=${ILLIXR_BUILD_DIR:-$work/build-$suffix}
artifact=${ILLIXR_ARTIFACT_DIR:-$work/artifacts/${ILLIXR_ARTIFACT_NAME:-$suffix}}
if [[ "${ILLIXR_CONFIGURE_ONLY:-0}" != 1 && -e "$artifact" ]]; then
    echo "Refusing to overwrite $artifact; select a new ILLIXR_ARTIFACT_NAME" >&2
    exit 2
fi
data=${ILLIXR_DATASET_DIR:-/home/prashanth/illixr-headless-reference/data/mav0}
export ZEPHYR_BASE="$deps/zephyr"
# Chipyard GCC's Newlib omits the retargetable locks required for SMP.
# Use the Zephyr-recommended matched compiler/C/C++ runtime, retaining Chipyard Spike.
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$deps/zephyr-sdk-0.17.0"
unset CROSS_COMPILE
size_tool="$ZEPHYR_SDK_INSTALL_DIR/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-size"
export PATH="$chipyard/.conda-env/bin:$PATH"
cmake -S "$repo" -B "$build" -G Ninja \
    -DBOARD=spike_riscv64 -DCMAKE_BUILD_TYPE=Release \
    -DPYTHON_EXECUTABLE="$chipyard/.conda-env/bin/python" \
    -DZEPHYR_MODULES= \
    -DEXTRA_CONF_FILE="$repo/config/spike_$mode.conf" \
    -DDTC_OVERLAY_FILE="$repo/config/spike_$mode.overlay" \
    -DOPENCV_SRC_DIR="$deps/opencv" -DYAML_FILE="$profile_file" \
    -DILLIXR_DATASET_DIR="$data" -DILLIXR_DATASET_FRAMES="$frames" \
    -DILLIXR_CAM_QUEUE_CAPACITY=8 -DILLIXR_IMU_QUEUE_CAPACITY=4096 \
    -DILLIXR_VIO_DELAY_MS=0 -DILLIXR_VIO_DELAY_AFTER_CAM=1 "$@"
if [[ "${ILLIXR_CONFIGURE_ONLY:-0}" == 1 ]]; then exit 0; fi
cmake --build "$build" --parallel "${ILLIXR_BUILD_JOBS:-6}"
generated=$(python -c 'import sys; from pathlib import Path; print(next(line.split("=",1)[1] for line in Path(sys.argv[1]).read_text().splitlines() if line.startswith("ILLIXR_DATA_INCLUDE_DIR:PATH=")))' "$build/CMakeCache.txt")
mkdir -p "$artifact"
cp "$build/zephyr/zephyr.elf" "$build/zephyr/.config" "$build/zephyr/zephyr.dts" \
   "$generated/dataset_manifest.json" "$build/CMakeCache.txt" "$artifact/"
"$size_tool" "$artifact/zephyr.elf" > "$artifact/size.txt"
python "$repo/scripts/audit_blas.py" --build "$build" --artifact "$artifact" --nm "$ZEPHYR_SDK_INSTALL_DIR/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-nm"
python "$repo/scripts/record_spike_build.py" "$repo" "$deps" "$artifact"
printf 'ELF: %s/zephyr.elf\n' "$artifact"
