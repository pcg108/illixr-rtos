"""Regression checks for the isolated clock experiment orchestration."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import run_clock_experiments as experiment


class ClockExperiments(unittest.TestCase):
    def test_combined_baseline_and_manifest_settings_preserve_legacy(self):
        cases = experiment.cases(Path('/example/run'), Path('/example/hardware'), combined=True)
        self.assertEqual([(c['modeled_clock_scale'], c['ticks_per_sec'], c['platform_check']) for c in cases],
                         [(2, 10000, True), (2, 10000, False)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / 'build_manifest.json'
            for target, expected in [({}, {}),
                    ({'modeled_clock_scale': 2, 'ticks_per_sec': 10000},
                     {'modeled_clock_scale': 2, 'ticks_per_sec': 10000}),
                    ({'modeled_clock_scale': 1, 'ticks_per_sec': 1000},
                     {'modeled_clock_scale': 1, 'ticks_per_sec': 1000})]:
                manifest.write_text(json.dumps({'target': target}))
                self.assertEqual(experiment.fs.firmware_clock_settings(root / 'zephyr.elf'), expected)

    def test_two_independent_changes_and_matching_preflights(self):
        cases = experiment.cases(Path('/example/run'), Path('/example/hardware'), 2)
        self.assertEqual([(c['modeled_clock_scale'], c['ticks_per_sec'], c['platform_check']) for c in cases],
                         [(1, 10000, True), (1, 10000, False), (2, 1000, True), (2, 1000, False)])
        self.assertTrue(all(c['output'].endswith('-2') for c in cases))
        self.assertTrue(all(c['harts'] == 4 and c['placement'] == 'unpinned' for c in cases))

    def test_build_guard_must_finish_cleanup_before_fpga_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            guard = work / 'control/build-test'
            guard.mkdir(parents=True)
            (guard / 'guard.json').write_text('{}')
            with patch.object(sys, 'argv', ['experiment', '--work', str(work), '--execute']), \
                    patch.object(experiment.fs, 'run_case') as run:
                with self.assertRaisesRegex(ValueError, 'All guarded builds must finish'):
                    experiment.main()
                run.assert_not_called()

    def test_warp_window_uses_own_target_not_saved_image_deadline(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            (work / 'analysis.json').write_text(json.dumps({'complete': True, 'summary': {'origin_ns': 1000}}))
            (work / 'console.log').write_text('')
            events = [{'stage': 'timewarp', 'target_ns': 1800, 'publication_ns': 700, 'presentation_ns': 400},
                      {'stage': 'timewarp', 'target_ns': 2_500_001_001,
                       'publication_ns': 2_500_000_002, 'presentation_ns': 400}]
            with patch.object(experiment.fs, 'records', return_value={'gpu_events': events}):
                result = experiment.collect('test', work, 1, 1000)
            self.assertEqual(result['first_2_5_seconds']['timewarp'],
                             {'completed': 1, 'missed': 0, 'miss_percent': 0})


if __name__ == '__main__':
    unittest.main()
