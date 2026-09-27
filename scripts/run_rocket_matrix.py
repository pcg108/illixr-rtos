#!/usr/bin/env python3
"""Run the Rocket preflights and five workload cases sequentially, with resumable evidence."""
import argparse
from datetime import datetime, timezone
import fcntl
import json
from pathlib import Path
import re
import signal
import sys
import tempfile

import run_rocket
from run_spike import sha256


MODES = {1: "single", 2: "dual", 4: "quad"}
CONFIGS = {1: "RocketConfig", 2: "DualRocketConfig", 4: "QuadRocketConfig"}


def load_cases(matrix, work):
    workloads = json.loads(matrix.read_text())
    expected = {(1, "unpinned"), (2, "unpinned"), (2, "pinned"), (4, "unpinned"), (4, "pinned")}
    if not isinstance(workloads, list) or len(workloads) != 5 or {(c["harts"], c["placement"]) for c in workloads} != expected:
        raise ValueError("The matrix must contain the five approved single/dual/quad placement cases")
    if len({c["name"] for c in workloads}) != 5 or len({c["output"] for c in workloads}) != 5:
        raise ValueError("Matrix names and output directories must be unique")
    workloads = sorted(workloads, key=lambda c: (c["harts"], c["placement"] == "pinned"))
    for case in workloads:
        case["platform_check"] = False
        for field in ("elf", "hardware_manifest", "output"):
            case[field] = str(Path(case[field]).resolve())
        if case.get("timeout_seconds", 86400) <= 0 or case.get("max_cycles", 100000000000) <= 0:
            raise ValueError("Every case needs positive watchdog and simulation cycle limits")
    hardware_by_hart = {}
    for case in workloads:
        previous = hardware_by_hart.setdefault(case["harts"], case["hardware_manifest"])
        if previous != case["hardware_manifest"]:
            raise ValueError("Placement variants for one core count must use the same hardware manifest")
    preflights = [{"name": f"preflight-{mode}", "harts": harts, "placement": "unpinned", "platform_check": True,
        "elf": str(work / "artifacts" / f"rocket-{mode}-preflight" / "zephyr.elf"),
        "hardware_manifest": hardware_by_hart[harts],
        "output": str(work / "results" / f"preflight-{mode}-1")} for harts, mode in MODES.items()]
    return preflights + workloads


def attempts(base):
    base = Path(base)
    match = re.fullmatch(r"(.*)-(\d+)", base.name)
    stem = match[1] if match else base.name
    found = []
    if base.parent.exists():
        for path in base.parent.iterdir():
            attempt = re.fullmatch(re.escape(stem) + r"-(\d+)", path.name)
            if path.is_dir() and attempt:
                found.append((int(attempt[1]), path))
            elif path == base and path.is_dir():
                found.append((0, path))
    return sorted(found)


def next_output(base):
    base = Path(base)
    existing = attempts(base)
    if not existing and not base.exists():
        return base
    if base.exists() and not any(base.iterdir()) and not existing:
        return base
    match = re.fullmatch(r"(.*)-(\d+)", base.name)
    stem = match[1] if match else base.name
    number = max((n for n, _ in existing), default=0) + 1
    return base.parent / f"{stem}-{number}"


def fingerprints(case):
    hardware_path = Path(case["hardware_manifest"])
    hardware = json.loads(hardware_path.read_text())
    simulator = Path(hardware["simulator"])
    run_rocket.validate_hardware(hardware, simulator, case["harts"])
    return {"elf_sha256": sha256(case["elf"]), "simulator_sha256": sha256(simulator),
            "hardware_manifest.json_sha256": sha256(hardware_path)}


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def inspect_case(case):
    result = {"name": case["name"], "platform_check": case["platform_check"], "harts": case["harts"],
              "placement": case["placement"], "status": "pending", "attempts": []}
    try:
        current = fingerprints(case)
        result["current_fingerprints"] = current
    except (OSError, ValueError, KeyError) as error:
        current = None
        result["input_error"] = str(error)
    for _, path in attempts(case["output"]):
        metadata, analysis = read_json(path / "run.json"), read_json(path / "analysis.json")
        status = metadata.get("status", "incomplete") if metadata else "incomplete"
        reusable = bool(current and metadata and analysis and status == "pass" and
                        analysis.get("passed") is True and not analysis.get("errors") and
                        analysis.get("complete") is True and metadata.get("returncode") == 0 and
                        all(metadata.get(key) == value for key, value in current.items()) and
                        metadata.get("harts") == case["harts"] and
                        metadata.get("placement") == case["placement"] and
                        metadata.get("platform_check") == case["platform_check"])
        if status == "pass" and not reusable:
            status = "stale"
        result["attempts"].append({"directory": str(path), "status": status, "reusable": reusable})
        result.update(status=status, output=str(path), reusable=reusable, metadata=metadata, analysis=analysis)
    result.setdefault("reusable", False)
    return result


def summarize(cases):
    items = [inspect_case(case) for case in cases]
    platforms = {item["harts"]: item for item in items if item["platform_check"]}
    for item in items:
        if not item["platform_check"] and item["status"] == "pending":
            platform = platforms[item["harts"]]
            if platform["status"] in ("fail", "incomplete", "stale"):
                item["status"] = "blocked"
                item["reason"] = f"Matching {MODES[item['harts']]}-core preflight has not passed"
    counts = {status: sum(item["status"] == status for item in items) for status in sorted({item["status"] for item in items})}
    return {"updated_utc": datetime.now(timezone.utc).isoformat(), "all_passed": all(item["reusable"] for item in items),
            "counts": counts, "cases": items,
            "interpretation": "Only recorded completed analyses are results; native agreement does not establish physical pose accuracy."}


def display(value, digits=3):
    if value is None:
        return "—"
    return f"{value:.{digits}f}" if isinstance(value, float) else str(value)


def markdown(summary):
    lines = ["# Rocket validation comparison", "", f"Updated: {summary['updated_utc']}", "",
             "Statuses reflect saved execution evidence. Pending, running, blocked, stale, or incomplete cases are not passes.", "",
             "| Case | Harts | Status | Host seconds | Evidence |", "|---|---:|---|---:|---|"]
    for item in summary["cases"]:
        metadata = item.get("metadata") or {}
        link = f"[artifacts]({item['output']})" if item.get("output") else "—"
        lines.append(f"| {item['name']} | {item['harts']} | {item['status']} | {display(metadata.get('host_elapsed_seconds'))} | {link} |")
    lines += ["", "## Workload measurements", "",
        "| Case | IMUs VIO / integration | Cameras processed / skipped / dropped | VIO poses | Camera pairs/s | Simulated seconds | Native max m / rad | Max VIO age ms | Missed probe deadlines |",
        "|---|---|---|---:|---:|---:|---|---:|---:|"]
    for item in summary["cases"]:
        if item["platform_check"]:
            continue
        analysis = item.get("analysis") or {}
        data, comparison, consumer = analysis.get("summary", {}), analysis.get("native_comparison", {}), analysis.get("consumer", {})
        imu = " / ".join(display(data.get(key)) for key in ("imu_processed", "imu_integrator_processed"))
        cameras = " / ".join(display(data.get(key)) for key in ("cam_processed", "cam_skipped", "cam_dropped"))
        errors = " / ".join(display(comparison.get(key), 9) for key in ("max_position_error_m", "max_orientation_error_rad"))
        age = consumer.get("maximum_vio_age_ns")
        runtime = analysis.get("simulated_runtime_seconds")
        camera_rate = data["cam_processed"] / runtime if runtime and "cam_processed" in data else None
        lines.append(f"| {item['name']} | {imu} | {cameras} | {display(analysis.get('pose_count'))} | "
                     f"{display(camera_rate)} | {display(runtime)} | {errors} | "
                     f"{display(age / 1e6 if age is not None else None)} | {display(data.get('probe_missed_deadlines'))} |")
    for item in summary["cases"]:
        analysis = item.get("analysis") or {}
        if not analysis and not item.get("reason") and not item.get("input_error"):
            continue
        lines += ["", f"## {item['name']}", ""]
        if item.get("reason"):
            lines.append(item["reason"] + ".")
        if item.get("input_error"):
            lines.append("Current input verification: " + item["input_error"])
        if analysis.get("errors"):
            lines += ["", "Recorded validation errors:", ""] + ["- " + error for error in analysis["errors"]]
        placements = analysis.get("placement", {}).get("plugins", [])
        if placements:
            lines += ["", "| Plugin | Requested hart | Observed mask | Work per hart | Publications per hart |", "|---|---:|---|---|---|"]
            for p in placements:
                mask = p.get("hart_mask")
                shown_mask = hex(mask) if type(mask) is int else display(mask)
                lines.append(f"| {p.get('plugin')} | {p.get('requested_hart')} | {shown_mask} | {p.get('work_counts')} | {p.get('publication_counts')} |")
        data = analysis.get("summary", {})
        queues = [f"{key}: {data[key]}" for key in ("imu_vio_highwater", "imu_integrator_highwater", "cam_highwater", "history_highwater") if key in data]
        if queues:
            lines += ["", "Queue/history high-water marks: " + "; ".join(queues) + "."]
        truth = analysis.get("trajectory_sanity", {})
        if truth.get("available"):
            lines += ["", f"Estimated endpoint displacement: {display(truth.get('estimated_displacement_m'))} m; "
                f"matching ground truth: {display(truth.get('ground_truth_displacement_m'))} m. This is an unaligned motion-magnitude diagnostic."]
        propagation = analysis.get("propagation", {})
        if "max_observed_position_norm_m" in propagation:
            lines += ["", "Maximum observed propagated-pose position norm: " + display(propagation["max_observed_position_norm_m"]) + " m."]
    lines += ["", "Camera pairs/s divides processed stereo pairs by simulated replay and queue-drain time, excluding trace printing.", "",
        "Native replay uses this port's unchanged estimator and the actual delivered sensor sequence. "
        "Agreement is a runtime-equivalence check, not proof of desktop OpenVINS equivalence or acceptable physical accuracy.", ""]
    return "\n".join(lines)


def save_report(cases, directory):
    directory.mkdir(parents=True, exist_ok=True)
    summary = summarize(cases)
    for name, content in (("summary.json", json.dumps(summary, indent=2, allow_nan=False) + "\n"),
                          ("comparison.md", markdown(summary))):
        with tempfile.NamedTemporaryFile(mode="w", dir=directory, prefix=name + ".", delete=False) as stream:
            stream.write(content)
            temporary = Path(stream.name)
        temporary.replace(directory / name)
    return summary


def runner_args(case, output, args):
    return argparse.Namespace(elf=Path(case["elf"]), hardware_manifest=Path(case["hardware_manifest"]), simulator=None,
        harts=case["harts"], placement=case["placement"], output=output, platform_check=case["platform_check"],
        timeout=case.get("timeout_seconds", 86400), max_cycles=case.get("max_cycles", 100000000000),
        chipyard=args.chipyard, dataset=args.dataset, native=args.native)


def run_matrix(cases, args):
    report = args.report_dir
    initial = save_report(cases, report)
    running = [attempt["directory"] for item in initial["cases"] for attempt in item["attempts"] if attempt["status"] == "running"]
    if running:
        raise ValueError("An existing run is still marked running; wait for it before launching the matrix: " + ", ".join(running))
    platforms = {}
    interrupted = False
    try:
        for case in cases:
            if interrupted:
                break
            item = inspect_case(case)
            if not case["platform_check"] and not platforms.get(case["harts"], False):
                print(f"BLOCKED {case['name']}: matching preflight did not pass", flush=True)
                continue
            if item["reusable"]:
                print(f"RESUME {case['name']}: matching completed PASS in {item['output']}", flush=True)
                passed = True
            else:
                if item.get("input_error"):
                    print(f"BLOCKED {case['name']}: {item['input_error']}", flush=True)
                    passed = False
                else:
                    output = next_output(case["output"])
                    print(f"START {case['name']}: {output}", flush=True)
                    try:
                        passed = run_rocket.run(runner_args(case, output, args))
                    except (OSError, ValueError, KeyError) as error:
                        print(f"FAILED {case['name']}: {error}", flush=True)
                        output.mkdir(parents=True, exist_ok=True)
                        failed_run = read_json(output / "run.json") or {}
                        failed_run.update(status="fail", launch_error=str(error))
                        (output / "run.json").write_text(json.dumps(failed_run, indent=2) + "\n")
                        (output / "analysis.json").write_text(json.dumps({"passed": False, "complete": False, "errors": [str(error)]}, indent=2) + "\n")
                        passed = False
                    metadata = read_json(output / "run.json") or {}
                    interrupted = metadata.get("interrupted", False)
            if case["platform_check"]:
                platforms[case["harts"]] = passed
            save_report(cases, report)
    finally:
        summary = save_report(cases, report)
    return summary["all_passed"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path, default=Path("/home/prashanth/illixr-rocket-work"))
    parser.add_argument("--matrix", type=Path)
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--report-only", action="store_true")
    parser.add_argument("--chipyard", type=Path, default=Path("/home/prashanth/chipyard"))
    parser.add_argument("--dataset", type=Path, default=Path("/home/prashanth/illixr-headless-reference/data/mav0"))
    parser.add_argument("--native", type=Path, default=Path("/home/prashanth/illixr-spike-validation/native/estimator_replay"))
    args = parser.parse_args()
    args.work = args.work.resolve()
    args.report_dir = (args.report_dir or args.work / "results").resolve()
    cases = load_cases((args.matrix or args.work / "matrix.json").resolve(), args.work)
    if args.report_only:
        summary = save_report(cases, args.report_dir)
        print(json.dumps(summary["counts"], sort_keys=True))
        return 0
    def interrupted(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupted)
    args.report_dir.mkdir(parents=True, exist_ok=True)
    with (args.report_dir / "matrix.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise ValueError("Another Rocket matrix runner holds the execution lock") from error
        return 0 if run_matrix(cases, args) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, KeyboardInterrupt) as error:
        print(str(error) or "Matrix execution interrupted", file=sys.stderr)
        sys.exit(1)
