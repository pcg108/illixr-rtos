import importlib.util
from pathlib import Path
import struct
import unittest

path = Path(__file__).resolve().parents[2]/'scripts/analyze_solve_capture.py'
spec = importlib.util.spec_from_file_location('solve_capture', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def capture(flags='LUNN', actual=None):
    # A=[[2,1],[0,2]], x=[3,4], alpha=.7, padded columns.
    arrays = [[2.,0.,0.,1.,2.,0.],
              [7.,5.6,99.] if flags == 'LUNN' else [4.2,7.7,99.],
              actual if actual is not None else [3.,4.,99.]]
    text = f'ILLIXR_SOLVE_CAPTURE present=1 mode=0 id=1 m=2 n=1 lda=3 ldb=3 flags={flags} input_errors=0\n'
    for index, values in enumerate(arrays):
        text += f'ILLIXR_SOLVE_DATA array={index} offset=0 '+' '.join(struct.pack('>d',x).hex() for x in values)+'\n'
    return text+'ILLIXR_SOLVE_CAPTURE_END\n'


class SolveCapture(unittest.TestCase):
    def test_transposed_lower_81_by_83_tail_column_residual(self):
        m, n, lda, ldb = 81, 83, 83, 82
        value = lambda i: ((i*17+11) % 41-20)/23.
        a = [.05*value(i) for i in range(lda*m)]
        for i in range(m):
            a[i+i*lda] = 2.
        expected = [value(i+2) for i in range(ldb*n)]
        before = expected.copy()
        for j in range(n):
            for i in range(m):
                before[i+j*ldb] = .7*sum(a[k+i*lda]*expected[k+j*ldb]
                                         for k in range(i, m))
        actual = expected.copy()
        # A single incorrect equation at row 31 propagates backward through
        # the transposed lower-triangular solve in the final column.
        delta = [0.]*m
        delta[31] = .02
        for i in range(30, -1, -1):
            delta[i] = -sum(a[k+i*lda]*delta[k] for k in range(i+1, m))/2.
        for i in range(m):
            actual[i+(n-1)*ldb] += delta[i]
        text = (f'ILLIXR_SOLVE_CAPTURE present=1 mode=3 id=353 m={m} n={n} '
                f'lda={lda} ldb={ldb} flags=LLTN input_errors=0\n')
        for array, values in enumerate((a, before, actual)):
            for offset in range(0, len(values), 4):
                text += (f'ILLIXR_SOLVE_DATA array={array} offset={offset} ' +
                         ' '.join(struct.pack('>d', v).hex() for v in values[offset:offset+4]) + '\n')
        result = module.analyze(text+'ILLIXR_SOLVE_CAPTURE_END\n')
        self.assertEqual(result['captured_input_host_solve_vs_original_fixture_errors'], 0)
        residuals = result['equation_residuals_above_tolerance']
        self.assertEqual([(r['row'], r['column']) for r in residuals], [(31, 82)])
        self.assertAlmostEqual(residuals[0]['residual'], .04)
        self.assertTrue(all(r['column'] == 82 for r in result['mismatches']))

    def test_known_upper_solve_and_transpose(self):
        for flags in ('LUNN','LUTN'):
            result = module.analyze(capture(flags))
            self.assertEqual(result['fpga_vs_host_solve_errors'], 0)
            self.assertEqual(result['equation_residuals_above_tolerance'], [])

    def test_corrupt_result_and_padding_are_detected(self):
        result = module.analyze(capture(actual=[4.,4.,98.]))
        self.assertEqual(result['fpga_vs_host_solve_errors'], 2)
        self.assertEqual([e['row'] for e in result['mismatches']], [0,2])
        self.assertEqual(len(result['equation_residuals_above_tolerance']), 1)

    def test_incomplete_and_duplicate_data_rejected(self):
        text = capture()
        for bad in (text.replace('ILLIXR_SOLVE_CAPTURE_END',''),
                    text+next(l for l in text.splitlines() if 'array=0' in l)+'\n'):
            with self.assertRaises(ValueError):
                module.analyze(bad)

    def test_no_reproduced_failure(self):
        self.assertEqual(module.analyze('ILLIXR_SOLVE_CAPTURE present=0\n'), {'captured':False})
