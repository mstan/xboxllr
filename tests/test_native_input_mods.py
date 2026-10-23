"""Independent checkbox-only camera mods and persistent launcher selection."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_input_mods as camera
from native_visual_mods import manifests as visual_manifests
from native_mod_launcher import Catalog, atomic_json


class CameraMods(unittest.TestCase):
    def test_title_revision_scope_filters_saved_controls_outside_blinx(self):
        identity = dict(game_id='4D530013',
            xbe_sha256='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249')
        supported = [dict(title=identity), dict(game=dict(title_id=identity['game_id'],
                     xbe_sha256=identity['xbe_sha256']))]
        unsupported = [dict(title=dict(id='xbox-dashboard-5960', name='Xbox Dashboard')),
                       dict(title=dict(name='Blinx: The Time Sweeper', xbe_sha256='a'*64)),
                       dict(title=dict(identity, game_id='5345000A')),
                       dict(title=dict(identity, xbe_sha256='a'*64)), {}]
        with tempfile.TemporaryDirectory() as d:
            state = Path(d)/'selection.json'
            atomic_json(state, dict(version=1, features={
                camera.PACKAGE+'/invert-horizontal': dict(enabled=True, options={}),
                camera.PACKAGE+'/invert-vertical': dict(enabled=True, options={})}))
            for data in supported + unsupported:
                with self.subTest(data=data):
                    admitted = data in supported
                    self.assertEqual(camera.manifests(data), [camera.manifest()] if admitted else [])
                    catalog = Catalog(visual_manifests(Path(d)/'title.toml', data), state)
                    self.assertEqual(camera.environment(catalog),
                        dict(XLLE_PILOT_CAMERA_INVERT_X=str(int(admitted)),
                             XLLE_PILOT_CAMERA_INVERT_Y=str(int(admitted))))
            # Removing this mod elsewhere must not erase Blinx's saved choices.
            self.assertEqual([f['enabled'] for f in Catalog(camera.manifests(supported[0]), state).features],
                             [True, True])

    def test_independent_defaults_saved_state_and_environment(self):
        with tempfile.TemporaryDirectory() as d:
            state = Path(d) / 'selection.json'
            for x, y in ((False, False), (True, False), (False, True), (True, True)):
                if x or y:
                    atomic_json(state, dict(version=1, features={
                        camera.PACKAGE + '/invert-horizontal': dict(enabled=x, options={}),
                        camera.PACKAGE + '/invert-vertical': dict(enabled=y, options={})}))
                c = Catalog([camera.manifest()], state)
                self.assertEqual(len(c.features), 2)
                self.assertEqual([f['enabled'] for f in c.features], [x, y])
                self.assertTrue(all(not f['options'] for f in c.features))
                self.assertEqual(camera.environment(c), dict(XLLE_PILOT_CAMERA_INVERT_X=str(int(x)),
                    XLLE_PILOT_CAMERA_INVERT_Y=str(int(y))))
                catalog = Path(d) / 'catalog.txt'
                c.write(catalog, 'Camera fixture', 'Test', 'a' * 64)
                self.assertEqual(sum(line.startswith('feature ') for line in catalog.read_text().splitlines()), 2)

    @unittest.skipUnless(os.environ.get('XLLE_TEST_LAUNCHER'), 'Compiled launcher is optional')
    def test_compiled_checkbox_only_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            c = Catalog([camera.manifest()], root / 'selection.json')
            c.features[0]['enabled'] = True
            result, _ = c.run(Path(os.environ['XLLE_TEST_LAUNCHER']), root / 'run',
                title='Camera fixture', region='Test', sha='a' * 64, image='owned.xbe', validate=True)
            self.assertEqual(result, 0)
            reloaded = Catalog([camera.manifest()], root / 'selection.json')
            self.assertEqual([f['enabled'] for f in reloaded.features], [True, False])


if __name__ == '__main__':
    unittest.main()
