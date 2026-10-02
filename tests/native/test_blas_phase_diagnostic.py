import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
from analyze_blas_phase_diagnostic import analyze, decode_mismatch


def phase(passed=True, mismatch=''):
    return '\n'.join([
        'ILLIXR_FIXTURE_PHASE_BEGIN phase=0 delay_us=0',
        'ILLIXR_BLAS_FAILURES errors=%d stored=%d' % (bool(mismatch), bool(mismatch)),
        mismatch,
        'ILLIXR_BLAS_SELFTEST ' + json.dumps({'passed': passed, 'checks': 610745}),
        'ILLIXR_FIXTURE_ORACLE ' + json.dumps({'mode': 'table', 'passed': True, 'products': 253728, 'triangles': 80, 'errors': 0}),
        'ILLIXR_FIXTURE_PHASE_END phase=0 passed=%d oracle_passed=1' % passed,
    ])


MISMATCH = ('ILLIXR_BLAS_MISMATCH check=301000 case=3 op=dtrsm m=3 n=5 k=0 '
            'flags=LUNN inc=0 actual=3ff0000000000000 expected=0000000000000000')


class PhaseDiagnosticTest(unittest.TestCase):
    def test_decode_exact_error_and_bound(self):
        r = decode_mismatch(MISMATCH)
        self.assertEqual(r['actual'], 1.)
        self.assertEqual(r['expected'], 0.)
        self.assertEqual(r['error_over_tolerance'], 1e12)

    def test_complete_failure_is_diagnostic_evidence_not_acceptance(self):
        r = analyze(phase(False, MISMATCH), phases=1)
        self.assertTrue(r['complete_records'])
        self.assertEqual(r['failed_phases'], [0])
        self.assertFalse(r['acceptance_eligible'])

    def test_reject_hidden_failure(self):
        self.assertIn('Phase hides numerical failures', analyze(phase(True, MISMATCH), phases=1)['record_errors'])

    def test_reject_truncation_and_duplicate_phases(self):
        self.assertFalse(analyze(phase().rsplit('\n', 1)[0], phases=1)['complete_records'])
        self.assertFalse(analyze(phase() + '\n' + phase(), phases=2)['complete_records'])

    def test_reject_missing_mismatch(self):
        self.assertFalse(analyze(phase(False, MISMATCH).replace(MISMATCH, ''), phases=1)['complete_records'])

    def test_nonfinite_error_preserves_bits_as_json_safe_text(self):
        r = decode_mismatch(MISMATCH.replace('3ff0000000000000', '7ff8000000000000'))
        self.assertEqual(r['actual'], 'nan')
        json.dumps(r, allow_nan=False)


if __name__ == '__main__':
    unittest.main()
