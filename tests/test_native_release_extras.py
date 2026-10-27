"""Release extras use an explicit file list, separate from native user saves."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_release_package import copy_release_extras


class ReleaseExtrasTests(unittest.TestCase):
    def test_only_declared_art_is_copied_and_hash_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            art = root / 'extras/graffiti'
            art.mkdir(parents=True)
            names = ['extras/graffiti/RPTK-SS.png', 'extras/graffiti/RAID-Discord-XL.png']
            for index, name in enumerate(names):
                (root / name).write_bytes(b'authored image fixture' + bytes([index]))
            (art / 'personal-third.png').write_bytes(b'private owner import')
            output = root / 'package'
            copy_release_extras(root / 'native-title.toml', {'release': {'extras': names}}, output)
            self.assertEqual(sorted(p.relative_to(output).as_posix() for p in output.rglob('*') if p.is_file()), sorted(names))
            for name in names:
                self.assertEqual((root / name).read_bytes(), (output / name).read_bytes())

    def test_rejects_private_paths_directories_and_oversized_files_before_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'extras').mkdir()
            large = root / 'extras/large.png'
            with large.open('wb') as stream:
                stream.truncate(16 * 1024 * 1024 + 1)
            for names in (['data/art.png'], ['extras/../data/art.png'], ['extras'],
                          ['extras/large.png'], ['extras/large.png', 'extras/large.png'], [{}]):
                with self.subTest(names=names), self.assertRaises(ValueError):
                    copy_release_extras(root / 'native-title.toml', {'release': {'extras': names}}, root / 'package')
                self.assertFalse((root / 'package').exists())


if __name__ == '__main__':
    unittest.main()
