#!/usr/bin/env python3
"""Original NT mutant object-manager component oracle, no replaced callees.

Uses the existing captured software floor and authored calling buffers. Does
not qualify competing threads, abandonment delivery or blocked waits. The
unowned-release exception is retained as a limit if original SEH cannot return.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ESI,
                              UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_EIP)
from native_io_device_reference import Reference, SNAPSHOT_SHA

KERNEL_SHA = '827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'


def verify(snapshot, kernel):
    blob = kernel.read_bytes()
    if hashlib.sha256(blob).hexdigest() != KERNEL_SHA:
        raise ValueError('unreviewed flat live kernel')
    base = 0x10000100
    registers = (UC_X86_REG_EBX, UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP)

    def floor():
        ref = Reference(snapshot)
        ref.enable_kernel_segments()
        # Same captured-thread priority setup as native_event_timer_reference.
        ref.call(148, ref.word(0x8003da64), 31)
        ref.cpu.mem_write(base, bytes(512))
        return ref

    def call(ref, ordinal, *args):
        saved = {reg: 0x12345000 + i * 0x100 for i, reg in enumerate(registers)}
        for reg, value in saved.items():
            ref.cpu.reg_write(reg, value)
        status = ref.call(ordinal, *args)
        if any(ref.cpu.reg_read(reg) != value for reg, value in saved.items()):
            raise AssertionError('original nonvolatile register changed')
        return status

    def word(ref, address):
        return struct.unpack('<I', ref.cpu.mem_read(address, 4))[0]

    def query(ref, handle):
        ref.cpu.mem_write(base + 124, b'\xcd' * 16)
        status = call(ref, 213, handle, base + 128)
        data = bytes(ref.cpu.mem_read(base + 124, 16))
        if status:
            assert data == b'\xcd' * 16
            return dict(status=f'{status:08x}', output_unchanged=True)
        assert data[:4] == b'\xcd' * 4 and data[10:] == b'\xcd' * 6
        return dict(status='00000000', count=struct.unpack_from('<i', data, 4)[0],
                    owned=data[8], abandoned=data[9], writes_exactly_six_bytes=True,
                    struct_padding_unchanged=True, outer_canaries_unchanged=True)

    def state(ref, handle, count, owned):
        value = query(ref, handle)
        assert (value['count'], value['owned'], value['abandoned']) == (count, owned, 0)
        return value

    source = []
    ref = floor()
    # Exact original export-body spans, including NtReleaseMutant's SEH
    # filter/handler after its normal RET. No invented function-size inference.
    for ordinal, size in ((192, 58), (213, 80), (221, 122), (235, 371)):
        address = ref.base + ref.word(ref.export_table + 4 * (ordinal - ref.ordinal_base))
        original = blob[address - ref.base:address - ref.base + size]
        captured = bytearray()
        while len(captured) < size:
            at = address + len(captured)
            take = min(size - len(captured), 4096 - (at & 4095))
            physical = ref.physical(at)
            captured.extend(ref.ram[physical:physical + take])
        assert bytes(captured) == original
        source.append(dict(ordinal=ordinal, address=f'{address:08x}', bytes=size,
                           sha256=hashlib.sha256(original).hexdigest(), capture_matches_flat=True))

    lifetimes = []
    for initial in (0, 1, 0x100, 0x101):
        ref = floor()
        assert call(ref, 192, base, 0, initial) == 0
        handle = word(ref, base)
        count = 0 if initial & 255 else 1
        observations = [state(ref, handle, count, int(count <= 0))]
        for ordinal in (233, 234):
            args = (handle, 0, base + 32) if ordinal == 233 else (handle, 0, 0, base + 32)
            assert call(ref, ordinal, *args) == 0
            count -= 1
            observations.append(state(ref, handle, count, 1))
        previous = []
        while count < 1:
            ref.cpu.mem_write(base + 64, b'\xcd' * 12)
            assert call(ref, 221, handle, base + 68) == 0
            data = bytes(ref.cpu.mem_read(base + 64, 12))
            assert data[:4] == data[8:] == b'\xcd' * 4
            assert struct.unpack_from('<i', data, 4)[0] == count
            previous.append(count)
            count += 1
            observations.append(state(ref, handle, count, int(count <= 0)))
        assert call(ref, 233, handle, 0, base + 32) == 0
        assert call(ref, 221, handle, 0) == 0  # optional PreviousCount
        assert call(ref, 187, handle) == 0
        assert query(ref, handle)['status'] == 'c0000008'
        assert call(ref, 221, handle, base + 68) == 0xc0000008
        assert call(ref, 233, handle, 0, base + 32) == 0xc0000008
        assert call(ref, 187, handle) == 0xc0000008
        lifetimes.append(dict(initial_argument=initial, initial_low_byte=initial & 255,
                              observations=observations, release_previous_counts=previous,
                              optional_previous_count_null=True, close_stale_rejected=True,
                              ret_cleanup_and_nonvolatiles=True))

    ref = floor()
    assert call(ref, 192, base, 0, 1) == 0
    handle = word(ref, base)
    assert call(ref, 187, handle) == 0
    assert query(ref, handle)['status'] == 'c0000008'
    close_owned = dict(close_status='00000000', stale_query='c0000008',
                       scope='Last owned handle close only; no competing waiter/abandonment claim')

    ref = floor()
    assert call(ref, 192, base, 0, 0) == call(ref, 189, base + 4, 0, 0, 0) == 0
    mutant, event = word(ref, base), word(ref, base + 4)
    wrong = []
    for ordinal, handle, name in ((213, event, 'query-mutant/event'),
                                  (221, event, 'release-mutant/event'),
                                  (209, mutant, 'query-event/mutant'),
                                  (225, mutant, 'set-event/mutant')):
        ref.cpu.mem_write(base + 124, b'\xcd' * 16)
        assert call(ref, ordinal, handle, base + 128) == 0xc0000024
        assert bytes(ref.cpu.mem_read(base + 124, 16)) == b'\xcd' * 16
        wrong.append(dict(case=name, status='c0000024', output_unchanged=True))
    assert call(ref, 187, mutant) == call(ref, 187, event) == 0

    ref = floor()
    assert call(ref, 192, base, 0, 0) == call(ref, 189, base + 4, 0, 1, 1) == 0
    mutant, event = word(ref, base), word(ref, base + 4)
    ref.cpu.mem_write(base + 48, struct.pack('<II', event, mutant))
    multi = []

    def event_state():
        assert call(ref, 209, event, base + 80) == 0
        return word(ref, base + 84)

    def wait_many(kind):
        # Actual ambient wrapper's UserMode1, nonalertable; zero timeout only.
        return call(ref, 235, 2, base + 48, kind, 1, 0, base + 32)

    assert wait_many(1) == 0 and event_state() == 0
    state(ref, mutant, 1, 0)
    multi.append(dict(case='WaitAny-first-event', status='00000000', auto_event_consumed=True,
                      mutant_untouched=True, ret24_nonvolatiles=True))
    for count in (0, -1):
        assert wait_many(1) == 1
        state(ref, mutant, count, 1)
        multi.append(dict(case='WaitAny-mutant', status='00000001', mutant_count=count,
                          ret24_nonvolatiles=True))
    assert call(ref, 221, mutant, 0) == call(ref, 221, mutant, 0) == 0
    assert wait_many(0) == 0x102
    state(ref, mutant, 1, 0)
    assert event_state() == 0
    multi.append(dict(case='WaitAll-timeout', status='00000102', no_partial_acquisition=True,
                      ret24_nonvolatiles=True))
    assert call(ref, 225, event, 0) == 0 and wait_many(0) == 0
    state(ref, mutant, 0, 1)
    assert event_state() == 0
    multi.append(dict(case='WaitAll-success', status='00000000', auto_event_consumed=True,
                      mutant_acquired=True, ret24_nonvolatiles=True))
    assert wait_many(0) == 0x102
    state(ref, mutant, 0, 1)
    multi.append(dict(case='WaitAll-timeout-while-owner', status='00000102', no_recursive_acquisition=True,
                      ret24_nonvolatiles=True))
    assert call(ref, 221, mutant, 0) == call(ref, 225, event, 0) == 0
    ref.cpu.mem_write(base + 52, struct.pack('<I', 0))
    assert wait_many(1) == 0xc0000008 and event_state() == 1
    state(ref, mutant, 1, 0)
    multi.append(dict(case='WaitAny-invalid-second-handle', status='c0000008',
                      signaled_first_event_not_consumed=True, ret24_nonvolatiles=True))
    assert call(ref, 187, mutant) == call(ref, 187, event) == 0

    ref = floor()
    assert call(ref, 192, base, 0, 0) == 0
    handle = word(ref, base)
    ref.cpu.mem_write(base + 64, b'\xcd' * 12)
    try:
        status = call(ref, 221, handle, base + 68)
    except RuntimeError as error:
        if str(error) != 'unmodelled interrupt 41':
            raise
        pointer = ref.cpu.reg_read(UC_X86_REG_EAX)
        code = word(ref, pointer)
        assert code == 0xc0000046
        assert bytes(ref.cpu.mem_read(base + 64, 12)) == b'\xcd' * 12
        unowned = dict(returned=False, blocker=str(error), exception_record_code=f'{code:08x}',
                       instruction_after_interrupt=f'{ref.cpu.reg_read(UC_X86_REG_EIP):08x}',
                       previous_count_unchanged=True, qualification='original raise path only; SEH return not qualified')
    else:
        assert status == 0xc0000046
        unowned = dict(returned=True, status=f'{status:08x}', qualification='original SEH returned')
    unowned['source_regions'] = []
    for role, address, size in (('NtReleaseMutant_exception_filter', 0x80016077, 14),
                                ('NtReleaseMutant_exception_handler', 0x80016085, 20),
                                ('raise_exception_interrupt_stub', 0x8001eee4, 14),
                                ('NtReleaseMutant_scope_table', 0x800109b8, 12)):
        raw = blob[address - ref.base:address - ref.base + size]
        unowned['source_regions'].append(dict(role=role, address=f'{address:08x}', bytes=size,
                                              sha256=hashlib.sha256(raw).hexdigest()))
    assert struct.unpack_from('<III', blob, 0x9b8) == (0xffffffff, 0x80016077, 0x80016085)
    unowned['source_derived_return'] = 'c0000046'
    unowned['source_projection'] = ('Scope level0 selects an unconditional EXECUTE_HANDLER1 filter, '
        'which saves ExceptionRecord.Code; handler dereferences the object, resets try level, '
        'loads the saved code and jumps to normal epilogue RET8. This API catches the exception internally.')

    return dict(schema='xboxllr.original-mutant-reference.v1', snapshot_sha256=SNAPSHOT_SHA,
                kernel_sha256=KERNEL_SHA, original_export_bodies=source, lifetimes=lifetimes,
                close_owned=close_owned, wrong_type=wrong, unowned_release=unowned,
                multiwait_zero_poll=multi,
                abi=dict(create192_args=3, query213_args=2, release221_args=2, multiwait235_args=6),
                query_layout=dict(current_count_offset=0, current_count_bytes=4, owned_offset=4,
                                  abandoned_offset=5, writes=6, padding_offsets_preserved=[6, 7]),
                limits=['Captured current thread and authored call buffers; no cold kernel startup.',
                        'No competing/blocked wait, thread exit, abandoned handoff or named-object qualification.',
                        'Whole unowned-release SEH return is not executed on this floor; public status46 is source-derived.',
                        'No callee replacement, synthesized kernel completion, NIC or physical hardware.'],
                source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.snapshot, args.kernel)
    args.output.write_bytes((json.dumps(result, indent=2) + '\n').encode())
    print(json.dumps(dict(lifetimes=len(result['lifetimes']), wrong_type_cases=len(result['wrong_type']),
                          multiwait_cases=len(result['multiwait_zero_poll']),
                          query_writes=6, abi=result['abi'], unowned_release=result['unowned_release'],
                          output=str(args.output))))
