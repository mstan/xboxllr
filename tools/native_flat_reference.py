#!/usr/bin/env python3
"""Bounded original NTSC XDK provoking-vertex packet; private input only.

Executes the actual initialization basic block up to the next helper call.
It does not claim to execute full CreateDevice or rasterize original hardware.
NV2A software pixel tests and native GPU tests supply the raster contract.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESI

def run(path):
    blob = path.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    assert digest == 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x10000, 0x1000000)
    for section in parse_xbe(blob)['sections']:
        if section['raw_size'] and section['address'] + section['raw_size'] <= 0x1010000:
            u.mem_write(section['address'], blob[section['raw_offset']:section['raw_offset']+section['raw_size']])
    u.mem_map(0x30000000, 0x10000)
    u.reg_write(UC_X86_REG_EAX, 0x30001000)
    u.reg_write(UC_X86_REG_ESI, 0x30000000)
    u.reg_write(UC_X86_REG_EDI, 0)
    u.reg_write(UC_X86_REG_ECX, 0xcccccccc)
    u.emu_start(0x1394e0, 0x139535, count=100)
    assert u.reg_read(UC_X86_REG_EIP) == 0x139535
    packet = struct.unpack('<15I', u.mem_read(0x30001000, 60))
    assert packet[:2] == (0x409fc, 1)
    return dict(xbe_sha256=digest, block_start='0x1394e0', block_end='0x139535',
                method='0x09fc', provoking='first', packet=list(packet),
                scope='original initialization packet basic block, not full device initialization')

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--xbe', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    a.output.write_text(json.dumps(run(a.xbe), indent=2)+'\n')
    print('PASS: original NTSC XDK initialization selects FIRST provoking vertex')
