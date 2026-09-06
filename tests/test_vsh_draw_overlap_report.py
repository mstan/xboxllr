"""Keep absent census data distinct from zero and preserve program identities."""
from pathlib import Path
import copy
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from measure_xbox_replay import parse_stdout
from summarize_xbox_vsh_draw_overlap import COUNTERS, summarize


class VshDrawOverlapReport(unittest.TestCase):
    def setUp(self):
        def row(index, outcome, batches, vertices, kind='program'):
            data = dict.fromkeys(COUNTERS, 0)
            data.update(scope='cumulative', kind=kind, index=index, known=1,
                        vsh_fp='0000000000000012', start_slot=0, slots=136,
                        outcome=outcome, transform_mode=2, fixed_function=0,
                        programmable=1, compiled=1, writes_context=0, fixture=0)
            data.update(batches=batches, submitted_vertices=vertices,
                        transformed_vertices=vertices, executed_vsh_vertices=vertices,
                        programmable_batches=batches, compiled_batches=batches,
                        mode_2=batches)
            data['outcome_' + outcome] = batches
            if outcome == 'generated_gpu':
                data.update(generated_setup_calls=batches,
                            generated_setup_raw_ticks=batches * 1000,
                            generated_setup_compensated_ticks=batches * 900,
                            fixture_setup_calls=batches,
                            fixture_setup_raw_ticks=batches * 500,
                            fixture_setup_compensated_ticks=batches * 400,
                            fixture_input_vertices=vertices,
                            fixture_output_vertices=vertices)
            return data
        first = [row(0, 'cpu_witnessed', 2, 20), row(256, 'unknown', 0, 0, 'overflow')]
        last = [row(0, 'cpu_witnessed', 5, 50), row(1, 'generated_gpu', 5, 200),
                row(256, 'unknown', 0, 0, 'overflow')]
        headers = []
        all_rows = []
        for deadline, rows in ((100, first), (300, last)):
            header = {field: sum(r[field] for r in rows) for field in COUNTERS}
            header.update(scope='cumulative', deadline=deadline, schema=1, enabled=1,
                          inclusive_setup=1, rows=len(rows) - 1, row_capacity=256,
                          clock_ns_per_tick=0.25, unknown_program_batches=0)
            headers.append(header)
            for item in rows:
                item['deadline'] = deadline
                all_rows.append(item)
        self.report = {'ok': True, 'parsed': {
            'frame_series': [{'deadline': d, 'insns': d + 1, 'wall_ns': 2e9,
                              'endpoint2': 0, 'vblank_delta': 1} for d in (100, 200, 300)],
            'vsh_draw_overlap_samples': headers, 'vsh_draw_overlap_rows': all_rows}}

    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'stdout.log'
            path.write_text(text, encoding='utf-8')
            return parse_stdout(path)

    def test_absent_census_is_not_zero_measurement(self):
        parsed = self.parse('[host] old log\n')
        self.assertEqual(parsed['vsh_draw_overlap_samples'], [])
        self.assertEqual(parsed['vsh_draw_overlap_rows'], [])

    def test_identity_is_text_even_when_only_decimal_digits(self):
        parsed = self.parse(
            '[host] vsh-draw-overlap scope=cumulative deadline=16000000000 enabled=1 '
            'clock_ns_per_tick=0.250000000\n'
            '[host] vsh-draw-overlap-row scope=cumulative deadline=16000000000 '
            'vsh_fp=0000000000000012 generic_fp=0123456789abcdef outcome=generated_gpu '
            'batches=12 program_vertices=36\n')
        self.assertEqual(parsed['vsh_draw_overlap_samples'][0]['clock_ns_per_tick'], 0.25)
        row = parsed['vsh_draw_overlap_rows'][0]
        self.assertEqual(row['vsh_fp'], '0000000000000012')
        self.assertEqual(row['generic_fp'], '0123456789abcdef')
        self.assertEqual(row['outcome'], 'generated_gpu')
        self.assertEqual(row['program_vertices'], 36)

    def test_vertex_weighted_routes_and_new_population(self):
        result = summarize(self.report, 100, 300)
        self.assertEqual(result['totals']['executed_vsh_vertices'], 230)
        self.assertEqual(result['totals']['batches'], 8)
        route = result['routes']['generated_gpu']
        self.assertAlmostEqual(route['executed_vsh_share_pct'], 200 / 230 * 100)
        self.assertAlmostEqual(route['setup_seconds'], 4500 * 0.25 / 1e9)
        self.assertAlmostEqual(route['fixture_setup_seconds'], 2000 * 0.25 / 1e9)
        self.assertEqual(result['window_wall_seconds'], 4)
        self.assertEqual(len(result['populations']), 2)

    def test_missing_duplicate_and_unaligned_endpoints(self):
        for family in ('vsh_draw_overlap_samples', 'vsh_draw_overlap_rows'):
            bad = copy.deepcopy(self.report)
            bad['parsed'][family].pop()
            with self.assertRaises(ValueError):
                summarize(bad, 100, 300)
            bad = copy.deepcopy(self.report)
            bad['parsed'][family].append(bad['parsed'][family][0].copy())
            with self.assertRaises(ValueError):
                summarize(bad, 100, 300)
        with self.assertRaises(ValueError):
            summarize(self.report, 101, 300)

    def test_row_identity_mutation_is_not_a_new_program(self):
        bad = copy.deepcopy(self.report)
        bad['parsed']['vsh_draw_overlap_rows'][2]['vsh_fp'] = '0000000000000013'
        with self.assertRaisesRegex(ValueError, 'identity changed'):
            summarize(bad, 100, 300)

    def test_counter_conservation_and_reset(self):
        bad = copy.deepcopy(self.report)
        bad['parsed']['vsh_draw_overlap_samples'][-1]['executed_vsh_vertices'] += 1
        bad['parsed']['vsh_draw_overlap_samples'][-1]['transformed_vertices'] += 1
        with self.assertRaisesRegex(ValueError, 'do not conserve'):
            summarize(bad, 100, 300)
        bad = copy.deepcopy(self.report)
        # Both endpoints are internally consistent; the CPU row goes backwards.
        for field in COUNTERS:
            before_value = bad['parsed']['vsh_draw_overlap_rows'][0][field]
            bad['parsed']['vsh_draw_overlap_rows'][2][field] = before_value // 2
        for field in COUNTERS:
            bad['parsed']['vsh_draw_overlap_samples'][-1][field] = sum(
                row[field] for row in bad['parsed']['vsh_draw_overlap_rows'] if row['deadline'] == 300)
        with self.assertRaisesRegex(ValueError, 'invalid cumulative'):
            summarize(bad, 100, 300)

    def test_discarded_clocks_unknown_routes_and_overflow_are_rejected(self):
        bad = copy.deepcopy(self.report)
        bad['parsed']['vsh_draw_overlap_rows'][3]['discarded_deltas'] = 1
        bad['parsed']['vsh_draw_overlap_samples'][-1]['discarded_deltas'] = 1
        with self.assertRaisesRegex(ValueError, 'discarded timing'):
            summarize(bad, 100, 300)
        bad = copy.deepcopy(self.report)
        row = bad['parsed']['vsh_draw_overlap_rows'][3]
        row['outcome'] = 'unknown'
        row['outcome_unknown'], row['outcome_generated_gpu'] = 5, 0
        header = bad['parsed']['vsh_draw_overlap_samples'][-1]
        header['outcome_unknown'], header['outcome_generated_gpu'] = 5, 0
        with self.assertRaisesRegex(ValueError, 'unresolved outcome'):
            summarize(bad, 100, 300)
        bad = copy.deepcopy(self.report)
        generated = bad['parsed']['vsh_draw_overlap_rows'][3]
        overflow = bad['parsed']['vsh_draw_overlap_rows'][4]
        for field in COUNTERS:
            overflow[field], generated[field] = generated[field], 0
        with self.assertRaisesRegex(ValueError, 'overflow prevents'):
            summarize(bad, 100, 300)

    def test_clock_scale_and_semantics_must_be_stable(self):
        for field, value in (('clock_ns_per_tick', float('nan')),
                             ('clock_ns_per_tick', 0.5), ('enabled', 0),
                             ('inclusive_setup', 0), ('schema', 99)):
            bad = copy.deepcopy(self.report)
            bad['parsed']['vsh_draw_overlap_samples'][-1][field] = value
            with self.assertRaises(ValueError):
                summarize(bad, 100, 300)


if __name__ == '__main__':
    unittest.main()
