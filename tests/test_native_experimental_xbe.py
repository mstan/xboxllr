"""Prerelease admission, isolated saves and public reports with authored XBEs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from test_native_save_keys import authored_xbe
from test_native_disc import make_disc, SilentProgress
from native_experimental_xbe import fingerprint, MODE
from native_revisions import select, profile_relative
import native_release_runtime as runtime
from native_mod_launcher import Catalog
from native_crash_report import export_report


class ExperimentalXbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.original = authored_xbe()
        self.reference = fingerprint(self.original)
        self.data = dict(title=dict(name='Authored title', game_id='F00D0001',
            xbe_sha256=self.reference['sha256']), executable='game.exe', runtime={},
            capabilities=dict(visual_mods=False), experimental_xbe=dict(mode=MODE, reference=self.reference))
        self.image = self.root / 'default.xbe'
        modified = bytearray(self.original)
        modified[4] ^= 1  # Signature/header bytes only; payload is unchanged.
        self.image.write_bytes(modified)

    def test_strict_catalog_still_rejects_and_experiment_requires_the_actual_file(self):
        strict = dict(self.data)
        del strict['experimental_xbe']
        with self.assertRaisesRegex(ValueError, 'not a supported'):
            select(strict, runtime.sha(self.image), image=self.image)
        with self.assertRaisesRegex(ValueError, 'not a supported'):
            select(self.data, runtime.sha(self.image))
        self.assertNotIn('_experimental_input_sha256', select(self.data, self.reference['sha256']))

    def test_reports_header_only_and_payload_changes_without_admitting_a_wrong_game(self):
        selected = select(self.data, runtime.sha(self.image), image=self.image)
        report = selected['_experimental_xbe_report']
        self.assertTrue(report['body_matches'])
        self.assertEqual(report['changed_sections'], [])
        raw = bytearray(self.image.read_bytes())
        raw[0x400] ^= 1
        self.image.write_bytes(raw)
        report = select(self.data, runtime.sha(self.image), image=self.image)['_experimental_xbe_report']
        self.assertFalse(report['body_matches'])
        self.assertEqual(report['changed_sections'], [0])
        struct.pack_into('<I', raw, 0x208, 0xf00d0002)
        self.image.write_bytes(raw)
        with self.assertRaisesRegex(ValueError, 'Wrong game'):
            select(self.data, runtime.sha(self.image), image=self.image)
        with self.assertRaisesRegex(ValueError, 'changed during'):
            select(self.data, '0' * 64, raw=self.original)

    def test_iso_import_uses_the_same_admission_and_preserves_embedded_bytes(self):
        disc = self.root / 'test.iso'
        make_disc(disc, xbe=self.image.read_bytes())
        selected = select(self.data, runtime.sha(self.image), image=disc)
        with patch('native_disc_progress.Progress', return_value=SilentProgress()):
            imported = runtime.prepare_game(self.root, self.data, disc)
        self.assertEqual(imported.read_bytes(), self.image.read_bytes())
        self.assertEqual(selected['_experimental_input_sha256'], runtime.sha(imported))

    def test_profile_uses_actual_hash_and_never_uses_normal_or_selected_external_saves(self):
        normal = self.root / 'original.xbe'
        normal.write_bytes(self.original)
        profile = runtime.prepare_profile(self.root, self.data, normal)
        before = {p.name: p.read_bytes() for p in profile.iterdir() if p.is_file()}
        marker = self.root / 'external/progress.sav'
        marker.parent.mkdir()
        marker.write_bytes(b'owner progress')
        choice = self.root / 'choice.txt'
        choice.write_text('not a valid choice; experimental mode must not read it')
        selected = select(self.data, runtime.sha(self.image), image=self.image)
        with runtime.game_storage(self.root, selected, self.image, choice) as (test_profile, storage, hdd):
            self.assertIsNone(storage)
            self.assertIsNone(hdd)
            self.assertNotEqual(profile, test_profile)
            self.assertEqual(runtime.verify_profile(self.root, selected), test_profile)
            self.assertEqual((test_profile/'save-keys.bin').read_bytes()[8:40].hex(), runtime.sha(self.image))
        self.assertEqual(before, {p.name: p.read_bytes() for p in profile.iterdir() if p.is_file()})
        self.assertEqual(marker.read_bytes(), b'owner progress')
        self.assertIn(runtime.sha(self.image), profile_relative(selected))

    def test_native_launch_passes_explicit_hash_and_exports_metadata_on_runtime_stop(self):
        selected = select(self.data, runtime.sha(self.image), image=self.image)
        profile = runtime.prepare_profile(self.root, selected, self.image)
        (self.root/'game.exe').write_bytes(b'authored process fixture')
        child = Mock(pid=1, returncode=2)
        child.wait.return_value = 2
        catalog = Catalog([], self.root/'selection.json')
        with patch('native_boot_launcher.run'), patch.object(runtime.subprocess, 'Popen', return_value=child) as spawn:
            self.assertEqual(runtime.launch_game(self.root, selected, self.image, profile, catalog, automated_seconds=5), 2)
        self.assertEqual(spawn.call_args.kwargs['env']['XLLE_PILOT_EXPERIMENTAL_XBE'], runtime.sha(self.image))
        reports = list((self.root/'data/diagnostics/crash-reports').glob('*.zip'))
        self.assertEqual(len(reports), 1)
        with zipfile.ZipFile(reports[0]) as archive:
            report = json.loads(archive.read('report.json'))
            self.assertEqual(report['xbe_sha256'], runtime.sha(self.image))
            self.assertTrue(report['experimental_xbe']['body_matches'])
            self.assertFalse(any('console' in name or name.endswith('.xbe') for name in archive.namelist()))
            self.assertNotIn(str(self.root), archive.read('report.json').decode())

    def test_native_admission_helper_requires_opt_in_exact_hash_same_title_and_bounded_certificate(self):
        compiler = Path('C:/msys64/mingw64/bin/g++.exe')
        if not compiler.is_file():
            found = shutil.which('g++') or shutil.which('c++')
            if not found:
                self.skipTest('C++ compiler unavailable')
            compiler = Path(found)
        harness = self.root/'check.cpp'
        harness.write_text('''#include "experimental_xbe.h"
#include <fstream>
#include <iterator>
#include <vector>
int main(int argc,char **argv){
 std::ifstream in(argv[1],std::ios::binary);
 std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)),{});
 const char *error=pilot_experimental_xbe_check(raw.data(),raw.size(),"actual",argc>2?argv[2]:nullptr,0xf00d0001u);
 return error?2:0;
}''')
        binary = self.root/('check.exe' if os.name == 'nt' else 'check')
        subprocess.run([str(compiler), '-std=c++17', '-Wall', '-Wextra', '-Werror',
            '-I'+str(ROOT/'pilots/native-title'), str(harness), '-o', str(binary)], check=True, timeout=90)
        self.assertEqual(subprocess.run([str(binary), str(self.image)]).returncode, 2)
        self.assertEqual(subprocess.run([str(binary), str(self.image), 'other']).returncode, 2)
        self.assertEqual(subprocess.run([str(binary), str(self.image), 'actual']).returncode, 0)
        raw = bytearray(self.image.read_bytes())
        struct.pack_into('<I', raw, 0x208, 0xf00d0002)
        self.image.write_bytes(raw)
        self.assertEqual(subprocess.run([str(binary), str(self.image), 'actual']).returncode, 2)
        struct.pack_into('<I', raw, 0x118, 0xffffffff)
        self.image.write_bytes(raw)
        self.assertEqual(subprocess.run([str(binary), str(self.image), 'actual']).returncode, 2)


if __name__ == '__main__':
    unittest.main()
