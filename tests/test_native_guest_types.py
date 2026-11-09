"""Known-byte integer/enum/bitfield expectations, independent of DIA availability."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_guest_types import TypeDecoder


def document():
    rows=[dict(id=1,tag=16,basic=6,length=4),dict(id=2,tag=16,basic=7,length=8),
          dict(id=3,tag=12,name='Authored',type=1,length=4),
          dict(id=4,tag=7,parent=3,name='Negative',constant=dict(kind='integer',value='-1')),
          dict(id=5,tag=7,parent=3,name='Folded',constant=dict(kind='integer',value='-1')),
          dict(id=6,tag=7,location=6,type=1,storage_offset=1,bit_position=7,bit_width=9),
          dict(id=7,tag=7,location=6,type=2,storage_offset=0,bit_position=0,bit_width=64),
          dict(id=8,tag=17,type=3)]
    return dict(schema='xboxllr.guest-types.v1',xbe_sha256='a'*64,types={str(r['id']):r for r in rows},objects=[])


class GuestTypes(unittest.TestCase):
    def test_enum_signed_value_aliases_and_typedef_are_retained(self):
        decoder=TypeDecoder(document(),'a'*64)
        for identifier in (3,8):
            result=decoder.scalar(identifier,b'\xff'*4)
            self.assertEqual(result['value'],'-1')
            self.assertEqual(result['names'],['Negative','Folded'])
        self.assertEqual(decoder.scalar(3,b'\x02\0\0\0')['names'],[])

    def test_unsigned_64_bit_values_keep_all_digits(self):
        result=TypeDecoder(document(),'a'*64).scalar(2,b'\xff'*8)
        self.assertEqual(result['value'],'18446744073709551615')
        self.assertFalse(result['signed'])

    def test_signed_field_crossing_byte_boundary(self):
        decoder=TypeDecoder(document(),'a'*64)
        # Nine-bit two's complement -3 at bit 7 of the 32-bit storage word.
        raw=b'\xa5'+((509<<7)|0x55).to_bytes(4,'little')+b'\x5a'
        result=decoder.bitfield(6,raw)
        self.assertEqual(result['value'],'-3')
        self.assertEqual(result['byte_offset'],1)
        self.assertEqual(result['bit_position'],7)

    def test_zero_position_is_available_and_full_width_is_not_truncated(self):
        result=TypeDecoder(document(),'a'*64).bitfield(7,b'\xff'*8)
        self.assertEqual(result['value'],'18446744073709551615')
        self.assertEqual(result['bit_position'],0)

    def test_missing_metadata_does_not_turn_into_zero(self):
        for key in ('bit_position','bit_width','storage_offset'):
            data=document();del data['types']['7'][key]
            result=TypeDecoder(data,'a'*64).bitfield(7,b'\xff'*8)
            self.assertEqual(result['status'],'unavailable')

    def test_invalid_ranges_and_short_reads_are_unavailable(self):
        for key,value in [('bit_position',64),('bit_width',0),('storage_offset',-1)]:
            data=document();data['types']['7'][key]=value
            self.assertEqual(TypeDecoder(data,'a'*64).bitfield(7,b'\xff'*8)['status'],'unavailable')
        decoder=TypeDecoder(document(),'a'*64)
        self.assertEqual(decoder.bitfield(7,b'\xff'*7)['status'],'unavailable')
        self.assertEqual(decoder.scalar(1,b'\xff'*8)['status'],'unavailable')

    def test_wrong_image_missing_underlying_type_and_cycles_are_rejected(self):
        with self.assertRaises(ValueError):TypeDecoder(document(),'b'*64)
        data=document();data['types']['3']['type']=99
        self.assertEqual(TypeDecoder(data,'a'*64).scalar(3,b'\0'*4)['status'],'unavailable')
        data['types']['3']['type']=8
        self.assertEqual(TypeDecoder(data,'a'*64).scalar(3,b'\0'*4)['status'],'unavailable')


if __name__=='__main__':unittest.main()
