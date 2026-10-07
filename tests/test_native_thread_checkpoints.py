import unittest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_thread_checkpoints import instrument


def function(name, code):
    return 'void ' + name + '(void)\n{\n' + code + '}\n'


class CheckpointTests(unittest.TestCase):
    def test_backward_only_and_idempotent(self):
        source = function('sub_A', 'loc_A: ;\n    if (x) goto loc_B;\n'
                          'loc_B: ;\n    if (y) goto loc_A;\n')
        result, count = instrument(source)
        self.assertEqual(count, 1)
        self.assertIn('loc_A: ;\n    PILOT_THREAD_CHECKPOINT();\n', result)
        self.assertIn('loc_B: ;\n    if (y)', result)
        self.assertEqual(instrument(result), (result, 1))

    def test_scope_and_conditional_syntax(self):
        first = function('sub_A', 'loc_A: ;\n    if (x) goto loc_A; else return;\n')
        second = function('sub_B', '    goto loc_A;\nloc_A: ;\n    return;\n')
        result, count = instrument(first + second)
        self.assertEqual(count, 1)
        self.assertTrue(result.endswith(second))
        self.assertIn('if (x) goto loc_A; else return;', result)

    def test_no_cross_function_target(self):
        source = function('sub_A', 'loc_A: ;\n') + function('sub_B', '    goto loc_A;\n')
        with self.assertRaisesRegex(ValueError, 'missing within function'):
            instrument(source)

    def test_renamed_original_and_tail_helper(self):
        source = function('pilot_original_audio_A', 'loc_A: ;\n    goto loc_A;\n')
        source += 'void missing(void) { RECOMP_UNIMPL("missing", 1); }\n'
        result, count = instrument(source)
        self.assertEqual(count, 1)
        self.assertTrue(result.endswith('void missing(void) { RECOMP_UNIMPL("missing", 1); }\n'))


if __name__ == '__main__':
    unittest.main()
