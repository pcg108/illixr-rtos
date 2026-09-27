#!/usr/bin/env python3
"""Validate a bounded ILLIXR Spike trace, optionally against exact native replay."""
import argparse
import bisect
import csv
import hashlib
import json
import math
from pathlib import Path
import sys


def records(path):
    parsed = {key: [] for key in ("events", "poses", "probes", "summaries", "delays", "clocks",
              "diagnostics", "propagation", "placements", "platforms", "gpu_events", "gpu_results", "predictions",
              "prediction_placements", "prediction_summaries", "warp_slots", "displays")}
    for number, raw in enumerate(Path(path).read_text(errors="replace").splitlines(), 1):
        for prefix, key in (("ILLIXR_TRACE ", "events"), ("ILLIXR_POSE ", "poses"),
                            ("ILLIXR_PROBE ", "probes"), ("ILLIXR_RESULT ", "summaries"),
                            ("ILLIXR_DELAY ", "delays"), ("ILLIXR_CLOCK ", "clocks"),
                            ("ILLIXR_DIAGNOSTIC ", "diagnostics"), ("ILLIXR_PROPAGATION ", "propagation"),
                            ("ILLIXR_PLACEMENT ", "placements"), ("ILLIXR_PLATFORM ", "platforms"),
                            ("ILLIXR_DISPLAY ", "displays"), ("ILLIXR_WARP_SLOT ", "warp_slots"),
                            ("ILLIXR_GPU_EVENT ", "gpu_events"), ("ILLIXR_GPU_RESULT ", "gpu_results"),
                            ("ILLIXR_PREDICTION ", "predictions"),
                            ("ILLIXR_PREDICTION_PLACEMENT ", "prediction_placements"),
                            ("ILLIXR_PREDICTION_SUMMARY ", "prediction_summaries")):
            if prefix not in raw:
                continue
            line = raw.split(prefix, 1)[1].strip()
            try:
                if key == "events":
                    values = line.split()
                    if values[0] not in ("IMU", "CAM"):
                        raise ValueError("unknown estimator event")
                    parsed[key].append((values[0], int(values[1])))
                elif key == "poses":
                    values = line.split()
                    if len(values) != 9:
                        raise ValueError("expected index, timestamp and seven pose values")
                    parsed[key].append({"index": int(values[0]), "timestamp_ns": int(values[1]),
                                        "p": list(map(float, values[2:5])), "q": list(map(float, values[5:9]))})
                elif key == "probes":
                    values = list(map(int, line.split()))
                    if len(values) != 6:
                        raise ValueError("expected runtime, slow seq/time, fast seq/time, hart")
                    parsed[key].append(dict(zip(("runtime_ns", "slow_seq", "slow_ts", "fast_seq", "fast_ts", "hart"), values)))
                elif key == "diagnostics":
                    parsed[key].append(line)
                elif key == "delays":
                    values = line.split()
                    if len(values) != 5 or values[0] not in ("BEGIN", "END"):
                        raise ValueError("expected BEGIN/END runtime and three publication/read counts")
                    parsed[key].append(dict(zip(("kind", "runtime_ns", "imu_published", "cam_published", "probe_reads"),
                                               [values[0]] + list(map(int, values[1:])))))
                else:
                    parsed[key].append(json.loads(line))
            except (ValueError, IndexError, json.JSONDecodeError) as error:
                raise ValueError(f"{path}:{number}: invalid {prefix.strip()} record: {error}") from error
            break
    return parsed


def trajectory_sanity(poses, dataset):
    """Report frame-invariant motion magnitudes; this is not trajectory alignment."""
    path = Path(dataset) / "state_groundtruth_estimate0/data.csv"
    if not path.exists():
        return {"available": False, "reason": "ground-truth CSV is absent"}
    rows = []
    with path.open() as stream:
        for row in csv.reader(stream):
            if row and not row[0].startswith("#"):
                rows.append((int(row[0]), list(map(float, row[1:4]))))
    timestamps = [row[0] for row in rows]
    pairs = []
    for pose in poses:
        if not all(map(math.isfinite, pose["p"])) or not timestamps or not timestamps[0] <= pose["timestamp_ns"] <= timestamps[-1]:
            continue
        index = bisect.bisect_left(timestamps, pose["timestamp_ns"])
        candidates = [i for i in (index - 1, index) if 0 <= i < len(rows)]
        nearest = min(candidates, key=lambda i: abs(timestamps[i] - pose["timestamp_ns"]))
        if abs(timestamps[nearest] - pose["timestamp_ns"]) <= 2500000:
            pairs.append((pose, nearest))
    result = {"available": len(pairs) >= 2, "ground_truth_csv": str(path.resolve()),
              "ground_truth_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "matching_pose_count": len(pairs), "comparison": "endpoint displacement magnitudes; no coordinate alignment or accuracy threshold"}
    if len(pairs) < 2:
        result["reason"] = "fewer than two finite poses overlap ground truth within 2.5 ms"
        return result
    first, last = pairs[0], pairs[-1]
    estimate = math.dist(first[0]["p"], last[0]["p"])
    truth = math.dist(rows[first[1]][1], rows[last[1]][1])
    result.update({"start_camera_index": first[0]["index"], "end_camera_index": last[0]["index"],
        "start_timestamp_ns": first[0]["timestamp_ns"], "end_timestamp_ns": last[0]["timestamp_ns"],
        "ground_truth_start_index": first[1], "ground_truth_end_index": last[1],
        "ground_truth_start_timestamp_ns": rows[first[1]][0], "ground_truth_end_timestamp_ns": rows[last[1]][0],
        "estimated_displacement_m": estimate, "ground_truth_displacement_m": truth,
        "displacement_magnitude_difference_m": abs(estimate - truth),
        "estimated_path_length_m": sum(math.dist(a[0]["p"], b[0]["p"]) for a, b in zip(pairs, pairs[1:])),
        "ground_truth_path_length_m": sum(math.dist(a[1], b[1]) for a, b in zip(rows[first[1]:last[1]], rows[first[1]+1:last[1]+1]))})
    return result


def startup_checks(data, harts=None, expected_timer_hz=None, expected_core_hz=None, require_platform=False):
    errors = []
    clocks = data["clocks"]
    if len(clocks) != 1:
        errors.append("Expected exactly one clock/publication check result")
    else:
        clock = clocks[0]
        expected_exchanges = 1024 * max(2, harts or 2)
        if not clock.get("monotonic") or clock.get("atomic_exchanges") != expected_exchanges or clock.get("timer_hz", 0) <= 0:
            errors.append("Clock/publication startup check did not pass")
        if harts is not None and clock.get("hart_mask") != (1 << harts) - 1:
            errors.append("Clock test did not execute on all requested harts")
        if expected_timer_hz is not None and clock.get("timer_hz") != expected_timer_hz:
            errors.append("Clock timer frequency differs from generated hardware")
    platforms = data["platforms"]
    if require_platform or platforms:
        if len(platforms) != 1:
            errors.append("Expected exactly one platform timer check result")
        else:
            platform = platforms[0]
            if platform.get("status") not in ("ok", "pass", "passed", "success"):
                errors.append("Platform timer progression check failed")
            if harts is not None and platform.get("online_harts") != harts:
                errors.append("Platform check online hart count differs from hardware")
            for key, expected in (("timer_hz", expected_timer_hz), ("core_hz", expected_core_hz)):
                if expected is not None and platform.get(key) != expected:
                    errors.append(f"Platform {key} differs from generated hardware")
            ticks, cycles = platform.get("elapsed_timer_ticks"), platform.get("elapsed_core_cycles")
            timer_hz, core_hz = platform.get("timer_hz"), platform.get("core_hz")
            strict_ratio = require_platform or expected_core_hz is not None
            ratio_checked = platform.get("ratio_checked", False)
            if strict_ratio and ratio_checked is not True:
                errors.append("Hardware validation requires measured timer/core frequency agreement")
            if not all(type(value) is int and value > 0 for value in (ticks, cycles, timer_hz)):
                errors.append("Platform timer/core counters did not advance")
            elif ticks < timer_hz // 100:
                errors.append("Platform timer progression interval is shorter than 10 ms")
            elif ratio_checked or strict_ratio:
                if type(core_hz) is not int or core_hz <= 0:
                    errors.append("Platform core frequency is missing for the hardware ratio check")
                else:
                    # The target busy-spins for 10 ms, avoiding WFI clock gating.
                    # Reject carrying Spike's 10 MHz into Rocket while allowing
                    # bounded counter-read and interrupt overhead.
                    relative_error = abs(cycles / ticks - core_hz / timer_hz) / (core_hz / timer_hz)
                    if relative_error > 0.05:
                        errors.append("Measured timer/core progression does not match hardware frequencies")
    return errors


def placement_checks(data, summary, harts, mode):
    errors = []
    plugins = ("offline_imu", "offline_cam", "openvins", "imu_integrator")
    if data["gpu_results"]:
        plugins += ("render_loop", "timewarp")
    placements = data["placements"]
    result = {"mode": mode, "plugins": placements}
    if harts not in (1, 2, 4):
        return result, ["Placement validation requires one, two or four harts"]
    names = [p.get("plugin") for p in placements]
    if len(names) != len(plugins) or set(names) != set(plugins):
        return result, ["Expected exactly one placement record for each active plugin"]
    pinned = {1: (0, 0, 0, 0), 2: (0, 0, 1, 0), 4: (0, 1, 2, 3)}[harts]
    expected_work = {"offline_imu": summary.get("imu_published"),
        "offline_cam": summary.get("cam_published", 0) + summary.get("cam_dropped", 0),
        "openvins": summary.get("imu_processed", 0) + summary.get("cam_processed", 0),
        "imu_integrator": summary.get("imu_integrator_processed")}
    expected_publications = {"offline_imu": summary.get("imu_published"),
        "offline_cam": summary.get("cam_published"), "openvins": len(data["poses"])}
    if data["gpu_results"]:
        gpu = data["gpu_results"][0]
        expected_work.update(render_loop=gpu.get("render_submitted"), timewarp=gpu.get("timewarp_submitted"))
        expected_publications.update(render_loop=gpu.get("render_completed"), timewarp=gpu.get("timewarp_completed"))
    union_mask = 0
    for placement in placements:
        plugin = placement["plugin"]
        requested = placement.get("requested_hart")
        # New GPU workers intentionally remain scheduler managed. Existing
        # baseline plugin mappings retain their historical pinning semantics.
        expected_hart = pinned[plugins.index(plugin)] if mode == "pinned" and plugin in plugins[:4] else -1
        if mode is not None and requested != expected_hart:
            errors.append(f"{plugin}: requested affinity differs from {mode} mapping")
        if type(requested) is not int or requested < -1 or requested >= harts:
            errors.append(f"{plugin}: invalid requested hart")
        work, publications = placement.get("work_counts"), placement.get("publication_counts")
        if not all(isinstance(values, list) and len(values) == harts and
                   all(type(value) is int and value >= 0 for value in values) for values in (work, publications)):
            errors.append(f"{plugin}: invalid per-hart counters")
            continue
        observed_mask = sum(1 << hart for hart in range(harts) if work[hart] or publications[hart])
        if placement.get("hart_mask") != observed_mask or not observed_mask:
            errors.append(f"{plugin}: hart mask does not match actual processing/publication evidence")
        if sum(work) != expected_work[plugin]:
            errors.append(f"{plugin}: work counts do not match consumed/published samples")
        if plugin in expected_publications and sum(publications) != expected_publications[plugin]:
            errors.append(f"{plugin}: publication counts do not match output evidence")
        if plugin == "imu_integrator" and not sum(publications):
            errors.append("imu_integrator: no propagated-pose publication placement evidence")
        if type(requested) is int and requested >= 0 and observed_mask != (1 << requested):
            errors.append(f"{plugin}: actual work escaped the requested affinity")
        union_mask |= observed_mask
    result["observed_plugin_hart_mask"] = union_mask
    if mode == "pinned" and union_mask != (1 << harts) - 1:
        errors.append("Pinned plugin processing did not cover all assigned harts")
    return result, errors


def prediction_checks(data, gpu, events, harts, errors):
    result = {}
    result["prediction_calls"] = len(data["predictions"])
    if len(data["predictions"]) != gpu["render_submitted"] + gpu["timewarp_submitted"]:
        errors.append("Prediction call trace does not account for every render and timewarp submission")
    prediction_summaries = data["prediction_summaries"]
    if len(prediction_summaries) != 1 or any(prediction_summaries[0].get(key) != value for key, value in
            (("calls", len(data["predictions"])), ("overflow", 0), ("invalid", 0), ("max_horizon_ns", 50_000_000))):
        errors.append("Prediction summary is absent or reports invalid/incomplete calls")
    placements = data["prediction_placements"]
    if len(placements) != 2 or {entry.get("caller") for entry in placements} != {0, 1}:
        errors.append("Expected prediction processing/publication placement for both callers")
    else:
        for placement in placements:
            caller = placement["caller"]
            stage = ("render", "timewarp")[caller]
            calls = [entry for entry in data["predictions"] if entry.get("caller") == caller]
            work, publications = [0] * harts, [0] * harts
            for call in calls:
                processing, publication = call.get("processing_hart"), call.get("publication_hart")
                if (type(processing) is not int or type(publication) is not int or
                        not 0 <= processing < harts or not 0 <= publication < harts):
                    errors.append(f"{stage}: invalid predictor hart evidence")
                    continue
                work[processing] += 1
                publications[publication] += 1
            mask = sum(1 << hart for hart in range(harts) if work[hart] or publications[hart])
            if (len(calls) != len(events[stage]) or placement.get("work_counts") != work or
                    placement.get("publication_counts") != publications or placement.get("hart_mask") != mask or not mask):
                errors.append(f"{stage}: predictor placement differs from actual prediction calls")
            for call, event in zip(calls, events[stage]):
                expected_status = {"valid": 0, "fallback": 1, "stale": 2, "invalid": 3}.get(event.get("prediction_status"))
                if any(call.get(key) != expected for key, expected in (("source_ns", event.get("source_ns")),
                        ("source_seq", event.get("source_sequence")), ("target_ns", event.get("target_ns")),
                        ("horizon_ns", event.get("prediction_horizon_ns")), ("status", expected_status))):
                    errors.append(f"{stage}: frame prediction differs from service trace")
    result["prediction_placement"] = placements
    return result


def gpu_checks(data, summary, harts):
    """Check asynchronous frame evidence; modeled latency is not measured GPU performance."""
    errors = []
    if len(data["gpu_results"]) != 1:
        return {}, ["Expected exactly one GPU pipeline result"]
    gpu = data["gpu_results"][0]
    if gpu.get("version", 1) == 2:
        from gpu_trace_v2 import check
        try:
            return check(data, summary, harts, prediction_checks)
        except (KeyError, TypeError, ValueError, IndexError) as error:
            return {"summary": gpu}, [f"Malformed GPU v2 trace: {error}"]
    if gpu.get("version", 1) != 1:
        return {"summary": gpu}, ["Unsupported GPU trace version"]
    counts = ("render_submitted", "render_completed", "render_skipped_slots", "timewarp_submitted",
              "timewarp_completed", "mailbox_replaced", "render_deadlines_missed",
              "timewarp_deadlines_missed", "fresh_warp_completed")
    if any(type(gpu.get(key)) is not int or gpu[key] < 0 for key in counts):
        return {"summary": gpu}, ["GPU summary has missing or invalid counters"]
    if gpu.get("trace_overflow", 0) != 0:
        errors.append("GPU trace overflowed")
    if gpu.get("render_closed") is not True or gpu.get("timewarp_done") is not True or gpu.get("mailbox_pending") is not False:
        errors.append("GPU pipeline did not drain and shut down")
    for stage in ("render", "timewarp"):
        if gpu[stage + "_submitted"] != gpu[stage + "_completed"]:
            errors.append(f"{stage}: submitted work did not complete")
        if type(gpu.get(stage + "_delay_ns")) is not int or gpu[stage + "_delay_ns"] <= 0:
            errors.append(f"{stage}: invalid configured asynchronous delay")
    if type(gpu.get("period_ns")) is not int or gpu["period_ns"] <= 0:
        errors.append("GPU display period is invalid")
    if gpu["render_completed"] != gpu["timewarp_completed"] + gpu["mailbox_replaced"]:
        errors.append("Completed render frames are not accounted for by warp completion or mailbox replacement")
    events = {stage: [event for event in data["gpu_events"] if event.get("stage") == stage]
              for stage in ("render", "timewarp")}
    if sum(map(len, events.values())) != len(data["gpu_events"]):
        errors.append("Unknown GPU trace stage")
    result = {"summary": gpu, "stages": {}, "model": "asynchronous fixed elapsed target-time delays; no shader execution or GPU contention"}
    origin = summary.get("origin_ns", 0)
    render_frames = {}
    fresh = 0
    for stage, items in events.items():
        if len(items) != gpu[stage + "_completed"]:
            errors.append(f"{stage}: completion trace count differs from summary")
        previous_id = previous_sequence = previous_submit = -1
        lateness, original_lateness, horizons, source_ages, durations, wakeup_delays, observed_harts = [], [], [], [], [], [], set()
        retargeted = retargeted_misses = 0
        statuses = {name: 0 for name in ("valid", "fallback", "stale", "invalid")}
        for event in items:
            identifier = event.get("frame_id")
            integer_fields = ("frame_id", "slot", "submit_ns", "scheduled_complete_ns", "observed_complete_ns",
                              "presentation_ns", "target_ns", "source_ns", "source_sequence", "prediction_horizon_ns", "hart")
            if any(type(event.get(field)) is not int for field in integer_fields):
                errors.append(f"{stage}: missing or noninteger frame metadata")
                continue
            submit, scheduled, observed = (event[key] for key in ("submit_ns", "scheduled_complete_ns", "observed_complete_ns"))
            sequence = event["source_sequence"]
            if identifier <= previous_id or submit < previous_submit or sequence < previous_sequence:
                errors.append(f"{stage}: frame IDs, source sequences, or submission times moved backwards")
            previous_id, previous_sequence, previous_submit = identifier, sequence, submit
            if submit < 0 or scheduled - submit != gpu.get(stage + "_delay_ns") or observed < scheduled:
                errors.append(f"{stage} frame {identifier}: asynchronous completion occurred early or has the wrong scheduled delay")
            if isinstance(summary.get("runtime_ns"), int) and observed > summary["runtime_ns"]:
                errors.append(f"{stage} frame {identifier}: completion exceeds final runtime")
            if not 0 <= event["hart"] < harts:
                errors.append(f"{stage}: invalid processing hart")
            observed_harts.add(event["hart"])
            status = event.get("prediction_status")
            if status not in statuses:
                errors.append(f"{stage}: invalid prediction status")
            else:
                statuses[status] += 1
                if status == "invalid":
                    errors.append(f"{stage}: invalid prediction was used for GPU work")
            values = event.get("position", []) + event.get("orientation", [])
            if len(values) != 7 or not all(isinstance(x, (int, float)) and math.isfinite(x) for x in values):
                errors.append(f"{stage}: nonfinite or missing predicted pose")
            elif abs(math.sqrt(sum(x*x for x in event["orientation"])) - 1) > 1e-5:
                errors.append(f"{stage}: unnormalized predicted quaternion")
            if status == "valid" and (sequence <= 0 or not 0 <= event["prediction_horizon_ns"] <= 50_000_000):
                errors.append(f"{stage}: prediction marked valid outside the accepted horizon")
            if status in ("valid", "stale") and event["prediction_horizon_ns"] != event["target_ns"] - event["source_ns"]:
                errors.append(f"{stage}: prediction horizon does not match source and target timestamps")
            horizons.append(event["prediction_horizon_ns"])
            if sequence:
                source_ages.append(origin + submit - event["source_ns"])
            durations.append(observed - submit)
            wakeup_delays.append(observed - scheduled)
            deadline = event["presentation_ns"] if stage == "render" else event["target_ns"] - origin
            lateness.append(max(0, observed - deadline))
            original_lateness.append(max(0, observed - event["presentation_ns"]))
            if stage == "render":
                render_frames[identifier] = event
            else:
                late_target = event.get("late_target")
                if (type(late_target) is not bool or
                        (late_target and deadline <= event["presentation_ns"]) or
                        (not late_target and deadline != event["presentation_ns"])):
                    errors.append(f"timewarp frame {identifier}: retargeting flag differs from selected presentation target")
                if late_target is True:
                    retargeted += 1
                    retargeted_misses += observed > deadline
                rendered = render_frames.get(identifier)
                if rendered is None or submit < rendered["observed_complete_ns"]:
                    errors.append(f"timewarp frame {identifier}: missing completed render dependency")
                elif event.get("render_source_sequence") != rendered["source_sequence"] or event.get("render_prediction_status") != rendered["prediction_status"]:
                    errors.append(f"timewarp frame {identifier}: saved render prediction does not match its frame")
                elif (event.get("render_orientation") != rendered["orientation"] or
                      event["slot"] != rendered["slot"] or event["presentation_ns"] != rendered["presentation_ns"]):
                    errors.append(f"timewarp frame {identifier}: saved frame orientation or deadline changed")
                matrix = event.get("transform", [])
                if len(matrix) != 16 or not all(isinstance(x, (int, float)) and math.isfinite(x) for x in matrix):
                    errors.append(f"timewarp frame {identifier}: invalid rotational transform")
                if status == "valid" and event.get("render_prediction_status") == "valid":
                    fresh += 1
        if sum(value > 0 for value in lateness) != gpu[stage + "_deadlines_missed"]:
            errors.append(f"{stage}: deadline misses differ from trace evidence")
        if stage == "render" and items:
            if [event.get("frame_id") for event in items] != list(range(1, len(items) + 1)):
                errors.append("Render frame IDs do not account for every submission")
            slots = [event.get("slot") for event in items]
            if (any(type(slot) is not int for slot in slots) or
                    any(new <= old for old, new in zip(slots, slots[1:])) or
                    slots[-1] + 1 != len(items) + gpu["render_skipped_slots"]):
                errors.append("Render slot sequence does not account for skipped display deadlines")
        result["stages"][stage] = {"completed": len(items), "prediction_status_counts": statuses,
            "observed_harts": sorted(observed_harts), "maximum_horizon_ns": max(horizons, default=0),
            "maximum_source_age_ns": max(source_ages, default=0), "maximum_deadline_lateness_ns": max(lateness, default=0),
            "minimum_observed_delay_ns": min(durations, default=0), "maximum_observed_delay_ns": max(durations, default=0),
            "maximum_wakeup_delay_ns": max(wakeup_delays, default=0),
            "original_presentation_deadlines_missed": sum(value > 0 for value in original_lateness),
            "selected_target_deadlines_missed": sum(value > 0 for value in lateness),
            "maximum_original_presentation_lateness_ns": max(original_lateness, default=0),
            "retargeted_frames": retargeted, "retargeted_target_deadlines_missed": retargeted_misses}
    if fresh != gpu["fresh_warp_completed"] or fresh < 1:
        errors.append("GPU pipeline lacks a matching non-stale predicted render/timewarp completion")
    result["fresh_warp_completed"] = fresh
    result.update(prediction_checks(data, gpu, events, harts, errors))
    runtime = summary.get("runtime_ns", 0)
    if isinstance(runtime, int) and runtime > 0:
        for stage in ("render", "timewarp"):
            result["stages"][stage]["completed_frames_per_target_second"] = len(events[stage]) * 1e9 / runtime
    return result, errors


def analyze(log, native=None, harts=None, require_initialized=False, require_async=False,
            expect_failure=None, require_camera_drop=False, require_delay=False, dataset=None, dataset_manifest=None,
            placement=None, expected_timer_hz=None, expected_core_hz=None, require_platform=False, require_gpu=False):
    result = {"passed": False, "errors": [], "warnings": []}
    errors = result["errors"]
    data = records(log)
    if len(data["summaries"]) != 1:
        errors.append(f"Expected one final result, found {len(data['summaries'])}")
        return result
    summary = data["summaries"][0]
    result["summary"] = summary
    status = summary.get("status")
    result["clock"] = data["clocks"]
    result["platform"] = data["platforms"]
    errors.extend(startup_checks(data, harts or summary.get("online_harts"), expected_timer_hz,
                                 expected_core_hz, require_platform))
    if expect_failure:
        if status in ("ok", "pass", "passed", "success"):
            errors.append("Expected a deliberate failure, but the target reported success")
        if expect_failure == "imu_overflow" and summary.get("imu_overflow", 0) <= 0:
            errors.append("Expected a counted IMU overflow")
        if expect_failure == "imu_overflow" and "imu_queue_overflow" not in data["diagnostics"]:
            errors.append("Expected explicit imu_queue_overflow diagnostic")
        result["passed"] = not errors
        result["expected_failure"] = expect_failure
        return result
    if status not in ("ok", "pass", "passed", "success"):
        errors.append(f"Target status is {status!r}: {summary.get('reason', '')}")
    for field in ("imu_overflow", "trace_overflow"):
        if summary.get(field) != 0:
            errors.append(f"{field} must be present and zero, got {summary.get(field)!r}")
    if harts is not None and summary.get("online_harts") != harts:
        errors.append(f"Expected {harts} online harts, got {summary.get('online_harts')!r}")
    if "clock_test_passed" in summary and not summary["clock_test_passed"]:
        errors.append("Cross-hart clock/publication check failed")
    events = data["events"]
    imu_indices = [index for kind, index in events if kind == "IMU"]
    camera_indices = [index for kind, index in events if kind == "CAM"]
    if imu_indices != list(range(len(imu_indices))):
        errors.append("Estimator IMU indices are not contiguous from zero")
    if not camera_indices or any(b <= a for a, b in zip(camera_indices, camera_indices[1:])):
        errors.append("Camera indices are missing or not strictly increasing")
    for field, count in (("imu_processed", len(imu_indices)), ("cam_processed", len(camera_indices))):
        if summary.get(field) != count:
            errors.append(f"{field}={summary.get(field)!r} differs from trace count {count}")
    if summary.get("imu_published") != len(imu_indices):
        errors.append("Published IMUs were not fully consumed by VIO")
    if summary.get("cam_published") != len(camera_indices):
        errors.append("Published camera frames were not fully consumed by VIO")
    if "imu_integrator_processed" in summary and summary["imu_integrator_processed"] != len(imu_indices):
        errors.append("Published IMUs were not fully consumed by the integrator")
    manifest_path = Path(dataset_manifest) if dataset_manifest else Path(log).parent / "dataset_manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        camera_total, imu_total = manifest.get("camera_pairs"), manifest.get("imu_samples")
        counts = {key: summary.get(key) for key in ("cam_published", "cam_skipped", "cam_dropped")}
        valid_counts = all(type(value) is int and value >= 0 for value in counts.values())
        accounted_cameras = sum(counts.values()) if valid_counts else None
        result["dataset_accounting"] = {"manifest": str(manifest_path.resolve()),
            "expected_camera_pairs": camera_total, "accounted_camera_pairs": accounted_cameras,
            "expected_imu_samples": imu_total, "published_imu_samples": summary.get("imu_published"), **counts}
        if type(camera_total) is not int or camera_total <= 0 or accounted_cameras != camera_total:
            errors.append("Published, skipped and dropped cameras do not account for the embedded dataset")
        if type(imu_total) is not int or imu_total <= 0 or summary.get("imu_published") != imu_total:
            errors.append("Published IMUs do not account for every embedded dataset sample")
        if isinstance(camera_total, int) and any(index < 0 or index >= camera_total for index in camera_indices):
            errors.append("Estimator camera index is outside the embedded dataset")
        if summary.get("origin_ns") != manifest.get("dataset_origin_ns"):
            errors.append("Runtime dataset origin differs from the embedded dataset manifest")
    if len(data["propagation"]) > 1:
        errors.append("Duplicate propagation diagnostic records")
    if data["propagation"]:
        diagnostic = data["propagation"][0]
        norm = diagnostic.get("max_observed_position_norm_m")
        if not isinstance(norm, (int, float)) or not math.isfinite(norm) or norm < 0:
            errors.append("Invalid propagated-pose diagnostic norm")
            diagnostic = {**diagnostic, "max_observed_position_norm_m": None}
        result["propagation"] = {**diagnostic, "diagnostic_only": True,
            "interpretation": "Observed propagated-pose magnitude, separate from VIO output; no physical-accuracy threshold applied"}
    if require_camera_drop and summary.get("cam_dropped", 0) <= 0:
        errors.append("Expected camera overload did not produce a counted drop")
    poses = data["poses"]
    if require_initialized and (not summary.get("initialized") or not poses):
        errors.append("Extended run did not publish an initialized VIO pose")
    for previous, current in zip(poses, poses[1:]):
        if current["timestamp_ns"] < previous["timestamp_ns"] or current["index"] <= previous["index"]:
            errors.append("Pose timestamps/indices moved backwards")
            break
    for pose in poses:
        values = pose["p"] + pose["q"]
        if not all(map(math.isfinite, values)):
            errors.append(f"Non-finite VIO pose at camera {pose['index']}")
        elif abs(math.sqrt(sum(x*x for x in pose["q"])) - 1) > 1e-5:
            errors.append(f"Unnormalized VIO quaternion at camera {pose['index']}")
    finite_poses = [p for p in poses if all(map(math.isfinite, p["p"] + p["q"]))]
    if finite_poses:
        result["pose_count"] = len(poses)
        result["displacement_m"] = math.dist(finite_poses[0]["p"], finite_poses[-1]["p"])
        result["max_position_norm_m"] = max(math.sqrt(sum(x*x for x in p["p"])) for p in finite_poses)
    probes = data["probes"]
    repeated = advanced = 0
    max_age = 0
    for old, new in zip(probes, probes[1:]):
        if new["runtime_ns"] < old["runtime_ns"]:
            errors.append("Consumer probe clock moved backwards")
        for prefix in ("slow", "fast"):
            if new[prefix + "_seq"] < old[prefix + "_seq"] or new[prefix + "_ts"] < old[prefix + "_ts"]:
                errors.append(f"Latest {prefix} state moved backwards")
        if old["slow_seq"] > 0 and old["slow_seq"] == new["slow_seq"]:
            repeated += 1
            if new["fast_seq"] > old["fast_seq"]:
                advanced += 1
    if "origin_ns" in summary:
        ages = [summary["origin_ns"] + p["runtime_ns"] - p["slow_ts"] for p in probes if p["slow_seq"]]
        max_age = max(ages, default=0)
    result["consumer"] = {"recorded_reads": len(probes), "repeated_vio_pairs": repeated,
                          "imu_advanced_on_repeated_vio_pairs": advanced,
                          "maximum_vio_age_ns": max_age,
                          "observed_harts": sorted({p["hart"] for p in probes})}
    if require_async:
        if not probes or repeated == 0:
            errors.append("No probe evidence of independent reads between VIO publications")
        if advanced == 0:
            errors.append("No probe evidence of advancing IMU state with an unchanged VIO pose")
    delay_results = []
    delay_begin = None
    for delay in data["delays"]:
        if delay["kind"] == "BEGIN":
            if delay_begin is not None:
                errors.append("Nested VIO delay records")
            delay_begin = delay
        elif delay_begin is None:
            errors.append("VIO delay ended without beginning")
        else:
            elapsed = delay["runtime_ns"] - delay_begin["runtime_ns"]
            counters = {key: delay[key] - delay_begin[key] for key in ("imu_published", "cam_published", "probe_reads")}
            delay_results.append({"duration_ns": elapsed, **counters})
            if elapsed <= 0 or counters["imu_published"] <= 0 or counters["probe_reads"] <= 0:
                errors.append("Sensor publication and consumer reads did not continue during VIO delay")
            delay_begin = None
    if delay_begin is not None:
        errors.append("Missing VIO delay end record")
    if require_delay and not delay_results:
        errors.append("Expected VIO delay evidence was absent")
    result["delays"] = delay_results
    if native:
        native_data = records(native)
        if native_data["events"] != events:
            errors.append("Native estimator input sequence differs from the target sequence")
        target_map = {p["index"]: p for p in poses}
        native_map = {p["index"]: p for p in native_data["poses"]}
        if target_map.keys() != native_map.keys():
            errors.append("Native and target initialized pose indices differ")
        comparisons = []
        for index in sorted(target_map.keys() & native_map.keys()):
            a, b = target_map[index], native_map[index]
            if a["timestamp_ns"] != b["timestamp_ns"]:
                errors.append(f"Native pose timestamp differs at camera {index}")
            distance = math.dist(a["p"], b["p"])
            norm = math.sqrt(sum(x*x for x in a["q"]) * sum(x*x for x in b["q"]))
            angle = 2 * math.acos(min(1.0, abs(sum(x*y for x, y in zip(a["q"], b["q"]))) / norm)) if norm > 0 else math.inf
            comparisons.append({"index": index, "position_error_m": distance if math.isfinite(distance) else None,
                                "orientation_error_rad": angle if math.isfinite(angle) else None})
            if not math.isfinite(distance) or not math.isfinite(angle) or distance > 0.001 or angle > 0.001:
                errors.append(f"Native discrepancy at camera {index}: {distance:.9g} m, {angle:.9g} rad")
        result["native_comparison"] = {"matching_poses": len(comparisons),
            "max_position_error_m": max((r["position_error_m"] for r in comparisons if r["position_error_m"] is not None), default=None),
            "max_orientation_error_rad": max((r["orientation_error_rad"] for r in comparisons if r["orientation_error_rad"] is not None), default=None),
            "per_pose": comparisons}
    if placement is not None or data["placements"]:
        result["placement"], placement_errors = placement_checks(data, summary, harts or summary.get("online_harts"), placement)
        errors.extend(placement_errors)
    if "runtime_ns" in summary:
        runtime = summary["runtime_ns"]
        if type(runtime) is not int or runtime <= 0 or (probes and runtime < probes[-1]["runtime_ns"]):
            errors.append("Final runtime is invalid or earlier than the last probe")
        else:
            result["simulated_runtime_seconds"] = runtime / 1e9
    elif probes:
        result["simulated_runtime_seconds"] = probes[-1]["runtime_ns"] / 1e9
        result["simulated_runtime_source"] = "last probe; final runtime absent in legacy trace"
    if dataset:
        result["trajectory_sanity"] = trajectory_sanity(poses, dataset)
    if require_gpu or data["gpu_results"] or data["gpu_events"]:
        result["gpu_pipeline"], gpu_errors = gpu_checks(data, summary, harts or summary.get("online_harts", 1))
        errors.extend(gpu_errors)
    result["passed"] = not errors
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--native", type=Path)
    parser.add_argument("--dataset", type=Path, help="Optional EuRoC mav0 path for ground-truth motion report")
    parser.add_argument("--dataset-manifest", type=Path, help="Embedded dataset manifest; defaults to one beside the log")
    parser.add_argument("--harts", type=int, choices=(1, 2, 4))
    parser.add_argument("--require-initialized", action="store_true")
    parser.add_argument("--require-async", action="store_true")
    parser.add_argument("--require-camera-drop", action="store_true")
    parser.add_argument("--require-delay", action="store_true")
    parser.add_argument("--expect-failure", choices=("imu_overflow",))
    parser.add_argument("--placement", choices=("unpinned", "pinned"))
    parser.add_argument("--expected-timer-hz", type=int)
    parser.add_argument("--expected-core-hz", type=int)
    parser.add_argument("--require-platform", action="store_true")
    parser.add_argument("--require-gpu", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = analyze(args.log, args.native, args.harts, args.require_initialized,
                         args.require_async, args.expect_failure, args.require_camera_drop, args.require_delay, args.dataset, args.dataset_manifest,
                         args.placement, args.expected_timer_hz, args.expected_core_hz, args.require_platform, args.require_gpu)
    except (OSError, ValueError) as error:
        result = {"passed": False, "errors": [str(error)]}
    encoded = json.dumps(result, indent=2, allow_nan=False) + "\n"
    if args.output:
        args.output.write_text(encoded)
    print(encoded, end="")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
