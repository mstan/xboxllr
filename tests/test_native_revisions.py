"""Revision routing must never select a different binary or replace old saves."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_release_runtime as runtime
import native_release_package as package
from native_mod_launcher import Catalog
from native_revisions import hashes, select, entries as revision_entries
from test_native_save_keys import authored_xbe


class RevisionsTests(unittest.TestCase):
    def test_exact_binary_and_persistent_profile_selection(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            first = root/'first.xbe'
            first.write_bytes(authored_xbe())
            second = root/'second.xbe'
            alternate = bytearray(authored_xbe())
            alternate[0x114:0x118] = b'USA1'
            second.write_bytes(alternate)
            title = dict(game_id='5345000A', name='Jet Set Radio Future', xbe_sha256=runtime.sha(first))
            data = dict(title=title, executable='first.exe', runtime={'XLLE_PILOT_RESOURCE_MIRROR_OWNER':'0','XLLE_PILOT_BASE_ONLY':'1'}, capabilities=dict(visual_mods=False),
                revisions=[dict(id='usa-standalone', title=dict(title, xbe_sha256=runtime.sha(second)), executable='second.exe',
                    runtime={'XLLE_PILOT_RESOURCE_MIRROR_OWNER':'1','XLLE_PILOT_USA_ONLY':'1'})])
            profile1 = runtime.prepare_profile(root, data, first)
            marker = root/'data/state/Partition1/progress.sav'
            marker.parent.mkdir(parents=True)
            marker.write_bytes(b'original owner progress')
            before = {p.relative_to(root): p.read_bytes() for p in profile1.iterdir() if p.is_file()}
            profile2 = runtime.prepare_profile(root, data, second)
            selected = select(data, runtime.sha(second))
            self.assertEqual(runtime.verify_profile(root, selected), profile2)
            self.assertNotEqual(profile1, profile2)
            self.assertEqual(runtime.verify_profile(root, data), profile1)
            self.assertEqual(select(selected, runtime.sha(first))['executable'], 'first.exe')
            self.assertEqual(selected['runtime'],data['revisions'][0]['runtime'])
            self.assertEqual(select(selected,runtime.sha(first))['runtime'],data['runtime'])
            again=select(select(selected,runtime.sha(first)),runtime.sha(second))
            self.assertEqual(again['runtime'],data['revisions'][0]['runtime'])
            again['runtime']['XLLE_PILOT_RESOURCE_MIRROR_OWNER']='changed copy'
            self.assertEqual(select(selected,runtime.sha(second))['runtime']['XLLE_PILOT_RESOURCE_MIRROR_OWNER'],'1')
            catalog = Catalog([], root/'mods.json')
            (root/'second.exe').write_bytes(b'authored process fixture')
            child = unittest.mock.Mock(pid=1, returncode=0)
            child.wait.return_value = 0
            with patch('native_boot_launcher.run'), patch('platform.uname', return_value=unittest.mock.Mock(system='Windows', release='test', machine='x64')), patch.object(runtime.subprocess, 'Popen', return_value=child) as spawn:
                self.assertEqual(runtime.launch_game(root, data, second, profile2, catalog), 0)
            command = spawn.call_args.args[0]
            self.assertEqual(Path(command[0]), root/'second.exe')
            self.assertEqual(Path(command[-1]), root/'data/revisions/usa-standalone/state')
            self.assertEqual(spawn.call_args.kwargs['env']['XLLE_PILOT_RESOURCE_MIRROR_OWNER'],'1')
            self.assertNotIn('XLLE_PILOT_BASE_ONLY',spawn.call_args.kwargs['env'])
            (root/'first.exe').write_bytes(b'authored base process fixture')
            with patch('native_boot_launcher.run'), patch('platform.uname', return_value=unittest.mock.Mock(system='Windows',release='test',machine='x64')), patch.object(runtime.subprocess,'Popen',return_value=child) as spawn:
                self.assertEqual(runtime.launch_game(root,selected,first,profile1,catalog),0)
            self.assertEqual(spawn.call_args.kwargs['env']['XLLE_PILOT_RESOURCE_MIRROR_OWNER'],'0')
            self.assertNotIn('XLLE_PILOT_USA_ONLY',spawn.call_args.kwargs['env'])
            self.assertEqual(marker.read_bytes(), b'original owner progress')
            self.assertEqual(before, {p.relative_to(root): p.read_bytes() for p in profile1.iterdir() if p.is_file()})
            with self.assertRaisesRegex(ValueError, 'profile'):
                runtime.launch_game(root, data, second, profile1, catalog)
            with self.assertRaisesRegex(ValueError, 'not a supported'):
                select(data, '0'*64)
            catalog.write(root/'catalog.txt', title['name'], 'NTSC-U', hashes(data))
            lines = (root/'catalog.txt').read_text().splitlines()
            self.assertEqual([line for line in lines if line.startswith('identity ')],
                ['identity '+runtime.sha(first), 'identity '+runtime.sha(second)])

    def test_ambiguous_or_unsafe_revisions_are_rejected(self):
        title = dict(game_id='5345000A', xbe_sha256='a'*64)
        for key, digest, game in [('bad/path','b'*64,'5345000A'), ('usa','a'*64,'5345000A'), ('usa','b'*64,'wrong')]:
            with self.subTest(key=key, digest=digest, game=game), self.assertRaisesRegex(ValueError, 'catalog'):
                hashes(dict(title=title, revisions=[dict(id=key, title=dict(game_id=game,xbe_sha256=digest),executable='usa.exe')]))
        for policy in (None,[],1):
            with self.subTest(runtime=policy),self.assertRaisesRegex(ValueError,'catalog'):
                hashes(dict(title=title,revisions=[dict(id='usa',title=dict(title,xbe_sha256='b'*64),executable='usa.exe',runtime=policy)]))

    def test_release_catalog_keeps_declared_revision_runtime(self):
        # Exercise the real packager's validated revision loop up to the
        # catalog boundary. Binary/DLL staging is intentionally not this test.
        class CatalogCaptured(Exception):pass
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);base=root/'base';other=root/'usa';base.mkdir();other.mkdir()
            policy='PILOT_NATIVE_OPTIMIZATION:STRING=OPTIMIZED\nPILOT_IO_DEVICE:STRING=OFF\nPILOT_SCHEDULING:STRING=NATIVE\n'
            (base/'CMakeCache.txt').write_text(policy);(other/'CMakeCache.txt').write_text(policy)
            (base/'Base.exe').write_bytes(b'authored base');(other/'USA.exe').write_bytes(b'authored USA')
            definition=root/'native-title.toml';revision=root/'usa.toml'
            definition.write_text('[title]\nid="base"\ngame_id="5345000A"\nxbe_sha256="'+'a'*64+'"\n[runtime]\nXLLE_PILOT_RESOURCE_MIRROR_OWNER="0"\n[revisions.usa]\ndefinition="usa.toml"\n')
            revision.write_text('[title]\nid="usa"\nexecutable="USA"\ngame_id="5345000A"\nxbe_sha256="'+'b'*64+'"\n[runtime]\nXLLE_PILOT_RESOURCE_MIRROR_OWNER="1"\n')
            (other/'native-title-build.json').write_text(json.dumps(dict(title='usa',executable_sha256=runtime.sha(other/'USA.exe'))))
            catalog={}
            def capture(data):
                catalog.update(data)
                revision_entries(data)
                raise CatalogCaptured()
            with patch('native_revisions.entries',side_effect=capture),self.assertRaises(CatalogCaptured):
                package.stage(definition,base/'Base.exe',root/'Launcher.exe',base/'CMakeCache.txt',root/'embed',[],root/'output',
                    boot_intro=root/'intro.exe',revisions=[('usa',other/'USA.exe',other/'CMakeCache.txt')])
            self.assertEqual(catalog['revisions'][0].get('runtime'),{'XLLE_PILOT_RESOURCE_MIRROR_OWNER':'1'})
            catalog.update(runtime={'XLLE_PILOT_RESOURCE_MIRROR_OWNER':'0'},executable='native/Base.exe')
            chosen=select(catalog,'b'*64)
            self.assertEqual(chosen['runtime']['XLLE_PILOT_RESOURCE_MIRROR_OWNER'],'1')
            self.assertEqual(select(chosen,'a'*64)['runtime']['XLLE_PILOT_RESOURCE_MIRROR_OWNER'],'0')
            self.assertFalse((root/'output').exists())

    def test_legacy_catalog_inherits_original_runtime(self):
        title=dict(game_id='5345000A',xbe_sha256='a'*64)
        data=dict(title=title,runtime={'XLLE_PILOT_RESOURCE_MIRROR_OWNER':'0'},executable='first.exe',
            revisions=[dict(id='usa',title=dict(title,xbe_sha256='b'*64),executable='second.exe')])
        self.assertEqual(select(data,'b'*64)['runtime'],data['runtime'])


if __name__ == '__main__':
    unittest.main()
