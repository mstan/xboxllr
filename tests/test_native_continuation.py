"""Real pinned donor -> framework lowering -> fresh-process AOT resume.

Run with py -3 tests/test_native_continuation.py. Optional XLLE_CPU_DONOR and
CC choose a locally available pinned donor/compiler. No game inputs required.
"""
from __future__ import annotations
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from native_continuation_lowering import lower_sources
from native_pilot import DONOR_PIN, harden_generated


def fixture_affinity(allowed):
    """Prefer the owner's cores; otherwise keep at most four permitted CPUs."""
    if allowed <= 0:
        raise RuntimeError('no allowed CPUs for the continuation fixture')
    preferred = sum(1 << i for i in (8, 10, 12, 15))
    if preferred & allowed == preferred:
        return preferred
    selected = 0
    for _ in range(4):
        if not allowed:
            break
        cpu = allowed & -allowed
        selected |= cpu
        allowed &= ~cpu
    return selected


def fixture_compiler(explicit=None):
    """Resolve a real executable path; an invalid explicit CC is an error."""
    def existing(command):
        if not command:
            return None
        path = Path(command).expanduser()
        if path.is_file():
            return str(path.resolve())
        found = shutil.which(command)
        if found and Path(found).is_file():
            return str(Path(found).resolve())
        return None
    if explicit:
        resolved = existing(explicit)
        if not resolved:
            raise RuntimeError('explicit CC executable unavailable: '+explicit)
        return resolved
    candidates = []
    if sys.platform == 'win32':
        candidates.append('C:/msys64/mingw64/bin/gcc.exe')
    candidates.extend(('gcc', 'clang', 'cc'))
    for command in candidates:
        resolved = existing(command)
        if resolved:
            return resolved
    raise unittest.SkipTest('C compiler unavailable; set CC to an existing compiler')


def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte)*16777619) & 0xffffffff
    return value


def authored_fixture():
    """Original IA-32 bytes, with only branch displacement assembly here."""
    base, middle, leaf, service = 0x10000, 0x10100, 0x10200, 0x10300
    root = bytearray.fromhex('6a07e8')
    root += struct.pack('<i', middle-(base+len(root)+4))
    root += bytes.fromhex('83c404b903000000')  # clean argument; ecx=3
    loop = len(root)
    root += bytes.fromhex('01050010010049')   # add [0x11000],eax; dec ecx
    root += bytes([0x75, (loop-(len(root)+2)) & 255])
    root += b'\xe8'+struct.pack('<i', service-(base+len(root)+5))+b'\xc3'
    # Middle owns a distinct ebp, passes a real guest-stack argument, and calls
    # leaf indirectly. Leaf overwrites EAX after capture, so re-evaluating a
    # caller's indirect target from current registers would be incorrect.
    mid = bytes.fromhex('5589e5ba00020100ff7508ffd283c40483c0018be55dc3')
    # cmp -> jump -> checkpoint label -> jne requires the saved lazy operands.
    child = bytearray.fromhex('5589e58b450883f807eb00')
    capture_pc = leaf+len(child)
    child += bytes.fromhex('750fb811000000892d041001008be55dc3')
    child += bytes.fromhex('b8ee0000008be55dc3')
    svc = bytes.fromhex('8b1d00100100891d08100100c3')
    parts = {base: bytes(root), middle: mid, leaf: bytes(child), service: svc}
    image = bytearray(0x400)
    for address, raw in parts.items():
        image[address-base:address-base+len(raw)] = raw
    return bytes(image), parts, capture_pc


class ContinuationRoundTrip(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # All compiler/fixture children inherit normal priority and the owner's
        # explicit four-logical-CPU limit. Compilation is sequential, one TU
        # compiler at a time; no MSBuild or /MP work is launched.
        if sys.platform == 'win32':
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.GetCurrentProcess.restype = ctypes.c_void_p
            handle = kernel.GetCurrentProcess()
            kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            kernel.GetProcessAffinityMask.argtypes = [ctypes.c_void_p,
                ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
            kernel.SetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.c_size_t]
            process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
            if not kernel.GetProcessAffinityMask(handle, ctypes.byref(process_mask), ctypes.byref(system_mask)):
                raise RuntimeError('cannot query allowed fixture CPUs')
            selected = fixture_affinity(process_mask.value & system_mask.value)
            if not kernel.SetPriorityClass(handle, 0x20) or not kernel.SetProcessAffinityMask(
                    handle, selected):
                raise RuntimeError('cannot enforce requested compiler priority/affinity')
        cls.donor = Path(os.environ.get('XLLE_CPU_DONOR', ROOT.parent/'refs/xboxrecomp-phobos665-review-20261006'))
        if not cls.donor.is_dir():
            raise unittest.SkipTest('local pinned CPU donor unavailable; set XLLE_CPU_DONOR')
        git = 'C:/Program Files/Git/cmd/git.exe' if sys.platform == 'win32' else 'git'
        pin = subprocess.check_output([git, '-C', str(cls.donor), 'rev-parse', 'HEAD'], text=True).strip()
        if pin != DONOR_PIN:
            raise RuntimeError('fixture CPU donor pin mismatch')
        subprocess.run([git, '-C', str(cls.donor), 'diff', '--exit-code', 'HEAD', '--',
                        'tools', 'templates/runtime'], check=True, capture_output=True)
        cls.cc = fixture_compiler(os.environ.get('CC'))
        sys.path.insert(0, str(cls.donor))
        from tools.recomp import config
        from tools.recomp.translator import FunctionTranslator
        cls.image, cls.parts, cls.capture_pc = authored_fixture()
        config._install([config.Section('.text', 0x10000, len(cls.image), 0,
                                        len(cls.image), True)], entry_point=0x10000,
                        kernel_thunk_addr=0, origin='xlle-authored-continuation-fixture')
        db = {a: {'_addr': a, 'start': f'0x{a:08X}', 'end': a+len(raw), 'size': len(raw)}
              for a, raw in cls.parts.items()}
        translator = FunctionTranslator(cls.image, db, trace_all_entries=True)
        cls.original = '#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n'+ '\n'.join(
            translator.translate_function(a, db[a]) for a in db)
        cls.read = staticmethod(lambda address, size: cls.image[address-0x10000:address-0x10000+size])
        cls.out = ROOT/'build/native-continuation-diagnostic'
        cls.out.mkdir(parents=True, exist_ok=True)
        (cls.out/'recomp_0000.c').write_text(cls.original, encoding='utf-8')
        shutil.copyfile(cls.donor/'templates/runtime/recomp_types.h', cls.out/'recomp_types.h')
        harden_generated(cls.out)
        cls.original = (cls.out/'recomp_0000.c').read_text(encoding='utf-8')
        cls.lowered, cls.schema, cls.manifest = lower_sources({'recomp_0000.c': cls.original}, cls.read)
        (cls.out/'original.c').write_text(cls.original, encoding='utf-8')
        (cls.out/'diagnostic.c').write_text(cls.lowered['recomp_0000.c'], encoding='utf-8')
        (cls.out/'pilot_continuation_schema.h').write_text(cls.schema, encoding='utf-8')
        (cls.out/'recomp_funcs.h').write_text('#include "recomp_types.h"\n'+ '\n'.join(
            f'void sub_{a:08X}(void);' for a in db)+'\n', encoding='utf-8')
        cls.host = ROOT/'tests/fixtures/native_continuation_host.c'
        cls.runtime = ROOT/'pilots/native-title'
        cls.executables = {}
        # The donor currently emits plain RET returns. Admit the unbraced-if
        # shape defensively and execute its false arm: an unconditional return
        # introduced by instrumentation would leave the guest ESP/depth wrong.
        conditional = cls.original.replace('    esp += 4; return;',
            '    if (eax == 0xfeedu) return;\n    esp += 4; return;', 1)
        conditional_sources, conditional_schema, _ = lower_sources({'recomp_0000.c': conditional}, cls.read)
        conditional_dir = cls.out/'conditional'
        conditional_dir.mkdir(exist_ok=True)
        (conditional_dir/'conditional.c').write_text(conditional_sources['recomp_0000.c'], encoding='utf-8')
        (conditional_dir/'pilot_continuation_schema.h').write_text(conditional_schema, encoding='utf-8')
        for profile in ('original', 'diagnostic', 'conditional'):
            exe = cls.out/(profile+('.exe' if sys.platform == 'win32' else ''))
            source_dir = conditional_dir if profile == 'conditional' else cls.out
            command = [cls.cc, '-std=c11', '-O2', '-I'+str(source_dir), '-I'+str(cls.out), '-I'+str(cls.runtime),
                       '-DFIXTURE_CAPTURE_PC='+str(cls.capture_pc), str(cls.host), str(source_dir/(profile+'.c'))]
            if profile != 'original':
                command += ['-DFIXTURE_CONTINUATIONS', str(cls.runtime/'continuation_diagnostic.c')]
            command += ['-o', str(exe)]
            compiled = subprocess.run(command, capture_output=True, text=True)
            if compiled.returncode:
                raise RuntimeError('fixture compilation failed:\n'+compiled.stdout+compiled.stderr)
            cls.executables[profile] = exe
        cls.snapshot = cls.out/'nested.xct'
        cls.run_exe('diagnostic', 'capture', str(cls.snapshot))
        cls.baseline = cls.run_exe('original')
        cls.manifest.update(donor=pin, fixture_sha256=hashlib.sha256(cls.image).hexdigest(),
                            capture_pc=cls.capture_pc)
        (cls.out/'evidence.json').write_text(json.dumps(cls.manifest, indent=2)+'\n')

    @classmethod
    def run_exe(cls, profile, *arguments):
        result = subprocess.run([str(cls.executables[profile]), *arguments], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(f'{profile} fixture exit {result.returncode}: {result.stderr}')
        return result.stdout

    def test_fresh_process_resumes_nested_direct_and_indirect_returns(self):
        self.assertEqual(self.run_exe('diagnostic'), self.baseline)
        self.assertEqual(self.run_exe('diagnostic', 'resume', str(self.snapshot)), self.baseline)
        self.assertIn('data=54,126952,54', self.baseline)  # original leaf17 + middle1, three loop trips
        self.assertIn('trace=4', self.baseline)
        wire = self.snapshot.read_bytes()
        self.assertEqual(struct.unpack_from('<I', wire, 66100+40)[0], 3)

    def test_unbraced_conditional_return_remains_conditional(self):
        self.assertEqual(self.run_exe('conditional'), self.baseline)

    def test_invalid_records_reject_before_replacing_pending_execution_cpu_ram(self):
        original = self.snapshot.read_bytes()
        offset = 8+40+0x10000+4+32*4*4
        for name, relative, value in (
                ('token', 44+4, 0xffffffff), ('function', 44, 0xdeadbeef),
                ('callee', 44+8, 0x10300), ('version', 4, 99),
                ('identity', 8, 0xdeadbeef), ('count', 40, 33),
                ('indirect-target', 44+44+40, 0x10300)):
            wire = bytearray(original)
            struct.pack_into('<I', wire, offset+relative, value)
            # Re-sign both integrity checks: rejection must come from schema,
            # token, and chain validation, not merely a checksum mismatch.
            struct.pack_into('<I', wire, len(wire)-8, fnv(wire[offset:-8]))
            struct.pack_into('<I', wire, len(wire)-4, fnv(wire[:-4]))
            path = self.out/(name+'.xct');path.write_bytes(wire)
            with self.subTest(name=name):
                self.assertEqual(self.run_exe('diagnostic', 'resume', str(self.snapshot), str(path)), self.baseline)
        for name, wire in (('truncated', original[:-1]), ('trailing', original+b'\0')):
            path = self.out/(name+'.xct');path.write_bytes(wire)
            self.assertEqual(self.run_exe('diagnostic', 'resume', str(self.snapshot), str(path)), self.baseline)

    def test_precheckpoint_lazy_flags_and_parent_local_ebp_are_observable(self):
        offset = 8+40+0x10000+4+32*4*4
        for name, relative, value in (('changed-flags', 44+2*44+12, 8),
                                      ('changed-parent-ebp', 44+44+32, 0x1efe8)):
            wire = bytearray(self.snapshot.read_bytes())
            struct.pack_into('<I', wire, offset+relative, value)
            struct.pack_into('<I', wire, len(wire)-8, fnv(wire[offset:-8]))
            struct.pack_into('<I', wire, len(wire)-4, fnv(wire[:-4]))
            path = self.out/(name+'.xct');path.write_bytes(wire)
            resumed = self.run_exe('diagnostic', 'resume', str(path))
            self.assertNotEqual(resumed, self.baseline)
            if name == 'changed-flags':
                self.assertIn('data=717,0,717', resumed)

    def test_unknown_automatic_storage_and_opcode_are_rejected(self):
        source = self.original.replace('uint32_t _fa =', 'uint32_t unowned =', 1)
        with self.assertRaisesRegex(ValueError, 'unsupported continuation local'):
            lower_sources({'recomp_0000.c': source}, self.read)
        for storage in ('uint32_t *unowned;', 'float unowned[8];', 'struct Other *unowned;'):
            source = self.original.replace('loc_00010000: ;', storage+'\nloc_00010000: ;')
            with self.subTest(storage=storage), self.assertRaisesRegex(ValueError, 'unhandled automatic storage'):
                lower_sources({'recomp_0000.c': source}, self.read)
        for transfer in ('RECOMP_ITAIL(eax);', 'RECOMP_ICALL_SAFE(eax, g_esp);', 'other_guest_function();'):
            source = self.original.replace('loc_00010000: ;', transfer+'\nloc_00010000: ;')
            with self.subTest(transfer=transfer), self.assertRaisesRegex(ValueError, 'unhandled generated'):
                lower_sources({'recomp_0000.c': source}, self.read)
        with self.assertRaisesRegex(ValueError, 'opcode outside'):
            lower_sources({'recomp_0000.c': self.original}, lambda address, size: b'\xf4'+self.read(address, size)[1:])

    def test_wire_is_data_only_and_schema_changes_with_lowering(self):
        _, _, changed = lower_sources({'recomp_0000.c': self.original+'\n/* changed lowering */\n'}, self.read)
        self.assertNotEqual(changed['identity'], self.manifest['identity'])
        self.assertEqual(self.snapshot.stat().st_size, 66100+48+3*44+4)

    def test_portable_runtime_compiles_for_aarch64_without_host_fibers(self):
        clang = os.environ.get('XLLE_CROSS_CC') or shutil.which('clang')
        if not clang:
            self.skipTest('clang cross compiler unavailable')
        result = subprocess.run([clang, '--target=aarch64-none-elf', '-ffreestanding',
                                 '-std=c11', '-Wall', '-Wextra', '-Werror', '-c',
                                 str(self.runtime/'continuation_diagnostic.c'),
                                 '-o', str(self.out/'continuation-aarch64.o')],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


class ContinuationHostSelection(unittest.TestCase):
    def test_affinity_prefers_owner_mask_and_falls_back_within_allowed_cpus(self):
        preferred = sum(1 << i for i in (8, 10, 12, 15))
        self.assertEqual(fixture_affinity((1 << 32)-1), preferred)
        self.assertEqual(fixture_affinity(0b1010), 0b1010)
        restricted = sum(1 << i for i in (1, 3, 8, 10, 12, 15))
        self.assertEqual(fixture_affinity(restricted), preferred)
        restricted &= ~(1 << 15)
        self.assertEqual(fixture_affinity(restricted), sum(1 << i for i in (1, 3, 8, 10)))
        with self.assertRaisesRegex(RuntimeError, 'no allowed CPUs'):
            fixture_affinity(0)

    def test_compiler_absence_skips_but_invalid_explicit_cc_errors(self):
        self.assertEqual(fixture_compiler(sys.executable), str(Path(sys.executable).resolve()))
        with mock.patch.object(Path, 'is_file', return_value=False), mock.patch.object(shutil, 'which', return_value=None):
            with self.assertRaises(unittest.SkipTest):
                fixture_compiler()
            with self.assertRaisesRegex(RuntimeError, 'explicit CC executable unavailable'):
                fixture_compiler('missing-explicit-compiler')


if __name__ == '__main__':
    unittest.main()
