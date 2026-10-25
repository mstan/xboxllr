import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('native_mod_launcher', ROOT / 'tools/native_mod_launcher.py')
ui = importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)
MANIFESTS = [ROOT / 'mods/preloaded/packages' / p / '1.0.0/manifest.toml'
             for p in ('xboxlle.enhancement.visuals', 'xboxlle.enhancement.frame-rate')]
PID = 'xboxlle.enhancement.visuals'


class LauncherPolicy(unittest.TestCase):
    def test_authored_categories_reach_native_ui_provider(self):
        packages={'xboxlle.enhancement.camera':'Controls','xboxlle.enhancement.visuals':'Visual enhancements',
                  'xboxllr.diagnostics':'Diagnostics','xboxllr.enhancement.startup':'Presentation'}
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            paths=[ROOT/f'mods/preloaded/packages/{pid}/1.0.0/manifest.toml' for pid in packages]
            for category,pid in [('Gameplay','fixture.test-runs'),(None,'fixture.unclassified')]:
                path=root/(pid+'.toml')
                path.write_text('format_version=1\nversion="1.0.0"\nid="'+pid+'"\nname="Fixture"\ndescription="Fixture"\n'+
                    (f'category="{category}"\n' if category else '')+
                    '[[feature]]\nid="fixture"\nname="Fixture"\ndefault_enabled=false\n')
                paths.append(path);packages[pid]=category or 'Other'
            catalog=ui.Catalog(paths,root/'selection.json')
            self.assertTrue(catalog.features)
            for feature in catalog.features:self.assertEqual(feature['group'],packages[feature['package']])
            output=root/'catalog';catalog.write(output,'Category fixture','Test','a'*64)
            if os.environ.get('XLLE_TEST_LAUNCHER'):
                command=[os.environ['XLLE_TEST_LAUNCHER'],str(output),str(root/'result'),'owned.xbe','--validate']
                result=subprocess.run(command,capture_output=True,text=True,check=True)
                actual={line.split('\t')[0].removeprefix('[LAUNCHER MOD] '):line.split('\t')[2]
                        for line in result.stdout.splitlines() if line.startswith('[LAUNCHER MOD] ')}
                self.assertEqual(actual,packages)
                # Old catalogs remain readable and receive a neutral category.
                rows=output.read_text().splitlines()
                output.write_text('\n'.join(' '.join(row.split()[:6]) if row.startswith('feature ') else row for row in rows)+'\n')
                legacy=subprocess.run(command,capture_output=True,text=True,check=True)
                self.assertTrue(all(line.endswith('\tOther') for line in legacy.stdout.splitlines() if line.startswith('[LAUNCHER MOD] ')))

    @unittest.skipUnless(os.environ.get('XLLE_TEST_LAUNCHER'), 'Native launcher is an optional separate target')
    def test_compiled_xbe_gate_hashes_the_whole_file_and_rejects_spoofed_identity(self):
        import hashlib
        from test_xbe_identity import fixture
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image, catalog, result = root/'owned.xbe', root/'catalog', root/'result'
            original = fixture()
            alternate = fixture('Halo retail revision')
            self.assertEqual(len(original) % 1024, 512)  # Never strip this as a SNES copier header.
            ui.Catalog([], root/'state').write(catalog, 'Halo', '',
                [hashlib.sha256(blob).hexdigest() for blob in (original, alternate)], game_id='4D530004')
            command = [os.environ['XLLE_TEST_LAUNCHER'], str(catalog), str(result), str(image), '--validate-xbe']
            for blob in (original, alternate):
                image.write_bytes(blob)
                subprocess.run(command, check=True, capture_output=True)
                self.assertIn('result 0', result.read_text())
            for blob, reason in ((fixture('Halo patched'), 'Unsupported XBE revision'),
                                 (fixture('Halo', 0x5345000a), 'Wrong game selected'), (b'XBEH', 'Not an XBE image')):
                image.write_bytes(blob)
                checked = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(checked.returncode, 2)
                self.assertIn(reason, checked.stderr)

    def test_title_id_handoff_is_optional_and_validated(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'catalog.txt'
            catalog = ui.Catalog([], Path(folder) / 'state.json')
            catalog.write(path, 'Halo', '', 'a' * 64, game_id='4D530004')
            self.assertIn('game-id 4d530004\n', path.read_text())
            for value in ('wrong', '4d530004\nend', 0x4d530004):
                with self.assertRaisesRegex(ValueError, 'title ID'):
                    catalog.write(path, 'Halo', '', 'a' * 64, game_id=value)

    @unittest.skipUnless(os.environ.get('XLLE_TEST_LAUNCHER'), 'Native launcher is an optional separate target')
    def test_compiled_startup_settings_roundtrip_without_changing_locked_console(self):
        import native_boot_launcher as boot_mod
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            setup = dict(required=False, kernel='signing-only.bin', acknowledged=True,
                         eeprom='original-eeprom.bin', storage='saves', hdd='', locked=True,
                         choice_path=str(root / 'console-choice.txt'))
            boot = dict(enabled=True, kernel='G:/Owned firmware/complete.img', status='Ready',
                        choice_path=str(root / 'boot-choice.txt'))
            manifests = boot_mod.manifests(dict(startup=dict(executable='native/XboxStartup.exe')))
            c = ui.Catalog(manifests, root / 'selection.json', setup=setup, boot=boot)
            c.features[0]['enabled'] = True
            result, image = c.run(Path(os.environ['XLLE_TEST_LAUNCHER']), root / 'run',
                title='Startup fixture', region='Test', sha='a' * 64, image='owned.xbe', validate=True)
            self.assertEqual(result, 0)
            self.assertEqual(c.boot, boot)
            self.assertEqual(c.setup, setup)
            self.assertTrue(boot_mod.skip_animation(c))
            reloaded = ui.Catalog(manifests, root / 'selection.json', setup=setup, boot=boot)
            self.assertTrue(boot_mod.skip_animation(reloaded))
            target = root / 'run/launcher.result'
            before = dict(c.boot)
            target.write_text(target.read_text().replace('boot 1 ', 'boot 7 '))
            with self.assertRaises(ValueError):
                c.ingest(target)
            self.assertEqual(c.boot, before)
            self.assertFalse((root / 'console-choice.txt').exists())

    def test_explicit_title_defaults_keep_saved_choices_and_capability_limits(self):
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / 'selection.json'
            resolution = PID + '/resolution'
            fps = 'xboxlle.enhancement.frame-rate/frame-rate'
            defaults = {resolution: dict(enabled=True, options={'height': '1080'}),
                        fps: dict(enabled=True, options={'mode': 'interpolated', 'rate': '60'})}
            fresh = ui.Catalog(MANIFESTS, state, defaults=defaults, frame_modes=['interpolated'])
            self.assertEqual(fresh.values()[(PID, 'resolution', 'height')], '1080')
            self.assertEqual(fresh.values()[('xboxlle.enhancement.frame-rate', 'frame-rate', 'mode')], 'interpolated')
            limited = ui.Catalog(MANIFESTS, state, defaults=defaults)
            self.assertFalse(any(f['id'] == 'frame-rate' for f in limited.features))
            ui.atomic_json(state, dict(version=1, features={resolution: dict(enabled=False, options={'height':'1440'})}))
            saved = ui.Catalog(MANIFESTS, state, defaults=defaults)
            feature = next(f for f in saved.features if f['id'] == 'resolution')
            self.assertFalse(feature['enabled'])
            self.assertEqual(feature['options'][0]['value'], '1440')

    def test_original_defaults_and_capability_filter(self):
        with tempfile.TemporaryDirectory() as d:
            c = ui.Catalog(MANIFESTS, Path(d) / 'selection.json')
            self.assertEqual(c.values(), {})
            self.assertEqual(len(c.features), 4)
            self.assertEqual(c.volume, 100)
            enabled = ui.Catalog(MANIFESTS, Path(d) / 'selection.json', frame_modes=['interpolated', 'actual'])
            frame = next(f for f in enabled.features if f['id'] == 'frame-rate')
            self.assertEqual([v for v, _ in frame['options'][0]['choices']], ['original', 'interpolated', 'actual'])

    def test_hidden_active_choice_disables_feature(self):
        with tempfile.TemporaryDirectory() as d:
            state, policy = Path(d) / 'selection.json', Path(d) / 'visibility.json'
            key = PID + '/resolution'
            ui.atomic_json(state, dict(version=1, volume=80, features={key: dict(enabled=True, options={'height': '2160'})}))
            ui.atomic_json(policy, dict(version=1, hidden_features=[], hidden_choices={key + '/height': ['2160']}))
            c = ui.Catalog(MANIFESTS, state, visibility=[policy])
            f = next(f for f in c.features if f['id'] == 'resolution')
            self.assertFalse(f['enabled'])
            self.assertNotIn('2160', [v for v, _ in f['options'][0]['choices']])
            self.assertEqual(c.volume, 80)

    def test_hiding_one_frame_mode_keeps_the_other(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'visibility.json'
            ui.atomic_json(path, dict(version=1, hidden_features=[], hidden_choices={
                'xboxlle.enhancement.frame-rate/frame-rate/mode': ['actual']}))
            c = ui.Catalog(MANIFESTS, Path(d) / 'selection.json', visibility=[path], frame_modes=['interpolated', 'actual'])
            f = next(f for f in c.features if f['id'] == 'frame-rate')
            self.assertEqual([v for v, _ in f['options'][0]['choices']], ['original', 'interpolated'])

    def test_saved_player_identity_validation(self):
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / 'selection.json'
            player = dict(source='gamepad', guid='0123456789abcdef' * 2)
            ui.atomic_json(state, dict(version=1, player1=player))
            self.assertEqual(ui.Catalog(MANIFESTS, state).player, player)
            for bad in (dict(source='gamepad', guid='wrong'), dict(source='keyboard', guid='a' * 32), dict(source='anything', guid='')):
                ui.atomic_json(state, dict(version=1, player1=bad))
                with self.assertRaises(ValueError):
                    ui.Catalog(MANIFESTS, state)

    @unittest.skipUnless(os.environ.get('XLLE_TEST_LAUNCHER'), 'Native launcher is an optional separate target')
    def test_compiled_provider_roundtrip_and_invalid_canvas(self):
        import subprocess
        with tempfile.TemporaryDirectory() as d:
            c = ui.Catalog(MANIFESTS, Path(d) / 'selection.json')
            for f in c.features:
                f['enabled'] = True
            result, image = c.run(Path(os.environ['XLLE_TEST_LAUNCHER']), Path(d) / 'run',
                                  title='Policy fixture', region='Test', sha='a' * 64, image='owned.xbe', validate=True,
                                  game_id='4D530004')
            self.assertEqual(result, 0)
            self.assertEqual(image, 'owned.xbe')
            self.assertEqual(len(c.values()), 4)
            for source, guid in [('none', ''), ('keyboard', ''), ('gamepad', '0123456789abcdef' * 2)]:
                c.player = dict(source=source, guid=guid)
                c.run(Path(os.environ['XLLE_TEST_LAUNCHER']), Path(d) / 'run',
                      title='Policy fixture', region='Test', sha='a' * 64, image='owned.xbe', validate=True)
                self.assertEqual(ui.Catalog(MANIFESTS, c.state_path).player, c.player)
            # A malicious/truncated child result must not invent another value.
            target = Path(d) / 'run/launcher.result'
            before = c.selection()
            target.write_text(target.read_text().replace(ui.encoded('fxaa'), ui.encoded('arbitrary')))
            with self.assertRaises(ValueError):
                c.ingest(target)
            self.assertEqual(before, c.selection())
            # Real C++ policy refuses a canvas beyond the renderer limit.
            for f in c.features:
                if f['id'] == 'supersampling':
                    f['options'][0]['value'] = '3'
                if f['id'] == 'resolution':
                    f['options'][0]['value'] = '2160'
            c.features.append(dict(package=PID, id='aspect-fixture', name='Aspect fixture', description='', enabled=True,
                                   options=[dict(id='aspect', label='Aspect', value='32:9', default='32:9', choices=[('32:9', '32:9')])]))
            c.write(Path(d) / 'invalid.catalog', 'Test', 'Test', 'a' * 64)
            path = Path(d) / 'invalid.catalog'
            with self.assertRaises(subprocess.CalledProcessError) as error:
                subprocess.run([os.environ['XLLE_TEST_LAUNCHER'], str(path), str(Path(d) / 'invalid.result'), 'owned.xbe', '--validate'], check=True)
            self.assertEqual(error.exception.returncode, 2)


if __name__ == '__main__':
    unittest.main()
