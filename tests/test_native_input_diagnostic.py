"""Drive the actual C input boundary with authored states, no game/assets.

Set XBOXLLR_INPUT_DIAGNOSTIC_TEST_EXE and XBOXLLR_DIAGNOSTIC_TEST_DIR to run.
The build's CTest registration supplies both; Python-only suites skip it.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_diagnostics import INPUT_HEADER, INPUT_RECORD, inspect_inputs


@unittest.skipUnless(os.environ.get('XBOXLLR_INPUT_DIAGNOSTIC_TEST_EXE'), 'host fixture executable not configured')
class HostInputDiagnosticTests(unittest.TestCase):
    def test_actual_capture_replay_mismatch_corruption_and_queue_exhaustion(self):
        binary = Path(os.environ['XBOXLLR_INPUT_DIAGNOSTIC_TEST_EXE']).resolve(strict=True)
        root = Path(os.environ['XBOXLLR_DIAGNOSTIC_TEST_DIR'])
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as temporary:
            work = Path(temporary)
            capture = work / 'capture.bin'
            env = {key: value for key, value in os.environ.items() if not key.startswith('XLLE_PILOT_')}
            for name in ('TEMP', 'TMP', 'APPDATA', 'LOCALAPPDATA'):
                directory = work / name.lower()
                directory.mkdir(exist_ok=True)
                env[name] = str(directory)
            def run(mode, path):
                return subprocess.run([str(binary), mode, str(path)], env=env, cwd=work,
                    capture_output=True, text=True, timeout=10)
            saved = run('capture', capture)
            self.assertEqual(saved.returncode, 0, saved.stderr)
            info = inspect_inputs(capture)
            self.assertEqual((info['records'], info['state_calls'], info['capabilities_calls'], info['rumble_calls']), (4, 2, 1, 1))
            with capture.open('rb') as source:
                self.assertEqual(source.read(16), INPUT_HEADER)
                record = INPUT_RECORD.unpack(source.read(64))
                self.assertEqual(record[6][2], 255)  # Actual converted A button.
                self.assertEqual(record[6][10:12], b'\x34\x12')
            replay = run('replay', capture)
            self.assertEqual(replay.returncode, 4, replay.stderr)
            self.assertIn('REPLAY EXHAUSTED', replay.stderr)
            mismatch = run('diverge', capture)
            self.assertEqual(mismatch.returncode, 3)
            self.assertIn('DIVERGENCE', mismatch.stderr)
            fatal_path = work / 'fatal.bin'
            fatal = run('fatal', fatal_path)
            self.assertEqual(fatal.returncode, 2, fatal.stderr)
            self.assertIn('records=4 flushed=1', fatal.stderr)
            self.assertEqual(inspect_inputs(fatal_path)['records'], 4)
            concurrent_path = work / 'concurrent.bin'
            concurrent = run('concurrent', concurrent_path)
            self.assertEqual(concurrent.returncode, 0, concurrent.stderr)
            # inspect_inputs checks every sequence and timestamp, not just size.
            self.assertEqual(inspect_inputs(concurrent_path)['records'], 2401)
            corrupt = work / 'corrupt.bin'
            changed = bytearray(capture.read_bytes())
            changed[16+63] = 1  # Reserved byte cannot become a hidden input.
            corrupt.write_bytes(changed)
            self.assertEqual(run('replay', corrupt).returncode, 3)
            corrupt.write_bytes(capture.read_bytes()[:-1])
            self.assertEqual(run('replay', corrupt).returncode, 3)
            overflow_path = work / 'overflow.bin'
            overflow = run('overflow', overflow_path)
            self.assertEqual(overflow.returncode, 0, overflow.stderr)
            self.assertIn('INPUTS INCOMPLETE', overflow.stderr)
            self.assertLessEqual(inspect_inputs(overflow_path)['records'], 4097)


if __name__ == '__main__':
    unittest.main()
