"""Authored CPU-loop XBE and matching inspection metadata; no game/SDK bytes."""
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
from native_guest_symbols import import_dia


def fixture():
    data = bytearray(0x580)
    data[:4] = b'XBEH'
    def put(offset, *values):
        struct.pack_into('<'+'I'*len(values), data, offset, *values)
    put(0x104, 0x10000, 0x400, 0x3000, 0x178)
    put(0x118, 0x10200, 2, 0x10300)
    put(0x128, 0x11000 ^ 0xA8FC57AB)
    put(0x12c, 0x12050)
    put(0x158, 0x12040 ^ 0x5B6D40B6)
    put(0x200, 0xa4)
    put(0x208, 0x12345678)
    put(0x29c, 2, 1)
    put(0x300, 6, 0x11000, 0x1000, 0x400, 0x100, 0x10380)
    put(0x338, 3, 0x12000, 0x1000, 0x500, 0x80, 0x10390)
    put(0x31c, 0x103a0, 0x103a2)
    put(0x354, 0x103a4, 0x103a6)
    data[0x380:0x386] = b'.text\0'
    data[0x390:0x396] = b'.data\0'
    data[0x180:0x190] = b'NB10'+struct.pack('<III', 0, 0x12345678, 1)
    data[0x190:0x19c] = b'fixture.pdb\0'
    code = bytes.fromhex('b82a000000 a300200100 b84d000000 e80c000000 ebea')
    data[0x400:0x500] = (code + b'\xcc'*(32-len(code)) + b'\xc3').ljust(0x100, b'\xcc')
    put(0x500, 7, 0xffffffff, 0xff81, 0x12000, 11, 22, 33, 0xfd000000, 0)
    put(0x540, 0x80000008, 0)  # Real DbgPrint import, never called by this program.
    put(0x550, 0x12070, 0x12074, 0x12074, 0, 12, 0)
    put(0x570, 345)
    identity = dict(pdb=True, signature=0x12345678, age=1, guid='00000000-0000-0000-0000-000000000000')
    def record(identifier, tag, **fields):
        return dict(dict(id=identifier, tag=tag, parent=0, name='', type=0, section=0,
                         offset=0, length=0), **fields)
    records = [identity,
        record(1, 16, basic=6, length=4, name='int'),
        record(2, 16, basic=7, length=4, name='unsigned int'),
        record(3, 16, basic=7, length=2, name='unsigned short'),
        record(4, 16, basic=6, length=2, name='short'),
        record(5, 12, type=1, length=4, name='State'),
        record(6, 7, parent=5, data_kind=9, location=10, type=5, name='Ready', constant=dict(kind='integer', value='-1')),
        record(10, 11, length=36, name='StateBlock'),
        record(11, 14, type=10, length=4),
        record(12, 15, type=2, length=12, count=3),
        record(13, 14, type=2, length=4),
        record(90, 7, name='state', type=10, data_kind=6, location=1, section=2),
        record(99, 5, name='loop', section=1, length=len(code)),
        record(100, 5, name='checkpoint', section=1, offset=32, length=1),
        record(101, 7, parent=100, name='argument', type=2, data_kind=3, location=5, register_id=17),
        record(102, 7, parent=100, name='return_address', type=13, data_kind=1,
               location=3, register_id=30006, location_offset=0),
        record(103, 7, parent=100, name='constant', type=1, data_kind=9,
               location=10, constant=dict(kind='integer', value='-7')),
        record(104, 7, parent=100, name='removed', type=2, data_kind=1, location=0, optimized_away=True),
        record(105, 7, name='thread_value', type=2, data_kind=6, location=2, section=2, offset=0x70),
        dict(tag=-1, kind='frame', section=1, offset=32, length=1, prolog_length=0,
             program='$T0 .raSearchStart = $eip $T0 ^ =')]
    for identifier, name, type_id, offset in [(20,'count',2,0), (21,'mode',5,4),
            (24,'self',11,12), (25,'items',12,16), (26,'device',13,28), (27,'empty',13,32)]:
        records.append(record(identifier, 7, parent=10, name=name, type=type_id,
                              data_kind=7, location=4, location_offset=offset, field_offset=offset))
    for identifier, name, type_id, position, width in [(22,'enabled',3,0,1),(23,'signed_bits',4,7,9)]:
        records.append(record(identifier, 7, parent=10, name=name, type=type_id, data_kind=7,
                              location=6, storage_offset=8, bit_position=position, bit_width=width))
    source_hash = hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()
    symbols, types = import_dia(bytes(data), records, source_hash)
    return bytes(data), symbols, types


def main():
    output = Path(sys.argv[1]).resolve()
    output.mkdir(parents=True, exist_ok=True)
    data, symbols, types = fixture()
    (output/'default.xbe').write_bytes(data)
    for name, document in [('symbols', symbols), ('types', types)]:
        (output/(name+'.aot-private.json')).write_text(json.dumps(document, indent=2)+'\n')
    (output/'title.toml').write_text('''schema = "xboxllr.native-title.v1"
[title]
id = "guest-inspection-fixture"
name = "Guest inspection fixture"
executable = "GuestInspectionFixture"
xbe_sha256 = "'''+symbols['xbe_sha256']+'''"
[content]
image = "default.xbe"
[build]
PILOT_RENDERER = "ORIGINAL"
PILOT_AUDIO = "ORIGINAL"
PILOT_AUDIO_DEVICE = "OFF"
PILOT_SCHEDULING = "NATIVE"
PILOT_MEMORY = "NATIVE"
PILOT_FILES = "NATIVE"
PILOT_IO_DEVICE = "OFF"
PILOT_USB = "OFF"
''')
    paths = dict(xbe=str(output/'default.xbe'), media_root=str(output),
                 cpu_donor='F:/Projects/xboxllr/refs/xboxrecomp-phobos665-review-20261006',
                 discovery_symbols=str(output/'symbols.aot-private.json'))
    (output/'paths.json').write_text(json.dumps(paths, indent=2)+'\n')
    print(json.dumps(dict(xbe_sha256=symbols['xbe_sha256'], output=str(output))))


if __name__ == '__main__':
    main()
