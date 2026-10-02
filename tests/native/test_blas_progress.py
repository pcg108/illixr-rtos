import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from diagnose_blas_progress import inspect


class ProgressTest(unittest.TestCase):
    def test_preserves_outstanding_dimensions_and_flags(self):
        result = inspect('ILLIXR_BLAS_PROGRESS enter id=1 op=reference_gemm m=3 n=5 k=4 flags=NT--\r\n'
                         'ILLIXR_BLAS_PROGRESS exit id=1 op=reference_gemm\r\n'
                         'ILLIXR_BLAS_PROGRESS enter id=2 op=dgemm m=3 n=5 k=4 flags=NT--\n')
        self.assertEqual(result['last_completed']['op'], 'reference_gemm')
        self.assertEqual(result['outstanding_region']['flags'], 'NT--')
        self.assertEqual(result['outstanding_region']['k'], '4')
        self.assertFalse(result['selftest_pass_record'])
        self.assertEqual(result['record_errors'], [])

    def test_malformed_or_missing_pair_is_reported(self):
        self.assertTrue(inspect('ILLIXR_BLAS_PROGRESS exit id=1 op=dgemm')['record_errors'])
        self.assertTrue(inspect('ILLIXR_BLAS_PROGRESS enter id=x op=dgemm')['record_errors'])

    def test_completion_does_not_imply_selftest_pass(self):
        result = inspect('ILLIXR_BLAS_PROGRESS enter id=1 op=dgemm\n'
                         'ILLIXR_BLAS_PROGRESS exit id=1 op=dgemm\n')
        self.assertIsNone(result['outstanding_region'])
        self.assertFalse(result['selftest_pass_record'])


if __name__ == '__main__':
    unittest.main()
