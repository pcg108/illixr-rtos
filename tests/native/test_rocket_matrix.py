#!/usr/bin/env python3
"""Check matrix ordering, resume rules and honest reports without running hardware."""
import argparse
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
import run_rocket_matrix as matrix


class MatrixValidation(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.fingerprints = {"elf_sha256": "firmware", "simulator_sha256": "simulator", "hardware_manifest.json_sha256": "hardware"}
        self.addCleanup(patch.stopall)
        patch.object(matrix, "fingerprints", return_value=self.fingerprints).start()
        self.path = self.work / "matrix.json"
        workloads = [{"name": f"{harts}-{mode}", "harts": harts, "placement": mode,
            "elf": str(self.work / f"{harts}-{mode}.elf"), "hardware_manifest": str(self.work / f"{harts}.json"),
            "output": str(self.work / "results" / f"{harts}-{mode}-1")} for harts, mode in
            ((1,"unpinned"),(2,"unpinned"),(2,"pinned"),(4,"unpinned"),(4,"pinned"))]
        self.path.write_text(json.dumps(workloads))
        self.cases = matrix.load_cases(self.path, self.work)
        self.args = argparse.Namespace(report_dir=self.work / "reports", chipyard=self.work, dataset=self.work, native=self.work / "native")

    def record(self, case, status="pass", output=None):
        path = output or Path(case["output"])
        path.mkdir(parents=True, exist_ok=True)
        metadata = {**self.fingerprints, "status": status, "returncode": 0 if status == "pass" else 1,
                    "harts": case["harts"], "placement": case["placement"], "platform_check": case["platform_check"]}
        analysis = {"passed": status == "pass", "errors": [] if status == "pass" else ["test failure"], "complete": status in ("pass", "fail")}
        (path / "run.json").write_text(json.dumps(metadata))
        (path / "analysis.json").write_text(json.dumps(analysis))
        return path

    def test_all_preflights_precede_five_cases(self):
        self.assertEqual([case["harts"] for case in self.cases[:3]], [1,2,4])
        self.assertTrue(all(case["platform_check"] for case in self.cases[:3]))
        self.assertFalse(any(case["platform_check"] for case in self.cases[3:]))

    def test_preflights_use_the_workload_simulator_manifest(self):
        for preflight in self.cases[:3]:
            workloads = [case for case in self.cases[3:] if case["harts"] == preflight["harts"]]
            self.assertTrue(all(case["hardware_manifest"] == preflight["hardware_manifest"] for case in workloads))

    def test_placement_variants_cannot_silently_use_different_simulators(self):
        workloads = json.loads(self.path.read_text())
        workloads[2]["hardware_manifest"] = str(self.work / "different.json")
        self.path.write_text(json.dumps(workloads))
        with self.assertRaisesRegex(ValueError, "same hardware manifest"):
            matrix.load_cases(self.path, self.work)

    def test_matching_completed_pass_is_reusable(self):
        self.record(self.cases[0])
        self.assertTrue(matrix.inspect_case(self.cases[0])["reusable"])

    def test_changed_firmware_cannot_resume_pass(self):
        self.record(self.cases[0])
        with patch.object(matrix, "fingerprints", return_value={**self.fingerprints, "elf_sha256": "changed"}):
            item = matrix.inspect_case(self.cases[0])
        self.assertFalse(item["reusable"])
        self.assertEqual(item["status"], "stale")

    def test_changed_simulator_cannot_resume_pass(self):
        self.record(self.cases[0])
        with patch.object(matrix, "fingerprints", return_value={**self.fingerprints, "simulator_sha256": "changed"}):
            self.assertFalse(matrix.inspect_case(self.cases[0])["reusable"])

    def test_failed_attempt_is_preserved_and_gets_new_directory(self):
        original = self.record(self.cases[0], "fail")
        before = (original / "analysis.json").read_bytes()
        new = matrix.next_output(self.cases[0]["output"])
        self.assertEqual(new.name, "preflight-single-2")
        self.assertEqual((original / "analysis.json").read_bytes(), before)
        self.assertFalse(matrix.inspect_case(self.cases[0])["reusable"])

    def test_newer_failure_is_not_hidden_by_older_pass(self):
        self.record(self.cases[0])
        self.record(self.cases[0], "fail", matrix.next_output(self.cases[0]["output"]))
        self.assertFalse(matrix.inspect_case(self.cases[0])["reusable"])
        self.assertEqual(matrix.inspect_case(self.cases[0])["status"], "fail")

    def test_running_attempt_prevents_launch(self):
        self.record(self.cases[1], "running")
        with patch.object(matrix.run_rocket, "run") as launch:
            with self.assertRaisesRegex(ValueError, "still marked running"):
                matrix.run_matrix(self.cases, self.args)
            launch.assert_not_called()

    def test_missing_results_are_not_invented(self):
        summary = matrix.summarize(self.cases)
        self.assertEqual(summary["counts"], {"pending": 8})
        self.assertFalse(summary["all_passed"])
        self.assertNotIn("0 / 0", matrix.markdown(summary))

    def test_failed_preflight_blocks_only_matching_workloads(self):
        calls = []
        def launch(args):
            case = next(case for case in self.cases if case["elf"] == str(args.elf))
            calls.append((case["platform_check"], case["harts"], case["placement"]))
            failed = case["platform_check"] and case["harts"] == 2
            self.record(case, "fail" if failed else "pass", args.output)
            return not failed
        with patch.object(matrix.run_rocket, "run", side_effect=launch), redirect_stdout(io.StringIO()):
            self.assertFalse(matrix.run_matrix(self.cases, self.args))
        self.assertEqual(calls[:3], [(True,1,"unpinned"),(True,2,"unpinned"),(True,4,"unpinned")])
        self.assertFalse(any(not preflight and harts == 2 for preflight, harts, _ in calls))
        self.assertEqual(matrix.summarize(self.cases)["counts"], {"blocked": 2, "fail": 1, "pass": 5})

    def test_resume_skips_all_completed_matching_cases(self):
        for case in self.cases:
            self.record(case)
        with patch.object(matrix.run_rocket, "run") as launch, redirect_stdout(io.StringIO()):
            self.assertTrue(matrix.run_matrix(self.cases, self.args))
            launch.assert_not_called()

    def test_interruption_does_not_launch_next_case(self):
        def launch(args):
            case = self.cases[0]
            path = self.record(case, "incomplete", args.output)
            metadata = matrix.read_json(path / "run.json")
            metadata["interrupted"] = True
            (path / "run.json").write_text(json.dumps(metadata))
            return False
        with patch.object(matrix.run_rocket, "run", side_effect=launch) as launch_fn, redirect_stdout(io.StringIO()):
            self.assertFalse(matrix.run_matrix(self.cases, self.args))
            self.assertEqual(launch_fn.call_count, 1)

    def test_runner_defaults_preserve_watchdog_and_cycle_limit(self):
        args = matrix.runner_args(self.cases[0], self.work / "output", self.args)
        self.assertEqual(args.timeout, 86400)
        self.assertEqual(args.max_cycles, 100000000000)


if __name__ == "__main__":
    unittest.main()
