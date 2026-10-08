#!/usr/bin/env python3
"""Execute original page/interrupt dependencies over the owned kernel capture.

No replaced callees. Outputs are private original-code observations, not a USB
delivery or complete MMU qualification. Uses the same CPU engine as the native
floor; the original software supplies the expectations, not an independent CPU.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from native_io_device_reference import Reference, SNAPSHOT_SHA


def run(snapshot):
    ref = Reference(snapshot)
    direct = []
    contiguous = ref.call(165, 8192)
    assert contiguous
    # A native allocation absent from the captured PTEs must not be translated
    # as if the original kernel owned it. This caught an integration defect.
    assert ref.call(173, 0x80a1d000) == 0
    for base, size, unlock in [(contiguous, 8192, 0), (contiguous+4095, 2, 0),
                               (contiguous, 8192, 1)]:
        before = hashlib.sha256(ref.ram).hexdigest()
        ref.call(175, base, size, unlock)
        assert hashlib.sha256(ref.ram).hexdigest() == before
        assert ref.call(173, base) == (base & 0x03ffffff)
        direct.append(dict(base=hex(base), size=size, unlock=unlock, ram_unchanged=True))
    ref.call(171, contiguous)
    # Allocate actual ordinary virtual pages through the original kernel.
    pointer = ref.call(14, 8192)
    assert pointer and not 0x80000000 <= pointer < 0x90000000
    addresses = [pointer + offset for offset in (0, 4096)]
    physical = [ref.physical(address) for address in addresses]
    # PFN database location is observed from this hash-bound original kernel.
    # It is fixture metadata, never a substituted page-lock implementation.
    metadata = [0x83ff0000 + (address >> 12) * 4 for address in physical]
    def counts():
        return [ref.word(address) & 0xffff for address in metadata]
    initial = counts()
    transitions = []
    for unlock, delta in [(0, 2), (0, 4), (1, 2), (1, 0)]:
        ref.call(175, pointer + 4095, 2, unlock)
        observed = counts()
        assert observed == [(value + delta) & 0xffff for value in initial]
        transitions.append(dict(unlock=unlock, lock_words=observed))
    for address, expected in zip(addresses, physical):
        assert ref.call(173, address + 17) == expected + 17
    ref.call(17, pointer)
    # Image-style fixed VA reservation with independently observed original
    # commit/PFN locking/decommit semantics. No fabricated PTEs or callbacks.
    ref.enable_kernel_segments()
    seh = ref.word(0x8003da3c)
    slots = 0x10000200
    def vm(ordinal, base, size, kind):
        ref.cpu.mem_write(slots, struct.pack('<II', base, size))
        args = (slots, 0, slots+4, kind, 4) if ordinal == 184 else (slots, slots+4, kind)
        status = ref.call(ordinal, *args)
        assert ref.word(0x8003da3c) == seh
        return status, struct.unpack('<II', ref.cpu.mem_read(slots, 8))
    assert vm(184, 0x400000, 0x20000, 0x2000) == (0, (0x400000, 0x20000))
    assert ref.call(173, 0x410000) == 0
    image_transitions = []
    for cycle in range(2):
        assert vm(184, 0x410000, 8192, 0x1000) == (0, (0x410000, 8192))
        pages = [ref.physical(va) for va in (0x410000, 0x411000)]
        words = [0x83ff0000 + (pa >> 12)*4 for pa in pages]
        before = [ref.word(va) & 65535 for va in words]
        for unlock, delta in ((0, 2), (0, 4), (1, 2), (1, 0)):
            ref.call(175, 0x410fff, 2, unlock)
            assert [ref.word(va) & 65535 for va in words] == [v+delta for v in before]
        assert ref.map_page(ref.cpu, 0, 0x410000, 4, 0, None)
        ref.cpu.mem_write(0x410000, b'\x12\x34\x56\x78')
        assert bytes(ref.ram[pages[0]:pages[0]+4]) == b'\x12\x34\x56\x78'
        assert ref.call(173, 0x410017) == pages[0]+23
        assert vm(199, 0x410000, 8192, 0x4000) == (0, (0x410000, 8192))
        assert ref.call(173, 0x410000) == 0
        image_transitions.append(dict(cycle=cycle, pages=pages, initial_lock_words=before))
    assert vm(199, 0x400000, 0, 0x8000)[0] == 0
    interrupts = []
    for irq in (0, 1, 9, 26, 27, 0xffffffff):
        ref.cpu.mem_write(0x10000400, b'\xcd' * 4)
        vector = ref.call(44, irq, 0x10000401)
        output = bytes(ref.cpu.mem_read(0x10000400, 4))
        expected = bytes([0xcd, 26-irq, 0xcd, 0xcd]) if irq <= 26 else b'\xcd' * 4
        assert vector == (0x30 + irq if irq <= 26 else 0) and output == expected
        interrupts.append(dict(irq=irq, vector=vector, output=output.hex()))
    ref.cpu.mem_write(0x10000800, b'\xcd' * 0x78)
    ref.call(109, 0x10000804, 0x11223344, 0x55667788, 0x31, 25, 0, 0)
    body = bytes(ref.cpu.mem_read(0x10000800, 0x78))
    assert body[:4] == body[-4:] == b'\xcd' * 4
    assert struct.unpack_from('<IIII', body, 4) == (0x11223344, 0x55667788, 1, 25)
    assert body[4+0x10] == 0 and body[4+0x12] == 0
    return dict(snapshot_sha256=SNAPSHOT_SHA, direct=direct, pool_pointer=hex(pointer),
                initial_lock_words=initial, pin_transitions=transitions, image_transitions=image_transitions,
                interrupts=interrupts, interrupt_body_private=body.hex())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps(run(args.snapshot), indent=2) + '\n', encoding='utf-8')
    print('PASS: original direct/ordinary page locking, physical translation, interrupt vector and object initialization')
