#!/usr/bin/env bash
# Fetch only pinned sources, without modifying Chipyard's checkout or environment.
set -euo pipefail
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
work=${ILLIXR_RTOS_WORK:-/home/prashanth/illixr-rtos-work}
deps="$work/deps"
mkdir -p "$deps"
fetch_exact() {
    local path=$1 url=$2 revision=$3
    if [[ ! -d "$path/.git" ]]; then
        git init "$path"
        git -C "$path" remote add origin "$url"
    fi
    if ! git -C "$path" cat-file -e "$revision^{commit}" 2>/dev/null; then
        git -C "$path" fetch --depth=1 origin "$revision"
    fi
    if [[ -n "$(git -C "$path" status --porcelain)" ]]; then
        if [[ "$path" == "$deps/opencv" ]] &&
           [[ "$(git -C "$path" rev-parse HEAD)" == "$revision" ]] &&
           diff -q <(git -C "$path" diff) "$repo/scripts/patches/opencv-4.5.4-zephyr-threads.patch" >/dev/null; then
            return
        fi
        echo "Dependency has unexpected local modifications: $path" >&2; exit 1
    fi
    git -C "$path" checkout --detach "$revision"
}
fetch_exact "$deps/zephyr" https://github.com/ucb-bar/zephyr.git cd45a528d3bf81f9c7aa2a63f9fe512eee0881b0
fetch_exact "$deps/opencv" https://github.com/opencv/opencv.git 4223495e6cd67011f86b8ecd9be1fa105018f3b1
fetch_exact "$deps/modules/lib/eigen" https://gitlab.com/libeigen/eigen.git 68f4e58cfacc686583d16cff90361f0b43bc2c1b
patch="$repo/scripts/patches/opencv-4.5.4-zephyr-threads.patch"
if git -C "$deps/opencv" apply --check "$patch" 2>/dev/null; then
    git -C "$deps/opencv" apply "$patch"
elif ! git -C "$deps/opencv" apply --reverse --check "$patch" 2>/dev/null; then
    echo 'OpenCV runtime patch does not apply' >&2; exit 1
fi
# Matched runtime is required: Chipyard's Newlib has no retargetable locks.
sdk="$deps/zephyr-sdk-0.17.0"
fetch_archive() {
    local name=$1 checksum=$2
    if [[ ! -f "$deps/$name" ]]; then
        curl -fL --retry 2 -o "$deps/$name" \
          "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.17.0/$name"
    fi
    printf '%s  %s\n' "$checksum" "$deps/$name" | sha256sum --check --status
}
fetch_archive zephyr-sdk-0.17.0_linux-x86_64_minimal.tar.xz \
    0514d2c684dfb5f6327374bfed0b3dcf727ff1500195d26b3730f98252fed095
fetch_archive toolchain_linux-x86_64_riscv64-zephyr-elf.tar.xz \
    cd97784c88de0207c93cf386f79d8d2606b46598dc74d4ad12cadd5617595964
if [[ ! -d "$sdk" ]]; then
    tar -xf "$deps/zephyr-sdk-0.17.0_linux-x86_64_minimal.tar.xz" -C "$deps"
fi
if [[ ! -d "$sdk/riscv64-zephyr-elf" ]]; then
    tar -xf "$deps/toolchain_linux-x86_64_riscv64-zephyr-elf.tar.xz" -C "$sdk"
fi
if [[ ! -d "$sdk/sysroots" ]]; then
    "$sdk/setup.sh" -h -t riscv64-zephyr-elf
fi
printf 'Dependencies ready in %s\n' "$deps"
