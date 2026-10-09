#!/usr/bin/env python3
"""Observe original Xbox VM services over the owned initialized kernel.

No replaced callees or host-native allocation oracle. The shared CPU transport
does not enforce x86 PTE permissions; protection observations below are original
query/PTE effects, not a claim of independently qualified CPU page faults.
"""
import argparse
import json
import struct
from pathlib import Path
from native_io_device_reference import Reference, SNAPSHOT_SHA


def run(snapshot):
    ref = Reference(snapshot)
    ref.enable_kernel_segments()
    slots = 0x10000200
    rows = []

    def vm(label, ordinal, base, size, kind=0, protect=4, zero_bits=0):
        ref.cpu.mem_write(slots, struct.pack('<III', base, size, 0xdeadbeef))
        if ordinal == 184:
            args = (slots, zero_bits, slots+4, kind, protect)
        elif ordinal == 199:
            args = (slots, slots+4, kind)
        else:
            args = (slots, slots+4, protect, slots+8)
        status = ref.call(ordinal, *args)
        observed = struct.unpack('<III', ref.cpu.mem_read(slots, 12))
        rows.append(dict(case=label, ordinal=ordinal, input=dict(base=base,
                         size=size, kind=kind, protect=protect, zero_bits=zero_bits),
                         status=status, output=list(observed)))
        return status, observed

    def query(label, address):
        ref.cpu.mem_write(slots, b'\xcd'*36)
        status = ref.call(217, address, slots+4)
        data = bytes(ref.cpu.mem_read(slots, 36))
        assert data[:4] == data[-4:] == b'\xcd'*4, 'query output bounds'
        result = list(struct.unpack('<7I', data[4:-4]))
        rows.append(dict(case=label, ordinal=217, address=address,
                         status=status, output=result))
        return status, result

    base = 0x400000
    assert vm('reserve', 184, base, 0x3000, 0x2000)[0] == 0
    query('reserved', base)
    assert ref.call(173, base) == 0
    assert vm('unaligned_commit', 184, base+4095, 2, 0x1000)[0] == 0
    query('committed', base+1)
    query('reserved_tail', base+8192)
    pages = [ref.physical(base+offset) for offset in (0, 4096)]
    assert pages[0] != pages[1]
    for offset, value in ((0, b'\x12\x34\x56\x78'), (4096, b'\x90\xab\xcd\xef')):
        if base+offset not in ref.pages:
            assert ref.map_page(ref.cpu, 0, base+offset, 4, 0, None)
        ref.cpu.mem_write(base+offset, value)
        assert bytes(ref.ram[ref.physical(base+offset):ref.physical(base+offset)+4]) == value
    assert vm('readonly_cross_page', 204, base+4095, 2, protect=2)[0] == 0
    query('readonly', base)
    assert vm('restore_first_page', 204, base+1, 4095, protect=4)[0] == 0
    query('first_page_rw', base)
    query('second_page_ro', base+4096)
    vm('invalid_protection', 204, base, 4096, protect=3)
    query('unchanged_after_invalid', base)
    assert vm('decommit_first', 199, base, 4096, 0x4000)[0] == 0
    assert ref.call(173, base) == 0
    query('decommitted_first', base)
    assert vm('recommit_first', 184, base, 4096, 0x1000)[0] == 0
    if base not in ref.pages:
        assert ref.map_page(ref.cpu, 0, base, 4096, 0, None)
    assert bytes(ref.cpu.mem_read(base, 4096)) == bytes(4096), 'real recommit zeroing'
    assert bytes(ref.cpu.mem_read(base+4096, 4)) == b'\x90\xab\xcd\xef'
    # Xbox accepts a sized release and splits the reservation. Do not import
    # the host Windows MEM_RELEASE requirement that RegionSize be zero.
    assert vm('release_prefix', 199, base, 4096, 0x8000)[0] == 0
    query('released_prefix', base)
    query('retained_suffix', base+4096)
    assert vm('release_remaining', 199, base+4096, 0, 0x8000)[0] == 0
    assert vm('release_again', 199, base, 0, 0x8000)[0] == 0xc00000a0
    assert ref.call(173, base) == 0
    query('released', base)
    assert vm('reserve_split_case', 184, base, 0x5000, 0x3000)[0] == 0
    vm('release_middle', 199, base+4097, 2, 0x8000)
    query('split_left', base)
    query('split_hole', base+4096)
    query('split_right', base+8192)
    assert vm('release_past_region', 199, base+8192, 4*4096, 0x8000)[0] == 0xc000001a
    assert vm('zero_release_interior', 199, base+3*4096, 0, 0x8000)[0] == 0xc000009f
    assert vm('noaccess_left', 204, base, 4096, protect=1)[0] == 0
    assert query('noaccess_left_query', base)[1][5] == 1
    assert vm('release_left_unaligned', 199, base+1, 0, 0x8000)[:2] == (0, (base, 4096, 0xdeadbeef))
    vm('release_right', 199, base+8192, 0, 0x8000)
    vm('free_unallocated', 199, base, 4096, 0x4000)
    vm('zero_length', 184, 0, 0, 0x3000)
    vm('bad_zero_bits', 184, 0, 4096, 0x3000, zero_bits=22)
    assert vm('topdown_reserve', 184, 0, 4096, 0x102000)[1][0] == 0x7ffd0000
    vm('topdown_release', 199, 0x7ffd0000, 0, 0x8000)
    assert query('above_user_space', 0x7ffe0000)[0] == 0xc000000d

    def statistics(label):
        ref.cpu.mem_write(slots, struct.pack('<11I', 0xdeadbeef, 36, *([0]*8), 0xaabbccdd))
        assert ref.call(181, slots+4) == 0
        values = struct.unpack('<11I', ref.cpu.mem_read(slots, 44))
        assert values[0] == 0xdeadbeef and values[-1] == 0xaabbccdd
        assert values[1:3] == (36, 16384)
        rows.append(dict(case=label, ordinal=181, status=0, output=list(values[1:-1])))
        return values[1:-1]

    baseline = statistics('statistics_baseline')
    assert vm('statistics_reserve', 184, base, 65536, 0x2000)[0] == 0
    reserved = statistics('statistics_reserved')
    assert reserved[2:4] == baseline[2:4] and reserved[4] == baseline[4]+65536
    assert vm('statistics_commit', 184, base, 8192, 0x1000)[0] == 0
    committed = statistics('statistics_committed')
    # This PDE was empty: two caller pages plus one real page-table page.
    assert committed[2] == baseline[2]-3 and committed[3] == baseline[3]+8192
    assert committed[4] == baseline[4]+65536
    assert vm('statistics_release', 199, base, 0, 0x8000)[0] == 0
    assert statistics('statistics_released') == baseline

    physical = ref.call(166, 8193, 0, 0x03ffffff, 0x10000, 0x404)
    assert physical and not physical & 0xffff
    size = ref.call(180, physical)
    assert size == 12288 and ref.call(179, physical) == 0x404
    ref.call(182, physical+4095, 2, 0x202)
    assert ref.call(179, physical) == ref.call(179, physical+4096) == 0x202
    assert ref.call(179, physical+8192) == 0x404
    ref.call(178, physical, 12288, 1)
    assert ref.call(180, physical) == size and ref.call(179, physical) == 0x202
    assert ref.call(1) == 0
    assert ref.call(4, physical) == physical and ref.call(1) == physical
    assert ref.call(4, 0) == 0 and ref.call(1) == 0
    ref.call(178, physical, 12288, 0)
    ref.call(171, physical)
    rows.append(dict(case='contiguous_queries', ordinal=[166, 179, 180, 182, 178, 1, 4, 171],
                     allocation=physical, bytes=size, first_protect=0x202, last_protect=0x404))
    pool = ref.call(15, 180, 0x74736554)
    assert pool and ref.call(23, pool) == 184
    ref.call(17, pool)
    rows.append(dict(case='pool_query', ordinal=[15, 23, 17], requested=180, usable=184))
    assert vm('relocation_allocate', 184, base, 8192, 0x3000)[0] == 0
    old_pa = ref.physical(base)
    ref.ram[old_pa:old_pa+4096] = bytes([0x5a])*4096
    # Constrain a contiguous request to a PFN occupied by a movable VM page.
    # The original allocator copies the live bytes and changes the real PTE.
    contiguous = ref.call(166, 4096, old_pa, old_pa+4095, 4096, 4)
    new_pa = ref.physical(base)
    assert contiguous == 0x80000000+old_pa and new_pa != old_pa
    assert bytes(ref.ram[new_pa:new_pa+4096]) == bytes([0x5a])*4096
    ref.call(175, base, 4096, 0)
    assert ref.call(166, 4096, new_pa, new_pa+4095, 4096, 4) == 0
    assert ref.physical(base) == new_pa
    ref.call(175, base, 4096, 1)
    ref.call(171, contiguous)
    rows.append(dict(case='contiguous_relocates_unpinned_vm', ordinal=[166, 175, 171],
                     old_pa=old_pa, new_pa=new_pa, pinned_allocation_result=0))
    assert vm('relocation_release', 199, base, 0, 0x8000)[0] == 0
    return dict(snapshot_sha256=SNAPSHOT_SHA, scope='original VM, pool/contiguous queries, statistics and relocation effects',
                rows=rows, initial_physical_pages=pages)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = run(args.snapshot)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f"Observed {len(result['rows'])} original VM cases; private output: {args.output}")
