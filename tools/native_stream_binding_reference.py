#!/usr/bin/env python3
"""Run original NTSC stream binding with unmapped vertex payloads.

Bounds the CPU binding contract: stream/stride, resource binding counts, fence
side effects and callee-cleaned ABI. Payload consumption belongs to later draw
execution; this fixture neither draws nor substitutes original instructions.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EBX,
                              UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)


def run(xbe, binding):
    blob = xbe.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    assert digest == 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    b = json.loads(binding.read_text())
    assert b['xbe_sha256'] == digest
    entry = next(int(c['address'], 16) for c in b['calls']
                 if c['name'] == 'D3DDevice_SetStreamSource')
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0, 0x4000000)
    u.mem_map(0x70000000, 0x10000)
    for section in parse_xbe(blob)['sections']:
        if section['preload']:
            u.mem_write(section['address'], blob[section['raw_offset']:
                section['raw_offset'] + section['raw_size']])

    def put(address, *values):
        u.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def get(address):
        return struct.unpack('<I', u.mem_read(address, 4))[0]

    device = b['device_object']
    streams = b['stateblock']['streams']
    fence = 0x12345678
    put(b['device_global'], device)
    put(device + b['resource_binding']['fence'], fence)
    u.mem_write(streams, bytes(16 * 12))
    headers = [0x70003000, 0x70003020]
    payloads = [0x50000000, 0x60000000]  # Deliberately unmapped: any access fails.
    for header, payload in zip(headers, payloads):
        put(header, 0x01000001, payload, 0)
    stack, end = 0x70008000, 0x70000000
    saved = [(UC_X86_REG_EBX, 111), (UC_X86_REG_ESI, 222),
             (UC_X86_REG_EDI, 333), (UC_X86_REG_EBP, 444)]
    cases = []
    for stream in range(16):
        old = 0
        for header, stride in [(headers[0], 20), (headers[0], 0),
                               (headers[1], 64), (headers[1], 64), (0, 0)]:
            put(stack, end, stream, header, stride)
            u.reg_write(UC_X86_REG_ESP, stack)
            for reg, value in saved:
                u.reg_write(reg, value)
            u.emu_start(entry, end, count=10000)
            assert u.reg_read(UC_X86_REG_EIP) == end
            assert u.reg_read(UC_X86_REG_ESP) == stack + 16
            assert all(u.reg_read(reg) == value for reg, value in saved)
            assert get(streams + stream * 12) == stride
            assert get(streams + stream * 12 + 8) == header
            if old:
                assert get(old + 8) == fence
            for resource, payload in zip(headers, payloads):
                assert get(resource) == 0x01000001 + (0x80000 if resource == header else 0)
                assert get(resource + 4) == payload
            cases.append(dict(stream=stream, header=header, stride=stride))
            old = header
    return dict(xbe_sha256=digest, cases=cases, payloads_unmapped=payloads,
                scope=__doc__.strip())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'binding', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    result = run(args.xbe, args.binding)
    with args.output.open('x') as out:
        out.write(json.dumps(result, indent=2) + '\n')
    print(f"PASS: {len(result['cases'])} original stream bindings, ABI and ownership; no payload access")
