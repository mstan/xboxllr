"""Authored XBE/CodeView/DIA records; no game or SDK fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import unittest
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from native_guest_symbols import Symbols, SCHEMA, import_dia, import_map, codeview_identities, describe_layout
from test_aot_inventory import xbe


SIGNATURE = 0x12345678
GUID = uuid.UUID('10203040-5060-7080-90a0-b0c0d0e0f000')


def image(kind='NB10'):
    cv = (b'NB10'+struct.pack('<III', 0, SIGNATURE, 2) if kind == 'NB10' else
          b'RSDS'+GUID.bytes_le+struct.pack('<I', 2))
    return bytes(xbe(b'\x90\xc3'+cv+b'private/source/game.pdb\0'))


def metadata():
    identity = dict(pdb=True, signature=SIGNATURE, age=2, guid=str(GUID))
    function = dict(id=2, parent=1, tag=5, section=1, offset=0, rva=0x400,
                    length=2, name='?Tick@@YAXXZ', undecorated='void Tick(void)', type=4)
    return [identity, function]


class GuestSymbols(unittest.TestCase):
    def test_compiler_context_keeps_actual_compiland_ownership(self):
        records=metadata()
        records[1]['lexical_parent']=1
        records += [dict(id=40,tag=2,section=0,name='authored.obj'),
                    dict(id=2,parent=40,tag=-2,kind='compiland_function'),
                    dict(id=41,parent=40,tag=3,section=0,name='',compiler_name='Authored compiler',
                         frontend_major=12,frontend_minor=1,frontend_build=1234,backend_major=12,platform=3)]
        symbols,_=import_dia(image(),records,'0'*64)
        self.assertEqual(symbols['functions'][0]['compiland_ids'],[40])
        self.assertEqual(symbols['compilers']['40'][0]['frontend_build'],1234)
        self.assertEqual(symbols['compilands']['40']['name'],'authored.obj')
        self.assertNotIn('backend_build',symbols['compilers']['40'][0])

    def test_scopes_liveness_frames_and_tls_bind_to_guest_coordinates(self):
        blob=bytearray(image())
        struct.pack_into('<I',blob,0x12c,0x101a0)
        struct.pack_into('<6I',blob,0x1a0,0x11000,0x11002,0,0,4,0)
        records=metadata()+[
            dict(id=20,parent=2,tag=6,section=1,offset=0,length=2,name=''),
            dict(id=21,parent=2,tag=21,section=1,offset=0,length=0,name=''),
            dict(id=22,parent=2,tag=22,section=1,offset=1,length=0,name=''),
            dict(id=23,parent=20,tag=7,section=0,offset=0,length=0,name='local',type=4,
                 location=3,data_kind=1,register_id=22,location_offset=-4,
                 live_range_count=1,live_section=1,live_offset=0,live_length=1),
            dict(tag=-1,kind='frame',section=1,offset=0,length=2,program='$T0 $ebp =',frame_type=4)]
        _,types=import_dia(bytes(blob),records,'0'*64)
        self.assertEqual(types['scopes']['2']['debug_start'],0x11000)
        self.assertEqual(types['scopes']['2']['debug_end'],0x11001)
        self.assertEqual(types['variables']['23']['live_start'],0x11000)
        self.assertEqual(types['variables']['23']['location_offset'],-4)
        self.assertNotIn('23',types['types'])
        self.assertEqual(types['frames'][0]['address'],0x11000)
        self.assertEqual(types['tls_template'],dict(start=0x11000,size=6,initialized_bytes=2))

    def test_import_preserves_constant_precision_and_zero_bit_position(self):
        records=metadata()+[
            dict(id=10,parent=0,tag=16,section=0,name='',type=0,length=8,basic=7),
            dict(id=11,parent=0,tag=12,section=0,name='AuthoredEnum',type=10,length=8),
            dict(id=12,parent=11,tag=7,section=0,name='All',type=10,length=0,location=10,
                 constant=dict(kind='integer',bits=64,signed=False,value='18446744073709551615')),
            dict(id=13,parent=20,tag=7,section=0,name='Bits',type=10,length=9,location=6,
                 field_offset=1,storage_offset=1,bit_position=0,bit_width=9)]
        _,types=import_dia(image(),records,'0'*64)
        self.assertEqual(types['types']['12']['constant']['value'],'18446744073709551615')
        self.assertEqual(types['types']['13']['bit_position'],0)
        self.assertEqual(types['types']['13']['bit_width'],9)
        self.assertEqual(types['types']['13']['storage_offset'],1)

    def map_text(self):
        return ('entry point at 0001:00000000\n'
                ' 0001:00000000 00001000H .text CODE\n'
                ' 0001:00000000 _Tick 00400400 f main.obj\n'
                ' 0001:00000000 _Alias 00400400 f main.obj\n'
                ' 0001:ffffffff _Discarded 004003ff f main.obj\n')

    def test_map_preserves_aliases_and_excludes_discarded_comdat(self):
        data = import_map(image(), self.map_text(), '0'*64)
        self.assertEqual(data['rejected_unbacked_records'], 1)
        self.assertEqual(data['functions'][0]['names'], ['_Tick', '_Alias'])
        self.assertEqual(data['functions'][0]['extent_kind'], 'entry-only')
        symbols = Symbols(data, data['xbe_sha256'])
        self.assertIsNotNone(symbols.lookup(0x11000))
        self.assertIsNone(symbols.lookup(0x11001))

    def test_map_entry_layout_and_file_backing_must_match(self):
        for bad in (self.map_text().replace('entry point at 0001:00000000', 'entry point at 0001:00000001'),
                    self.map_text().replace('.text CODE', '.other CODE'),
                    self.map_text().replace('00001000H', '00000100H'),
                    self.map_text().replace('00000000 _Tick', '00000900 _Tick')):
            with self.assertRaises(ValueError):
                import_map(image(), bad, '0'*64)

    def test_nb10_and_rsds_bind_to_actual_image_identity(self):
        for kind in ('NB10', 'RSDS'):
            blob = image(kind)
            symbols, types = import_dia(blob, metadata(), '0'*64)
            self.assertEqual(symbols['xbe_sha256'], hashlib.sha256(blob).hexdigest())
            found = Symbols(symbols, symbols['xbe_sha256']).lookup(0x11001)
            self.assertEqual(found['names'], ['void Tick(void)'])
            self.assertEqual(found['offset'], 1)
            self.assertNotIn('private/source', json.dumps((symbols, types)))

    def test_pdb_age_and_signature_must_match(self):
        for key in ('age', 'signature'):
            records = metadata()
            records[0][key] += 1
            with self.assertRaisesRegex(ValueError, 'CodeView'):
                import_dia(image(), records, '0'*64)

    def test_multiple_identities_are_rejected_as_ambiguous(self):
        blob = image() + b'NB10'+struct.pack('<III', 0, SIGNATURE+1, 2)+b'other.pdb\0'
        with self.assertRaisesRegex(ValueError, 'uniquely'):
            import_dia(blob, metadata(), '0'*64)

    def test_arbitrary_codeview_text_is_not_an_identity(self):
        self.assertFalse(codeview_identities(b'NB10'+b'\0'*12+b'not-a-debug-file\0'))
        self.assertFalse(codeview_identities(b'RSDS'+b'\0'*20+b'no-terminator.pdb'))

    def test_zero_fill_or_nonexistent_section_is_not_a_function(self):
        for key, value in [('section', 2), ('offset', 0x500), ('length', 0x5000)]:
            records = metadata()
            records[1][key] = value
            symbols, _ = import_dia(image(), records, '0'*64)
            self.assertFalse(symbols['functions'])
            self.assertEqual(symbols['rejected_unbacked_records'], 1)

    def test_folded_function_names_are_retained_as_aliases(self):
        records = metadata()
        records.append(dict(records[1], id=3, undecorated='void Alias(void)'))
        symbols, _ = import_dia(image(), records, '0'*64)
        self.assertEqual(symbols['functions'][0]['names'], ['void Tick(void)', 'void Alias(void)'])

    def test_heavily_folded_functions_keep_all_aliases(self):
        # Real linked builds can fold over ten thousand instantiations to one ret.
        names = ['Folded%d' % i for i in range(10583)]
        data = dict(schema=SCHEMA, xbe_sha256='a'*64, functions=[
            dict(address=100, size=1, names=names)])
        self.assertEqual(Symbols(data, 'a'*64).lookup(100)['names'], names)

    def test_type_members_retain_owner_offset_and_referenced_type(self):
        records = metadata()+[
            dict(id=20, parent=1, tag=11, section=0, name='Actor', type=0, length=16),
            dict(id=21, parent=20, tag=7, section=0, name='health', type=22,
                 length=0, field_offset=12, data_kind=7, location=4),
            dict(id=22, parent=0, tag=16, section=0, name='', type=0, length=4, basic=6)]
        _, types = import_dia(image(), records, '0'*64)
        self.assertEqual(types['types']['21']['parent'], 20)
        self.assertEqual(types['types']['21']['field_offset'], 12)
        self.assertEqual(types['types']['21']['type'], 22)
        self.assertEqual(types['types']['22']['length'], 4)

    def test_bss_globals_are_mapped_but_tls_is_not_a_static_object(self):
        records = metadata()+[
            dict(id=30, tag=7, section=1, offset=0x500, length=4,
                 name='global_counter', undecorated='', type=31, location=1),
            dict(id=32, tag=7, section=1, offset=0x508, length=4,
                 name='thread_counter', undecorated='', type=31, location=10)]
        _, layouts = import_dia(image(), records, '0'*64)
        self.assertEqual([r['name'] for r in layouts['objects']], ['global_counter'])
        self.assertEqual(layouts['objects'][0]['address'], 0x11500)

    def test_wrong_build_cannot_load_a_symbol_map(self):
        symbols, _ = import_dia(image(), metadata(), '0'*64)
        self.assertIsNotNone(Symbols(symbols, symbols['xbe_sha256'].upper()).lookup(0x11000))
        with self.assertRaisesRegex(ValueError, 'different XBE'):
            Symbols(symbols, 'f'*64)

    def test_ranges_are_half_open_and_overlapping_aliases_fall_back(self):
        data = dict(schema=SCHEMA, xbe_sha256='a'*64, functions=[
            dict(address=100, size=100, names=['outer']),
            dict(address=120, size=10, names=['inner'])])
        symbols = Symbols(data, 'a'*64)
        self.assertEqual(symbols.lookup(129)['names'], ['inner'])
        self.assertEqual(symbols.lookup(130)['names'], ['outer'])
        self.assertIsNone(symbols.lookup(200))
        self.assertIsNone(symbols.lookup(99))

    def test_invalid_extents_and_order_are_rejected(self):
        for functions in ([dict(address=0xffffffff, size=2, names=['bad'])],
                          [dict(address=1, size=0, names=['bad'])],
                          [dict(address=2, size=1, names=['b']),dict(address=1,size=1,names=['a'])]):
            with self.assertRaises(ValueError):
                Symbols(dict(schema=SCHEMA, xbe_sha256='a'*64, functions=functions), 'a'*64)

    def test_guest_stops_and_recent_entries_are_named_without_host_guessing(self):
        symbols, _ = import_dia(image(), metadata(), '0'*64)
        names = Symbols(symbols, symbols['xbe_sha256'])
        log = ('[PILOT STOP] authored failure at 00011001; entries=3 esp=00011000\n'
               '[NATIVE STOP RECENT] 00011000 00011001 12345678\n'
               '[FAULT SITE] pc=00011001 module=host.exe rva=00011000\n')
        found = names.annotate_log(log)
        self.assertEqual([r['address'] for r in found], [0x11001, 0x11000, 0x11001])

    def layouts(self):
        return dict(schema='xboxllr.guest-types.v1', xbe_sha256='a'*64, types={
            '10': dict(id=10, parent=0, tag=11, name='Actor', length=16, type=0),
            '11': dict(id=11, parent=10, tag=7, name='health', location=4,
                       field_offset=12, type=20),
            '12': dict(id=12, parent=10, tag=7, name='bits', location=6,
                       field_offset=4, type=20),
            '13': dict(id=13, parent=0, tag=11, name='Actor', length=24, type=0),
            '20': dict(id=20, parent=0, tag=16, name='', length=4, type=0),
            '30': dict(id=30, parent=0, tag=17, name='Entity', length=16, type=10),
        }, objects=[dict(name='g_actor', address=0x12000, type=10, mapped_bytes=64)])

    def test_layout_lookup_keeps_ambiguous_type_names_and_owner_links(self):
        found = describe_layout(self.layouts(), 'a'*64, type_name='Actor')
        self.assertEqual([r['type']['length'] for r in found], [16, 24])
        fields = found[0]['members']
        self.assertEqual(fields[0]['byte_offset'], 12)
        self.assertEqual(fields[0]['type_info']['length'], 4)
        self.assertIsNone(fields[1]['byte_offset'])
        self.assertFalse(found[1]['members'])

    def test_layout_alias_and_static_object_retain_referenced_types(self):
        alias = describe_layout(self.layouts(), 'a'*64, type_id=30)[0]
        self.assertEqual(alias['target_type']['name'], 'Actor')
        obj = describe_layout(self.layouts(), 'a'*64, object_name='g_actor')[0]
        self.assertEqual(obj['address'], 0x12000)
        self.assertEqual(obj['type_info']['id'], 10)
        self.assertEqual(obj['mapped_bytes'], 64)
        self.assertFalse(describe_layout(self.layouts(), 'a'*64, type_name='Missing'))

    def test_layout_lookup_rejects_wrong_build_and_ambiguous_queries(self):
        self.assertTrue(describe_layout(self.layouts(), 'A'*64, type_id=10))
        with self.assertRaisesRegex(ValueError, 'different XBE'):
            describe_layout(self.layouts(), 'b'*64, type_id=10)
        with self.assertRaisesRegex(ValueError, 'exactly one'):
            describe_layout(self.layouts(), 'a'*64, type_id=10, type_name='Actor')


if __name__ == '__main__':
    unittest.main()
