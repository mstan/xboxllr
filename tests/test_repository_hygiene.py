"""Exercise actual index/history publication boundaries using disposable Git repos."""
import importlib.util
import hashlib
import pathlib
import subprocess
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'tools/check_repository_hygiene.py'
spec = importlib.util.spec_from_file_location('hygiene', SCRIPT)
hygiene = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hygiene)


class HygieneTests(unittest.TestCase):
    def test_private_paths_and_source_exceptions(self):
        for name in ['xbox-hdd-stockdash.img', 'renamed.rom', 'backup/eeprom.json',
                     'private/data.txt', 'a/.env.local', 'a/hardware.json', 'disk.vhdx',
                     'backup.zip', 'key.pem', 'a/test.rep/data', 'capture.pcapng',
                     'EEPROMBackup.cfg', 'export/EEPROM.INI']:
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

    def test_reviewed_image_requires_exact_index_bytes_and_path(self):
        image = b'\x89PNG\r\n\0reviewed documentation fixture'
        name = 'docs/screenshots/reviewed.png'
        with tempfile.TemporaryDirectory() as temp, patch.dict(
                hygiene.DOCUMENTATION_IMAGES, {name: hashlib.sha256(image).hexdigest()}, clear=True):
            root = pathlib.Path(temp)
            hygiene.git(root, 'init', '-q')
            path = root / name
            path.parent.mkdir(parents=True)
            path.write_bytes(image)
            hygiene.git(root, 'add', '.')
            self.assertEqual(hygiene.scan(root), 0)
            path.write_bytes(image + b'changed')
            hygiene.git(root, 'add', '.')
            path.write_bytes(image)  # Unstaged approved bytes cannot hide changed index bytes.
            self.assertEqual(hygiene.scan(root), 1)
            hygiene.git(root, 'add', '.')
            (root / 'renamed.txt').write_bytes(image)
            hygiene.git(root, 'add', '.')
            self.assertEqual(hygiene.scan(root), 1)

    def test_reviewed_image_history_and_unreviewed_historical_alias(self):
        image = b'\x89PNG\r\n\0reviewed historical fixture'
        name = 'docs/screenshots/reviewed.png'
        with tempfile.TemporaryDirectory() as temp, patch.dict(
                hygiene.DOCUMENTATION_IMAGES, {name: hashlib.sha256(image).hexdigest()}, clear=True):
            root = pathlib.Path(temp)
            hygiene.git(root, 'init', '-q')
            hygiene.git(root, 'config', 'user.email', 'fixture@example.invalid')
            hygiene.git(root, 'config', 'user.name', 'Fixture')
            path = root / name
            path.parent.mkdir(parents=True)
            path.write_bytes(image)
            hygiene.git(root, 'add', '.')
            hygiene.git(root, 'commit', '-qm', 'approved image')
            self.assertEqual(hygiene.scan(root, history=True), 0)
            (root / 'alias.txt').write_bytes(image)
            hygiene.git(root, 'add', '.')
            hygiene.git(root, 'commit', '-qm', 'unreviewed binary alias')
            hygiene.git(root, 'rm', '-q', 'alias.txt')
            hygiene.git(root, 'commit', '-qm', 'delete alias')
            self.assertEqual(hygiene.scan(root), 0)
            self.assertEqual(hygiene.scan(root, history=True), 1)

    def test_replaced_reviewed_image_keeps_exact_old_and_new_history_fingerprints(self):
        versions = (b'\x89PNG\0old reviewed capture', b'\x89PNG\0new owner-selected capture')
        name = 'docs/screenshots/reviewed.png'
        with tempfile.TemporaryDirectory() as temp, patch.dict(hygiene.DOCUMENTATION_IMAGES,
                {name: tuple(hashlib.sha256(image).hexdigest() for image in versions)}, clear=True):
            root = pathlib.Path(temp)
            hygiene.git(root, 'init', '-q')
            hygiene.git(root, 'config', 'user.email', 'fixture@example.invalid')
            hygiene.git(root, 'config', 'user.name', 'Fixture')
            path = root / name
            path.parent.mkdir(parents=True)
            for index, image in enumerate(versions):
                path.write_bytes(image)
                hygiene.git(root, 'add', '.')
                hygiene.git(root, 'commit', '-qm', f'reviewed capture {index}')
            self.assertEqual(hygiene.scan(root, history=True), 0)
            path.write_bytes(versions[-1] + b'unreviewed change')
            hygiene.git(root, 'add', '.')
            self.assertEqual(hygiene.scan(root, history=True), 1)


if __name__ == '__main__':
    unittest.main()
