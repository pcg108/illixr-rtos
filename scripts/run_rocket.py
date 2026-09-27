#!/usr/bin/env python3
"""Validate a Rocket Verilator ELF, retaining hardware, placement and native evidence."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import resource
import selectors
import shutil
import signal
import subprocess
import sys
import time

from analyze_spike import analyze, records, startup_checks
from run_spike import git_revision, sha256, stop


CONFIG_HARTS = {"RocketConfig": 1, "DualRocketConfig": 2, "QuadRocketConfig": 4}
FATAL_MARKERS = (b"ZEPHYR FATAL ERROR", b"Halting system", b"*** FAILED ***", b"%Error:")


def simulator_command(simulator, elf, chipyard, max_cycles):
    """Same flags as common.mk run-binary-fast with LOADMEM=1; no verbose/wave flags."""
    ini = chipyard / "generators/testchipip/src/main/resources/dramsim2_ini"
    if not ini.is_dir():
        raise ValueError(f"DRAMSim configuration is absent: {ini}")
    return [str(simulator), "+permissive", "+dramsim", f"+dramsim_ini_dir={ini}",
            f"+max-cycles={max_cycles}", f"+loadmem={elf}", "+permissive-off", str(elf)]


def capture(command, directory, timeout):
    """Keep a watchdog and stop only our simulator process group on interruption/fatal."""
    started = time.monotonic()
    timed_out = interrupted = cycle_limit_reached = False
    fatal_markers = set()
    scan_tail = b""
    # Large generated Verilator functions can exceed the usual 8 MiB host stack.
    # Raise the inherited soft limit only for the child; guest stacks are unrelated.
    original_stack = resource.getrlimit(resource.RLIMIT_STACK)
    soft, hard = original_stack
    child_stack = max(soft, 64 * 1024 * 1024) if soft != resource.RLIM_INFINITY else soft
    if hard != resource.RLIM_INFINITY:
        child_stack = min(child_stack, hard)
    try:
        resource.setrlimit(resource.RLIMIT_STACK, (child_stack, hard))
        process = subprocess.Popen(command, cwd=directory, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    finally:
        resource.setrlimit(resource.RLIMIT_STACK, original_stack)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    try:
        with (directory / "console.log").open("wb", buffering=0) as output:
            while True:
                for key, _ in selector.select(timeout=0.2):
                    block = os.read(key.fileobj.fileno(), 65536)
                    if block:
                        output.write(block)
                        scanned = scan_tail + block
                        fatal_markers.update(marker.decode() for marker in FATAL_MARKERS if marker in scanned)
                        if b"*** FAILED ***" in scanned and b"(timeout)" in scanned:
                            cycle_limit_reached = True
                        scan_tail = scanned[-128:]
                    else:
                        selector.unregister(key.fileobj)
                if process.poll() is not None and not selector.get_map():
                    break
                if fatal_markers:
                    stop(process)
                if time.monotonic() - started > timeout:
                    timed_out = True
                    stop(process)
    except KeyboardInterrupt:
        interrupted = True
        stop(process)
    except BaseException:
        stop(process)
        raise
    finally:
        selector.close()
        process.stdout.close()
    return {"host_elapsed_seconds": time.monotonic() - started, "returncode": process.returncode,
            "host_stack_soft_limit_bytes": child_stack,
            "timed_out": timed_out, "interrupted": interrupted, "cycle_limit_reached": cycle_limit_reached,
            "fatal_markers": sorted(fatal_markers)}


def validate_hardware(manifest, simulator, harts):
    if CONFIG_HARTS.get(manifest.get("config")) != harts or manifest.get("harts") != harts:
        raise ValueError("Generated hardware configuration does not match requested hart count")
    if manifest.get("hart_ids") != list(range(harts)):
        raise ValueError("Generated hardware hart IDs must be contiguous from zero")
    if manifest.get("simulator_sha256") != sha256(simulator):
        raise ValueError("Simulator hash differs from its hardware manifest")
    dts = Path(manifest["dts"]).resolve()
    if manifest.get("dts_sha256") != sha256(dts):
        raise ValueError("Generated hardware DTS hash differs from its manifest")
    for key in ("timer_hz", "core_hz", "memory_base", "memory_size"):
        if type(manifest.get(key)) is not int or manifest[key] <= 0:
            raise ValueError(f"Hardware manifest requires a positive integer {key}")
    if manifest["memory_base"] != 0x80000000 or manifest["memory_size"] != 0x10000000:
        raise ValueError("This validation requires 256 MiB RAM at 0x80000000")
    return dts


def preserve_inputs(directory, elf, manifest_path, hardware_dts, root):
    provenance = {}
    candidates = {"zephyr.config": (elf.parent / ".config",),
                  "zephyr.dts": (elf.parent / "zephyr.dts",),
                  "dataset_manifest.json": (elf.parent / "dataset_manifest.json", elf.parent.parent / "generated/dataset_manifest.json"),
                  "firmware_build_manifest.json": (elf.parent / "build_manifest.json",),
                  "hardware_manifest.json": (manifest_path,), "hardware.dts": (hardware_dts,)}
    for name, paths in candidates.items():
        source = next((path for path in paths if path.is_file()), None)
        if source:
            shutil.copy2(source, directory / name)
            provenance[name + "_sha256"] = sha256(source)
    paths = subprocess.check_output(["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard", "-z"]).decode().split("\0")
    manifest = {relative: sha256(root / relative) for relative in sorted(set(paths))
                if relative and (root / relative).is_file() and "__pycache__" not in Path(relative).parts}
    (directory / "source_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    provenance["source_manifest_sha256"] = sha256(directory / "source_manifest.json")
    diff = subprocess.check_output(["git", "-C", str(root), "diff", "--binary"])
    provenance["tracked_source_diff_sha256"] = hashlib.sha256(diff).hexdigest()
    return provenance


def execution_errors(execution):
    errors = []
    if execution["timed_out"]:
        errors.append("Host watchdog expired: validation is incomplete")
    if execution["cycle_limit_reached"]:
        errors.append("Simulation cycle limit reached: validation is incomplete")
    if execution["interrupted"]:
        errors.append("Simulation interrupted: validation is incomplete")
    if execution["fatal_markers"]:
        errors.append("Fatal simulation marker observed: " + ", ".join(execution["fatal_markers"]))
    if execution["returncode"] != 0:
        errors.append(f"Rocket simulator did not exit normally with HTIF success: {execution['returncode']}")
    return errors


def run(args):
    manifest_path = args.hardware_manifest.resolve()
    hardware = json.loads(manifest_path.read_text())
    elf = args.elf.resolve()
    simulator = (args.simulator or Path(hardware["simulator"])).resolve()
    hardware_dts = validate_hardware(hardware, simulator, args.harts)
    if not elf.is_file():
        raise ValueError(f"Firmware does not exist: {elf}")
    # Share firmware/provenance checks with FireSim while retaining the
    # generated hardware frequencies as immutable platform identity.
    from run_firesim_matrix import firmware_clock_settings, validate_firmware, effective_clocks
    clock_case = dict(elf=str(elf), harts=args.harts, placement=args.placement,
                      platform_check=args.platform_check, **firmware_clock_settings(elf))
    validate_firmware(clock_case, hardware)
    clocks = effective_clocks(clock_case, hardware)
    if not args.platform_check and not args.native.is_file():
        raise ValueError(f"Native estimator harness does not exist: {args.native}")
    command = simulator_command(simulator, elf, args.chipyard.resolve(), args.max_cycles)
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        raise ValueError(f"Refusing to overwrite prior artifacts in {directory}")
    root = Path(__file__).resolve().parents[1]
    metadata = {"status": "running", "started_utc": datetime.now(timezone.utc).isoformat(), "command": command,
                "elf": str(elf), "elf_sha256": sha256(elf), "simulator": str(simulator),
                "simulator_sha256": sha256(simulator), "hardware_manifest": str(manifest_path),
                "repo_revision": git_revision(root), "chipyard_revision": git_revision(args.chipyard),
                "harts": args.harts, "placement": args.placement, "platform_check": args.platform_check,
                "timeout_seconds": args.timeout, "max_cycles": args.max_cycles,
                "fast_elf_loading": True, "instruction_log": False, "waveforms": False}
    metadata.update(preserve_inputs(directory, elf, manifest_path, hardware_dts, root))
    if not args.platform_check:
        dataset_path = directory / "dataset_manifest.json"
        dataset_manifest = json.loads(dataset_path.read_text())
        if dataset_manifest.get("camera_pairs") != 50 or dataset_manifest.get("imu_samples") != 501:
            raise ValueError("Rocket validation requires the complete 50-pair / 501-IMU embedded subset")
    (directory / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    execution = capture(command, directory, args.timeout)
    metadata.update(execution)
    log = directory / "console.log"
    native_log = None
    extra_errors = execution_errors(execution)
    try:
        data = records(log)
        metadata["final_result_seen"] = len(data["summaries"]) == 1
        metadata["platform_result_seen"] = len(data["platforms"]) == 1
        if args.platform_check:
            result = {"clock": data["clocks"], "platform": data["platforms"], "errors": startup_checks(
                data, args.harts, clocks["timer_hz"], clocks["core_hz"], True)}
        else:
            if metadata["final_result_seen"] and not extra_errors:
                native_log = directory / "native.log"
                native_command = [str(args.native.resolve()), "--dataset", str(args.dataset.resolve()),
                                  "--trace", str(log), "--output", str(native_log)]
                metadata["native_command"] = native_command
                metadata["native_sha256"] = sha256(args.native)
                native_result = subprocess.run(native_command, text=True, capture_output=True)
                (directory / "native-console.log").write_text(native_result.stdout + native_result.stderr)
                metadata["native_returncode"] = native_result.returncode
                native_sources = args.native.resolve().parent / "estimator_sources.json"
                if native_sources.is_file():
                    shutil.copy2(native_sources, directory / native_sources.name)
                if native_result.returncode:
                    extra_errors.append("Native estimator replay failed; see native-console.log")
                    native_log = None
            else:
                extra_errors.append("Complete successful target execution is required before native replay")
            result = analyze(log, native=native_log, harts=args.harts, require_initialized=True, require_async=True,
                dataset=args.dataset, placement=args.placement, expected_timer_hz=clocks["timer_hz"],
                expected_core_hz=clocks["core_hz"], require_platform=True)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {"errors": [f"Could not validate target evidence: {error}"]}
    result["errors"].extend(extra_errors)
    result["passed"] = not result["errors"]
    result["host_elapsed_seconds"] = execution["host_elapsed_seconds"]
    result["hardware_config"] = hardware["config"]
    result["firmware_sha256"] = metadata["elf_sha256"]
    result["complete"] = not (execution["timed_out"] or execution["interrupted"] or execution["cycle_limit_reached"])
    metadata["status"] = "pass" if result["passed"] else ("fail" if result["complete"] else "incomplete")
    (directory / "analysis.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    (directory / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"{'PASS' if result['passed'] else 'FAIL'} {directory}", flush=True)
    for error in result["errors"]:
        print(f"  {error}", flush=True)
    return result["passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--hardware-manifest", type=Path, required=True)
    parser.add_argument("--simulator", type=Path, help="Defaults to the simulator recorded in the hardware manifest")
    parser.add_argument("--harts", type=int, choices=(1, 2, 4), required=True)
    parser.add_argument("--placement", choices=("unpinned", "pinned"), default="unpinned")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--platform-check", action="store_true", help="Require boot/clock evidence and exit, without replay")
    parser.add_argument("--timeout", type=float, default=86400)
    parser.add_argument("--max-cycles", type=int, default=100000000000)
    parser.add_argument("--chipyard", type=Path, default=Path("/home/prashanth/chipyard"))
    parser.add_argument("--dataset", type=Path, default=Path("/home/prashanth/illixr-headless-reference/data/mav0"))
    parser.add_argument("--native", type=Path, default=Path("/home/prashanth/illixr-spike-validation/native/estimator_replay"))
    args = parser.parse_args()
    if args.timeout <= 0 or args.max_cycles <= 0:
        parser.error("watchdog and cycle limit must be positive")
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    return 0 if run(args) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
