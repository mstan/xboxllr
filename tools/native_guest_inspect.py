"""Bounded read-only typed queries against a paused native guest snapshot."""
import argparse
import json
from pathlib import Path
import re

from native_debug import request
from native_guest_symbols import TYPE_LIMIT
from native_guest_variables import Variables


class Inspector:
    def __init__(self, document, port, *, transport=request, max_bytes=65536):
        if type(max_bytes) is not int or not 1 <= max_bytes <= 65536:
            raise ValueError('read budget must be in 1..65536')
        self.port, self.transport, self.remaining = port, transport, max_bytes
        self.snapshot = transport(port, 'guest_snapshot')
        self.variables = Variables(document, self.snapshot['xbe_sha256'])
        self.decoder = self.variables.decoder
        self.document = document
        self.context = dict(self.snapshot, registers=dict(self.snapshot['registers']))
        for index, raw in enumerate(self.snapshot.get('xmm', [])):
            value = bytes.fromhex(raw)
            if len(value) != 16 or index >= 8:
                raise ValueError('invalid guest vector register snapshot')
            self.context['registers']['xmm'+str(index)] = value
        if self.snapshot.get('point') == 'function-entry':
            self.context['frame_search_start'] = {
                str(scope['id']): self.snapshot['entry_sp']
                for scope in document.get('scopes', {}).values()
                if scope.get('tag') == 5 and scope.get('address') == self.snapshot['pc']}
        self.members = {}
        for row in self.decoder.types.values():
            if row.get('tag') == 7 and row.get('data_kind') == 7:
                self.members.setdefault(row.get('parent'), []).append(row)

    def read(self, address, size):
        if (type(address) is not int or type(size) is not int or not 1 <= size <= 4096 or
                not 0 <= address < address+size <= 0x100000000 or size > self.remaining):
            raise ValueError('guest extent or inspection read budget exceeded')
        result = self.transport(self.port, f'read_guest {self.snapshot["snapshot"]} {address} {size}')
        if result.get('snapshot') != self.snapshot['snapshot'] or result.get('address') != address:
            raise ValueError('guest snapshot changed during inspection')
        data = bytes.fromhex(result['data'])
        if len(data) != size:
            raise ValueError('incomplete guest memory response')
        self.remaining -= size
        return data

    def row(self, type_id):
        seen = set()
        while type_id not in seen and len(seen) < 64:
            seen.add(type_id)
            row = self.decoder.types.get(str(type_id))
            if row is None or row.get('tag') != 17:
                return row
            type_id = row.get('type')
        return None

    @staticmethod
    def unavailable(reason):
        return dict(status='unavailable', reason=reason)

    def _member(self, row, name):
        matches = [m for m in self.members.get(row['id'], []) if m.get('name') == name]
        if len(matches) != 1:
            raise ValueError('member unknown or ambiguous')
        member = matches[0]
        offset = member.get('storage_offset') if member.get('location') == 6 else member.get('location_offset')
        layout = self.row(member.get('type'))
        size = layout.get('length') if layout else None
        if (member.get('location') not in (4, 6) or type(offset) is not int or offset < 0 or
                type(size) is not int or size <= 0 or type(row.get('length')) is not int or
                offset+size > row['length']):
            raise ValueError('member offset or containing layout unavailable')
        return member, offset

    def _path(self, address, type_id, path):
        tokens = re.findall(r'->|[A-Za-z_$][\w$]*|\[\d+\]|\.', path)
        if ''.join(tokens) != path or len(tokens) > 32:
            raise ValueError('invalid or excessive member path')
        bitfield = None
        expect_member = True
        for index, token in enumerate(tokens):
            row = self.row(type_id)
            if row is None or bitfield is not None:
                raise ValueError('path continues beyond an unavailable or scalar member')
            if token == '->':
                if row.get('tag') != 14 or row.get('length') != 4 or (expect_member and index > 0):
                    raise ValueError('pointer path requires a 32-bit pointer')
                address = int.from_bytes(self.read(address, 4), 'little')
                if not address:
                    raise ValueError('null pointer')
                type_id = row.get('type'); expect_member = True
            elif token == '.':
                if expect_member:
                    raise ValueError('missing path member')
                expect_member = True
            elif token.startswith('['):
                if row.get('tag') != 15:
                    raise ValueError('array subscript requires declared array bounds')
                element = self.row(row.get('type')); index = int(token[1:-1])
                size = element.get('length') if element else None
                count = row.get('count')
                if (type(count) is not int or not 0 <= index < count or type(size) is not int or
                        size <= 0 or row.get('length') != size*count):
                    raise ValueError('array bounds or element layout unavailable')
                address += index*size; type_id = row.get('type'); expect_member = False
            else:
                if not expect_member or row.get('tag') != 11:
                    raise ValueError('member path requires a declared aggregate')
                member, offset = self._member(row, token)
                address += offset; type_id = member.get('type')
                bitfield = member if member.get('location') == 6 else None
                expect_member = False
        if tokens and expect_member:
            raise ValueError('incomplete member path')
        return address, type_id, bitfield

    def _value(self, address, type_id, follow, depth, seen, nodes, bitfield=None):
        nodes[0] += 1
        if depth > 8 or nodes[0] > 128:
            return self.unavailable('inspection nesting or node limit exceeded')
        row = self.row(type_id)
        if row is None:
            return self.unavailable('type unavailable')
        key = (address, row['id'])
        if key in seen:
            return dict(status='cycle', address=address, type_id=type_id)
        size = row.get('length')
        try:
            if bitfield:
                layout = self.decoder.integer_layout(bitfield.get('type'))
                if layout is None:
                    return self.unavailable('bitfield storage unavailable')
                return self.decoder.bitfield_storage(bitfield['id'], self.read(address, layout['bytes']))
            if row.get('tag') == 11:
                members = self.members.get(row['id'], [])
                if len(members) > 64:
                    return self.unavailable('aggregate exceeds member limit; select a member path')
                values = {}
                for member in members:
                    name = member.get('name', '')
                    try:
                        selected, offset = self._member(row, name)
                        values[name] = self._value(address+offset, selected.get('type'), follow, depth+1,
                                                  seen | {key}, nodes,
                                                  selected if selected.get('location') == 6 else None)
                    except ValueError as error:
                        values[name] = self.unavailable(str(error))
                return dict(status='available', kind='aggregate', address=address, members=values)
            if row.get('tag') == 15:
                element = self.row(row.get('type')); count = row.get('count')
                stride = element.get('length') if element else None
                if (type(count) is not int or not 0 <= count <= 64 or type(stride) is not int or
                        stride <= 0 or size != stride*count):
                    return self.unavailable('array layout unavailable or exceeds 64 elements; select an index')
                return dict(status='available', kind='array', address=address, elements=[
                    self._value(address+i*stride, row['type'], follow, depth+1, seen | {key}, nodes)
                    for i in range(count)])
            if type(size) is not int or not 1 <= size <= 4096:
                return self.unavailable('scalar size unavailable or exceeds read limit')
            result = self.decoder.scalar(type_id, self.read(address, size))
            if result.get('kind') == 'pointer' and follow:
                pointer = result['address']
                result['pointee'] = (self._value(pointer, row.get('type'), follow-1, depth+1,
                                                seen | {key}, nodes) if pointer else dict(status='null'))
            return result
        except (OSError, ValueError) as error:
            return self.unavailable(str(error))

    def query(self, *, object_name=None, variable_id=None, path='', follow=0):
        if (object_name is None) == (variable_id is None) or type(follow) is not int or not 0 <= follow <= 8:
            raise ValueError('select one object or variable; pointer depth must be in 0..8')
        try:
            if object_name is not None:
                matches = [obj for obj in self.document.get('objects', []) if obj.get('name') == object_name]
                if len(matches) != 1:
                    raise ValueError('object unknown or ambiguous')
                obj = matches[0]; address, type_id = obj['address'], obj['type']
                root = self.row(type_id)
                if (root is None or type(root.get('length')) is not int or
                        not 0 < root['length'] <= obj.get('mapped_bytes', 0)):
                    raise ValueError('object layout exceeds its mapped image extent')
            else:
                location = self.variables.locate(variable_id, self.context, self.read)
                if location.get('status') != 'available':
                    result = location
                elif location['kind'] != 'memory':
                    if path:
                        raise ValueError('member path requires an addressed variable')
                    result = self.variables.value(variable_id, self.context, self.read)
                else:
                    address, type_id = location['address'], location['type_id']
                if location.get('status') != 'available' or location.get('kind') != 'memory':
                    self._check_epoch()
                    return result
            address, type_id, bitfield = self._path(address, type_id, path)
            result = self._value(address, type_id, follow, 0, set(), [0], bitfield)
        except (OSError, ValueError) as error:
            result = self.unavailable(str(error))
        self._check_epoch()
        return dict(snapshot=self.snapshot['snapshot'], pc=self.snapshot['pc'], **result)

    def _check_epoch(self):
        current = self.transport(self.port, 'guest_snapshot')
        if current != self.snapshot:
            raise ValueError('guest snapshot changed during inspection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--types', type=Path, required=True)
    selector = parser.add_mutually_exclusive_group(required=True)
    selector.add_argument('--object', dest='object_name')
    selector.add_argument('--variable', dest='variable_id', type=int)
    parser.add_argument('--path', default='')
    parser.add_argument('--follow', type=int, default=0)
    args = parser.parse_args()
    with args.types.open('rb') as source:
        raw = source.read(TYPE_LIMIT+1)
    if len(raw) > TYPE_LIMIT:
        raise ValueError('guest type map exceeds diagnostic size limit')
    inspector = Inspector(json.loads(raw), args.port)
    print(json.dumps(inspector.query(object_name=args.object_name, variable_id=args.variable_id,
                                     path=args.path, follow=args.follow)))


if __name__ == '__main__':
    main()
