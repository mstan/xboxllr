"""Composition must recheck the complete nested payload, not just its frontend."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_release_archive import compose
from test_windows_dependencies import pe


class CompositionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.output = self.root / 'composed.zip'
        self.files = {'Player.exe': pe(['kernel32.dll']),
            'runtime/Runtime.exe': pe(delayed=['bridge.dll']),
            'runtime/bridge.dll': pe(['user32.dll']),
            'runtime/Source/NOTICE.txt': b'authored source notice',
            'runtime/Source/helper.c': b'/* authored matching source */'}

    def build(self):
        return compose(self.files, self.output, title={'name': 'Authored fixture'},
            distribution='private-owner-preview', provenance={'runtime_revision': 'frozen fixture'})

    def test_exact_payload_hashes_and_nested_delay_closure(self):
        self.build()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(set(archive.namelist()), set(self.files) | {'release.json'})
            metadata = json.loads(archive.read('release.json'))
            for name, expected in self.files.items():
                self.assertEqual(archive.read(name), expected)
                self.assertEqual(metadata['files'][name], hashlib.sha256(expected).hexdigest())
            self.assertTrue(metadata['windows_dependencies']['complete'])
            sources = [x['source'] for x in metadata['windows_dependencies']['files']]
            self.assertEqual(set(sources), {'Player.exe', 'runtime/Runtime.exe', 'runtime/bridge.dll'})

    def test_missing_nested_delay_dependency_leaves_no_archive(self):
        del self.files['runtime/bridge.dll']
        with self.assertRaisesRegex(ValueError, 'dependency closure'):
            self.build()
        self.assertFalse(self.output.exists())

    def test_nested_used_profile_rejected_even_when_manifested(self):
        self.files['runtime/data/settings.json'] = b'{}'
        with self.assertRaisesRegex(ValueError, 'Private or unsafe'):
            self.build()
        self.assertFalse(self.output.exists())

    def test_case_collision_and_reserved_metadata_reject(self):
        for name in ('PLAYER.EXE', 'Release.JSON', '../escape.txt'):
            with self.subTest(name=name):
                self.files[name] = b'authored fixture'
                with self.assertRaises(ValueError):
                    self.build()
                self.files.pop(name)
        self.assertFalse(self.output.exists())

    def test_existing_destination_is_preserved(self):
        self.output.write_bytes(b'keep existing owner archive')
        with self.assertRaises(ValueError):
            self.build()
        self.assertEqual(self.output.read_bytes(), b'keep existing owner archive')
