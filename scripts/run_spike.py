#!/usr/bin/env python3
"""Run a bounded Spike test and preserve commands, hashes, console, native replay and analysis."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import selectors
import shutil
import signal
import subprocess
import sys
import time

from analyze_spike import analyze


# This Spike build requires Zicntr explicitly for CSR_CYCLE (rdcycle), used by
# the platform startup check. Zicsr enables CSR instructions, not counters.
DEFAULT_ISA = "rv64imafdc_zicsr_zifencei_zicntr"


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_revision(path):
    result = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], text=True, capture_output=True)
    return result.stdout.strip() if result.returncode == 0 else None


def stop(process):
    if process.poll() is None:
        os.killpg(process.pid, signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


def prediction_reference(executable, log, directory, metadata):
    """Preserve an independent desktop prediction/transform replay and its identity."""
    executable = Path(executable).resolve()
    output = directory / "prediction-native.json"
    command = [str(executable), "--trace", str(log), "--output", str(output)]
    metadata.update(prediction_native_command=command, prediction_native_sha256=sha256(executable))
    run = subprocess.run(command, capture_output=True, text=True, timeout=600)
    (directory / "prediction-native-console.log").write_text(run.stdout + run.stderr)
    metadata["prediction_native_returncode"] = run.returncode
    result = json.loads(output.read_text()) if output.exists() else {}
    if run.returncode or result.get("passed") is not True or result.get("errors"):
        return result, ["Desktop prediction/transform reference comparison failed; see prediction-native-console.log"]
    return result, []


def run(args, case):
    elf = Path(case["elf"]).resolve()
    harts = int(case["harts"])
    if harts not in (1, 2, 4):
        raise ValueError("harts must be one, two or four")
    if case.get("require_gpu") and (not args.native or not getattr(args, "prediction_native", None)):
        raise ValueError("GPU pipeline validation requires both native estimator and prediction reference executables")
    directory = Path(case.get("output", args.output)).resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if (directory / "run.json").exists():
        raise ValueError(f"Refusing to overwrite prior run artifacts in {directory}")
    spike = Path(args.spike).resolve()
    isa = case.get("isa", args.isa)
    command = [str(spike), f"-p{harts}", "-m0x80000000:0x10000000", f"--isa={isa}", str(elf)]
    root = Path(__file__).resolve().parents[1]
    started = time.monotonic()
    metadata = {"started_utc": datetime.now(timezone.utc).isoformat(), "command": command,
                "elf": str(elf), "elf_sha256": sha256(elf), "spike": str(spike),
                "spike_sha256": sha256(spike), "repo_revision": git_revision(root),
                "harts": harts, "isa": isa, "timeout_seconds": args.timeout, "case": case,
                "default_simulated_clint": True}
    source_diff = subprocess.run(["git", "-C", str(root), "diff", "--binary"], capture_output=True, check=True).stdout
    metadata["tracked_source_diff_sha256"] = hashlib.sha256(source_diff).hexdigest()
    sources = subprocess.run(["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
                             capture_output=True, check=True).stdout.decode().split("\0")
    source_manifest = {}
    for relative in sorted(set(sources)):
        path = root / relative
        if not path.is_file() or "__pycache__" in path.parts:
            continue
        if path.suffix in (".cpp", ".cc", ".c", ".hpp", ".h", ".cmake", ".conf", ".overlay", ".yaml", ".py", ".sh") or path.name in ("CMakeLists.txt", "Kconfig"):
            source_manifest[relative] = sha256(path)
    (directory / "source_manifest.json").write_text(json.dumps(source_manifest, indent=2) + "\n")
    metadata["source_manifest_sha256"] = sha256(directory / "source_manifest.json")
    for name, candidates in (("zephyr.config", (elf.parent / ".config",)),
                             ("zephyr.dts", (elf.parent / "zephyr.dts",)),
                             ("dataset_manifest.json", (elf.parent.parent / "generated" / "dataset_manifest.json",
                                                        elf.parent / "dataset_manifest.json"))):
        source = next((path for path in candidates if path.exists()), None)
        if source is not None:
            shutil.copy2(source, directory / name)
            metadata[name + "_sha256"] = sha256(source)
    log = directory / "console.log"
    marker = False
    timed_out = False
    interrupted = False
    fatal = False
    fatal_markers = set()
    scan_tail = b""
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    pending = b""
    try:
        with log.open("wb", buffering=0) as output:
            while True:
                for key, _ in selector.select(timeout=0.2):
                    block = os.read(key.fileobj.fileno(), 65536)
                    if block:
                        output.write(block)
                        pending += block
                        scanned = scan_tail + block
                        for panic in (b"ZEPHYR FATAL ERROR", b"Halting system"):
                            if panic in scanned:
                                fatal = True
                                fatal_markers.add(panic.decode())
                        scan_tail = scanned[-32:]
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            if b"ILLIXR_RESULT " in line:
                                marker = True
                    else:
                        selector.unregister(key.fileobj)
                if process.poll() is not None and not selector.get_map():
                    break
                if fatal:
                    stop(process)
                if time.monotonic() - started > args.timeout:
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
    metadata.update({"host_elapsed_seconds": time.monotonic() - started,
                     "returncode": process.returncode, "timed_out": timed_out, "interrupted": interrupted,
                     "final_result_seen": marker, "fatal": fatal, "fatal_markers": sorted(fatal_markers)})
    native_log = None
    native_failed = False
    if marker and not timed_out and not interrupted and not fatal and args.native and not case.get("expect_failure"):
        native_log = directory / "native.log"
        native_command = [str(Path(args.native).resolve()), "--dataset", str(Path(args.dataset).resolve()),
                          "--trace", str(log), "--output", str(native_log)]
        metadata["native_command"] = native_command
        native_result = subprocess.run(native_command, text=True, capture_output=True)
        (directory / "native-console.log").write_text(native_result.stdout + native_result.stderr)
        metadata["native_returncode"] = native_result.returncode
        native_failed = native_result.returncode != 0
        source_manifest = Path(args.native).resolve().parent / "estimator_sources.json"
        if source_manifest.exists():
            shutil.copy2(source_manifest, directory / source_manifest.name)
    try:
        result = analyze(log, native_log if native_log and not native_failed else None, harts,
                         case.get("require_initialized", False), case.get("require_async", False),
                         case.get("expect_failure"), case.get("require_camera_drop", False), case.get("require_delay", False), args.dataset,
                         placement=case.get("placement"), require_gpu=case.get("require_gpu", False))
    except (OSError, ValueError) as error:
        result = {"passed": False, "errors": [str(error)]}
    if timed_out:
        result["errors"].append("Host watchdog expired: validation is incomplete")
    if interrupted:
        result["errors"].append("Execution interrupted: validation is incomplete")
    if fatal:
        result["errors"].append("Definitive Zephyr fatal marker observed: " + ", ".join(sorted(fatal_markers)))
    if native_failed:
        result["errors"].append("Native estimator replay failed; see native-console.log")
    if case.get("require_gpu") and marker and not timed_out and not interrupted and not fatal:
        try:
            result["prediction_native"], reference_errors = prediction_reference(args.prediction_native, log, directory, metadata)
            result["errors"].extend(reference_errors)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            result["errors"].append(f"Desktop prediction/transform reference failed: {error}")
    if case.get("require_reuse") and result.get("gpu_pipeline", {}).get("fresh_reuse_with_updated_prediction", 0) < 1:
        result["errors"].append("Slow render case requires repeated-image warps with a fresh, updated prediction")
    expected_error_exit = (case.get("expect_failure") == "imu_overflow" and process.returncode == 1
                           and result["passed"])
    if process.returncode != 0 and not expected_error_exit:
        result["errors"].append(f"Spike exited unexpectedly with code {process.returncode}")
    result["passed"] = not result["errors"]
    result["complete"] = bool(marker and not timed_out and not interrupted and not fatal and process.returncode == 0)
    metadata["status"] = "pass" if result["passed"] else ("fail" if result["complete"] else "incomplete")
    (directory / "analysis.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
    (directory / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"{'PASS' if result['passed'] else 'FAIL'} {directory}", flush=True)
    for error in result["errors"]:
        print(f"  {error}", flush=True)
    return result["passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spike", default="/home/prashanth/chipyard/.conda-env/riscv-tools/bin/spike")
    parser.add_argument("--isa", default=DEFAULT_ISA)
    parser.add_argument("--elf")
    parser.add_argument("--harts", type=int, choices=(1, 2, 4))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=3600)
    parser.add_argument("--dataset", default="/home/prashanth/illixr-headless-reference/data/mav0")
    parser.add_argument("--native")
    parser.add_argument("--prediction-native", type=Path, help="Independent desktop prediction/transform replay executable")
    parser.add_argument("--require-initialized", action="store_true")
    parser.add_argument("--require-async", action="store_true")
    parser.add_argument("--require-camera-drop", action="store_true")
    parser.add_argument("--require-delay", action="store_true")
    parser.add_argument("--require-gpu", action="store_true")
    parser.add_argument("--placement", choices=("unpinned", "pinned"))
    parser.add_argument("--expect-failure", choices=("imu_overflow",))
    parser.add_argument("--matrix", type=Path, help="JSON list of cases with elf,harts,name and optional repeat/checks")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    if args.matrix:
        cases = json.loads(args.matrix.read_text())
    else:
        if not args.elf or args.harts is None:
            parser.error("--elf and --harts are required without --matrix")
        cases = [{"elf": args.elf, "harts": args.harts,
                  "require_initialized": args.require_initialized, "require_async": args.require_async,
                  "require_camera_drop": args.require_camera_drop, "expect_failure": args.expect_failure,
                  "require_delay": args.require_delay, "require_gpu": args.require_gpu,
                  "placement": args.placement}]
    passed = True
    for case in cases:
        for repeat in range(case.get("repeat", 1)):
            current = dict(case)
            if args.matrix:
                name = case.get("name", f"{Path(case['elf']).parents[1].name}-{case['harts']}hart")
                current["output"] = str(args.output / f"{name}-{repeat + 1}")
            passed = run(args, current) and passed
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
