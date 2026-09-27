#!/usr/bin/env python3
"""Read completed console traces and preserve asynchronous-pipeline evidence.

This audit never changes firmware or input artifacts. The only output is the
explicit --output JSON file. Repeated --case arguments allow a combined report.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path


def audit_case(name, console):
    raw = console.read_bytes()
    events, gpu_results, runtime_results = [], [], []
    for line in raw.decode("utf-8").splitlines():
        for prefix, destination in (("ILLIXR_GPU_EVENT ", events),
                                    ("ILLIXR_GPU_RESULT ", gpu_results),
                                    ("ILLIXR_RESULT ", runtime_results)):
            if line.startswith(prefix):
                destination.append(json.loads(line[len(prefix):]))
                break
    if len(gpu_results) != 1 or len(runtime_results) != 1:
        raise ValueError(f"{name}: expected one complete GPU and runtime result")
    if runtime_results[0].get("status") != "pass":
        raise ValueError(f"{name}: runtime did not report pass")
    gpu = gpu_results[0]
    renders = [event for event in events if event["stage"] == "render"]
    warps = [event for event in events if event["stage"] == "timewarp"]
    if len(renders) != gpu["render_completed"] or len(warps) != gpu["timewarp_completed"]:
        raise ValueError(f"{name}: incomplete frame trace")
    v2 = gpu.get("version", 1) == 2
    if v2:
        from analyze_spike import records, gpu_checks
        data = records(console)
        _, errors = gpu_checks(data, runtime_results[0], runtime_results[0]['online_harts'])
        if errors: raise ValueError(f"{name}: " + '; '.join(errors))
    if not v2 and len(renders) != len(warps) + gpu["mailbox_replaced"]:
        raise ValueError(f"{name}: frame accounting mismatch")
    if not (gpu.get("render_closed") and gpu.get("timewarp_done")) or gpu.get("mailbox_pending"):
        raise ValueError(f"{name}: pipeline did not close and drain")
    render_by_id = {event["frame_id"]: event for event in renders}
    if len(render_by_id) != len(renders):
        raise ValueError(f"{name}: duplicate render frame IDs")
    overlap, next_before = [], []
    for warp in warps:
        if warp["frame_id"] not in render_by_id:
            raise ValueError(f"{name}: warp has no corresponding render frame")
        for render in renders:
            if render["frame_id"] <= warp["frame_id"]:
                continue
            amount = min(render["scheduled_complete_ns"], warp["scheduled_complete_ns"]) - max(
                render["submit_ns"], warp["submit_ns"])
            if amount > 0:
                overlap.append({"render_frame_id": render["frame_id"],
                                "warp_frame_id": warp["frame_id"], "warp_id": warp.get("warp_id"), "overlap_ns": amount})
            if warp["submit_ns"] < render["submit_ns"] < warp["observed_complete_ns"]:
                next_before.append({"render_frame_id": render["frame_id"], "warp_frame_id": warp["frame_id"]})
    fresh = [warp for warp in warps if warp["prediction_status"] == "valid" and
             render_by_id[warp["frame_id"]]["prediction_status"] == "valid"]
    newer = [warp["frame_id"] for warp in fresh if
             warp["source_sequence"] > render_by_id[warp["frame_id"]]["source_sequence"]]
    reuse = [new["frame_id"] for old, new in zip(renders, renders[1:]) if
             old["source_sequence"] == new["source_sequence"] and
             old["prediction_status"] == "valid" and new["prediction_status"] == "valid"]
    retarget = [warp["frame_id"] for warp in warps if warp.get("late_target", False)]
    return {
        "case": name, "console_path": str(console), "console_sha256": hashlib.sha256(raw).hexdigest(),
        "modeled_GPU_overlap_pairs": len(overlap),
        "modeled_GPU_overlap_total_ns": sum(record["overlap_ns"] for record in overlap),
        "overlap_pair_records": overlap,
        "next_render_submitted_before_previous_warp_observed_complete": len(next_before),
        "next_render_before_warp_observed_records": next_before,
        "fresh_frame_pairs": len(fresh), "warp_used_newer_source_than_saved_render": len(newer),
        "newer_source_frame_ids": newer, "successive_valid_renders_reusing_source": len(reuse),
        "reused_source_frame_ids": reuse, "retargeted_frames": len(retarget),
        "retargeted_frame_ids": retarget, "mailbox_replaced": gpu.get("mailbox_replaced"),
        "trace_version": gpu.get("version",1), "image_reuses": gpu.get("repeated_uses"),
        "fresh_on_time_presentations": gpu.get("fresh_on_time_presentations"),
        "render_total": len(renders), "warp_total": len(warps),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", required=True, metavar="NAME=CONSOLE")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    cases = []
    names = set()
    output = args.output.resolve()
    for value in args.case:
        if "=" not in value:
            parser.error("--case requires NAME=CONSOLE")
        name, path = value.split("=", 1)
        if not name or not path or name in names:
            parser.error("--case requires a nonempty unique name and console path")
        console = Path(path).resolve()
        if console == output:
            parser.error("--output must not overwrite an input console")
        names.add(name)
        cases.append(audit_case(name, console))
    script = Path(__file__).resolve()
    result = {
        "recorded_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "audit_script": str(script), "audit_script_sha256": hashlib.sha256(script.read_bytes()).hexdigest(),
        "method": "Read existing completed console traces. Modeled GPU interval=[submit_ns,scheduled_complete_ns); "
                  "worker-observed interval=[submit_ns,observed_complete_ns). Compare later rendered frame submissions "
                  "against preceding timewarp intervals. Fresh comparisons require valid status in both saved-render "
                  "and warp predictions. Retargeting is counted from actual late_target flags.",
        "interpretation": "Overlap and a later render starting before an earlier warp completes demonstrate "
                          "independent asynchronous stages. Newer warp source sequences show a fresh state lookup; "
                          "unchanged source sequences reused by consecutive valid renders show no requirement to wait "
                          "for a new state. Zero occurrences of a branch are not evidence that the branch is untested "
                          "elsewhere. No shader execution or GPU resource contention is measured.",
        "cases": cases,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n")
    for case in cases:
        print(json.dumps({key: value for key, value in case.items() if not isinstance(value, list)}))
    print(f"Saved {output}")


if __name__ == "__main__":
    main()
