import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('native_mod_launcher', ROOT / 'tools/native_mod_launcher.py')
ui = importlib.util.module_from_spec(spec);spec.loader.exec_module(ui)
MANIFESTS = [ROOT / 'mods/preloaded/packages' / p / '1.0.0/manifest.toml'
             for p in ('xboxlle.enhancement.visuals', 'xboxlle.enhancement.frame-rate')]
PID = 'xboxlle.enhancement.visuals'


class LauncherPolicy(unittest.TestCase):
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
                                  title='Policy fixture', region='Test', sha='a' * 64, image='owned.xbe', validate=True)
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
