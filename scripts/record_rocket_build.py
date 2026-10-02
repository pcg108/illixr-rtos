#!/usr/bin/env python3
"""Validate the standard Rocket platform and snapshot firmware provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_platform(path, harts):
    platform = json.loads(Path(path).read_text())
    expected = {"harts": harts, "hart_ids": list(range(harts)), "timer_hz": 500000,
                "memory_base": 0x80000000, "memory_size": 0x10000000,
                "core_hz": 500000000}
    for key, value in expected.items():
        if platform.get(key) != value:
            raise ValueError(f"Rocket platform {key}: expected {value}, got {platform.get(key)}")
    dts = Path(platform["dts"])
    if not dts.is_file():
        raise ValueError(f"Generated Rocket DTS does not exist: {dts}")
    if sha(dts) != platform["dts_sha256"]:
        raise ValueError("Generated Rocket DTS no longer matches its platform manifest")
    for name, expected_registers in {"clint": (0x02000000, 0x10000),
                                     "plic": (0x0C000000, 0x04000000)}.items():
        device = platform[name]
        if (device["base"], device["size"]) != expected_registers:
            raise ValueError(f"Unsupported Rocket {name} register map")
    if (platform["plic"]["ndev"], platform["plic"]["max_priority"]) != (1, 1):
        raise ValueError("Rocket PLIC source count/priority differs from the firmware overlay")
    if platform["clint"]["interrupts_extended"][1::2] != [3, 7] * harts:
        raise ValueError("Rocket CLINT interrupt contexts differ from the firmware overlay")
    if platform["plic"]["interrupts_extended"][1::2] != [11, 9] * harts:
        raise ValueError("Rocket PLIC interrupt contexts differ from the firmware overlay")
    if any(not cpu["isa"].startswith("rv64imafdc") for cpu in platform["cpus"]):
        raise ValueError("Rocket CPUs must support the compiled RV64IMAFDC ISA")
    return platform


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validate-platform", type=Path)
    parser.add_argument("--harts", type=int, choices=(1, 2, 4), required=True)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--deps", type=Path)
    parser.add_argument("--artifact", type=Path)
    parser.add_argument("--platform", type=Path)
    parser.add_argument("--placement", choices=("scheduler", "pinned"))
    parser.add_argument("--preflight", type=int, choices=(0, 1))
    parser.add_argument("--modeled-clock-scale", type=int, choices=(1, 2), default=1,
                        help="Explicit simulation time reinterpretation; generated hardware remains unchanged")
    args = parser.parse_args()
    if args.validate_platform:
        validate_platform(args.validate_platform, args.harts)
        return
    for name in ("repo", "deps", "artifact", "platform", "placement", "preflight"):
        if getattr(args, name) is None:
            parser.error(f"--{name} is required when recording a build")
    platform = validate_platform(args.platform, args.harts)
    with (args.artifact / "zephyr.elf").open("rb") as stream:
        elf_header = stream.read(64)
    if (elf_header[:6] != b"\x7fELF\x02\x01" or
            struct.unpack_from("<H", elf_header, 18)[0] != 243 or
            struct.unpack_from("<I", elf_header, 48)[0] & 6 != 4):
        raise ValueError("Firmware must be a little-endian RV64 ELF with double-float ABI")
    config = dict(line.split("=", 1) for line in (args.artifact / ".config").read_text().splitlines()
                  if line.startswith("CONFIG_") and "=" in line)
    for key, value in {"CONFIG_MP_MAX_NUM_CPUS": str(args.harts),
                       "CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC": str(platform['timer_hz'] * args.modeled_clock_scale),
                       "CONFIG_FPU": "y", "CONFIG_FPU_SHARING": "y", "CONFIG_UART_HTIF": "y"}.items():
        if config.get(key) != value:
            raise ValueError(f"Firmware {key}: expected {value}, got {config.get(key)}")
    if config.get("CONFIG_UART_SIFIVE") == "y":
        raise ValueError("Unused SiFive UART unexpectedly enabled")
    cache = dict(line.split("=", 1) for line in (args.artifact / "CMakeCache.txt").read_text().splitlines()
                 if not line.startswith(("//", "#")) and "=" in line)
    cache = {key.split(":", 1)[0]: value for key, value in cache.items()}
    true_values = {"1", "ON", "TRUE", "YES"}
    if (cache["ILLIXR_PIN_PLUGINS"].upper() in true_values) != (args.placement == "pinned"):
        raise ValueError("Firmware placement differs from the requested artifact placement")
    if (cache["ILLIXR_PLATFORM_CHECK_ONLY"].upper() in true_values) != bool(args.preflight):
        raise ValueError("Firmware preflight mode differs from the requested artifact mode")
    if int(cache["ILLIXR_CORE_HZ"]) != platform["core_hz"] * args.modeled_clock_scale:
        raise ValueError("Firmware core frequency differs from the declared clock model")
    expected_replay = {"ILLIXR_DATASET_FRAMES": "50", "ILLIXR_CAM_QUEUE_CAPACITY": "8",
                       "ILLIXR_IMU_QUEUE_CAPACITY": "4096", "ILLIXR_VIO_DELAY_MS": "0",
                       "ILLIXR_VIO_DELAY_AFTER_CAM": "1"}
    for key, value in expected_replay.items():
        if cache.get(key) != value:
            raise ValueError(f"Rocket replay {key}: expected {value}, got {cache.get(key)}")
    dataset = json.loads((args.artifact / "dataset_manifest.json").read_text())
    if (dataset["camera_pairs"], dataset["imu_samples"]) != (50, 501):
        raise ValueError("Rocket validation requires exactly 50 stereo pairs and 501 IMUs")
    shutil.copy2(args.platform, args.artifact / "hardware-platform.json")
    shutil.copy2(platform["dts"], args.artifact / "hardware.dts")
    subprocess.run([sys.executable, str(args.repo / "scripts/record_spike_build.py"),
                    str(args.repo), str(args.deps), str(args.artifact)], check=True)
    manifest_path = args.artifact / "build_manifest.json"
    manifest = json.loads(manifest_path.read_text())
    family = "chipyard-shuttle-saturn" if "shuttle_saturn" in platform.get("config", "") else "chipyard-rocket"
    manifest["target"] = {"platform": family, "harts": args.harts,
                          "hardware_config": platform.get("config"),
                          "placement": args.placement, "platform_check_only": bool(args.preflight),
                          "timer_hz": platform["timer_hz"] * args.modeled_clock_scale,
                          "core_hz": platform["core_hz"] * args.modeled_clock_scale,
                          "ticks_per_sec": int(config["CONFIG_SYS_CLOCK_TICKS_PER_SEC"]),
                          "modeled_clock_scale": args.modeled_clock_scale,
                          "generated_timer_hz": platform["timer_hz"],
                          "generated_core_hz": platform["core_hz"],
                          "hardware_dts_sha256": sha(args.artifact / "hardware.dts")}
    if cache.get("ILLIXR_EYE_TRACKING_ENABLED", "OFF").upper() in true_values:
        ritnet = args.repo / "third_party/ritnet"
        source_hashes = {str(p.relative_to(args.repo)): sha(p) for p in (ritnet / "port").rglob("*") if p.is_file()}
        source_hashes.update({str(p.relative_to(args.repo)): sha(p) for p in
            (args.repo / "src/eye_tracking.cpp", args.repo / "src/eye_tracking.hpp",
             args.repo / "plugins/offline_eye/plugin.cpp", args.repo / "plugins/eye_tracking/plugin.cpp")})
        manifest["ritnet"] = {"enabled": True, "precision": "int8", "opcode": 2,
            "accelerator_hart": 0, "array_dim": 16, "publication_hz": 120,
            "params_sha256": sha(ritnet / "port/include/gemmini_params.h"),
            "reference": json.loads((ritnet / "reference/manifest.json").read_text()),
            "sources": source_hashes}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
