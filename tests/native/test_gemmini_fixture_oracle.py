import importlib.util
from pathlib import Path
import tempfile
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from fixture_oracle_analysis import analyze_oracle

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('fixture_oracle', ROOT / 'scripts/prepare_gemmini_fixture_oracle.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


class CaptureIntegrity(unittest.TestCase):
    def test_record_requires_matching_compiled_mode_and_all_checks(self):
        import json
        record = {'mode':'verify','passed':True,'products':253728,'triangles':80,'errors':0}
        text = 'ILLIXR_FIXTURE_ORACLE ' + json.dumps(record)
        self.assertEqual(analyze_oracle(text, 'verify')[1], [])
        for mode in (None, 'table', 'capture'):
            self.assertTrue(analyze_oracle(text, mode)[1])
        self.assertTrue(analyze_oracle(text+'\n'+text, 'verify')[1])
        record['products'] -= 1
        self.assertTrue(analyze_oracle('ILLIXR_FIXTURE_ORACLE '+json.dumps(record), 'verify')[1])

    def test_hidden_mismatch_rejected(self):
        text='ILLIXR_FIXTURE_ORACLE {"mode":"table","passed":true,"products":253728,"triangles":80,"errors":0}\nILLIXR_FIXTURE_MISMATCH product=0\n'
        self.assertTrue(analyze_oracle(text, 'table')[1])

    def rejected(self, text):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'console.log'
            path.write_text(text)
            with self.assertRaises(ValueError):
                oracle.capture_tables(path)

    def test_truncated_triangle(self):
        self.rejected('ILLIXR_FIXTURE_T 1234 2\nILLIXR_FIXTURE_V 0000000000000000\n')

    def test_unexpected_triangle_value(self):
        self.rejected('ILLIXR_FIXTURE_V 0000000000000000\n')

    def test_complete_marker_does_not_replace_fixture_coverage(self):
        self.rejected('ILLIXR_FIXTURE_ORACLE {"mode":"capture","passed":true,"products":0,"triangles":0,"errors":0}\nILLIXR_GEMMINI_STANDALONE_END pass\n')

    def test_duplicate_completion(self):
        marker = 'ILLIXR_FIXTURE_ORACLE {"mode":"capture","passed":true,"products":0,"triangles":0,"errors":0}\n'
        self.rejected(marker + marker)

    def test_source_hook_drift_fails(self):
        with self.assertRaises(ValueError):
            oracle.replace_once('changed source', 'required hook', 'replacement')
        with self.assertRaises(ValueError):
            oracle.replace_once('hook hook', 'hook', 'replacement')

    def test_capture_keeps_original_reference_loops_and_sources(self):
        files = [ROOT / 'src/gemmini_selftest.cpp', ROOT / 'src/blas_reference.cpp', ROOT / 'tests/gemmini/main.cpp']
        before = {str(p): oracle.sha(p) for p in files}
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            oracle.prepare(ROOT, out, 'capture')
            copied = (out / 'gemmini_selftest.cpp').read_text()
            self.assertIn('for(int l=0;l<k;++l)', copied)
            self.assertIn('for(int j=0;j<nx;++j)', copied)
            self.assertIn('if(side==\'L\') result=alpha*(mat*input);', (out / 'blas_reference.cpp').read_text())
            self.assertIn('fixture_oracle::finish()', (out / 'main.cpp').read_text())
        self.assertEqual(before, {str(p): oracle.sha(p) for p in files})


if __name__ == '__main__':
    unittest.main()
