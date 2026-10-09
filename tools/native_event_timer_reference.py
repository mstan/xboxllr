#!/usr/bin/env python3
"""Original event lifecycle and timer deadline reference, without replaced calls.

Uses the owned kernel/RAM bridge and real object manager. Timer clocks remain
the captured inputs: this qualifies insertion/cancellation and tick arithmetic,
not clock interrupt delivery, sleeping-thread wakeups or elapsed wall time.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from native_io_device_reference import Reference, SNAPSHOT_SHA


def verify(snapshot):
    lifetimes = []
    for _ in range(3):
        ref = Reference(snapshot)
        ref.enable_kernel_segments()
        # Actual kernel selection, as in the filesystem reference. This caller
        # does not claim scheduling competition with captured boot workers.
        ref.call(148, ref.word(0x8003da64), 31)
        base = 0x10000100

        def put(offset, *values):
            ref.cpu.mem_write(base + offset, struct.pack('<' + 'I' * len(values), *values))

        def words(offset, count):
            return struct.unpack('<' + 'I' * count, ref.cpu.mem_read(base + offset, count * 4))

        def export(ordinal):
            return ref.base + ref.word(ref.export_table + 4 * (ordinal - ref.ordinal_base))

        tick = ref.word(export(157))
        assert tick == 10000, 'captured KeTimeIncrement must be independently reviewed'
        interrupt = export(120)
        system = export(154)
        interrupt_time = ref.word(interrupt) | (ref.word(interrupt + 4) << 32)
        system_time = ref.word(system) | (ref.word(system + 4) << 32)
        events = []
        for repeat in range(8):
            for kind in (0, 1):
                for initial in (0, 1):
                    put(0, 0xcccccccc)
                    assert ref.call(189, base, 0, kind, initial) == 0
                    handle = words(0, 1)[0]
                    put(24, 0, 0)
                    assert ref.call(209, handle, base + 8) == 0
                    assert words(8, 2) == (kind, initial)
                    assert ref.call(233, handle, 0, base + 24) == (0 if initial else 0x102)
                    assert ref.call(234, handle, 0, 0, base + 24) == (0 if initial and not kind else 0x102)
                    expected_previous = initial if not kind else 0
                    for previous in (expected_previous, 1):
                        put(12, 0x12345678, 0xcccccccc, 0x87654321)
                        assert ref.call(225, handle, base + 16) == 0
                        assert words(12, 3) == (0x12345678, previous, 0x87654321)
                    assert ref.call(233, handle, 0, base + 24) == 0
                    assert ref.call(233, handle, 0, base + 24) == (0x102 if kind else 0)
                    assert ref.call(186, handle) == 0
                    assert ref.call(209, handle, base + 8) == 0 and words(8, 2) == (kind, 0)
                    assert ref.call(187, handle) == 0
                    put(8, 0xcccccccc, 0xcccccccc)
                    assert ref.call(209, handle, base + 8) == 0xc0000008
                    assert words(8, 2) == (0xcccccccc, 0xcccccccc)
                    assert ref.call(187, handle) == 0xc0000008
                    events.append(dict(kind=kind, initial=initial, repeat=repeat,
                                       zero_waits=True, set_clear_query=True, closed_handle_rejected=True))

        timers = []
        timer = base + 0x100
        for kind in (0, 1):
            for ordinal in (149, 150):
                for duration in (1, tick - 1, tick, tick + 1, 100000, 0x100000001):
                    ref.call(113, timer, kind)
                    due = (-duration) & 0xffffffffffffffff
                    args = [timer, due & 0xffffffff, due >> 32]
                    if ordinal == 150:
                        args.append(0)  # nonperiodic timer
                    args.append(0)  # no DPC, rather than a substituted callback
                    assert ref.call(ordinal, *args) & 255 == 0
                    state = bytes(ref.cpu.mem_read(timer, 40))
                    assert state[0] == 8 + kind and state[2:4] == bytes([10, 1])
                    assert struct.unpack_from('<I', state, 4)[0] == 0
                    assert struct.unpack_from('<Q', state, 16)[0] == interrupt_time + duration
                    assert ref.call(ordinal, *args) & 255 == 1
                    assert ref.call(97, timer) & 255 == 1
                    assert ref.call(97, timer) & 255 == 0
                    assert ref.cpu.mem_read(timer + 3, 1) == b'\0'
                    timers.append(dict(kind=kind, ordinal=ordinal, relative=duration,
                                       due=interrupt_time + duration, cancelled=True))
            # Absolute past timers signal immediately in the original routine.
            ref.call(113, timer, kind)
            assert ref.call(149, timer, 1, 0, 0) & 255 == 0
            state = bytes(ref.cpu.mem_read(timer, 40))
            assert state[3] == 0 and struct.unpack_from('<I', state, 4)[0] == 1
            assert ref.call(97, timer) & 255 == 0
        lifetimes.append(dict(events=events, timers=timers, tick_100ns=tick,
                              interrupt_time=interrupt_time, system_time=system_time,
                              absolute_past_signals=True))
    return dict(snapshot_sha256=SNAPSHOT_SHA, lifetimes=lifetimes,
                scope='Original event lifecycle/polls and timer insertion/cancel; no expiration ISR or blocked-thread qualification',
                source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.snapshot)
    args.output.write_bytes((json.dumps(result, indent=2) + '\n').encode())
    print('PASS: 96 original event lifetimes, 72 relative timer cases, past-deadline signaling and cancellation; kernel tick 1 ms')
