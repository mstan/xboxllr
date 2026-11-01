"""Executable driver exit/argument contracts and archive path rejection."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from release_toolkit import extract


class Toolkit(unittest.TestCase):
    def cli(self, *args):
        return subprocess.run([sys.executable, str(ROOT/'tools/xboxllr.py'), *args],
                              capture_output=True, text=True)

    def test_python_arguments_and_exit(self):
        args = ['a b', 'quote"inside', 'trailing\\', '']
        result = self.cli('python', '-c', 'import json,sys;print(json.dumps(sys.argv[1:]));sys.exit(7)', *args)
        self.assertEqual(result.returncode, 7)
        self.assertEqual(json.loads(result.stdout), args)

    def test_title_help_and_unknown_command(self):
        result = self.cli('title', '--help')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('--build-dir', result.stdout)
        self.assertEqual(self.cli('no-such-command').returncode, 2)

    def test_zip_traversal_and_links_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            # zipfile normalizes backslashes on Windows before exposing names.
            for name in ('../escape', '/absolute', 'C:/escape'):
                with zipfile.ZipFile(root/'bad.zip', 'w') as archive:
                    entry = zipfile.ZipInfo('unsafe')
                    entry.filename = name  # Preserve names the Windows writer normally normalizes.
                    archive.writestr(entry, 'bad')
                with self.assertRaises(ValueError):
                    extract(root/'bad.zip', root/'unpacked')
            with zipfile.ZipFile(root/'bad.zip', 'w') as archive:
                entry = zipfile.ZipInfo('link'); entry.external_attr = 0o120777 << 16
                archive.writestr(entry, 'destination')
            with self.assertRaises(ValueError):
                extract(root/'bad.zip', root/'unpacked')
            self.assertFalse((root/'unpacked').exists())


if __name__ == '__main__':
    unittest.main()
