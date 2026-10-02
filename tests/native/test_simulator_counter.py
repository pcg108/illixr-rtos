import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'scripts'))
from simulator_counter import counter_parameters, LIMIT


class SimulatorCounters(unittest.TestCase):
    def test_spike_and_unknown_reference_clock_are_not_cpu_counts(self):
        self.assertEqual(counter_parameters('spike', None)['counter_limit'], LIMIT)
        result = counter_parameters('verilator', None)
        self.assertNotIn('nominal_rocket_cycle_limit', result)
        self.assertIn('period not supplied', result['counter_semantics'])

    def test_equal_target_budget_across_reference_clocks(self):
        with tempfile.TemporaryDirectory() as tmp:
            simulator, manifest = Path(tmp)/'simulator', Path(tmp)/'clock.json'
            simulator.write_bytes(b'verified executable fixture')
            for period, ratio in ((1.0, 2), (2.0, 1)):
                data = dict(testdriver_period_ns=period, rocket_period_ns=2.0,
                            model_sha256=hashlib.sha256(simulator.read_bytes()).hexdigest())
                manifest.write_text(json.dumps(data))
                result = counter_parameters('verilator', simulator, manifest)
                self.assertEqual(result['counter_limit'], LIMIT*ratio)
                self.assertEqual(result['nominal_rocket_cycle_limit'], LIMIT)
                simulator.write_bytes(simulator.read_bytes()+b'changed')
                with self.assertRaisesRegex(ValueError, 'does not match'):
                    counter_parameters('verilator', simulator, manifest)

    def test_reject_unknown_clock_ratio(self):
        with tempfile.TemporaryDirectory() as tmp:
            simulator, manifest = Path(tmp)/'simulator', Path(tmp)/'clock.json'
            simulator.write_bytes(b'model')
            manifest.write_text(json.dumps(dict(testdriver_period_ns=3.0, rocket_period_ns=2.0,
                model_sha256=hashlib.sha256(simulator.read_bytes()).hexdigest())))
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                counter_parameters('verilator', simulator, manifest)


if __name__ == '__main__':
    unittest.main()
