"""The release boundary must exclude used profiles, inputs and tampering."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_release_archive import archive
import native_release_archive as release_archive


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root/'pristine'
        self.source.mkdir()
        (self.source/'README-FIRST.txt').write_bytes(b'example')
        self.metadata = dict(schema='xboxllr.native-preview.v1', title=dict(name='Authored fixture'),
            distribution='private-owner-preview; not publication-cleared',
            files={'README-FIRST.txt': hashlib.sha256(b'example').hexdigest()})
        self.save()
        self.output = self.root/'candidate.zip'

    def save(self):
        (self.source/'release.json').write_text(json.dumps(self.metadata))

    def test_archive_contains_exact_verified_payload(self):
        result = archive(self.source, self.output)
        with zipfile.ZipFile(self.output) as z:
            self.assertEqual(set(z.namelist()), {'release.json', 'README-FIRST.txt'})
            self.assertEqual(z.read('README-FIRST.txt'), b'example')
        self.assertEqual(result['sha256'], hashlib.sha256(self.output.read_bytes()).hexdigest())

    def test_used_profile_or_extra_file_rejects_before_output(self):
        (self.source/'data').mkdir()
        (self.source/'data'/'save-keys.bin').write_bytes(b'private')
        with self.assertRaisesRegex(ValueError, 'unmanifested'):
            archive(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_changed_payload_rejects_before_output(self):
        (self.source/'README-FIRST.txt').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'changed'):
            archive(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_owned_input_is_rejected_even_when_manifested(self):
        (self.source/'default.xbe').write_bytes(b'owned input')
        self.metadata['files']['default.xbe'] = hashlib.sha256(b'owned input').hexdigest()
        self.save()
        with self.assertRaisesRegex(ValueError, 'Private or unsafe'):
            archive(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_startup_packs_are_rejected_including_renamed_payloads(self):
        for name in ('xboot.pak', 'renamed-startup.dat'):
            payload = b'XBPK' + bytes(32)
            path = self.source / name
            path.write_bytes(payload)
            self.metadata['files'][name] = hashlib.sha256(payload).hexdigest()
            self.save()
            with self.assertRaisesRegex(ValueError, 'Private'):
                archive(self.source, self.output)
            self.assertFalse(self.output.exists())
            path.unlink()
            del self.metadata['files'][name]

    def test_existing_output_is_preserved(self):
        self.output.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            archive(self.source, self.output)
        self.assertEqual(self.output.read_bytes(), b'keep')

    def test_loose_startup_logo_and_renamed_known_content_rejected(self):
        # Authored payload only; a fingerprint fixture models content admission.
        payload = b'authored private after-image fixture'
        fingerprint = hashlib.sha256(payload).hexdigest()
        for name in ('original-logo.rle', 'renamed-original-logo.dat'):
            path = self.source / name
            path.write_bytes(payload)
            self.metadata['files'][name] = fingerprint
            self.save()
            with patch.object(release_archive, 'PRIVATE_STARTUP_LOGOS', frozenset({fingerprint})):
                with self.assertRaisesRegex(ValueError, 'Private'):
                    archive(self.source, self.output)
            self.assertFalse(self.output.exists())
            path.unlink()
            del self.metadata['files'][name]

    def test_private_diagnostic_metadata_rejected_even_when_manifested(self):
        for folder in ('diagnostics', 'PRIVATE-DO-NOT-SHARE'):
            path = self.source / folder / 'replay.json'
            path.parent.mkdir()
            path.write_text('{"fixture": "private tester metadata"}')
            self.metadata['files'][path.relative_to(self.source).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
            self.save()
            with self.assertRaisesRegex(ValueError, 'Private or unsafe'):
                archive(self.source, self.output)
            self.assertFalse(self.output.exists())
            del self.metadata['files'][path.relative_to(self.source).as_posix()]
            path.unlink()
            path.parent.rmdir()
