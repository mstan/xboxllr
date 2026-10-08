#!/usr/bin/env python3
"""Execute bounded original NTSC pacing fragments; no substituted calls."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EBX, UC_X86_REG_EBP, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP


def run(path):
    blob = path.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    assert digest == 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0, 0x4000000)
    u.mem_map(0x30000000, 0x10000)
    for section in parse_xbe(blob)['sections']:
        if section['preload']:
            start = section['raw_offset']
            u.mem_write(section['address'], blob[start:start + section['raw_size']])

    def put(address, value):
        u.mem_write(address, struct.pack('<I', value))

    def get(address):
        return struct.unpack('<I', u.mem_read(address, 4))[0]

    # Execute the original main entry's register initializers. This is not
    # execution of the intervening startup, title-state update or rendering.
    for begin, end in [(0xab4fe, 0xab500), (0xab55c, 0xab561), (0xab665, 0xab66a)]:
        u.emu_start(begin, end, count=2)
        assert u.reg_read(UC_X86_REG_EIP) == end
    assert u.reg_read(UC_X86_REG_EBX) == 0
    assert u.reg_read(UC_X86_REG_EBP) == 1
    assert u.reg_read(UC_X86_REG_EDI) == 2
    rows = []
    for state in range(256):
        put(0x50a57c, state)
        put(0x50a520, 99)
        u.reg_write(UC_X86_REG_ESP, 0x30008000)
        # Stop immediately before the original pacing-parameter call.
        u.emu_start(0xab750, 0xab785, count=80)
        assert u.reg_read(UC_X86_REG_EIP) == 0xab785
        divisor = get(0x1dbd88)
        expected = 2 if (state & 0x7f) in (4, 9, 10) else 1
        assert divisor == expected and get(0x1dbd8c) == divisor
        assert get(0x50a520) == 0 and get(0x30007ffc) == divisor
        gates = []
        for tick in range(4):
            # This original callback prefix increments the display counter.
            # Stop before its tail call; no callback remainder is fabricated.
            if tick:
                u.emu_start(0xa04f0, 0xa04fb, count=5)
                assert u.reg_read(UC_X86_REG_EIP) == 0xa04fb
            assert get(0x50a520) == tick
            u.emu_start(0xab7fc, 0xab809, count=32)
            ready = u.reg_read(UC_X86_REG_EIP) == 0xab809
            assert ready == (tick >= divisor)
            gates.append(ready)
        rows.append(dict(state=state,masked_state=state & 0x7f,display_ticks=divisor,gate_ready=gates))
    return dict(xbe_sha256=digest,cases=rows,two_tick_states=[4,9,10],
                nominal_display_hz=60,nominal_two_tick_fps=30,
                scope='Original register initializers, state-to-display-tick selection, vblank counter prefix and pre-Swap gate only. No calls replaced; full frame work and real hardware timing are outside this fixture.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(run(args.xbe), indent=2) + '\n')
    print('PASS: 256 original state/divisor cases and 1024 original pre-Swap gate cases')
