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
            env['XLLE_PILOT_INPUT_REPLAY_OWNER'] = os.environ.get('XBOXLLR_INPUT_REPLAY_OWNER', '1')
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
            timed = work / 'timed.bin'
            press_a=bytes([0,0,255])+bytes(19)
            press_b=bytes([0,0,0,255])+bytes(18)
            rows=[(500,1,0,press_a),(500,2,0,bytes([255])*22),
                  (600,1,0,bytes(22)),(700,1,0,press_b),(800,1,1167,bytes(22))]
            timed.write_bytes(INPUT_HEADER+b''.join(INPUT_RECORD.pack(seq,at,kind,0,status,0,payload,bytes(10))
                for seq,(at,kind,status,payload) in enumerate(rows)))
            played=run('timed',timed)
            self.assertEqual(played.returncode,0,played.stderr)
            self.assertIn('INPUT PLAYBACK END',played.stderr)
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

    def test_large_replay_exact_bytes_order_eof_and_refill_boundary_corruption(self):
        binary = Path(os.environ['XBOXLLR_INPUT_DIAGNOSTIC_TEST_EXE']).resolve(strict=True)
        root = Path(os.environ['XBOXLLR_DIAGNOSTIC_TEST_DIR'])
        root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=root) as temporary:
            path = Path(temporary)/'large.bin'
            def write_rows(timed):
                with path.open('wb') as stream:
                    stream.write(INPUT_HEADER)
                    for seq in range(262144):
                        payload = bytearray(22)
                        payload[0]=seq&255;payload[1]=(seq>>8)&255;payload[2]=255
                        payload[10:12]=b'\x34\x12'
                        stream.write(INPUT_RECORD.pack(seq,(seq//1024)*10000 if timed else 0,
                            1,0,0,0,bytes(payload),bytes(10)))
            write_rows(False)
            env = {key: value for key, value in os.environ.items() if not key.startswith('XLLE_PILOT_')}
            env['XLLE_PILOT_INPUT_REPLAY_OWNER'] = os.environ.get('XBOXLLR_INPUT_REPLAY_OWNER', '1')
            def run(mode):
                return subprocess.run([str(binary),mode,str(path)],env=env,capture_output=True,text=True,timeout=20)
            result=run('bulk')
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('records=262144',result.stdout)
            self.assertEqual(run('bulk-eof').returncode,4)
            # Future malformed rows must be rejected on their own delivery,
            # including either side of the 64KiB read-ahead boundary.
            for seq in (1023,1024):
                with path.open('r+b') as stream:
                    stream.seek(16+64*seq+63);stream.write(b'\x01')
                result=run('bulk')
                self.assertEqual(result.returncode,3,result.stderr)
                self.assertIn(f'sequence={seq} ',result.stderr)
                with path.open('r+b') as stream:
                    stream.seek(16+64*seq+63);stream.write(b'\0')
            # The timed reader must retain exactly one future row across the
            # same refill boundaries, then consume it only when its time is due.
            write_rows(True)
            result=run('bulk-timed')
            self.assertEqual(result.returncode,0,result.stderr)
            with path.open('r+b') as stream:
                stream.seek(16+64*1025+63);stream.write(b'\x01')
            result=run('bulk-timed')
            self.assertEqual(result.returncode,3,result.stderr)
            self.assertIn('block=0 records=1024',result.stdout)


if __name__ == '__main__':
    unittest.main()
