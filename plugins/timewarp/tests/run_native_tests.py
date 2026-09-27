#!/usr/bin/env python3
"""Test actual GPU snapshots, schedules, presentation and waits and compare math to pinned desktop source.

The host adapter tests synchronization logic, not Zephyr's scheduler or timing.
Spike/FireSim remain the authority for target behavior and observed placement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--desktop", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
source_path = args.desktop / "plugins/headless_timewarp_vk/plugin.cpp"
source = source_path.read_text()
start = source.index("static void calculate_timewarp_transform(")
opening = source.index("{", start)
depth = 1
end = opening + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
reference = '#pragma once\n#include "illixr/math_util.hpp"\nnamespace desktop_reference {\n' + source[start:end] + '\n}\n'
(args.output / "desktop_transform_reference.hpp").write_text(reference)
here = Path(__file__).resolve().parent
root = here.parents[2]
binary = args.output / "test_gpu_workers"
command = ["g++", "-std=c++20", "-O2", "-pthread", "-DEIGEN_RUNTIME_NO_MALLOC",
           "-DCONFIG_MP_MAX_NUM_CPUS=4", "-DILLIXR_GPU_PIPELINE=1",
           "-I" + str(here / "host_stubs"), "-I" + str(args.output.resolve()),
           "-I" + str(args.desktop / "include"), "-I/usr/include/eigen3",
           "-I" + str(root / "src"), str(here / "test_gpu_workers.cpp"), "-o", str(binary)]
subprocess.run(command, check=True)
result = subprocess.run([str(binary)], text=True, check=True, capture_output=True)
data = json.loads(result.stdout)
invalid = [('ILLIXR_RENDER_OFFSET_NS', -1), ('ILLIXR_RENDER_OFFSET_NS', 8333333),
           ('ILLIXR_TIMEWARP_MARGIN_NS', -1), ('ILLIXR_TIMEWARP_MARGIN_NS', 7333333),
           ('ILLIXR_RENDER_DELAY_NS', 0)]
for key, value in invalid:
    check = subprocess.run(command[:-2] + ['-fsyntax-only', f'-D{key}={value}'], text=True, capture_output=True)
    if check.returncode == 0 or 'static assertion failed' not in check.stderr:
        raise RuntimeError(f'Invalid GPU configuration was not rejected: {key}={value}')
data['invalid_configurations_rejected'] = len(invalid)
data.update({"desktop_commit": subprocess.check_output(["git", "-C", str(args.desktop), "rev-parse", "HEAD"], text=True).strip(),
             "desktop_source_sha256": hashlib.sha256(source_path.read_bytes()).hexdigest(),
             "reference_function_sha256": hashlib.sha256(source[start:end].encode()).hexdigest(),
             "compile_command": command})
(args.output / "gpu-native-tests.json").write_text(json.dumps(data, indent=2) + "\n")
print(json.dumps(data, indent=2))
