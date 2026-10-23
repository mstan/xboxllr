"""Independent checkbox-only camera mods and persistent launcher selection."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_input_mods as camera
from native_mod_launcher import Catalog, atomic_json


class CameraMods(unittest.TestCase):
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
