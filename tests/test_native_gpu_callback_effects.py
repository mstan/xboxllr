"""Synthetic inputs only: no original game bytes or generated code fixtures."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import native_gpu_callback_effects as effects


class CallbackEffectsTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / 'build/generated'
        self.gen = self.output / 'gen'
        self.gen.mkdir(parents=True)
        self.blob = bytes(range(128))
        self.xbe = self.output / 'synthetic-input.bin'
        self.xbe.write_bytes(self.blob)
        self.image = dict(sha256=effects.digest(self.blob), sections=[
            dict(name='.text', address=4096, size=128, raw_size=128, raw_offset=0, flags=4),
            dict(name='.data', address=8192, size=4096, raw_size=0, raw_offset=128, flags=1)])
        self.proof = dict(schema='gpu-callback-effects.v1', adapter='timestamp-context16.v1',
                          xbe_sha256=self.image['sha256'], routine='0x00001000',
                          write_base='0x00002000', write_stride=16, write_bytes=8, write_section='.data', closure=[])
        for index, address in enumerate((4096, 4128)):
            size = 4
            body = 'void sub_%08X(void)\n{\n    return;\n}\n' % address
            text = '/** Original: 0x%08X - 0x%08X (%d bytes, synthetic) */\n' % (address, address+size, size) + body
            (self.gen / ('recomp_%04d.c' % index)).write_text(text, newline='\n')
            self.proof['closure'].append(dict(address='0x%08x' % address, bytes=size,
                original_sha256=effects.digest(self.blob[address-4096:address-4096+size]),
                generated_body_sha256=effects.digest(body.encode())))
        self.proof_path = self.output / 'synthetic-proof.json'
        self.proof_path.write_text(json.dumps(self.proof))
        self.write_manifest()

    def write_manifest(self):
        manifest = dict(sha256=self.image['sha256'], files={p.name:effects.digest(p.read_bytes()) for p in self.gen.iterdir()})
        (self.output / 'generation.aot-private.json').write_text(json.dumps(manifest))

    def emit(self):
        with patch.object(effects, 'PROOFS', (self.proof_path,)), patch.object(effects, 'parse_xbe', return_value=self.image):
            return effects.emit(self.xbe, self.output)

    def test_exact_closure_inventory_and_idempotence(self):
        original = {p.name:p.read_bytes() for p in self.gen.iterdir()}
        self.assertEqual(self.emit()['version'], 1)
        before = (self.output / 'generation.aot-private.json').read_bytes()
        self.assertEqual(self.emit()['version'], 1)
        self.assertEqual(before, (self.output / 'generation.aot-private.json').read_bytes())
        self.assertTrue((self.gen / effects.HEADER).is_file())
        self.assertEqual(original, {name:(self.gen / name).read_bytes() for name in original})
        effects.checked_manifest(self.output)

    def test_original_mutation_refuses_effects(self):
        changed = bytearray(self.blob);changed[0] ^= 1;self.xbe.write_bytes(changed)
        self.assertEqual(self.emit()['version'], 0)

    def test_legitimate_generated_change_falls_back(self):
        path = self.gen / 'recomp_0000.c'
        path.write_text(path.read_text().replace('return;', 'int changed = 1; return;'))
        self.write_manifest()
        self.assertEqual(self.emit()['version'], 0)

    def test_tamper_and_stray_inventory_are_not_blessed(self):
        path = self.gen / 'recomp_0000.c';original = path.read_bytes();path.write_bytes(original+b' ')
        with self.assertRaisesRegex(ValueError, 'changed generated'):
            self.emit()
        path.write_bytes(original)
        (self.gen / 'unreceipted.h').write_text('unreceipted')
        with self.assertRaisesRegex(ValueError, 'inventory'):
            self.emit()
        self.assertFalse((self.gen / effects.HEADER).exists())

    def test_missing_extent_or_writable_backing_refuses(self):
        self.image['sections'][1]['flags'] = 0
        self.assertEqual(self.emit()['version'], 0)
        self.image['sections'][1]['flags'] = 1
        path = self.gen / 'recomp_0000.c';path.write_text(path.read_text().replace('4 bytes', '5 bytes'))
        self.write_manifest()
        self.assertEqual(self.emit()['version'], 0)

    def test_unknown_image_and_wrong_shape_refuse(self):
        self.proof['xbe_sha256'] = '0'*64;self.proof_path.write_text(json.dumps(self.proof))
        self.assertEqual(self.emit()['version'], 0)
        self.proof['xbe_sha256'] = self.image['sha256'];self.proof['write_stride'] = 32
        self.proof_path.write_text(json.dumps(self.proof))
        self.assertEqual(self.emit()['version'], 0)

    def test_native_and_other_configuration_compile_guards(self):
        compiler = os.environ.get('GPU_EFFECTS_TEST_CC') or shutil.which('gcc') or shutil.which('clang')
        if not compiler:
            self.skipTest('optional C preprocessor unavailable')
        source = self.output / 'admission.c'
        # A guard-only stub needs no host system header: undefining _WIN32 on
        # MinGW intentionally excludes platform admission, not its CRT headers.
        source.write_text('#include "graphics_callback_admission.h"\n#if PILOT_GPU_SCALAR_PREPARATION_AVAILABLE != EXPECTED\n#error incorrect admission\n#endif\n')
        certificate = self.output / effects.HEADER
        common = [compiler, '-E', '-P', '-x', 'c', str(source), '-I', str(self.output), '-I', str(ROOT / 'pilots/native-title'), '-DPILOT_MEMORY_WRITE_WATCH', '-D_WIN32']
        cases = [(None, [], 0), (0, [], 0), (1, [], 1), (1, ['-U_WIN32'], 0),
                 (1, ['-UPILOT_MEMORY_WRITE_WATCH'], 0)]
        cases += [(1, ['-D'+name], 0) for name in ('PILOT_SCHEDULING_LLE', 'PILOT_AUDIO_REFERENCE',
            'PILOT_AUDIO_DEVICE_LLE','PILOT_KERNEL_CLOCK_NATIVE','PILOT_USB_LLE','PILOT_IO_DEVICE_LLE',
            'PILOT_MEMORY_LLE','PILOT_FILES_LLE','PILOT_WRITE_WATCH_PORTABLE')]
        for version, defines, expected in cases:
            if version is None:
                certificate.unlink(missing_ok=True)
            else:
                certificate.write_text('#define PILOT_GPU_CALLBACK_EFFECTS_VERSION %d\n' % version)
            with self.subTest(version=version, defines=defines):
                result=subprocess.run(common+defines+['-DEXPECTED='+str(expected)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
                self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))


if __name__ == '__main__':
    unittest.main()
