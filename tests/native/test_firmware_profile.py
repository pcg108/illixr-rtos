"""Portable profile validation must survive relocation and cannot skip stages."""
import argparse
import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import collect_firesim_results as collector
from firmware_profile import profile_metadata, verified_pipeline
import setup_firesim


class FirmwareProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.artifact = self.root / 'relocated-firmware'
        self.artifact.mkdir()
        (self.artifact / 'zephyr.elf').write_bytes(b'fixture')
        (self.artifact / 'profile.yaml').write_text('plugins: offline_imu, offline_cam, openvins, imu_integrator, pose_prediction, render_loop, timewarp\n')
        (self.artifact / 'CMakeCache.txt').write_text('YAML_FILE:FILEPATH=/missing/original/checkout/profiles/gpu_pipeline.yaml\n')
        pipeline = profile_metadata(self.artifact / 'profile.yaml')
        self.build = {'target': {'harts': 4, 'placement': 'scheduler', 'platform_check_only': False,
                      'modeled_clock_scale': 2, 'ticks_per_sec': 10000},
                      'linalg': {'backend': 'eigen'}, 'pipeline': pipeline,
                      'artifact_sha256': {'profile.yaml': pipeline['profile_sha256']}}
        self.write_build()

    def write_build(self):
        (self.artifact / 'build_manifest.json').write_text(json.dumps(self.build))

    def test_relocation_preserves_required_gpu_validation(self):
        case = collector.case_metadata({'require_gpu': False, 'require_eye': False,
            'memory_profile_interval_cycles': 1000000, 'max_cycles': 100000000000,
            'timeout_seconds': 86400}, self.artifact, self.root / 'output', self.root / 'hardware.json')
        self.assertTrue(case['require_gpu'])
        self.assertFalse(case['require_eye'])
        self.assertEqual(verified_pipeline(self.artifact, self.build), self.build['pipeline'])

    def test_modified_profile_fails_hash_validation(self):
        (self.artifact / 'profile.yaml').write_text('plugins: offline_imu, offline_cam, openvins, imu_integrator\n')
        with self.assertRaisesRegex(ValueError, 'profile hash'):
            verified_pipeline(self.artifact, self.build)

    def test_inconsistent_or_nonboolean_pipeline_flags_are_rejected(self):
        for key, value in [('require_gpu', False), ('require_eye', True), ('require_gpu', 1),
                           ('plugins', ['offline_imu']), ('profile_sha256', 'incorrect')]:
            with self.subTest(key=key, value=value):
                modified = copy.deepcopy(self.build)
                modified['pipeline'][key] = value
                with self.assertRaises(ValueError):
                    verified_pipeline(self.artifact, modified)

    def test_unsafe_yaml_and_missing_profile_are_rejected(self):
        (self.artifact / 'profile.yaml').write_text('!!python/object/apply:os.system ["false"]\n')
        with self.assertRaisesRegex(ValueError, 'Invalid packaged plugin profile'):
            verified_pipeline(self.artifact, self.build)
        (self.artifact / 'profile.yaml').unlink()
        with self.assertRaisesRegex(ValueError, 'preserved profile.yaml'):
            verified_pipeline(self.artifact, self.build)

    def test_setup_uses_preserved_profile_with_absent_original_checkout(self):
        chipyard = self.root / 'chipyard'
        deploy = chipyard / 'sims/firesim/deploy'
        deploy.mkdir(parents=True)
        (deploy / 'firesim').write_text('fixture manager\n')
        config = setup_firesim.manifest()['canonical_config']
        args = argparse.Namespace(chipyard=chipyard, work=self.root / 'runtime', config=config,
            workload_name='relocated-profile', build_only=False, elf=self.artifact / 'zephyr.elf',
            fpga_db=self.root / 'fpga-db.json', hardware_manifest=None)
        with contextlib.redirect_stdout(io.StringIO()):
            setup_firesim.configure(args)
        case = json.loads((args.work / 'case.json').read_text())
        self.assertTrue(case['require_gpu'])
        self.assertFalse(case['require_eye'])

    def test_eye_profile_matches_compiled_eye_metadata(self):
        (self.artifact / 'profile.yaml').write_text('plugins: pose_prediction, render_loop, timewarp, eye_tracking, offline_eye\n')
        self.build['pipeline'] = profile_metadata(self.artifact / 'profile.yaml')
        self.build['artifact_sha256']['profile.yaml'] = self.build['pipeline']['profile_sha256']
        with self.assertRaisesRegex(ValueError, 'eye metadata'):
            verified_pipeline(self.artifact, self.build)
        self.build['ritnet'] = {'enabled': True}
        self.assertTrue(verified_pipeline(self.artifact, self.build)['require_eye'])


if __name__ == '__main__':
    unittest.main()
