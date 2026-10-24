"""Public certificate fixtures; no original executable or certificate keys."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from xbe_identity import parse_identity, read_identity, title_id_label


def fixture(name='Halo', title_id=0x4d530004, *, base=0x10000, cert=0x200):
    data = bytearray(0x600)
    data[:4] = b'XBEH'
    struct.pack_into('<II', data, 0x104, base, len(data))
    struct.pack_into('<I', data, 0x118, base + cert)
    struct.pack_into('<III', data, cert, 0x1d0, 0, title_id)
    raw = name.encode('utf-16le', errors='surrogatepass')
    if len(raw) > 80:
        raise ValueError('Fixture title is too long')
    data[cert + 12:cert + 12 + len(raw)] = raw
    return data


def cases():
    valid = [fixture(), fixture('ジェット 🚀', base=0x500000, cert=0x300), fixture('A' * 40),
             fixture('A' * 39 + '\ud800'), fixture('\ud800X\udfff'), fixture(' Halo\n\t\x1b\u202e '),
             fixture('Hi\0PRIVATE_SUFFIX'), fixture('', title_id=0xfffe0000)]
    invalid = [b'', b'XBEH', fixture()[:0x240]]
    for offset, value in ((0, 0), (0x108, 0x100), (0x108, 0x1000), (0x118, 0xffff),
                          (0x118, 0x10160), (0x118, 0x105b0), (0x118, 0xffffffff),
                          (0x200, 0x5b), (0x200, 0xffffffff)):
        data = fixture()
        struct.pack_into('<I', data, offset, value)
        invalid.append(data)
    return valid, invalid


class IdentityTests(unittest.TestCase):
    def test_title_id_name_and_relocated_certificate(self):
        self.assertEqual(parse_identity(fixture()), dict(title_id=0x4d530004, title_name='Halo'))
        self.assertEqual(parse_identity(fixture('ジェット 🚀', base=0x500000, cert=0x300))['title_name'], 'ジェット 🚀')
        self.assertEqual(title_id_label(0x4d530004), 'MS-004 (0x4D530004)')
        self.assertEqual(title_id_label(0x4d53000a), 'MS-010 (0x4D53000A)')
        self.assertEqual(title_id_label(0xfffe0000), '0xFFFE0000')

    def test_utf16_termination_full_field_and_safe_display(self):
        self.assertEqual(parse_identity(fixture('Hi\0PRIVATE_SUFFIX'))['title_name'], 'Hi')
        self.assertEqual(parse_identity(fixture('A' * 40))['title_name'], 'A' * 40)
        self.assertEqual(parse_identity(fixture('A' * 39 + '\ud800'))['title_name'], 'A' * 39 + '\ufffd')
        self.assertEqual(parse_identity(fixture('\ud800X\udfff'))['title_name'], '\ufffdX\ufffd')
        self.assertEqual(parse_identity(fixture(' Halo\n\t\x1b\u202e '))['title_name'], 'Halo')

    def test_rejects_invalid_headers_addresses_and_certificate_extents(self):
        for i, data in enumerate(cases()[1]):
            with self.subTest(case=i), self.assertRaises(ValueError):
                parse_identity(data)

    def test_extracted_file_needs_no_section_or_disc_parser(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / '日本語.xbe'
            path.write_bytes(fixture())
            self.assertEqual(read_identity(path), parse_identity(path.read_bytes()))

    def test_portable_launcher_reader_matches_the_python_reader(self):
        compiler = shutil.which('g++') or shutil.which('c++')
        if sys.platform == 'win32' and Path('C:/msys64/mingw64/bin/g++.exe').is_file():
            compiler = 'C:/msys64/mingw64/bin/g++.exe'
        if not compiler:
            self.skipTest('A C++17 compiler is required for the native reader check')
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            harness = root / 'inspect.cpp'
            harness.write_text('''#include "xbe_identity.h"
#include <iostream>
#include <cstdlib>
int main(int argc, char **argv) {
    xboxllr_expected_title_id=argc>2?(uint32_t)std::strtoul(argv[2],nullptr,16):0;
    xboxllr_has_expected_title_id=argc>2;
    xboxllr_xbe_select(argv[1]);const auto &id=xboxllr_xbe_identity;
    std::cout<<id.valid<<"\\n"<<id.title_id<<"\\n"<<id.title_name<<"\\n"<<id.title_label<<"\\n"<<xboxllr_xbe_rejection()<<"\\n";
}''', encoding='utf-8')
            binary = root / ('inspect.exe' if sys.platform == 'win32' else 'inspect')
            source = ROOT / 'pilots/native-launcher'
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I' + str(source),
                            str(harness), str(source / 'xbe_identity.cpp'), '-o', str(binary)], check=True, timeout=90)
            path = root / 'fixture.xbe'
            for i, data in enumerate(sum(cases(), [])):
                with self.subTest(case=i):
                    path.write_bytes(data)
                    lines = subprocess.check_output([str(binary), str(path), '4d530004'], encoding='utf-8', timeout=10).splitlines()
                    try:
                        expected = parse_identity(data)
                    except ValueError:
                        self.assertEqual(lines[0], '0')
                        continue
                    self.assertEqual(lines[:4], ['1', str(expected['title_id']), expected['title_name'], title_id_label(expected['title_id'])])
                    self.assertEqual(lines[4], 'Unsupported XBE revision' if expected['title_id'] == 0x4d530004 else 'Wrong game selected')


if __name__ == '__main__':
    unittest.main()
