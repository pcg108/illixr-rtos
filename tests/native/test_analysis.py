#!/usr/bin/env python3
"""Failure-oriented checks for trace evidence and simulator-process lifecycle."""
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
import unittest

spec = importlib.util.spec_from_file_location("analysis", Path(__file__).resolve().parents[2] / "scripts/analyze_spike.py")
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class TraceValidation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.log = Path(self.temp.name) / "console.log"
        self.summary = {"status": "pass", "imu_overflow": 0, "trace_overflow": 0,
                        "online_harts": 2, "origin_ns": 100, "initialized": True,
                        "imu_published": 2, "imu_processed": 2, "imu_integrator_processed": 2,
                        "cam_published": 1, "cam_processed": 1, "cam_dropped": 0}
        self.lines = ['ILLIXR_CLOCK {"hart_mask":3,"atomic_exchanges":2048,"timer_hz":10000000,"monotonic":true}',
                      "ILLIXR_TRACE IMU 0", "ILLIXR_TRACE IMU 1", "ILLIXR_TRACE CAM 0",
                      "ILLIXR_POSE 0 200 0 0 0 1 0 0 0",
                      "ILLIXR_PROBE 100 1 200 1 200 0", "ILLIXR_PROBE 110 1 200 2 210 1"]

    def write(self):
        self.log.write_text("\n".join(self.lines + ["ILLIXR_RESULT " + json.dumps(self.summary)]))

    def check(self):
        self.write()
        return analysis.analyze(self.log, harts=2, require_initialized=True, require_async=True)

    def test_valid_async_evidence(self):
        self.assertTrue(self.check()["passed"])

    def test_missing_final_result_fails(self):
        self.log.write_text("\n".join(self.lines))
        self.assertFalse(analysis.analyze(self.log)["passed"])

    def test_missing_imu_fails_even_with_matching_counts(self):
        self.lines[2] = "ILLIXR_TRACE IMU 2"
        self.assertFalse(self.check()["passed"])

    def test_shutdown_did_not_drain_fails(self):
        self.summary["imu_integrator_processed"] = 1
        self.assertFalse(self.check()["passed"])

    def test_no_initialized_pose_fails(self):
        self.summary["initialized"] = False
        self.assertFalse(self.check()["passed"])

    def test_quaternion_fails(self):
        self.lines[4] = "ILLIXR_POSE 0 200 0 0 0 2 0 0 0"
        self.assertFalse(self.check()["passed"])

    def test_no_async_advancement_fails(self):
        self.lines[-1] = "ILLIXR_PROBE 110 1 200 1 200 1"
        self.assertFalse(self.check()["passed"])

    def test_native_discrepancy_fails(self):
        self.write()
        native = Path(self.temp.name) / "native.log"
        native.write_text("\n".join(self.lines[:4] + ["ILLIXR_POSE 0 200 .002 0 0 1 0 0 0"]))
        self.assertFalse(analysis.analyze(self.log, native=native)["passed"])

    def test_quaternion_sign_is_equivalent(self):
        self.write()
        native = Path(self.temp.name) / "native.log"
        native.write_text("\n".join(self.lines[:4] + ["ILLIXR_POSE 0 200 0 0 0 -1 0 0 0"]))
        self.assertTrue(analysis.analyze(self.log, native=native)["passed"])

    def test_bad_clock_fails(self):
        self.lines[0] = 'ILLIXR_CLOCK {"hart_mask":1,"atomic_exchanges":2048,"timer_hz":10000000,"monotonic":true}'
        self.assertFalse(self.check()["passed"])

    def test_delay_independence(self):
        self.lines += ["ILLIXR_DELAY BEGIN 0 10 1 100", "ILLIXR_DELAY END 250000000 60 6 130"]
        self.assertTrue(self.check()["passed"])
        self.lines[-1] = "ILLIXR_DELAY END 250000000 10 1 100"
        self.assertFalse(self.check()["passed"])

    def test_nonfinite_pose_fails_serializably(self):
        self.lines[4] = "ILLIXR_POSE 0 200 nan 0 0 1 0 0 0"
        result = self.check()
        self.assertFalse(result["passed"])
        json.dumps(result, allow_nan=False)

    def test_camera_dataset_accounting(self):
        manifest = self.log.parent / "dataset_manifest.json"
        manifest.write_text(json.dumps({"camera_pairs": 3, "imu_samples": 2, "dataset_origin_ns": 100}))
        self.summary.update(cam_skipped=1, cam_dropped=1)
        self.assertTrue(self.check()["passed"])
        self.summary["cam_dropped"] = 0
        self.assertFalse(self.check()["passed"])

    def test_propagation_norm_is_diagnostic_without_radius_gate(self):
        self.lines += ['ILLIXR_PROPAGATION {"max_observed_position_norm_m":39.199956780552988}']
        result = self.check()
        self.assertTrue(result["passed"])
        self.assertTrue(result["propagation"]["diagnostic_only"])
        self.lines[-1] = 'ILLIXR_PROPAGATION {"max_observed_position_norm_m":NaN}'
        result = self.check()
        self.assertFalse(result["passed"])
        json.dumps(result, allow_nan=False)

    def test_new_spike_platform_without_core_frequency(self):
        self.lines += ['ILLIXR_PLATFORM {"status":"pass","online_harts":2,"timer_hz":10000000,"core_hz":0,"elapsed_timer_ticks":100000,"elapsed_core_cycles":1000,"ratio_checked":false}']
        self.assertTrue(self.check()["passed"])

    def test_expected_overflow_must_be_counted(self):
        self.summary["status"] = "fail"
        self.write()
        self.assertFalse(analysis.analyze(self.log, expect_failure="imu_overflow")["passed"])
        self.summary["imu_overflow"] = 1
        self.lines += ["ILLIXR_DIAGNOSTIC imu_queue_overflow"]
        self.write()
        self.assertTrue(analysis.analyze(self.log, expect_failure="imu_overflow")["passed"])


class RocketValidation(unittest.TestCase):
    def setUp(self):
        TraceValidation.setUp(self)
        self.summary.update(online_harts=4, runtime_ns=100000000, cam_skipped=0)
        self.lines[0] = 'ILLIXR_CLOCK {"hart_mask":15,"atomic_exchanges":4096,"timer_hz":500000,"monotonic":true}'
        self.lines += ['ILLIXR_PLATFORM {"status":"pass","online_harts":4,"timer_hz":500000,"core_hz":500000000,"elapsed_timer_ticks":5000,"elapsed_core_cycles":5000000,"ratio_checked":true}']
        self.placements = [
            {"plugin": "offline_imu", "requested_hart": 0, "hart_mask": 1, "work_counts": [2,0,0,0], "publication_counts": [2,0,0,0]},
            {"plugin": "offline_cam", "requested_hart": 1, "hart_mask": 2, "work_counts": [0,1,0,0], "publication_counts": [0,1,0,0]},
            {"plugin": "openvins", "requested_hart": 2, "hart_mask": 4, "work_counts": [0,0,3,0], "publication_counts": [0,0,1,0]},
            {"plugin": "imu_integrator", "requested_hart": 3, "hart_mask": 8, "work_counts": [0,0,0,2], "publication_counts": [0,0,0,2]},
        ]

    def write(self):
        self.log.write_text("\n".join(self.lines + ["ILLIXR_PLACEMENT " + json.dumps(p) for p in self.placements]
                                      + ["ILLIXR_RESULT " + json.dumps(self.summary)]))

    def check(self, mode="pinned"):
        self.write()
        return analysis.analyze(self.log, harts=4, require_initialized=True, require_async=True, placement=mode,
                                expected_timer_hz=500000, expected_core_hz=500000000, require_platform=True)

    def test_quad_harts_and_affinity(self):
        self.assertTrue(self.check()["passed"])

    def test_missing_placement_fails(self):
        self.placements.pop()
        self.assertFalse(self.check()["passed"])

    def test_duplicate_placement_fails(self):
        self.placements.append(self.placements[0])
        self.assertFalse(self.check()["passed"])

    def test_wrong_placement_mask_fails(self):
        self.placements[0]["hart_mask"] = 3
        self.assertFalse(self.check()["passed"])

    def test_affinity_escape_fails_even_with_consistent_counts(self):
        self.placements[0].update(hart_mask=3, work_counts=[1,1,0,0])
        self.assertFalse(self.check()["passed"])

    def test_unpinned_allows_observed_scheduler_placement(self):
        for placement in self.placements:
            placement["requested_hart"] = -1
        self.placements[0].update(hart_mask=3, work_counts=[1,1,0,0])
        result = self.check("unpinned")
        self.assertTrue(result["passed"], result["errors"])

    def test_affinity_must_match_requested_mode(self):
        self.assertFalse(self.check("unpinned")["passed"])

    def test_work_count_mismatch_fails(self):
        self.placements[2]["work_counts"][2] = 2
        self.assertFalse(self.check()["passed"])

    def test_publication_count_mismatch_fails(self):
        self.placements[0]["publication_counts"][0] = 1
        self.assertFalse(self.check()["passed"])

    def test_counter_length_mismatch_fails(self):
        self.placements[0]["work_counts"] = [2,0]
        self.assertFalse(self.check()["passed"])

    def test_invalid_requested_hart_fails(self):
        self.placements[0]["requested_hart"] = None
        self.assertFalse(self.check()["passed"])

    def test_spike_timer_frequency_fails_on_rocket(self):
        self.lines[0] = self.lines[0].replace("500000", "10000000")
        self.assertFalse(self.check()["passed"])

    def test_measured_timer_ratio_fails(self):
        self.lines[-1] = self.lines[-1].replace('"elapsed_core_cycles":5000000', '"elapsed_core_cycles":10000000')
        self.assertFalse(self.check()["passed"])

    def test_missing_platform_fails(self):
        self.lines.pop()
        self.assertFalse(self.check()["passed"])

    def test_rocket_cannot_skip_frequency_ratio(self):
        self.lines[-1] = self.lines[-1].replace('"ratio_checked":true', '"ratio_checked":false')
        self.assertFalse(self.check()["passed"])

    def test_missing_quad_clock_worker_fails(self):
        self.lines[0] = self.lines[0].replace('"atomic_exchanges":4096', '"atomic_exchanges":2048')
        self.assertFalse(self.check()["passed"])

    def test_runtime_earlier_than_probe_fails(self):
        self.summary["runtime_ns"] = 50
        self.assertFalse(self.check()["passed"])


sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import run_rocket


class RunnerLifecycle(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def capture(self, source, timeout=2):
        return run_rocket.capture([sys.executable, "-u", "-c", source], self.directory, timeout)

    def test_normal_exit_is_required(self):
        result = self.capture("print('ILLIXR_RESULT {}'); raise SystemExit(2)")
        self.assertTrue(run_rocket.execution_errors(result))
        self.assertFalse(result["timed_out"])

    def test_watchdog_after_final_marker_is_incomplete(self):
        result = self.capture("import time; print('ILLIXR_RESULT {}'); time.sleep(60)", .1)
        self.assertTrue(result["timed_out"])
        self.assertTrue(run_rocket.execution_errors(result))

    def test_fatal_aborts_owned_process(self):
        result = self.capture("import time; print('ZEPHYR FATAL ERROR'); time.sleep(60)")
        self.assertEqual(result["fatal_markers"], ["ZEPHYR FATAL ERROR"])
        self.assertFalse(result["timed_out"])
        self.assertTrue(run_rocket.execution_errors(result))

    def test_cycle_limit_failure_is_not_success(self):
        result = self.capture("print('*** FAILED *** (timeout) after 100000000001 simulation cycles')")
        self.assertTrue(run_rocket.execution_errors(result))
        self.assertTrue(result["cycle_limit_reached"])
        self.assertTrue(any("incomplete" in error for error in run_rocket.execution_errors(result)))

    def test_successful_process_retains_console(self):
        result = self.capture("print('complete')")
        self.assertFalse(run_rocket.execution_errors(result))
        self.assertEqual((self.directory / "console.log").read_text(), "complete\n")


if __name__ == "__main__":
    unittest.main()
