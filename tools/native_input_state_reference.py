#!/usr/bin/env python3
"""Observe original XAPI report/state bytes with real original kernel callees.

This bounded software oracle uses authored handle/report inputs, not a fake USB
transport. The maintained OHCI route and actual title runs qualify transport.
Owned binaries and output observations remain private. Unicorn is shared with
the current LLE bridge, so this is not an independent CPU implementation.
"""
import argparse
import json
from pathlib import Path
import struct
from aot_inventory import parse_xbe
from native_input_binding import IMAGE
from native_io_device_reference import Reference, SNAPSHOT_SHA
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP,
                               UC_X86_REG_EIP, UC_X86_REG_EFLAGS)


def run(xbe, snapshot):
    blob = xbe.read_bytes()
    image = parse_xbe(blob)
    if image['sha256'] != IMAGE:
        raise ValueError('unreviewed XAPI image')
    ref = Reference(snapshot)
    ref.enable_kernel_segments()
    cpu = ref.cpu
    base = image['base']
    cpu.mem_map(base, (image['image_size'] + 4095) & ~4095)
    cpu.mem_write(base, blob[:struct.unpack_from('<I', blob, 0x108)[0]])
    for section in image['sections']:
        offset, size = section['raw_offset'], section['raw_size']
        cpu.mem_write(section['address'], blob[offset:offset+size])
    # Loader relocation to real original exports, never replacement functions.
    thunk = struct.unpack_from('<I', blob, 0x158)[0] ^ 0x5b6d40b6
    imports = 0
    while True:
        ordinal = struct.unpack('<I', cpu.mem_read(thunk, 4))[0]
        if not ordinal:
            break
        if ordinal & 0xffff0000 != 0x80000000:
            raise ValueError('unexpected original import encoding')
        target = ref.base + ref.word(ref.export_table + 4*((ordinal & 0xffff)-ref.ordinal_base))
        cpu.mem_write(thunk, struct.pack('<I', target))
        thunk += 4
        imports += 1
        if imports > 512:
            raise ValueError('unterminated import table')

    def put(va, value):
        cpu.mem_write(va, struct.pack('<I', value))

    def call(entry, *args, ecx=0):
        sp, end = 0x10008000, 0x10009000
        cpu.mem_write(sp, struct.pack('<'+'I'*(len(args)+1), end, *args))
        cpu.reg_write(UC_X86_REG_ESP, sp)
        cpu.reg_write(UC_X86_REG_ECX, ecx)
        cpu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        ref.failure = None
        cpu.emu_start(entry, end, count=1000000)
        assert not ref.failure, ref.failure
        assert cpu.reg_read(UC_X86_REG_EIP) == end, 'original code did not return'
        assert cpu.reg_read(UC_X86_REG_ESP) == sp+4*(len(args)+1)
        assert ref.word(0x8003da60) & 255 == 0
        return cpu.reg_read(UC_X86_REG_EAX)

    handle, config, device, length, out = (0x10002000, 0x10002200,
                                          0x10002300, 0x10002400, 0x10003000)
    put(handle, device)
    put(handle+0xa3, config)
    put(config+8, length)
    cpu.mem_write(length, bytes([18]))
    state = struct.pack('<IH8Bhhhh', 42, 0x10, 0, 31, 32, 255, 9, 0, 17, 241,
                        -32768, 32767, -1234, 4321)
    put(handle+8, 42)
    cpu.mem_write(handle+0x14, state[4:])
    rows = []
    for name, connected, flags, expected in [
            ('connected', device, 0, 0), ('removed', device, 2, 1167),
            ('detached', 0, 0, 1167)]:
        put(handle, connected)
        cpu.mem_write(device+4, bytes([flags]))
        cpu.mem_write(out-1, b'\xcc'*24)
        status = call(0x183b28, handle, out)
        actual = bytes(cpu.mem_read(out-1, 24))
        assert status == expected and actual == b'\xcc'+state+b'\xcc'
        rows.append(dict(case=name, status=status, state=actual[1:-1].hex()))
    cpu.mem_write(config+0x28, b'\x10')
    cpu.mem_write(out, b'\xcc'*22)
    assert call(0x183b28, handle, out) == 87
    assert bytes(cpu.mem_read(out, 22)) == b'\xcc'*22
    # Low analog values are filtered in the original report completion body.
    # This case has no active buttons and does not call the inactivity timer.
    analog = bytes([0, 1, 17, 31, 31, 17, 1, 0])
    report = bytes(2)+analog+struct.pack('<hhhh', -32768, 32767, -1234, 4321)
    cpu.mem_write(handle+0x34, report)
    put(handle+0x66, 20)
    call(0x186559, ecx=handle)
    filtered = bytes(cpu.mem_read(handle+0x14, 18))
    assert filtered == bytes(10)+report[10:]
    # Execute the real completion wrapper, with authored successful URB input
    # and the real report callback. Manual submission prevents a new USB
    # request here; device transport is covered by the OHCI integration route.
    table, urb = 0x10002500, 0x10002600
    put(handle, device)
    cpu.mem_write(device+4, b'\0')
    put(device+0xe, table)
    put(table+0x24, 0x186559)
    put(handle+8, 0)
    packets = []
    for expected in (1, 2, 3):
        call(0x186403, urb, handle)
        packet = struct.unpack('<I', cpu.mem_read(handle+8, 4))[0]
        assert packet == expected
        packets.append(packet)
    return dict(xbe_sha256=IMAGE, snapshot_sha256=SNAPSHOT_SHA,
                imports=imports, states=rows, invalid_type_preserves_output=True,
                identical_report_packet_numbers=packets,
                inactive_analog_filter=dict(input=report.hex(), output=filtered.hex()),
                limits='Authored handle/report states; transport, full initialization and active-button timer are separate qualification')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'snapshot', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    args = p.parse_args()
    result = run(args.xbe, args.snapshot)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result))
