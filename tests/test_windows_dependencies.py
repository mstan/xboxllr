"""ROM-free PE fixtures and release dependency selection contracts."""
from pathlib import Path
from contextlib import redirect_stderr, redirect_stdout
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import windows_dependencies as w


def pe(imports=(), delayed=(), *, machine=0x8664, old_delay=False):
    # An authored minimal PE32+ file: one raw .rdata section, no guest code.
    data = bytearray(0x1000)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, machine, 1, 0, 0, 0, 240, 0x2022)
    optional = 0x98
    struct.pack_into('<H', data, optional, 0x20b)
    struct.pack_into('<Q', data, optional + 24, 0x400000)
    struct.pack_into('<I', data, optional + 60, 0x200)
    struct.pack_into('<I', data, optional + 108, 16)
    section = optional + 240
    data[section:section + 8] = b'.rdata\0\0'
    struct.pack_into('<IIII', data, section + 8, 0xe00, 0x1000, 0xe00, 0x200)
    cursor = 0x900
    for index, at, names, stride in [(1, 0x200, imports, 20), (13, 0x500, delayed, 32)]:
        if not names:
            continue
        struct.pack_into('<II', data, optional + 112 + 8 * index, at + 0xe00, (len(names) + 1) * stride)
        for i, name in enumerate(names):
            encoded = name.encode() + b'\0'
            data[cursor:cursor + len(encoded)] = encoded
            name_rva = cursor + 0xe00
            if index == 1:
                struct.pack_into('<IIIII', data, at + i * stride, 1, 0, 0, name_rva, 1)
            else:
                struct.pack_into('<IIIIIIII', data, at + i * stride,
                                 0 if old_delay else 1,
                                 name_rva + (0x400000 if old_delay else 0), 0, 0, 0, 0, 0, 0)
            cursor += len(encoded)
    return bytes(data)


class WindowsDependencyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def put(self, name, imports=(), delayed=(), **kwargs):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(pe(imports, delayed, **kwargs))
        return path

    def test_normal_delay_and_transitive_imports_and_cycles(self):
        app = self.put('app.exe', ['Bridge.DLL', 'KERNEL32.dll'], ['SDL2.dll'])
        self.put('bridge.dll', ['SDL2.dll'])
        self.put('sdl2.dll', ['bridge.dll', 'USER32.dll'])
        result = w.inventory([app])
        self.assertTrue(result['complete'])
        self.assertEqual([p['name'] for p in result['files']], ['app.exe', 'bridge.dll', 'sdl2.dll'])
        self.assertEqual(result['system_imports'], ['kernel32.dll', 'user32.dll'])
        self.assertEqual(result['files'][0]['delay_imports'], ['sdl2.dll'])

    def test_transitive_delay_missing_is_failure(self):
        app = self.put('app.exe', ['bridge.dll'])
        self.put('bridge.dll', delayed=['vcruntime140.dll'])
        result = w.inventory([app])
        self.assertFalse(result['complete'])
        self.assertEqual(result['missing'], [dict(importer='bridge.dll', name='vcruntime140.dll', kind='delay_imports')])

    def test_os_api_sets_do_not_exempt_redist_libraries(self):
        app = self.put('app.exe', ['api-ms-win-core-file-l1-1-0.dll', 'xinput1_4.dll', 'bcryptprimitives.dll',
                                   'xaudio2_9redist.dll', 'vcruntime140.dll'])
        result = w.inventory([app])
        self.assertEqual({x['name'] for x in result['missing']}, {'xaudio2_9redist.dll', 'vcruntime140.dll'})

    def test_only_declared_roots_are_inspected_without_recursion(self):
        app = self.put('app.exe', ['helper.dll'])
        self.put('nested/helper.dll')
        self.assertFalse(w.inventory([app])['complete'])
        self.assertTrue(w.inventory([app], [self.root / 'nested'])['complete'])

    def test_different_dll_versions_are_ambiguous(self):
        app = self.put('app.exe', ['helper.dll'])
        self.put('one/helper.dll')
        changed = self.put('two/helper.dll', ['kernel32.dll'])
        with self.assertRaisesRegex(ValueError, 'ambiguous DLL versions'):
            w.inventory([app], [changed.parent, self.root / 'one'])

    def test_identical_duplicates_are_not_copied_twice(self):
        app = self.put('app.exe', ['helper.dll'])
        self.put('one/helper.dll'); self.put('two/helper.dll')
        self.assertEqual(len(w.inventory([app], [self.root / 'one', self.root / 'two'])['files']), 2)

    def test_explicit_runtime_module_is_a_root(self):
        app = self.put('app.exe')
        plugin = self.put('plugin.dll', ['missing.dll'])
        self.assertTrue(w.inventory([app])['complete'])
        self.assertFalse(w.inventory([app, plugin])['complete'])

    def test_wrong_architecture_fails_even_for_transitive_dependency(self):
        app = self.put('app.exe', ['helper.dll'])
        self.put('helper.dll', machine=0x14c)
        with self.assertRaisesRegex(ValueError, 'x64 dependencies'):
            w.inventory([app])

    def test_unsafe_import_name_is_never_resolved(self):
        for name in ('../helper.dll', 'C:helper.dll', 'path\\helper.dll'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'plain DLL'):
                w.Image(pe([name])).imports()

    def test_old_delay_va_descriptors(self):
        self.assertEqual(w.Image(pe(delayed=['helper.dll'], old_delay=True)).imports(delayed=True), ['helper.dll'])

    def test_truncated_file_and_unmapped_rva_fail(self):
        with self.assertRaisesRegex(ValueError, 'truncated PE'):
            w.Image(pe()[:300])
        data = bytearray(pe(['helper.dll']))
        struct.pack_into('<I', data, 0x200 + 12, 0x80000000)
        with self.assertRaisesRegex(ValueError, 'unmapped'):
            w.Image(data).imports()

    def test_missing_descriptor_terminator_fails(self):
        data = bytearray(pe(['helper.dll']))
        struct.pack_into('<I', data, 0x98 + 112 + 8 + 4, 20)
        with self.assertRaisesRegex(ValueError, 'unterminated import'):
            w.Image(data).imports()

    def test_entry_basename_collision_is_not_silently_overwritten(self):
        first = self.put('one/app.exe')
        second = self.put('two/app.exe', ['kernel32.dll'])
        with self.assertRaisesRegex(ValueError, 'conflicting package basename'):
            w.inventory([first, second])

    def test_cli_missing_import_reports_failure_with_private_inventory(self):
        app = self.put('app.exe', ['helper.dll'])
        report = self.root / 'inventory.aot-private.json'
        with patch.object(sys, 'argv', ['audit', '--entry', str(app), '--output', str(report)]), redirect_stdout(io.StringIO()):
            self.assertEqual(w.main(), 1)
        result = json.loads(report.read_text())
        self.assertFalse(result['complete'])
        self.assertEqual(result['missing'][0]['name'], 'helper.dll')

    def test_cli_cannot_overwrite_an_input_through_a_report_hardlink(self):
        app = self.put('app.exe')
        before = app.read_bytes()
        report = self.root / 'inventory.aot-private.json'
        os.link(app, report)
        with patch.object(sys, 'argv', ['audit', '--entry', str(app), '--output', str(report)]), redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit) as stopped:
                w.main()
        self.assertEqual(stopped.exception.code, 2)
        self.assertEqual(app.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
