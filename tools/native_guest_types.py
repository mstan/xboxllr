"""Portable, image-bound scalar, enum and bitfield value decoding."""
import struct
from native_guest_symbols import _matches_image


class TypeDecoder:
    def __init__(self, document, expected_xbe_sha256):
        if (document.get('schema') != 'xboxllr.guest-types.v1' or
                not _matches_image(document, expected_xbe_sha256) or
                not isinstance(document.get('types'), dict)):
            raise ValueError('guest type metadata belongs to another image or schema')
        self.types = document['types']
        self.constants = {}
        for member in self.types.values():
            if member.get('tag') == 7 and member.get('constant', {}).get('kind') == 'integer':
                self.constants.setdefault(member.get('parent'), []).append(member)

    def integer_layout(self, type_id):
        seen = set()
        while type_id not in seen and len(seen) < 64:
            seen.add(type_id)
            row = self.types.get(str(type_id))
            if row is None:
                return None
            if row.get('tag') in (12, 17):  # Enum or typedef.
                type_id = row.get('type')
                continue
            size, basic = row.get('length'), row.get('basic')
            if (row.get('tag') != 16 or type(size) is not int or
                    not 1 <= size <= 16 or basic not in (6, 7, 10, 13, 14, 31)):
                return None
            return dict(bytes=size, bits=size*8, signed=basic in (6, 13, 31))
        return None

    def scalar(self, type_id, raw):
        row = self.types.get(str(type_id))
        seen = set()
        while row and row.get('tag') == 17 and row['id'] not in seen:
            seen.add(row['id']);row = self.types.get(str(row.get('type')))
        if row and row.get('tag')==14 and row.get('length')==4:
            if len(raw)!=4:
                return dict(status='unavailable',reason='incomplete or oversized pointer read')
            return dict(status='available',kind='pointer',address=int.from_bytes(raw,'little'),bits=32)
        if row and row.get('tag')==16 and row.get('basic')==8 and row.get('length') in (4,8):
            size=row['length']
            if len(raw)!=size:
                return dict(status='unavailable',reason='incomplete or oversized floating-point read')
            value=struct.unpack('<f' if size==4 else '<d',raw)[0]
            return dict(status='available',kind='float',value=str(value),bits=size*8)
        layout = self.integer_layout(type_id)
        if layout is None:
            return dict(status='unavailable', reason='integer width or signedness unavailable')
        if len(raw) != layout['bytes']:
            return dict(status='unavailable', reason='incomplete or oversized scalar read')
        value = int.from_bytes(raw, 'little', signed=layout['signed'])
        result = dict(status='available', kind='integer', value=str(value), **layout)
        if row and row.get('tag') == 12:
            result['kind'] = 'enum'
            result['names'] = []
            mask = (1 << layout['bits'])-1
            for member in self.constants.get(row['id'], []):
                constant = member.get('constant', {})
                try:
                    number = int(constant['value'], 10)
                except (KeyError, ValueError, TypeError):
                    continue
                if -(1 << (layout['bits']-1)) <= number <= mask and number & mask == value & mask:
                    result['names'].append(member['name'])
        return result

    def bitfield(self, member_id, instance_bytes):
        member = self.types.get(str(member_id))
        if member is None or member.get('tag') != 7 or member.get('location') != 6:
            return dict(status='unavailable', reason='not a bitfield member')
        layout = self.integer_layout(member.get('type'))
        offset, position, width = (member.get(key) for key in
                                   ('storage_offset', 'bit_position', 'bit_width'))
        if (layout is None or any(type(n) is not int for n in (offset, position, width)) or
                offset < 0 or position < 0 or width <= 0 or position+width > layout['bits']):
            return dict(status='unavailable', reason='invalid or unavailable bitfield storage metadata')
        if offset+layout['bytes'] > len(instance_bytes):
            return dict(status='unavailable', reason='incomplete bitfield storage read')
        return self.bitfield_storage(member_id, instance_bytes[offset:offset+layout['bytes']])

    def bitfield_storage(self, member_id, raw):
        """Decode only the field's storage, without allocating its enclosing object."""
        member = self.types.get(str(member_id), {})
        layout = self.integer_layout(member.get('type'))
        offset, position, width = (member.get(key) for key in
                                   ('storage_offset', 'bit_position', 'bit_width'))
        if (member.get('tag') != 7 or member.get('location') != 6 or layout is None or
                any(type(n) is not int for n in (offset, position, width)) or
                offset < 0 or position < 0 or width <= 0 or position+width > layout['bits'] or
                len(raw) != layout['bytes']):
            return dict(status='unavailable', reason='invalid or unavailable bitfield storage metadata')
        value = (int.from_bytes(raw, 'little') >> position) & ((1 << width)-1)
        if layout['signed'] and value & (1 << (width-1)):
            value -= 1 << width
        return dict(status='available', kind='bitfield', value=str(value),
                    signed=layout['signed'], bits=width, storage_bytes=layout['bytes'],
                    byte_offset=offset, bit_position=position)
