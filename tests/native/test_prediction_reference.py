#!/usr/bin/env python3
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('prediction_reference', HERE/'prediction_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)

class PredictionReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='illixr-prediction-oracle-')
        cls.directory = Path(cls.temp.name)
        reference.build_helper(cls.directory)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    @staticmethod
    def records():
        source = dict(timestamp_ns=1_000_000_000, previous_timestamp_ns=995_000_000,
                      position=[0,0,0], velocity=[0,0,0], orientation=[1,0,0,0],
                      w_hat=[0,0,0], a_hat=[0,0,9.81], w_hat2=[0,0,0], a_hat2=[0,0,9.81])
        prediction = dict(caller=0, status=0, processing_hart=0, publication_hart=0,
                          source_ns=1_000_000_000, source_seq=1, computed_ns=20,
                          target_ns=1_005_000_000, horizon_ns=5_000_000,
                          input_seq=1, input=source, raw=[0,0,0,1,0,0,0,0,0,0,0,0,0],
                          position=[0,0,0], orientation=[1,0,0,0], offset=[1,0,0,0])
        warp_prediction = copy.deepcopy(prediction)
        warp_prediction.update(caller=1, processing_hart=1, publication_hart=1)
        warp = dict(stage='timewarp', orientation=[1,0,0,0],render_orientation=[1,0,0,0],
                    transform=[.5,0,-.5,0, 0,.5,-.5,0, 0,0,-1,0, 0,0,0,1])
        return [('ILLIXR_PREDICTION', prediction), ('ILLIXR_PREDICTION', warp_prediction),
                ('ILLIXR_PREDICTION_PLACEMENT', dict(caller=0,hart_mask=1,work_counts=[1,0],publication_counts=[1,0])),
                ('ILLIXR_PREDICTION_PLACEMENT', dict(caller=1,hart_mask=2,work_counts=[0,1],publication_counts=[0,1])),
                ('ILLIXR_PREDICTION_SUMMARY', dict(calls=2,overflow=0,invalid=0)),
                ('ILLIXR_GPU_EVENT', warp)]

    def analyze(self, records):
        trace = self.directory/'console.log'
        trace.write_text('\n'.join(marker+' '+json.dumps(value) for marker,value in records)+'\n')
        return reference.analyze(trace, self.directory)

    def test_accepts_independent_reference(self):
        result = self.analyze(self.records())
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['predictions_compared'], 2)
        self.assertEqual(result['transforms_compared'], 1)

    def test_v2_warps_can_reuse_frame_id(self):
        records = self.records()
        records[-1][1].update(version=2, frame_id=1, warp_id=1)
        records.append(('ILLIXR_GPU_EVENT', dict(records[-1][1], warp_id=2)))
        result = self.analyze(records)
        self.assertTrue(result['passed'], result['errors'])
        self.assertEqual(result['transforms_compared'], 2)
        self.assertEqual(result['distinct_render_images_warped'], 1)

    def test_v2_duplicate_warp_ids_rejected(self):
        records = self.records()
        records[-1][1].update(version=2, frame_id=1, warp_id=1)
        records.append(copy.deepcopy(records[-1]))
        self.assertFalse(self.analyze(records)['passed'])

    def test_rejects_pose_mismatch(self):
        records = self.records()
        records[0][1]['position'][0] = 0.01
        result = self.analyze(records)
        self.assertFalse(result['passed'])
        self.assertIn('desktop pose mismatch', str(result['errors']))

    def test_rejects_transform_mismatch(self):
        records = self.records()
        records[-1][1]['transform'][0] += 0.1
        result = self.analyze(records)
        self.assertFalse(result['passed'])
        self.assertIn('desktop transform mismatch', str(result['errors']))

    def test_rejects_missing_trace(self):
        records = self.records()
        del records[0]
        self.assertFalse(self.analyze(records)['passed'])

    def test_rejects_stale_not_frozen(self):
        records = self.records()
        stale = copy.deepcopy(records[0][1])
        stale.update(status=2,target_ns=1_060_000_000,horizon_ns=60_000_000)
        stale['position'][0] = 1
        records.insert(2, ('ILLIXR_PREDICTION', stale))
        records[3][1].update(work_counts=[2,0],publication_counts=[2,0])
        records[5][1]['calls'] = 3
        result = self.analyze(records)
        self.assertFalse(result['passed'])
        self.assertIn('not frozen', str(result['errors']))

if __name__ == '__main__':
    unittest.main()
