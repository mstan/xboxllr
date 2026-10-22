"""Header provenance and transactional private-preparation upgrade contracts."""
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_cpu_helpers as h
import native_title as n
from native_thread_checkpoints import HEADER as CHECKPOINTS


class CpuHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.source=self.root/'source';(self.source/'gen').mkdir(parents=True)
        self.title=self.root/'title.toml'
        self.title.write_text('''schema="xboxllr.native-title.v1"
[title]
id="authored"
name="Authored"
executable="Authored"
xbe_sha256="'''+ 'a'*64 +'''"
[generation]
bindings=["cpu"]
[build]
PILOT_RENDERER="D3D11"
''')
        _,self.data=n.definition(self.title)
        (self.source/'gen/recomp_types.h').write_bytes((h.prepared_header()+CHECKPOINTS).encode())
        (self.source/'gen/body.c').write_text('/* Authored transaction fixture, no guest bytes. */\n')
        manifest=dict(sha256='a'*64,cpu_binding={'fixture':'authored admission'},
                      files={p.name:n.digest(p) for p in (self.source/'gen').iterdir()})
        (self.source/'generation.aot-private.json').write_text(json.dumps(manifest))
        (self.source/'native-title-preparation.json').write_text(json.dumps(n.preparation_identity(self.title,self.data,self.source)))
        self.destination=self.root/'build/new-generation'

    def test_clone_preserves_source_and_body_bytes_and_refreshes_preparation(self):
        before={str(p.relative_to(self.source)):n.digest(p) for p in self.source.rglob('*') if p.is_file()}
        record=h.clone_preparation(self.title,self.source,self.destination)
        self.assertEqual(before,{str(p.relative_to(self.source)):n.digest(p) for p in self.source.rglob('*') if p.is_file()})
        self.assertEqual((self.source/'gen/body.c').read_bytes(),(self.destination/'gen/body.c').read_bytes())
        self.assertIn(CHECKPOINTS,(self.destination/'gen/recomp_types.h').read_text())
        n.require_receipt(self.destination/'native-title-preparation.json',n.preparation_identity(self.title,self.data,self.destination))
        self.assertEqual(record['runtime_helpers']['revision'],h.PIN)
        header=(self.destination/'gen/recomp_types.h').read_bytes()
        h.install(self.destination/'gen');self.assertEqual(header,(self.destination/'gen/recomp_types.h').read_bytes())

    def test_refuses_changed_input_before_publishing(self):
        (self.source/'gen/body.c').write_text('changed input')
        with self.assertRaisesRegex(ValueError,'changed generated artifact'):
            h.clone_preparation(self.title,self.source,self.destination)
        self.assertFalse(self.destination.exists())

    def test_consistent_concurrent_source_change_does_not_publish_stale_copy(self):
        copytree=h.shutil.copytree
        def change_after_copy(*args,**kwargs):
            result=copytree(*args,**kwargs)
            body=self.source/'gen/body.c';body.write_text('/* Concurrent authored change */\n')
            manifest=json.loads((self.source/'generation.aot-private.json').read_text())
            manifest['files'][body.name]=n.digest(body)
            (self.source/'generation.aot-private.json').write_text(json.dumps(manifest))
            (self.source/'native-title-preparation.json').write_text(json.dumps(n.preparation_identity(self.title,self.data,self.source)))
            return result
        with patch.object(h.shutil,'copytree',side_effect=change_after_copy),self.assertRaisesRegex(ValueError,'changed during'):
            h.clone_preparation(self.title,self.source,self.destination)
        self.assertFalse(self.destination.exists())

    def test_refuses_overwrite_and_nested_target(self):
        self.destination.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError,'new, separate'):
            h.clone_preparation(self.title,self.source,self.destination)
        with self.assertRaisesRegex(ValueError,'new, separate'):
            h.clone_preparation(self.title,self.source,self.source/'build/new')

    def test_unreviewed_hook_never_publishes_a_preparation(self):
        header=self.source/'gen/recomp_types.h'
        header.write_text(header.read_text()+'\nvoid unreviewed_hook(void);\n')
        manifest=json.loads((self.source/'generation.aot-private.json').read_text())
        manifest['files'][header.name]=n.digest(header)
        (self.source/'generation.aot-private.json').write_text(json.dumps(manifest))
        (self.source/'native-title-preparation.json').write_text(json.dumps(n.preparation_identity(self.title,self.data,self.source)))
        with self.assertRaisesRegex(ValueError,'unreviewed native CPU helper extension'):
            h.clone_preparation(self.title,self.source,self.destination)
        self.assertFalse(self.destination.exists())

    def test_upstream_license_is_bound_to_the_reviewed_bytes(self):
        wrong=self.root/'wrong-upstream';wrong.mkdir()
        (wrong/'recomp_types.h').write_bytes((h.UPSTREAM/'recomp_types.h').read_bytes())
        (wrong/'LICENSE').write_text('unreviewed license')
        with patch.object(h,'UPSTREAM',wrong),self.assertRaisesRegex(ValueError,'source/license differs'):
            h.prepared_header()

    def symlink(self, target, link):
        try:
            link.symlink_to(target, target_is_directory=True)
        except OSError as error:
            self.skipTest('host cannot create test directory links: ' + str(error))

    def test_install_does_not_modify_a_hardlinked_header_peer(self):
        header=self.source/'gen/recomp_types.h';peer=self.root/'outside-header.h'
        before=header.read_bytes();os.link(header,peer)
        h.install(self.source/'gen')
        self.assertEqual(peer.read_bytes(),before)
        self.assertFalse(peer.samefile(header))

    def test_linked_notice_target_is_rejected_before_any_outside_write(self):
        target=self.root/'outside';target.mkdir();license=target/'LICENSE'
        license.write_text('Preserve owner file')
        notices=self.source/'gen/third_party';notices.mkdir()
        self.symlink(target,notices/'sp00nznet-cpu-helpers')
        before=(self.source/'gen/recomp_types.h').read_bytes()
        with self.assertRaisesRegex(ValueError,'ordinary directories'):
            h.install(self.source/'gen')
        self.assertEqual(license.read_text(),'Preserve owner file')
        self.assertEqual((self.source/'gen/recomp_types.h').read_bytes(),before)

    def test_clone_never_follows_linked_extra_directories(self):
        target=self.root/'outside';target.mkdir();(target/'keep').write_text('Preserve owner file')
        self.symlink(target,self.source/'linked-extra')
        with self.assertRaisesRegex(ValueError,'linked entries'):
            h.clone_preparation(self.title,self.source,self.destination)
        self.assertFalse(self.destination.exists())
        self.assertEqual((target/'keep').read_text(),'Preserve owner file')


if __name__=='__main__':unittest.main()
