#!/usr/bin/env bash
# Build standard Chipyard Rocket RTL simulators without modifying Chipyard sources.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
chipyard_dir=${CHIPYARD_DIR:-/home/prashanth/chipyard}
work_dir=${ILLIXR_ROCKET_WORK:-/home/prashanth/illixr-rocket-work}
sim_output_dir=${ILLIXR_SIM_OUTPUT_DIR:-$work_dir/simulators}
sim_threads=${ILLIXR_SIM_THREADS:-1}
testdriver_period_ns=${ILLIXR_TESTDRIVER_PERIOD_NS:-1.0}
config=${1:-RocketConfig}
case "$config" in
  RocketConfig) harts=1 ;;
  DualRocketConfig) harts=2 ;;
  QuadRocketConfig) harts=4 ;;
  *) echo "Expected RocketConfig, DualRocketConfig, or QuadRocketConfig" >&2; exit 2 ;;
esac
export RISCV=${RISCV:-$chipyard_dir/.conda-env/riscv-tools}
export PATH="$chipyard_dir/.conda-env/bin:$RISCV/bin:$PATH"
firtool=${FIRTOOL_BIN:-$chipyard_dir/.vecadd-sim/firtool-1.62.0/bin/firtool}
export JAVA_TOOL_OPTIONS="-Xmx12G -Xss8M -Djava.io.tmpdir=$work_dir/java-tmp"
mkdir -p "$work_dir"/{simulators,logs,provenance,java-tmp,classpath-cache}
mkdir -p "$sim_output_dir"
# Seed existing assembly outputs. sbt-assembly's content cache may otherwise
# report an old output path as up-to-date without copying it to the new path.
for jar in chipyard tapeout; do
  if [[ ! -s "$work_dir/classpath-cache/$jar.jar" && -s "$chipyard_dir/.classpath_cache/$jar.jar" ]]; then
    cp -p "$chipyard_dir/.classpath_cache/$jar.jar" "$work_dir/classpath-cache/$jar.jar"
  fi
done
stamp=$(date -u +%Y%m%dT%H%M%SZ)
provenance="$work_dir/provenance/$config-$stamp"
mkdir -p "$provenance"
git -C "$chipyard_dir" status --porcelain=v1 > "$provenance/chipyard-status.txt"
git -C "$chipyard_dir" rev-parse HEAD > "$provenance/chipyard-head.txt"
git -C "$chipyard_dir" diff --binary HEAD > "$provenance/chipyard-diff.patch"
git -C "$chipyard_dir" submodule status > "$provenance/submodules.txt"
git -C "$chipyard_dir" submodule foreach --recursive 'git diff --binary HEAD' > "$provenance/submodule-diffs.patch"
verilator --version > "$provenance/verilator-version.txt"
"$firtool" --version > "$provenance/firtool-version.txt"
command=(make -C "$chipyard_dir/sims/verilator" -j "${ILLIXR_BUILD_JOBS:-12}"
  "CONFIG=$config" "sim_dir=$sim_output_dir"
  "CLASSPATH_CACHE=$work_dir/classpath-cache" "FIRTOOL_BIN=$firtool"
  "JAVA_TMP_DIR=$work_dir/java-tmp" "VERILATOR_THREADS=$sim_threads"
  "RUNTIME_THREADS=--threads $sim_threads --threads-dpi none"
  "CLOCK_PERIOD=$testdriver_period_ns")
if [[ -n ${ILLIXR_SIM_SPLIT_CFUNCS:-} ]]; then
  command+=("VERILATOR_OPT_FLAGS=-O3 --x-assign fast --x-initial fast --output-split 10000 --output-split-cfuncs $ILLIXR_SIM_SPLIT_CFUNCS")
fi
if [[ ${ILLIXR_SIM_NATIVE_OPTIMIZATION:-0} == 1 ]]; then
  command+=("EXTRA_SIM_CXXFLAGS=-O3 -march=native -mtune=native -flto=auto"
            "EXTRA_SIM_LDFLAGS=-flto=auto"
            "AR=$chipyard_dir/.conda-env/bin/x86_64-conda-linux-gnu-gcc-ar")
fi
printf '%q ' "${command[@]}" > "$provenance/command.sh"
printf '\n' >> "$provenance/command.sh"
"${command[@]}" 2>&1 | tee "$work_dir/logs/$config-$stamp.log"
sha256sum "$work_dir/classpath-cache/chipyard.jar" "$work_dir/classpath-cache/tapeout.jar" \
  "$firtool" "$chipyard_dir/.conda-env/bin/verilator_bin" \
  "$RISCV/lib/libriscv.so" "$RISCV/lib/libfesvr.a" "$RISCV/lib/libdramsim.so" \
  "$chipyard_dir/tools/DRAMSim2/libdramsim.a" > "$provenance/build-inputs.sha256"
ldd "$sim_output_dir/simulator-chipyard.harness-$config" > "$provenance/dynamic-libraries.txt"
python3 "$repo_dir/scripts/inspect_rocket_platform.py" \
  --build-dir "$sim_output_dir/generated-src/chipyard.harness.TestHarness.$config" \
  --simulator "$sim_output_dir/simulator-chipyard.harness-$config" \
  --testdriver-period-ns "$testdriver_period_ns" --simulator-threads "$sim_threads" \
  --harts "$harts" --config "$config" --provenance "$provenance" \
  --output "$sim_output_dir/$config-platform.json"
