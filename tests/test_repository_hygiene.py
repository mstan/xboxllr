"""Exercise actual index/history publication boundaries using disposable Git repos."""
import importlib.util
import pathlib
import subprocess
import tempfile
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'tools/check_repository_hygiene.py'
spec = importlib.util.spec_from_file_location('hygiene', SCRIPT)
hygiene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hygiene)


class HygieneTests(unittest.TestCase):
    def test_private_paths_and_source_exceptions(self):
        for name in ['xbox-hdd-stockdash.img', 'renamed.rom', 'backup/eeprom.json',
                     'private/data.txt', 'a/.env.local', 'a/hardware.json', 'disk.vhdx',
                     'backup.zip', 'key.pem', 'a/test.rep/data', 'capture.pcapng']:
            self.assertTrue(hygiene.private_path(name), name)
        for name in ['src/machine/nv2a.cpp', 'goldens/reference.json', 'docs/INPUTS.md']:
            self.assertFalse(hygiene.private_path(name), name)

    def test_renamed_dump_and_quoted_console_key(self):
        self.assertIn(('binary data', 1), list(hygiene.content_findings(b'renamed\0dump')))
        payload = b'{"hdd_key": "' + b'1a' * 16 + b'"}'
        self.assertIn(('literal credential', 1), list(hygiene.content_findings(payload)))

    def test_index_is_scanned_not_unstaged_replacement(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            hygiene.git(root, 'init', '-q')
            path = root / 'apparently-safe.txt'
            path.write_bytes(b'\0private dump')
            hygiene.git(root, 'add', '.')
            path.write_text('safe working copy\n')
            self.assertEqual(hygiene.scan(root), 1)

    def test_deleted_history_and_reused_blob_path(self):
        with tempfile.TemporaryDirectory() as temp:
            root = pathlib.Path(temp)
            hygiene.git(root, 'init', '-q')
            hygiene.git(root, 'config', 'user.email', 'fixture@example.invalid')
            hygiene.git(root, 'config', 'user.name', 'Fixture')
            (root / 'safe.txt').write_text('same harmless content\n')
            hygiene.git(root, 'add', '.')
            hygiene.git(root, 'commit', '-qm', 'safe')
            (root / 'disk.img').write_text('same harmless content\n')
            hygiene.git(root, 'add', '.')
            hygiene.git(root, 'commit', '-qm', 'private path shares existing blob')
            hygiene.git(root, 'rm', '-q', 'disk.img')
            hygiene.git(root, 'commit', '-qm', 'delete')
            self.assertEqual(hygiene.scan(root), 0)
            self.assertEqual(hygiene.scan(root, history=True), 1)


if __name__ == '__main__':
    unittest.main()
