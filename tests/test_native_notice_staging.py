"""Real build-system checks for native-title license delivery, without a CPU build."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


class NativeNoticeStagingTests(unittest.TestCase):
    def test_incremental_notices_and_missing_output_recovery(self):
        cmake = os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
        if not cmake:
            self.skipTest('CMake is unavailable')
        ninja = os.environ.get('NINJA_COMMAND') or shutil.which('ninja')
        generators = [('Ninja', ninja)] if ninja else []
        if os.name == 'nt':
            generators.append(('Visual Studio 17 2022', None))
        if not generators:
            self.skipTest('No supported fixture generator is available')
        module = (Path(__file__).resolve().parents[1] / 'pilots/native-title/notice_staging.cmake').as_posix()
        for generator, make_program in generators:
            with self.subTest(generator=generator), tempfile.TemporaryDirectory(prefix='native notice fixture ') as temp:
                root = Path(temp)
                source, build = root / 'source', root / 'build'
                (source / 'tree/nested').mkdir(parents=True)
                license_file = source / 'LICENSE'
                license_file.write_bytes(b'Authored license\r\n${literal}\n')
                nested = source / 'tree/nested/NOTICE'
                nested.write_bytes(b'Authored dependency notice\n')
                (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(native_notice_fixture NONE)
include("''' + module + '''")
set(output "${CMAKE_CURRENT_BINARY_DIR}/stage")
if(CMAKE_CONFIGURATION_TYPES)
  string(APPEND output "/$<CONFIG>")
endif()
pilot_notices_begin("${output}")
pilot_notice_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "LICENSE")
pilot_notice_tree("${CMAKE_CURRENT_SOURCE_DIR}/tree" "third_party")
pilot_notices_finish()
''')

                def run(*args):
                    result = subprocess.run([cmake, *map(str, args)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    return result.stdout

                configure = ['-S', source, '-B', build, '-G', generator]
                if make_program:
                    configure.append('-DCMAKE_MAKE_PROGRAM=' + make_program.replace('\\', '/'))
                run(*configure)
                output = build / 'stage'
                if generator.startswith('Visual Studio'):
                    output /= 'Release'

                def build_notices():
                    return run('--build', build, '--config', 'Release', '--target', 'pilot_notices', '--parallel', '2')

                build_notices()
                staged = output / 'third_party/nested/NOTICE'
                self.assertEqual((output / 'LICENSE').read_bytes(), license_file.read_bytes())
                self.assertEqual(staged.read_bytes(), nested.read_bytes())
                before = {p.relative_to(output): p.stat().st_mtime_ns for p in output.rglob('*') if p.is_file()}
                build_notices()
                self.assertEqual(before, {p.relative_to(output): p.stat().st_mtime_ns for p in output.rglob('*') if p.is_file()})

                # A notice-only edit must be delivered without linking any app.
                time.sleep(1.05)
                license_file.write_bytes(b'Changed authored license\r\n')
                build_notices()
                self.assertEqual((output / 'LICENSE').read_bytes(), license_file.read_bytes())
                self.assertEqual(staged.stat().st_mtime_ns, before[Path('third_party/nested/NOTICE')])

                # Build tools must inspect all outputs, not just a stamp file.
                staged.unlink()
                build_notices()
                self.assertEqual(staged.read_bytes(), nested.read_bytes())

                (source / 'tree/new').mkdir()
                (source / 'tree/new/LICENSE').write_bytes(b'New authored dependency\n')
                # Refresh the source inventory before the build. VS can load
                # its project before ZERO_CHECK regenerates it; unlike Ninja,
                # that invocation need not see newly introduced build rules.
                run(*configure)
                build_notices()
                self.assertEqual((output / 'third_party/new/LICENSE').read_bytes(), b'New authored dependency\n')

                # A timestamp-only source edit must settle after a single copy.
                time.sleep(1.05)
                license_file.touch()
                build_notices()
                settled = (output / 'LICENSE').stat().st_mtime_ns
                build_notices()
                self.assertEqual(settled, (output / 'LICENSE').stat().st_mtime_ns)


if __name__ == '__main__':
    unittest.main()
