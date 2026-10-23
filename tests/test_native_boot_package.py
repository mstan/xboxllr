"""All title packages share a source-complete helper, never original startup data."""
import hashlib
from pathlib import Path
import re
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_release_package as package
import native_boot_launcher as boot
import native_input_mods as camera
from native_mod_launcher import Catalog
from native_release_archive import archive
from native_title_switches import manifests as title_manifests, environment as title_environment


class StartupPackageTests(unittest.TestCase):
    def test_native_audio_source_build_closure(self):
        sources = set(package.startup_source_files())
        roots = (package.ROOT/'src', package.ROOT/'src/third_party/xbox-boot-re/core/include',
                 package.ROOT/'src/third_party/xbox-boot-re/core/src')
        for path in sources:
            if path.suffix.lower() not in ('.h', '.hpp', '.c', '.cpp'):
                continue
            for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(encoding='utf-8'), re.M):
                candidates = [(path.parent/name).resolve(), *((root/name).resolve() for root in roots)]
                self.assertTrue(any(p in sources for p in candidates), f'{path}: missing include {name}')
        for name in ('cmake/ApuRate.cmake', 'cmake/ApuAdpcm.cmake',
                     'src/machine/apu_rate.cpp', 'src/machine/apu_envelope.cpp',
                     'src/machine/apu_voice_quantum.cpp', 'src/machine/apu_filter.cpp',
                     'src/machine/apu_adpcm.c', 'LICENSES/libsamplerate-BSD.txt',
                     'src/third_party/libsamplerate/README.md', 'src/third_party/dr_wav/LICENSE'):
            self.assertIn(package.ROOT/name, sources)
        rate = (package.ROOT/'cmake/ApuRate.cmake').read_text()
        self.assertIn('GIT_TAG c96f5e3de9c4488f4e6c97f59f5245f22fda22f7', rate)
        # Source and notice inputs referenced by the shared CMake fragments
        # must resolve inside the kit as well as the C/C++ include closure.
        for fragment in ('cmake/ApuRate.cmake', 'cmake/ApuAdpcm.cmake',
                         'pilots/native-boot/CMakeLists.txt'):
            path = package.ROOT/fragment
            variables = {'${CMAKE_CURRENT_LIST_DIR}': path.parent,
                         '${BOOT_CORE}': package.ROOT/'src/third_party/xbox-boot-re/core'}
            for name in re.findall(r'(\$\{(?:CMAKE_CURRENT_LIST_DIR|BOOT_CORE)\}/[^\s"\)]+)', path.read_text()):
                variable, tail = name.split('}', 1)
                target = (variables[variable+'}']/tail.lstrip('/')).resolve()
                self.assertTrue(target in sources or any(target in p.parents for p in sources),
                                f'{fragment}: missing CMake input {name}')
        self.assertFalse(any(p.suffix.lower() in ('.pak', '.bin', '.img', '.rle', '.xbe') for p in sources))

    def test_three_title_defaults_helper_sources_and_private_asset_exclusion(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            runtime, launcher, helper = root/'build/Title.exe', root/'ui/Launcher.exe', root/'boot/startup.exe'
            for path in (runtime, launcher, helper):
                path.parent.mkdir()
                path.write_bytes(b'MZ authored PE fixture')
            module = helper.with_name('XboxStartup.dll')
            module.write_bytes(b'MZ authored startup module fixture')
            launch_stub=launcher.with_name('xboxllr_start.exe')
            launch_stub.write_bytes(b'MZ authored GUI entry point fixture')
            helper_notice = helper.parent/'third_party/libsamplerate/README.md'
            helper_notice.parent.mkdir(parents=True)
            helper_notice.write_bytes(b'authored helper dependency notice fixture')
            cache = root/'build/CMakeCache.txt'
            cache.write_text('PILOT_NATIVE_OPTIMIZATION:STRING=OPTIMIZED\n'
                'PILOT_IO_DEVICE:BOOL=OFF\nPILOT_SCHEDULING:STRING=NATIVE\n')
            embed = root/'embed.zip'
            with zipfile.ZipFile(embed, 'w') as z:
                z.writestr('python312._pth', 'python312.zip\n.\n')
                z.writestr('pythonw.exe', b'MZ fixture')
            closure = dict(complete=True, missing=[], files=[dict(source=str(p), name=p.name)
                for p in (runtime, launcher, helper, module, launch_stub)])
            for name, requested, camera_enabled in (('Blinx', False, False), ('JSRF', False, False),
                    ('Xbox Dashboard', True, False), ('Another native game', None, False),
                    ('Blinx admitted revision', False, True)):
                enabled = requested is True
                definition = root/'title.toml'
                definition.write_text('[title]\nid="fixture"\ngame_id="12345678"\nname="'+name+'"\nxbe_sha256="'+'a'*64+'"\n'
                    + ('[launcher]\nstartup_intro='+str(requested).lower()+'\n' if requested is not None else ''))
                if camera_enabled:
                    definition.write_text('[game]\nname="Blinx admitted revision"\nregion="NTSC-U"\n'
                        'title_id="4D530013"\nxbe_sha256="e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249"\n')
                title_switch = name == 'JSRF'
                if title_switch:
                    with definition.open('a') as file:
                        file.write('[launcher.defaults."fixture.enhancement.unlock/early-unlock"]\n'
                            'enabled=true\n[mods]\n'
                            'switches=[{manifest="mods/preloaded/packages/fixture.enhancement.unlock/1.0.0/manifest.toml",'
                            'package="fixture.enhancement.unlock",feature="early-unlock",key="EARLY_UNLOCK"}]\n')
                    manifest=root/'mods/preloaded/packages/fixture.enhancement.unlock/1.0.0/manifest.toml'
                    manifest.parent.mkdir(parents=True)
                    manifest.write_text('format_version=1\nid="fixture.enhancement.unlock"\nname="Early unlock"\n'
                        'version="1.0.0"\ndescription="Reviewed static title mod"\n[[target]]\ngame_id="12345678"\nxbe_sha256="'+'a'*64+'"\n'
                        '[[feature]]\nid="early-unlock"\nname="Early unlock"\ndefault_enabled=false\n')
                cache.write_text('PILOT_NATIVE_OPTIMIZATION:STRING=OPTIMIZED\n'
                    'PILOT_IO_DEVICE:BOOL=OFF\nPILOT_SCHEDULING:STRING=NATIVE\n'
                    + ('PILOT_TITLE_MOD_SOURCE:FILEPATH=reviewed-title.c\n' if title_switch else ''))
                output = root/(name.replace(' ', '-')+'-package')
                with patch.object(package, 'inventory', return_value=closure):
                    record = package.stage(definition, runtime, launcher, cache, embed, [], output, boot_intro=helper)
                self.assertEqual(record['startup']['default_enabled'], enabled)
                self.assertEqual(record['capabilities']['visual_mods'], title_switch)
                self.assertTrue((output/'tools/native_title_switches.py').is_file())
                if title_switch:
                    choices=Catalog(title_manifests(output/'release.json',record),output/'selection.json',
                                    defaults=record['launcher']['defaults'])
                    self.assertEqual(title_environment(choices,record),{'XLLE_PILOT_TITLE_MOD_EARLY_UNLOCK':'1'})
                    choices.features[0]['enabled']=False
                    self.assertEqual(title_environment(choices,record),{'XLLE_PILOT_TITLE_MOD_EARLY_UNLOCK':'0'})
                self.assertEqual((output/'mods/preloaded/packages'/camera.PACKAGE/'1.0.0/manifest.toml').is_file(),
                                 camera_enabled)
                relative = Path('mods/preloaded/packages') / boot.MOD_PACKAGE / '1.0.0/manifest.toml'
                self.assertEqual((output/relative).read_bytes(), (package.ROOT/relative).read_bytes())
                catalog = Catalog([output/relative], output/'data/mod-selection.json')
                self.assertFalse(boot.skip_animation(catalog))
                self.assertEqual(catalog.features[0]['name'], 'Skip startup animation')
                self.assertTrue((output/'native/XboxStartup.exe').is_file())
                self.assertEqual(record['startup']['module'], 'native/XboxStartup.dll')
                self.assertEqual((output/'native/XboxStartup.dll').read_bytes(), module.read_bytes())
                self.assertEqual((output/'Start Game.exe').read_bytes(), launch_stub.read_bytes())
                self.assertEqual((output/'native/third_party/libsamplerate/README.md').read_bytes(),
                                 helper_notice.read_bytes())
                self.assertTrue((output/'notices/xbox-boot-re/LICENSE').is_file())
                self.assertTrue((output/'source/native-startup/pilots/native-boot/boot_main.cpp').is_file())
                self.assertTrue((output/'source/native-startup/pilots/native-boot/boot_timeline.h').is_file())
                self.assertTrue((output/'source/native-startup/pilots/native-boot/boot_timeline_test.cpp').is_file())
                for source in ('boot_after_image.h', 'boot_after_image_test.cpp', 'boot_handoff.h'):
                    self.assertTrue((output/'source/native-startup/pilots/native-boot'/source).is_file())
                self.assertTrue((output/'source/native-startup/src/third_party/xbox-boot-re/core/src/Audio.c').is_file())
                for source in package.BOOT_SHARED_SOURCES:
                    self.assertEqual((output/'source/native-startup'/source).read_bytes(),
                                     (package.ROOT/source).read_bytes())
                self.assertTrue((output/'tools/native_boot_launcher.py').is_file())
                self.assertFalse(any(p.suffix in ('.pak', '.bin', '.img') for p in output.rglob('*')))
                archive(output, root/(name.replace(' ', '-')+'.zip'))


if __name__ == '__main__':
    unittest.main()
