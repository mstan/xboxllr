"""Prepared dependency archives retain modifications and reject changed inputs."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from native_release_sources import package


class DependencySourceTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        self.renderer=self.root/'renderer'
        self.ui=self.root/'ui'
        self.dsp=self.root/'dsp'
        self.output=self.root/'sources.zip'
        for root in (self.renderer,self.ui,self.dsp):
            root.mkdir()

        def put(root,name,data=b'authored fixture\n'):
            path=root/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)
            return hashlib.sha256(data).hexdigest()

        renderer_hash=put(self.renderer,'src/renderer.c')
        for name in ('LICENSE','LICENSE.upstream-MIT','NOTICE','MODIFICATIONS.md',
                     'LICENSES/example.txt','nv2a-reference/LICENSE','pilot_d3d_checks.h'):
            put(self.renderer,name)
        (self.renderer/'donor.json').write_text(json.dumps(dict(donor='renderer-pin',
            files={'src/renderer.c':dict(prepared=renderer_hash)},
            nv2a_reference=dict(files={}))))
        ui_hash=put(self.ui,'src/main.c')
        rom_hash=put(self.ui,'test_data/sample.rom',b'not part of this build\0')
        (self.ui/'xboxlle-ui.json').write_text(json.dumps(dict(revision='ui-pin',
            files={'src/main.c':ui_hash,'test_data/sample.rom':rom_hash})))
        guarded=put(self.dsp,'crates/core.rs',b'pub fn guarded() {}\n')
        put(self.dsp,'third_party/LICENSE')
        (self.dsp/'donor.json').write_text(json.dumps(dict(revision='dsp-pin',
            sources={'crates/core.rs':'upstream-unpatched-hash'},
            guarded={'crates/core.rs':guarded})))

    def run_package(self):
        return package(self.renderer,self.ui,self.dsp,self.output)

    def test_keeps_guarded_source_and_excludes_unrelated_rom(self):
        self.run_package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read('dsp/crates/core.rs'),b'pub fn guarded() {}\n')
            self.assertEqual(archive.read('renderer/nv2a-reference/LICENSE'),b'authored fixture\n')
            self.assertFalse(any(name.endswith('.rom') for name in archive.namelist()))
            receipt=json.loads(archive.read('DEPENDENCY-SOURCES.json'))
            for name,expected in receipt['files'].items():
                self.assertEqual(hashlib.sha256(archive.read(name)).hexdigest(),expected)

    def test_tampering_rejects_before_archive(self):
        (self.renderer/'src/renderer.c').write_text('changed source')
        with self.assertRaisesRegex(ValueError,'differs from its receipt'):
            self.run_package()
        self.assertFalse(self.output.exists())

    def test_existing_output_is_preserved(self):
        self.output.write_bytes(b'keep')
        with self.assertRaises(ValueError):
            self.run_package()
        self.assertEqual(self.output.read_bytes(),b'keep')


if __name__=='__main__':
    unittest.main()
