"""Typed paths, stale snapshots and unsafe reads using authored guest layouts."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parent/'fixtures'))
from native_guest_inspect import Inspector
from native_guest_inspection import fixture


class GuestInspector(unittest.TestCase):
    def setUp(self):
        data, _, self.document = fixture()
        self.memory = bytearray(data[0x500:0x580])
        self.memory[:4] = (42).to_bytes(4, 'little')
        self.snapshot = dict(ok=True, snapshot=1, point='function-entry',
                             xbe_sha256=self.document['xbe_sha256'], pc=0x11020,
                             entry_sp=0x20000, registers=dict(eax=77, esp=0x20000))
        self.reads = 0

    def transport(self, port, command):
        self.assertEqual(port, 1)
        if command == 'guest_snapshot':
            return copy.deepcopy(self.snapshot)
        op, epoch, address, size = command.split()
        self.assertEqual(op, 'read_guest')
        self.assertEqual(int(epoch), self.snapshot['snapshot'])
        address, size = int(address), int(size)
        self.reads += 1
        if address == 0x20000 and size == 4:
            raw = (0x11014).to_bytes(4, 'little')
        elif 0x12000 <= address < address+size <= 0x12080:
            raw = self.memory[address-0x12000:address-0x12000+size]
        else:
            raise ValueError('guest range is unmapped, unsafe or unsupported')
        return dict(ok=True, snapshot=int(epoch), address=address, data=raw.hex())

    def inspector(self, **kw):
        return Inspector(self.document, 1, transport=self.transport, **kw)

    def test_member_array_pointer_enum_and_bitfield_paths(self):
        inspector = self.inspector()
        before = bytes(self.memory)
        for path, value in [('count','42'), ('mode','-1'), ('enabled','1'),
                            ('signed_bits','-1'), ('items[2]','33'), ('self->count','42')]:
            result = inspector.query(object_name='state', path=path)
            self.assertEqual(result['value'], value, result)
        self.assertEqual(inspector.query(object_name='state', path='mode')['names'], ['Ready'])
        self.document['objects'].append(dict(name='root_pointer', address=0x1200c, type=11, mapped_bytes=4))
        self.assertEqual(inspector.query(object_name='root_pointer', path='->count')['value'], '42')
        self.assertEqual(bytes(self.memory), before)

    def test_expansion_reports_cycle_null_and_mmio(self):
        result = self.inspector().query(object_name='state', follow=2)['members']
        self.assertEqual(result['self']['pointee']['status'], 'cycle')
        self.assertEqual(result['empty']['pointee']['status'], 'null')
        self.assertEqual(result['device']['pointee']['status'], 'unavailable')
        self.assertEqual([x['value'] for x in result['items']['elements']], ['11','22','33'])

    def test_guest_variables_use_registers_and_certified_entry_frame(self):
        inspector = self.inspector()
        self.assertEqual(inspector.query(variable_id=101)['decoded']['value'], '77')
        self.assertEqual(inspector.query(variable_id=102)['address'], 0x11014)
        self.assertEqual(inspector.query(variable_id=103)['value'], '-7')
        self.assertEqual(inspector.query(variable_id=104)['status'], 'optimized-away')
        self.snapshot['point'] = 'unknown'
        self.assertEqual(self.inspector().query(variable_id=102)['status'], 'unavailable')

    def test_missing_offsets_bounds_and_budget_are_not_guessed(self):
        del self.document['types']['20']['location_offset']
        inspector = self.inspector()
        for path in ('count','items[3]','items[-1]','empty->count','unknown'):
            self.assertEqual(inspector.query(object_name='state', path=path)['status'], 'unavailable')
        inspector = self.inspector(max_bytes=4)
        self.assertEqual(inspector.query(object_name='state', path='mode')['value'], '-1')
        self.assertEqual(inspector.query(object_name='state', path='items[0]')['status'], 'unavailable')

    def test_wrong_image_fails_before_any_memory_reads(self):
        self.snapshot['xbe_sha256'] = 'b'*64
        with self.assertRaisesRegex(ValueError, 'another image'):
            self.inspector()
        self.assertEqual(self.reads, 0)

    def test_expired_or_changed_snapshot_never_returns_a_typed_result(self):
        inspector = self.inspector()
        self.snapshot['snapshot'] = 2
        with self.assertRaises(ValueError):
            inspector.query(variable_id=101)
        def bad_read(port, command):
            result = self.transport(port, command)
            if command.startswith('read_guest '):
                result['snapshot'] += 1
            return result
        inspector = Inspector(self.document, 1, transport=bad_read)
        self.assertEqual(inspector.query(object_name='state', path='count')['status'], 'unavailable')


if __name__ == '__main__':
    unittest.main()
