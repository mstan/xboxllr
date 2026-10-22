"""All title packages share a source-complete helper, never original startup data."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_release_package as package
from native_release_archive import archive


class StartupPackageTests(unittest.TestCase):
    def test_three_title_defaults_helper_sources_and_private_asset_exclusion(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            runtime, launcher, helper = root/'build/Title.exe', root/'ui/Launcher.exe', root/'boot/startup.exe'
            for path in (runtime, launcher, helper):
                path.parent.mkdir()
                path.write_bytes(b'MZ authored PE fixture')
            cache = root/'build/CMakeCache.txt'
            cache.write_text('PILOT_NATIVE_OPTIMIZATION:STRING=OPTIMIZED\n'
                'PILOT_IO_DEVICE:BOOL=OFF\nPILOT_SCHEDULING:STRING=NATIVE\n')
            embed = root/'embed.zip'
            with zipfile.ZipFile(embed, 'w') as z:
                z.writestr('python312._pth', 'python312.zip\n.\n')
                z.writestr('pythonw.exe', b'MZ fixture')
            closure = dict(complete=True, missing=[], files=[dict(source=str(p), name=p.name)
                for p in (runtime, launcher, helper)])
            for name, enabled in (('Blinx', False), ('JSRF', False), ('Xbox Dashboard', True)):
                definition = root/'title.toml'
                definition.write_text('[title]\nid="fixture"\nname="'+name+'"\nxbe_sha256="'+'a'*64+'"\n'
                    '[launcher]\nstartup_intro='+str(enabled).lower()+'\n')
                output = root/(name.replace(' ', '-')+'-package')
                with patch.object(package, 'inventory', return_value=closure):
                    record = package.stage(definition, runtime, launcher, cache, embed, [], output, boot_intro=helper)
                self.assertEqual(record['startup']['default_enabled'], enabled)
                self.assertTrue((output/'native/XboxStartup.exe').is_file())
                self.assertTrue((output/'notices/xbox-boot-re/LICENSE').is_file())
                self.assertTrue((output/'source/native-startup/pilots/native-boot/boot_main.cpp').is_file())
                self.assertTrue((output/'source/native-startup/src/third_party/xbox-boot-re/core/src/Audio.c').is_file())
                self.assertTrue((output/'tools/native_boot_launcher.py').is_file())
                self.assertFalse(any(p.suffix in ('.pak', '.bin', '.img') for p in output.rglob('*')))
                archive(output, root/(name.replace(' ', '-')+'.zip'))


if __name__ == '__main__':
    unittest.main()
