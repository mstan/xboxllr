"""Build-bound guest names for diagnostics, using locally supplied DIA metadata.

The source PDB must match a CodeView identity retained in the XBE. Names never
authorize CPU/service replacements or become implicit discovery seeds. Runtime
lookup consumes portable JSON; the original PDB and DIA SDK stay local.
"""
from __future__ import annotations
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import uuid

from aot_inventory import parse_xbe

SCHEMA = 'xboxllr.guest-symbols.v1'
LIMIT = 32 * 1024 * 1024
TYPE_LIMIT = 128 * 1024 * 1024


def _matches_image(document, expected):
    actual = document.get('xbe_sha256')
    return (isinstance(actual, str) and isinstance(expected, str) and
            re.fullmatch(r'[0-9a-fA-F]{64}', expected) is not None and
            actual.lower() == expected.lower())


def codeview_identities(data):
    identities = set()
    for magic, size in ((b'NB10', 16), (b'RSDS', 24)):
        start = 0
        while (offset := data.find(magic, start)) >= 0:
            start = offset + 4
            if offset + size >= len(data):
                continue
            tail = data[offset+size:offset+size+1024]
            if b'\0' not in tail:
                continue
            path = tail.split(b'\0', 1)[0]
            if not path.lower().endswith(b'.pdb') or any(c < 32 for c in path):
                continue
            if magic == b'NB10':
                signature, age = struct.unpack_from('<II', data, offset+8)
                identities.add(('NB10', signature, age))
            else:
                age = struct.unpack_from('<I', data, offset+20)[0]
                identities.add(('RSDS', data[offset+4:offset+20].hex(), age))
    return identities


def tls_template(data, image):
    """The XBE TLS template and zero-fill extent, never a host TLS address."""
    directory = struct.unpack_from('<I',data,0x12c)[0]
    if not directory:
        return None
    def offset(address, size):
        if image['base']<=address and address+size<=image['base']+struct.unpack_from('<I',data,0x108)[0]:
            return address-image['base']
        for section in image['sections']:
            if section['address']<=address and address+size<=section['address']+section['raw_size']:
                return section['raw_offset']+address-section['address']
        raise ValueError('TLS directory/template lacks file-backed bytes')
    start,end,_,_,zero_fill,_ = struct.unpack_from('<6I',data,offset(directory,24))
    if end<start or end-start+zero_fill>64*1024*1024 or end+zero_fill>0x100000000:
        raise ValueError('invalid guest TLS template extent')
    if end>start:
        offset(start,end-start)
    return dict(start=start,size=end-start+zero_fill,initialized_bytes=end-start)


def import_dia(data, records, source_sha256):
    image = parse_xbe(data)
    records = iter(records)
    identity = next(records)
    if identity.get('pdb') is not True:
        raise ValueError('expected DIA metadata identity')
    possible = {('NB10', identity['signature'], identity['age']),
                ('RSDS', uuid.UUID(identity['guid']).bytes_le.hex(), identity['age'])}
    stored = codeview_identities(data)
    if len(stored) != 1 or not stored.intersection(possible):
        raise ValueError('PDB CodeView identity does not match this XBE uniquely')
    functions, types, objects, scopes, variables, frames = {}, {}, [], {}, {}, []
    compilands, compilers, function_owners, function_addresses = {}, {}, {}, {}
    def section_address(section_number, offset, length=0):
        if type(section_number) is not int or not 1 <= section_number <= len(image['sections']):
            return None
        section = image['sections'][section_number-1]
        if offset < 0 or length < 0 or offset+length > section['size']:
            return None
        return section['address']+offset
    rejected = 0
    for record in records:
        tag = record['tag']
        if record.get('kind') == 'compiland_function':
            function_owners.setdefault(record['id'],set()).add(record['parent'])
        if tag == 2:
            compilands[str(record['id'])] = dict(name=record['name'])
        if tag == 3:
            compilers.setdefault(str(record['parent']), []).append({
                key: record[key] for key in ('compiler_name','platform','language',
                    'frontend_major','frontend_minor','frontend_build','frontend_qfe',
                    'backend_major','backend_minor','backend_build','backend_qfe') if key in record})
        if record.get('kind')=='frame':
            frame = {key:record[key] for key in ('length','prolog_length','locals_length',
                     'params_length','saved_registers_length','frame_type','program') if key in record}
            if 'section' in record and 'offset' in record and 'length' in record:
                address = section_address(record['section'],record['offset'],record['length'])
                if address is not None:
                    frame.update(address=address,end=address+record['length'])
            frames.append(frame)
        if tag in (5, 6, 21, 22):
            scope = {key:record[key] for key in ('id','parent','tag','name','length',
                     'frame_pointer_present','local_base_register','parameter_base_register') if key in record}
            address = section_address(record['section'],record['offset'],record['length'])
            if address is not None:
                scope.update(address=address,end=address+record['length'])
            scopes[str(record['id'])] = scope
        if tag == 7:
            variable = {key:record[key] for key in ('id','parent','name','type','data_kind','location',
                        'register_id','location_offset','constant','optimized_away','live_range_count',
                        'control_flow_dependent') if key in record}
            if record.get('location') in (1,2):
                address = section_address(record['section'],record['offset'])
                if address is not None:
                    variable['address'] = address
            if all(key in record for key in ('live_section','live_offset','live_length')):
                address = section_address(record['live_section'],record['live_offset'],record['live_length'])
                if address is not None:
                    variable['live_start'], variable['live_end'] = address,address+record['live_length']
            variables[str(record['id'])] = variable
        if tag in (5, 27) or (tag == 7 and record['section']):
            index, offset, size = record['section']-1, record['offset'], record['length']
            if not 0 <= index < len(image['sections']):
                rejected += 1
                continue
            section = image['sections'][index]
            limit = min(section['size'], section['raw_size']+15) if tag in (5, 27) else section['size']
            if (offset < 0 or size < 0 or offset + max(1, size) > limit or
                    (tag in (5, 27) and offset >= section['raw_size'])):
                rejected += 1
                continue
            address = section['address'] + offset
            name = record.get('undecorated') or record['name']
            if not isinstance(name, str) or not name or len(name) > 8192:
                continue
            if tag in (5, 27):
                if not size or not section['executable_candidate']:
                    rejected += 1
                    continue
                function = functions.setdefault(address, dict(address=address, size=size, names=[]))
                function['extent_kind'] = 'pdb-length'
                function['size'] = max(function['size'], size)
                if name not in function['names']:
                    function['names'].append(name)
                if tag == 5:
                    function_addresses[record['id']] = address
                    if record.get('lexical_parent'):
                        function_owners.setdefault(record['id'],set()).add(record['lexical_parent'])
            else:
                if record.get('location') == 1:  # LocIsStatic, including zero-filled BSS.
                    objects.append(dict(address=address, name=name, type=record['type'],
                                        mapped_bytes=section['size']-offset))
        if (tag in (7, 11, 12, 13, 14, 15, 16, 17, 18, 20) and not record['section'] and
                (tag!=7 or record.get('data_kind') not in (1,2,3,4))):
            types[str(record['id'])] = {key: record[key] for key in
                ('id', 'parent', 'tag', 'name', 'type', 'length', 'field_offset',
                 'data_kind', 'location', 'basic', 'count', 'calling', 'dia_packed', 'unmodified_type',
                 'constant', 'bit_position', 'bit_width', 'storage_offset','location_offset') if key in record}
    common = dict(xbe_sha256=image['sha256'], source_sha256=source_sha256)
    for function_id,address in function_addresses.items():
        owners = {owner for owner in function_owners.get(function_id,()) if str(owner) in compilands}
        if owners:
            function = functions[address]
            function['compiland_ids'] = sorted(set(function.get('compiland_ids',())) | owners)
    symbols = dict(schema=SCHEMA, **common, identity='matching-codeview',
                   functions=sorted(functions.values(), key=lambda f: f['address']),
                   rejected_unbacked_records=rejected,
                   compilands=compilands, compilers=compilers)
    for marker in scopes.values():
        if marker['tag'] in (21,22) and 'address' in marker:
            owner = scopes.get(str(marker.get('parent')))
            if owner is not None:
                owner['debug_start' if marker['tag']==21 else 'debug_end'] = marker['address']
    layouts = dict(schema='xboxllr.guest-types.v1', **common, types=types, objects=objects,
                   scopes=scopes, variables=variables, frames=frames,tls_template=tls_template(data,image))
    return symbols, layouts


def import_map(data, text, source_sha256):
    """Bind named entries only; a MAP function flag does not supply its size."""
    image = parse_xbe(data)
    contributions, records = [], []
    entry = re.search(r'entry point at\s+([0-9a-f]{4}):([0-9a-f]{8})', text, re.I)
    if not entry:
        raise ValueError('MAP has no entry-point coordinates')
    segment, offset = int(entry[1], 16), int(entry[2], 16)
    if (not 1 <= segment <= len(image['sections']) or
        image['sections'][segment-1]['address']+offset != image['entry']):
        raise ValueError('MAP entry point does not match this XBE')
    for line in text.splitlines():
        match = re.match(r'^\s*([0-9a-f]{4}):([0-9a-f]{8})\s+([0-9a-f]{8})H\s+(\S+)\s+(CODE|DATA)', line, re.I)
        if match:
            contributions.append(dict(segment=int(match[1], 16), offset=int(match[2], 16),
                                      size=int(match[3], 16), name=match[4]))
        match = re.match(r'^\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{8})\s+(.+)$', line, re.I)
        if match and 'f' in match[5].split()[:2]:
            records.append(dict(segment=int(match[1], 16), offset=int(match[2], 16),
                                name=match[3], pe_address=int(match[4], 16)))
    functions, rejected = {}, 0
    for segment in sorted({r['segment'] for r in records}):
        parts = [p for p in contributions if p['segment'] == segment]
        if not parts or not 1 <= segment <= len(image['sections']):
            raise ValueError('MAP function segment is absent from this XBE')
        section = image['sections'][segment-1]
        size = max(p['offset']+p['size'] for p in parts)
        names = {p['name'].split('$', 1)[0].lstrip('.') for p in parts}
        if (section['name'].lstrip('.') not in names | {n[:8] for n in names} or
            not size <= section['size'] <= (size+15)//16*16):
            raise ValueError('MAP section layout does not match this XBE')
        rows = [r for r in records if r['segment'] == segment and r['offset'] < size]
        rejected += sum(r['segment'] == segment and r['offset'] >= size for r in records)
        if len({r['pe_address']-r['offset'] for r in rows}) > 1:
            raise ValueError('MAP contains inconsistent virtual-address coordinates')
        for row in rows:
            if not section['executable_candidate'] or row['offset'] >= section['raw_size']:
                raise ValueError('MAP function entry has no executable file-backed bytes')
            address = section['address']+row['offset']
            item = functions.setdefault(address, dict(address=address, size=1,
                                        extent_kind='entry-only', names=[]))
            if row['name'] not in item['names']:
                item['names'].append(row['name'])
    if not functions:
        raise ValueError('MAP contains no backed function entries')
    return dict(schema=SCHEMA, xbe_sha256=image['sha256'], source_sha256=source_sha256,
                identity='entry-and-section-layout', functions=sorted(functions.values(), key=lambda f: f['address']),
                rejected_unbacked_records=rejected)


class Symbols:
    def __init__(self, data, expected_xbe_sha256):
        if data.get('schema') != SCHEMA or not _matches_image(data, expected_xbe_sha256):
            raise ValueError('guest symbol map belongs to a different XBE or schema')
        functions = data.get('functions')
        if not isinstance(functions, list) or len(functions) > 500000:
            raise ValueError('invalid guest symbol function table')
        previous = -1
        for row in functions:
            address, size, names = row['address'], row['size'], row['names']
            if (type(address) is not int or type(size) is not int or
                not previous < address < address + size <= 0x100000000 or
                not isinstance(names, list) or not names or len(names) > 500000 or
                any(not isinstance(n, str) or not n or len(n) > 8192 for n in names)):
                raise ValueError('invalid guest symbol extent or names')
            previous = address
        self.functions = functions
        self.starts = [f['address'] for f in functions]
        high = 0
        self.prefix_ends = []
        for row in functions:
            high = max(high, row['address']+row['size'])
            self.prefix_ends.append(high)

    @classmethod
    def load(cls, path, expected_xbe_sha256):
        path = Path(path)
        with path.open('rb') as source:
            raw = source.read(LIMIT + 1)
        if len(raw) > LIMIT:
            raise ValueError('guest symbol map exceeds diagnostic size limit')
        return cls(json.loads(raw), expected_xbe_sha256)

    def lookup(self, address):
        index = bisect.bisect_right(self.starts, address)-1
        while index >= 0 and self.prefix_ends[index] > address:
            row = self.functions[index]
            if address < row['address']+row['size']:
                return dict(address=address, function_address=row['address'],
                            offset=address-row['address'], names=list(row['names']),
                            extent_kind=row.get('extent_kind', 'declared'))
            index -= 1
        return None

    def annotate_log(self, text):
        observations = []
        for line in text.splitlines():
            addresses = []
            if line.startswith('[PILOT STOP]'):
                match = re.search(r' at ([0-9a-fA-F]{8})(?:;|\s|$)', line)
                if match:
                    addresses.append(int(match[1], 16))
            elif line.startswith(('[NATIVE STOP RECENT]', '[NATIVE EXCEPTION RECENT]')):
                addresses = [int(v, 16) for v in re.findall(r'\b[0-9a-fA-F]{8}\b', line)]
            for address in addresses:
                symbol = self.lookup(address)
                if symbol is not None:
                    observations.append(symbol)
        return observations


def describe_layout(document, expected_xbe_sha256, *, type_name=None, type_id=None,
                    object_name=None):
    """Return exact local type/global matches without interpreting guest memory."""
    if (document.get('schema') != 'xboxllr.guest-types.v1' or
        not _matches_image(document, expected_xbe_sha256)):
        raise ValueError('guest type map belongs to a different XBE or schema')
    if sum(value is not None for value in (type_name, type_id, object_name)) != 1:
        raise ValueError('select exactly one type name, type ID or object name')
    types, objects = document['types'], document['objects']
    if not isinstance(types, dict) or not isinstance(objects, list):
        raise ValueError('invalid guest type or object table')
    def info(identifier):
        return types.get(str(identifier))
    if object_name is not None:
        return [dict(obj, type_info=info(obj['type'])) for obj in objects
                if obj['name'] == object_name]
    matches = ([info(type_id)] if type_id is not None else
               [row for row in types.values() if row.get('tag') in (11, 12, 17)
                and row['name'] == type_name])
    owners = {row['id']: [] for row in matches if row is not None}
    for member in types.values():
        if member.get('parent') in owners:
            owners[member['parent']].append(member)
    results = []
    for row in matches:
        if row is None:
            continue
        members = []
        for member in owners[row['id']]:
            # A bitfield, constant, static member or virtual base cannot be
            # treated as a plain byte offset from an instance's address.
            offset = (member.get('field_offset') if member.get('tag') == 7 and
                      member.get('location') == 4 else None)  # LocIsThisRel
            members.append(dict(member, byte_offset=offset, type_info=info(member.get('type'))))
        results.append(dict(type=row, target_type=info(row.get('type')), members=members))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    build = sub.add_parser('import-dia')
    build.add_argument('--xbe', type=Path, required=True)
    build.add_argument('--metadata', type=Path, required=True)
    build.add_argument('--output', type=Path, required=True)
    build.add_argument('--types-output', type=Path)
    map_parser = sub.add_parser('import-map')
    map_parser.add_argument('--xbe', type=Path, required=True)
    map_parser.add_argument('--metadata', type=Path, required=True)
    map_parser.add_argument('--output', type=Path, required=True)
    lookup = sub.add_parser('lookup')
    lookup.add_argument('--symbols', type=Path, required=True)
    lookup.add_argument('--xbe-sha256', required=True)
    lookup.add_argument('--address', type=lambda v: int(v, 0))
    lookup.add_argument('--log', type=Path)
    describe = sub.add_parser('describe', help='Look up an exact private type or static global')
    describe.add_argument('--types', type=Path, required=True)
    describe.add_argument('--xbe-sha256', required=True)
    query = describe.add_mutually_exclusive_group(required=True)
    query.add_argument('--type-name')
    query.add_argument('--type-id', type=lambda value: int(value, 0))
    query.add_argument('--object-name')
    args = parser.parse_args()
    if args.action in ('import-dia', 'import-map'):
        targets = [p.resolve() for p in (args.output, getattr(args, 'types_output', None)) if p is not None]
        if (len(set(targets)) != len(targets) or any(not p.name.endswith('.aot-private.json') or
            p in (args.xbe.resolve(), args.metadata.resolve()) for p in targets)):
            parser.error('use distinct .aot-private.json outputs, separate from inputs')
        with args.metadata.open('rb') as source:
            metadata_sha256 = hashlib.file_digest(source, 'sha256').hexdigest()
        if args.action == 'import-dia':
            with args.metadata.open(encoding='utf-8') as source:
                symbols, types = import_dia(args.xbe.read_bytes(), (json.loads(line) for line in source), metadata_sha256)
        else:
            symbols = import_map(args.xbe.read_bytes(), args.metadata.read_text(encoding='utf-8', errors='replace'), metadata_sha256)
            types = dict(types={}, objects=[])
        args.output.write_text(json.dumps(symbols, separators=(',', ':'))+'\n', encoding='utf-8', newline='\n')
        if getattr(args, 'types_output', None):
            args.types_output.write_text(json.dumps(types, separators=(',', ':'))+'\n', encoding='utf-8', newline='\n')
        print(json.dumps(dict(functions=len(symbols['functions']), type_records=len(types['types']),
                             objects=len(types['objects']), xbe_sha256=symbols['xbe_sha256'])))
    elif args.action == 'describe':
        with args.types.open('rb') as source:
            raw = source.read(TYPE_LIMIT + 1)
        if len(raw) > TYPE_LIMIT:
            raise ValueError('guest type map exceeds diagnostic size limit')
        print(json.dumps(describe_layout(json.loads(raw), args.xbe_sha256,
              type_name=args.type_name, type_id=args.type_id, object_name=args.object_name)))
    else:
        symbols = Symbols.load(args.symbols, args.xbe_sha256)
        if (args.address is None) == (args.log is None):
            parser.error('select exactly one address or log')
        print(json.dumps(symbols.lookup(args.address) if args.address is not None else
                         symbols.annotate_log(args.log.read_text(encoding='utf-8', errors='replace'))))


if __name__ == '__main__':
    main()
