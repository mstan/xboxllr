"""Hash-bound whole-draw admission controls; leaf arithmetic has separate fixtures."""
from pathlib import Path
import copy
import hashlib
import json
import sys
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_scene_surviving_mixed_reference as surviving
import qualify_native_scene_surviving_mixed as qualifier


class SurvivingMixedAdmissionControls(unittest.TestCase):
    def call(self,number,capture_sha,scene_sha):
        capture = {'input_sequence':number,'sha256':capture_sha}
        scene = {'sha256':scene_sha}
        return surviving.expected_bounded_surviving_draw(
            capture,scene,[],[],{}, {},'fma-b-first','fma')

    def test_both_exact_pairs_route_the_unchanged_whole_draw_component(self):
        for number,pair in surviving.SURVIVING_INPUT_SHA256.items():
            with self.subTest(input=number):
                with patch.object(surviving,'expected_surviving_mixed_draw',return_value='survivor') as admit:
                    with patch.object(surviving,'expected_empty_depth_draw') as old:
                        self.assertEqual(self.call(number,*pair),'survivor')
                admit.assert_called_once();old.assert_not_called()
                self.assertEqual(admit.call_args.args[0]['input_sequence'],number)
                self.assertEqual(admit.call_args.args[-2:],('fma-b-first','fma'))

    def test_capture_scene_or_cross_pair_faults_do_not_reach_equations(self):
        a,b = surviving.SURVIVING_INPUT_SHA256[1969]
        faults = [('changed capture',b),(a,'changed scene'),
                  surviving.SURVIVING_INPUT_SHA256[1943],
                  (a,surviving.SCENE_1943_SHA256),
                  (surviving.CAPTURE_1943_SHA256,b)]
        for pair in faults:
            with self.subTest(pair=pair):
                with patch.object(surviving,'expected_surviving_mixed_draw') as admit:
                    with patch.object(surviving,'expected_empty_depth_draw') as old:
                        with self.assertRaisesRegex(ValueError,'Wrong original draw1969'):
                            self.call(1969,*pair)
                admit.assert_not_called();old.assert_not_called()

    def test_1943_rejects_1969_pair_and_keeps_its_original_binding(self):
        with self.assertRaisesRegex(ValueError,'Wrong original draw1943'):
            self.call(1943,*surviving.SURVIVING_INPUT_SHA256[1969])
        self.assertEqual(surviving.SURVIVING_INPUT_SHA256[1943],
            (surviving.CAPTURE_1943_SHA256,surviving.SCENE_1943_SHA256))

    def test_other_input_with_matching_1969_hashes_keeps_old_admission(self):
        for number in (1968,1970,2560):
            with self.subTest(input=number):
                with patch.object(surviving,'expected_surviving_mixed_draw') as admit:
                    with patch.object(surviving,'expected_empty_depth_draw',
                                      side_effect=ValueError('existing domain guard')) as old:
                        with self.assertRaisesRegex(ValueError,'existing domain guard'):
                            self.call(number,*surviving.SURVIVING_INPUT_SHA256[1969])
                admit.assert_not_called();old.assert_called_once()

    def test_public_wrapper_rejects_other_or_noninteger_bounds_before_execution(self):
        for number in (1942,1944,1968,1970,1983,2119,2121,2560,True,1969.0,2120.0):
            with self.subTest(bound=number):
                with patch.object(qualifier,'original_prefix') as execute:
                    with self.assertRaisesRegex(ValueError,'stops at original draw'):
                        qualifier.run(SimpleNamespace(last_draw=number))
                execute.assert_not_called()

    def wrapper(self,bound,rows):
        with TemporaryDirectory() as directory:
            root = Path(directory)
            library = root/'declared-library';library.write_bytes(b'authored build identity')
            evidence = root/'clip.json'
            evidence.write_text(json.dumps(dict(library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
                clip_arithmetic='fma',selected_from_disassembly_before_pixels=True)))
            args = SimpleNamespace(last_draw=bound,library=library,clip_evidence=evidence,
                clip_arithmetic='fma',depth_arithmetic='fma-b-first',source=root/'source',
                diagnostics=root/'diagnostics',output=root/'report.json')
            with patch.object(qualifier,'surviving_software_evidence',return_value={'code_sites':[]}):
                with patch.object(qualifier,'original_prefix',return_value={'rows':copy.deepcopy(rows)}) as execute:
                    report = qualifier.run(args)
            return report,execute.call_args

    def test_public_wrapper_uses_explicit_bound_and_reports_only_exercised_components(self):
        rows = [dict(input=1943,surviving_mixed_w_component=True,expected_written_pixels=160),
                dict(input=1969,surviving_mixed_w_component=True,expected_written_pixels=880),
                dict(input=1983,surviving_mixed_w_component=True,expected_written_pixels=207)]
        for bound,selected in ((1943,rows[:1]),(1969,rows[:2]),(2120,rows)):
            with self.subTest(bound=bound):
                report,call = self.wrapper(bound,selected)
                self.assertEqual(call.kwargs['prefix_limit'],bound)
                self.assertEqual(report['surviving_mixed_draws'],[r['input'] for r in selected])
                self.assertFalse(report['surviving_mixed_w_pixels_qualified'])
                self.assertFalse(report['surviving_depth_range_component_qualified'])

    def test_missing_new_component_or_no_new_writes_cannot_qualify(self):
        first = dict(input=1943,surviving_mixed_w_component=True,expected_written_pixels=160)
        for rows in ([first],[first,dict(input=1969,surviving_mixed_w_component=True,
                                        expected_written_pixels=0)]):
            with self.subTest(rows=rows):
                with self.assertRaisesRegex(ValueError,'not independently exercised'):
                    self.wrapper(1969,rows)

    def test_unexpected_third_component_cannot_expand_a_report(self):
        rows = [dict(input=n,surviving_mixed_w_component=True,expected_written_pixels=1)
                for n in (1943,1969,1970)]
        with self.assertRaisesRegex(ValueError,'not independently exercised'):
            self.wrapper(1969,rows)

    def test_original1983_wrong_capture_scene_or_cross_pair_cannot_reach_equations(self):
        a,b = surviving.SURVIVING_INPUT_SHA256[1983]
        faults = [('changed capture',b),(a,'changed scene'),
                  surviving.SURVIVING_INPUT_SHA256[1943],surviving.SURVIVING_INPUT_SHA256[1969],
                  (a,surviving.SCENE_1969_SHA256),(surviving.CAPTURE_1969_SHA256,b)]
        for pair in faults:
            with self.subTest(pair=pair):
                with patch.object(surviving,'expected_surviving_mixed_draw') as admit:
                    with patch.object(surviving,'expected_empty_depth_draw') as old:
                        with self.assertRaisesRegex(ValueError,'Wrong original draw1983'):
                            self.call(1983,*pair)
                admit.assert_not_called();old.assert_not_called()

    def test_1983_hashes_on_unbound_inputs_keep_existing_guard(self):
        for number in (1984,2120,2121):
            with self.subTest(input=number):
                with patch.object(surviving,'expected_surviving_mixed_draw') as admit:
                    with patch.object(surviving,'expected_empty_depth_draw',
                                      side_effect=ValueError('existing domain guard')) as old:
                        with self.assertRaisesRegex(ValueError,'existing domain guard'):
                            self.call(number,*surviving.SURVIVING_INPUT_SHA256[1983])
                admit.assert_not_called();old.assert_called_once()

    def test_2120_report_requires_all_three_positive_write_components_and_no_fourth(self):
        rows = [dict(input=n,surviving_mixed_w_component=True,expected_written_pixels=1)
                for n in (1943,1969,1983)]
        faults = [rows[:2],rows+[dict(input=2121,surviving_mixed_w_component=True,expected_written_pixels=1)]]
        no_writes = copy.deepcopy(rows);no_writes[-1]['expected_written_pixels'] = 0
        faults.append(no_writes)
        for faulty in faults:
            with self.subTest(rows=faulty):
                with self.assertRaisesRegex(ValueError,'not independently exercised'):
                    self.wrapper(2120,faulty)

if __name__ == '__main__':unittest.main()
