"""Do not manufacture timing windows from missing or misaligned phase samples."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from measure_xbox_replay import parse_stdout
from summarize_xbox_generated_submit import PHASES, summarize


class GeneratedSubmitReport(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stdout.log'
            path.write_text(text, encoding='utf-8')
            return parse_stdout(path)

    def test_old_logs_remain_unmeasured(self):
        parsed = self.parse('[host] unrelated\n')
        self.assertEqual(parsed['generated_submit_cpu_samples'], [])
        self.assertEqual(parsed['generated_submit_phase_samples'], [])

    def test_gpu_query_disable_is_distinct_from_capability_and_old_logs(self):
        fields = ('gpu_draw_ns gpu_draw_n gpu_upload_ns gpu_upload_n '
                  'gpu_resolve_ns gpu_resolve_n drain_ns readpixels_ns cpu_copy_ns '
                  'bytes_read dirty_bytes nested_skipped queries_dropped').split()
        line = ('[perf-gl-gpu] scope=cumulative gpu_timing_available=1 ' +
                ' '.join(f'{field}=0' for field in fields) + ' resolve_bracket_ok=1')
        old = self.parse(line + '\n')['gl_gpu_samples'][0]
        disabled = self.parse(line + ' gpu_timing_enabled=0\n')['gl_gpu_samples'][0]
        self.assertIsNone(old['gpu_timing_enabled'])
        self.assertEqual(disabled['gpu_timing_available'], 1)
        self.assertEqual(disabled['gpu_timing_enabled'], 0)

    def test_explicit_large_deadline_and_phase(self):
        parsed = self.parse(
            '[host] generated-submit-cpu scope=cumulative deadline=14500000000 '
            'submits=5 record_bytes=128 discarded_deltas=0 inclusive_children=1 '
            'clock_ns_per_tick=0.2 overhead_ticks=42\n'
            '[host] generated-submit-phase scope=cumulative deadline=15000000000 '
            'phase=records calls=7 raw_ns=105 compensated_ns=84\n')
        self.assertEqual(parsed['generated_submit_cpu_samples'][0]['deadline'], 14500000000)
        self.assertEqual(parsed['generated_submit_cpu_samples'][0]['clock_ns_per_tick'], 0.2)
        self.assertEqual(parsed['generated_submit_phase_samples'][0]['deadline'], 15000000000)
        self.assertEqual(parsed['generated_submit_phase_samples'][0]['phase'], 'records')

    def setUp(self):
        self.report = {'ok': True, 'parsed': {
            'frame_series': [{'deadline': d, 'insns': d + 1, 'wall_ns': 2e9,
                              'endpoint2': 0, 'vblank_delta': 1} for d in (100, 200, 300)],
            'generated_submit_cpu_samples': [
                {'scope': 'cumulative', 'deadline': d, 'submits': d,
                 'record_bytes': d * 8, 'discarded_deltas': 0, 'inclusive_children': 1,
                 'available': 1, 'enabled': 1}
                for d in (100, 300)],
            'generated_submit_phase_samples': [
                {'scope': 'cumulative', 'deadline': d, 'phase': phase,
                 'calls': d, 'raw_ns': d * 100, 'compensated_ns': d * 90}
                for d in (100, 300) for phase in PHASES]}}

    def test_cumulative_subtraction_and_semantics(self):
        result = summarize(self.report, 100, 300)
        self.assertEqual(result['submits'], 200)
        self.assertEqual(result['record_bytes'], 1600)
        self.assertEqual(result['window_wall_seconds'], 4)
        self.assertEqual(result['phases']['records']['compensated_ns_per_call'], 90)
        self.assertAlmostEqual(result['raw_seconds'], 12 * 20000 / 1e9)
        self.assertIn('not exclusive', result['semantics'])

    def test_missing_and_duplicate_endpoints_rejected(self):
        for family in ('generated_submit_cpu_samples', 'generated_submit_phase_samples'):
            missing = copy.deepcopy(self.report)
            missing['parsed'][family].pop()
            with self.assertRaises(ValueError):
                summarize(missing, 100, 300)
            duplicate = copy.deepcopy(self.report)
            duplicate['parsed'][family].append(duplicate['parsed'][family][0].copy())
            with self.assertRaises(ValueError):
                summarize(duplicate, 100, 300)

    def test_discarded_intervals_and_counter_resets_rejected(self):
        bad = copy.deepcopy(self.report)
        bad['parsed']['generated_submit_cpu_samples'][-1]['discarded_deltas'] = 1
        with self.assertRaises(ValueError):
            summarize(bad, 100, 300)
        for value in (-1, float('nan'), 0):
            bad = copy.deepcopy(self.report)
            bad['parsed']['generated_submit_phase_samples'][-1]['raw_ns'] = value
            with self.assertRaises(ValueError):
                summarize(bad, 100, 300)

    def test_inconsistent_compensation_and_wrong_scope_rejected(self):
        bad = copy.deepcopy(self.report)
        bad['parsed']['generated_submit_phase_samples'][-1]['compensated_ns'] = 50000
        with self.assertRaises(ValueError):
            summarize(bad, 100, 300)
        bad = copy.deepcopy(self.report)
        bad['parsed']['generated_submit_cpu_samples'][-1]['scope'] = 'interval'
        with self.assertRaises(ValueError):
            summarize(bad, 100, 300)

    def test_unknown_phase_and_disabled_census_rejected(self):
        bad = copy.deepcopy(self.report)
        extra = bad['parsed']['generated_submit_phase_samples'][-1].copy()
        extra['phase'] = 'bogus'
        bad['parsed']['generated_submit_phase_samples'].append(extra)
        with self.assertRaises(ValueError):
            summarize(bad, 100, 300)
        bad = copy.deepcopy(self.report)
        bad['parsed']['generated_submit_cpu_samples'][-1]['enabled'] = 0
        with self.assertRaises(ValueError):
            summarize(bad, 100, 300)


if __name__ == '__main__':
    unittest.main()
