"""Reject stale or changed DSP preparation before selecting its external library."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_dsp_donor import PIN, preparation_identity


class PreparationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='xboxllr-dsp-preparation-')
        self.addCleanup(self.temporary.cleanup)
        self.source = Path(self.temporary.name)
        self.record = dict(revision=PIN, preparation=preparation_identity(),
                           sources={}, guarded={})
        for name, data in {'Cargo.lock': b'authored dependency fixture\n',
                           'guard.rs': b'authored maintained guard\n'}.items():
            (self.source / name).write_bytes(data)
            self.record['sources'][name] = hashlib.sha256(data).hexdigest()
        # A patched source must be checked against its guarded hash, not the
        # pristine hash, just as in a real prepared donor.
        self.record['guarded']['guard.rs'] = self.record['sources']['guard.rs']
        self.record['sources']['guard.rs'] = hashlib.sha256(b'pristine fixture').hexdigest()
        self.driver = self.source / 'validate.cmake'
        self.driver.write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            f'include("{(ROOT / "cmake/ValidateDspSource.cmake").as_posix()}")\n'
            f'xlle_validate_dsp_source("{self.source.as_posix()}")\n', encoding='utf-8')

    def run_gate(self, expected=None):
        (self.source / 'donor.json').write_text(json.dumps(self.record), encoding='utf-8')
        cmake = os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
        self.assertIsNotNone(cmake, 'CMake is required for the preparation check')
        result = subprocess.run([cmake, '-P', str(self.driver)],
                                capture_output=True, text=True, timeout=30)
        if expected is None:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(expected, result.stderr)

    def test_current_preparation_passes(self):
        self.run_gate()

    def test_pre_sen_manifest_rejected(self):
        del self.record['preparation']
        self.run_gate('Stale DSP preparation')

    def test_changed_generator_rejected(self):
        self.record['preparation']['generators']['native_dsp_sen.py'] = '0' * 64
        self.run_gate('Stale DSP generator identity')

    def test_changed_source_rejected(self):
        (self.source / 'guard.rs').write_bytes(b'stale guarded implementation\n')
        self.run_gate('Prepared DSP source hash mismatch: guard.rs')

    def test_missing_source_rejected(self):
        (self.source / 'Cargo.lock').unlink()
        self.run_gate('Prepared DSP source is missing: Cargo.lock')

    def test_unreviewed_revision_rejected(self):
        self.record['revision'] = '0' * 40
        self.run_gate('Unreviewed prepared DSP revision')


if __name__ == '__main__':
    unittest.main()
