import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'scripts'))
from run_gemmini_edges import analyze_edges


class EdgeEvidence(unittest.TestCase):
    def fixture(self):
        rows = [('ILLIXR_BLAS ', dict(backend='openblas_gemmini_fp32')),
                ('ILLIXR_GEMMINI_EDGE ', dict(passed=True, cases=590, checks=46552,
                    errors=0, largest_dim=135, reference='exact_dyadic_and_closed_form'))]
        for name in ('sgemm','dgemm','sgemv','dgemv'):
            rows.append(('ILLIXR_GEMMINI ', dict(name=name, phase='edge', precision='fp32',
                accelerator_hart_mask=1, submissions=1 if name.endswith('gemm') else 4,
                scratch_high_water=218700)))
        return rows

    def analyze(self, rows, **execution):
        console = '\n'.join(prefix+json.dumps(row) for prefix,row in rows)
        return analyze_edges(console+'\nILLIXR_GEMMINI_EDGE_END pass\n',
                             dict(returncode=0, **execution))

    def test_complete(self):
        self.assertTrue(self.analyze(self.fixture())['passed'])

    def test_early_exit_does_not_satisfy_fixture_count(self):
        rows = self.fixture()
        rows[1][1]['checks'] -= 1
        self.assertFalse(self.analyze(rows)['passed'])

    def test_zero_product_only_cannot_claim_accelerator_success(self):
        rows = self.fixture()
        rows[2][1].update(submissions=0, accelerator_hart_mask=0)
        self.assertFalse(self.analyze(rows)['passed'])

    def test_wrong_accelerator_hart(self):
        rows = self.fixture()
        rows[2][1]['accelerator_hart_mask'] = 2
        self.assertFalse(self.analyze(rows)['passed'])

    def test_duplicate_operation(self):
        rows = self.fixture()
        rows[3] = rows[2]
        self.assertFalse(self.analyze(rows)['passed'])

    def test_timeout_is_incomplete_even_with_final_record(self):
        result = self.analyze(self.fixture(), timed_out=True)
        self.assertFalse(result['passed'])
        self.assertFalse(result['complete'])


if __name__ == '__main__':
    unittest.main()
