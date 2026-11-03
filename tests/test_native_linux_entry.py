"""Player-data and path containment checks for AppImage replacement."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('native_linux_entry', Path(__file__).resolve().parents[1] / 'tools/native_linux_entry.py')
entry = importlib.util.module_from_spec(spec)
spec.loader.exec_module(entry)


class UpdateTests(unittest.TestCase):
    def record(self, payload, files):
        for name, content in files.items():
            path = payload / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        return dict(files={name: hashlib.sha256(content).hexdigest() for name, content in files.items()})

    def test_replace_preserves_saves_settings_imports_and_unknown_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            payload, root = base / 'payload', base / 'user'
            payload.mkdir(); root.mkdir()
            first = self.record(payload, {'native/game': b'old', 'tools/retired.py': b'retired'})
            entry.seed(payload, root, first)
            for name in ('data/console/eeprom.bin', 'data/state/save.dat', 'data/setup.json', 'data/discs/import/game.xbe', 'notes.txt'):
                path = root / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'preserved')
            second = self.record(payload, {'native/game': b'new'})
            entry.seed(payload, root, second)
            self.assertEqual((root / 'native/game').read_bytes(), b'new')
            self.assertFalse((root / 'tools/retired.py').exists())
            for name in ('data/console/eeprom.bin', 'data/state/save.dat', 'data/setup.json', 'data/discs/import/game.xbe', 'notes.txt'):
                self.assertEqual((root / name).read_bytes(), b'preserved')
            self.assertEqual(json.loads((root / 'release.json').read_text()), second)

    def test_corrupt_payload_is_rejected_before_any_replacement(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary); payload = base / 'payload'; root = base / 'user'
            payload.mkdir(); root.mkdir()
            record = self.record(payload, {'native/game': b'new', 'tools/runtime.py': b'expected'})
            (payload / 'tools/runtime.py').write_bytes(b'corrupt')
            with self.assertRaises(ValueError): entry.seed(payload, root, record)
            self.assertFalse((root / 'native/game').exists())

    def test_player_data_and_traversal_cannot_be_managed_payload_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            with self.assertRaises(ValueError): entry.ordinary(base, '../outside')
            record = self.record(base, {'data/save': b'overwrite'})
            with self.assertRaises(ValueError): entry.seed(base, base, record)

    @unittest.skipIf(__import__('os').name == 'nt', 'Linux filesystem symlink semantics')
    def test_application_update_rejects_symlink_escape(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary); root = base / 'user'; root.mkdir()
            outside = base / 'outside'; outside.mkdir()
            (root / 'native').symlink_to(outside, target_is_directory=True)
            with self.assertRaises(ValueError): entry.ordinary(root, 'native/game')


if __name__ == '__main__': unittest.main()
