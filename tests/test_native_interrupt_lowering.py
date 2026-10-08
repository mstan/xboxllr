import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_interrupt_lowering import lower


def marker(op, address):
    return f'RECOMP_UNIMPL("{op}", 0x{address:08X}u); /* TODO: {op} */'


class InterruptLoweringTests(unittest.TestCase):
    def test_byte_port_read_keeps_upper_eax_and_uses_dx(self):
        source=marker('in al, dx',0x1000)
        code,rows=lower(source,lambda *args:b'\xec'+bytes(15))
        self.assertEqual(code,'SET_LO8(eax, pilot_cpu_in8(LO16(edx)));')
        self.assertTrue(rows[0]['supported'])

    def test_port_width_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'differs from original'):
            lower(marker('in al, dx',0x1000),lambda *args:b'\xed'+bytes(15))

    def test_real_following_instruction_precedes_delivery(self):
        code = marker('cli', 0x1000) + '\n' + marker('sti', 0x1001) + '\n    esi = eax;\n    next();'
        raw = bytes.fromhex('fafb89c6') + bytes(16)
        result, rows = lower(code, lambda address, size: raw[address-0x1000:address-0x1000+size])
        self.assertEqual(len(rows), 2)
        self.assertIn('pilot_cpu_cli();', result)
        self.assertIn('pilot_cpu_sti();\n    esi = eax;\n    pilot_cpu_sti_complete();', result)
        self.assertEqual(lower(result, lambda *args: b'')[0], result)

    def test_control_flow_shadow_is_not_silently_accepted(self):
        source = marker('sti', 0x1000) + '\n    goto loc_X;'
        result, rows = lower(source, lambda *args: bytes.fromhex('fbeb01') + bytes(16))
        self.assertEqual(source, result)
        self.assertFalse(rows[0]['supported'])

    def test_mismatched_original_opcode_fails(self):
        with self.assertRaisesRegex(ValueError, 'differs from original'):
            lower(marker('cli', 0x1000), lambda *args: bytes.fromhex('fb90') + bytes(16))

    def test_missing_or_altered_shadow_statement_keeps_guard(self):
        for statement in ('edi = eax;', 'PILOT_THREAD_CHECKPOINT();\n esi = eax;'):
            source = marker('sti', 0x1000) + '\n' + statement
            result, rows = lower(source, lambda *args: bytes.fromhex('fb89c6') + bytes(16))
            self.assertEqual(source, result)
            self.assertFalse(rows[0]['supported'])


if __name__ == '__main__':
    unittest.main()
