"""Portable FireSim packaging invariants; no FPGA, synthesis or network access."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


setup = module('setup_firesim')
guard = module('firesim_resource_guard')


class PackagingTests(unittest.TestCase):
    def test_manifest_sources_and_patches_match(self):
        data = setup.manifest()
        for item in data['scala_files']:
            self.assertEqual(setup.digest(ROOT / item['source']), item['sha256'])
        for item in data['patches']:
            self.assertEqual(setup.digest(ROOT / item['file']), item['sha256'])

    def test_no_accelerator_dual_core_alias(self):
        configs = setup.manifest()['configurations']
        self.assertEqual([k for k,v in configs.items() if v['harts']==2], ['illixr_u250_rocket_dual'])
        self.assertTrue(configs[setup.manifest()['canonical_config']]['int8_gemmini'])
        self.assertTrue(configs[setup.manifest()['canonical_config']]['fp32_gemmini'])

    def test_refuse_different_file_and_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'owned'
            path.write_bytes(b'old')
            setup.same_or_new(path, b'old')
            with self.assertRaises(ValueError): setup.same_or_new(path, b'new')
            link = Path(tmp) / 'link'; link.symlink_to(path)
            with self.assertRaises(ValueError): setup.same_or_new(link, b'old')
            self.assertEqual(path.read_bytes(), b'old')

    def test_only_owned_processes_selected(self):
        processes = {101:{'parent':1,'tagged':True},102:{'parent':101},
                     103:{'parent':1,'cwd':'/private/build'},104:{'parent':103}}
        self.assertEqual(guard.select(processes, 101, '/private/build'), {101,102})

    def test_resource_thresholds_and_psi(self):
        memory = {'available_mib':48*1024,'oom_kill':9,'some_avg10':20,'full_avg10':10}
        self.assertFalse(guard.stop_reasons(memory,9,{}))
        self.assertEqual(guard.warning_reasons(memory), ['global_memory_PSI'])
        self.assertIn('available_memory_below_48_GiB', guard.stop_reasons({**memory,'available_mib':48*1024-1},9,{}))
        self.assertIn('kernel_oom_kill_counter_increased', guard.stop_reasons(memory,8,{}))
        self.assertIn('process_RSS_at_least_64_GiB', guard.stop_reasons(memory,9,{1:{'rss_mib':64*1024}}))

    def test_runtime_preserves_bounds_and_no_rootfs(self):
        data = json.loads((ROOT / 'config/firesim/config_runtime.yaml.in').read_text())
        self.assertEqual(data['target_config']['plusarg_passthrough'], '+max-cycles=100000000000')
        self.assertTrue(data['host_debug']['zero_out_dram'])
        self.assertFalse(data['tracing']['enable'])
        data = json.loads((ROOT / 'config/firesim/workload.json.in').read_text())
        self.assertIsNone(data['common_rootfs'])


if __name__ == '__main__': unittest.main()
