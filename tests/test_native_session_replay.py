"""Exercise the compiled transport: concurrent actors, exact replay and hostile journals."""
import os
import gzip
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('XBOXLLR_SESSION_REPLAY_TEST_EXE'), 'compiled transport fixture not configured')
class TransportTests(unittest.TestCase):
    def test_record_replay_and_rejection(self):
        binary = Path(os.environ['XBOXLLR_SESSION_REPLAY_TEST_EXE']).resolve(strict=True)
        parent = Path(os.environ['XBOXLLR_SESSION_REPLAY_TEST_DIR'])
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=parent) as name:
            root = Path(name)
            capture = root / 'capture'
            capture.mkdir()

            def run(mode, folder, expected):
                result = subprocess.run([str(binary), mode, str(folder)], capture_output=True, text=True, timeout=20)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
                return result.stderr

            self.assertIn('complete=1', run('record', capture, 0))
            run('replay', capture, 0)
            for mode in ('identity', 'evidence', 'schedule'):
                self.assertIn('SESSION REPLAY DIVERGENCE', run(mode, capture, 3))
            original = gzip.decompress((capture / 'actor-00000000.xlr').read_bytes())
            for change in ('truncated', 'payload', 'reserved', 'oversized', 'sequence'):
                corrupt = root / change
                shutil.copytree(capture, corrupt)
                altered = bytearray(original)
                if change == 'truncated':
                    altered = altered[:-1]
                elif change == 'payload':
                    altered[48+32] ^= 1
                elif change == 'reserved':
                    altered[48+28] = 1
                elif change == 'oversized':
                    altered[48+12:48+16] = (0xffffffff).to_bytes(4, 'little')
                else:
                    altered[48] = 1
                (corrupt / 'actor-00000000.xlr').write_bytes(gzip.compress(altered))
                self.assertIn('SESSION REPLAY DIVERGENCE', run('replay', corrupt, 3))
            for change in ('gzip-truncated', 'gzip-checksum'):
                corrupt = root / change
                shutil.copytree(capture, corrupt)
                path = corrupt / 'actor-00000000.xlr'
                raw = bytearray(path.read_bytes())
                if change == 'gzip-truncated':
                    raw = raw[:-6]
                else:
                    raw[-8] ^= 1
                path.write_bytes(raw)
                self.assertIn('SESSION REPLAY DIVERGENCE', run('replay', corrupt, 3))
            overflow = root / 'overflow'
            overflow.mkdir()
            self.assertIn('INCOMPLETE', run('overflow', overflow, 0))
            terminal = root / 'terminal'
            terminal.mkdir()
            self.assertIn('complete=1', run('terminal-cut', terminal, 0))
            self.assertEqual([p.name for p in terminal.iterdir()], ['actor-00000000.xlr'])
            # Exclusive new-session ownership: never truncate an existing run.
            before = {p.name: p.read_bytes() for p in capture.iterdir()}
            run('record', capture, 10)
            self.assertEqual(before, {p.name: p.read_bytes() for p in capture.iterdir()})


if __name__ == '__main__':
    unittest.main()
