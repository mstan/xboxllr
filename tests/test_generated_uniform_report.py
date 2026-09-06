"""Keep A/B upload counters attributable to their explicit guest checkpoints."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('measure', ROOT / 'tools/measure_xbox_replay.py')
measure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(measure)


class GeneratedUniformReportTests(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stdout.log'
            path.write_text(text, encoding='utf-8')
            return measure.parse_stdout(path)['generated_uniform_samples']

    def test_old_log_has_no_invented_measurements(self):
        self.assertEqual(self.parse('[host] unrelated diagnostic\n'), [])

    def test_scope_and_large_deadline_are_preserved(self):
        rows = self.parse(
            '[host] generic-uniforms scope=interval deadline=14500000000 '
            'scalar_calls=320 bulk_calls=0 vectors_requested=320\n'
            '[host] generic-uniforms scope=cumulative deadline=0 '
            'scalar_calls=320 bulk_calls=20 vectors_requested=480\n')
        self.assertEqual(rows[0]['deadline'], 14500000000)
        self.assertIsInstance(rows[0]['deadline'], int)
        self.assertEqual(rows[0]['scope'], 'interval')
        self.assertEqual(rows[1]['scope'], 'cumulative')
        self.assertEqual(rows[1]['bulk_calls'], 20)
        self.assertEqual(rows[1]['vectors_requested'], 480)

    def test_missing_checkpoint_does_not_shift_later_sample(self):
        rows = self.parse(
            '[host] generic-uniforms scope=interval deadline=500000000 '
            'scalar_calls=16 bulk_calls=0 vectors_requested=16\n'
            '[host] unrelated diagnostic\n'
            '[host] generic-uniforms scope=interval deadline=1500000000 '
            'scalar_calls=16 bulk_calls=4 vectors_requested=48\n')
        self.assertEqual([r['deadline'] for r in rows], [500000000, 1500000000])


if __name__ == '__main__':
    unittest.main()
