#!/usr/bin/env python3
"""Observe unchanged owned Xbox counted-string helpers; keep output private.

No kernel bytes or tables are exported. The existing hash-bound reference owns
execution, real original RET and stack validation. Windows RTL is only a second
comparison, never the oracle for Xbox behavior.
"""
import argparse
import ctypes
import hashlib
import json
import struct
from pathlib import Path

from native_io_device_reference import Reference, SNAPSHOT_SHA


class String(ctypes.Structure):
    _fields_ = [('length', ctypes.c_ushort), ('maximum', ctypes.c_ushort),
                ('buffer', ctypes.c_void_p)]


def observe(snapshot):
    ref = Reference(snapshot)
    ref.cpu.mem_map(0x10010000, 0x40000)
    ntdll = ctypes.WinDLL('ntdll')
    init = [ntdll.RtlInitAnsiString, ntdll.RtlInitUnicodeString]
    for function in init:
        function.argtypes = [ctypes.POINTER(String), ctypes.c_void_p]
        function.restype = None
    equal = ntdll.RtlEqualString
    equal.argtypes = [ctypes.POINTER(String), ctypes.POINTER(String), ctypes.c_ubyte]
    equal.restype = ctypes.c_ubyte
    counts = [[None, 0, 1, 255, 65533, 65534, 65535, 65536, 65537],
              [None, 0, 1, 255, 32765, 32766, 32767, 32768, 32769]]
    initializers = []
    for kind, lengths in enumerate(counts):
        for count in lengths:
            source = 0 if count is None else 0x10010003
            data = ((b'A' if kind == 0 else b'\x34\x12') * (count or 0)
                    + bytes(kind + 1))
            if source:
                ref.cpu.mem_write(source, data)
            ref.cpu.mem_write(0x10000010, b'\xa5' * 12)
            ref.call(289 + kind, 0x10000010, source)
            record = bytes(ref.cpu.mem_read(0x10000010, 12))
            length, maximum, pointer = struct.unpack('<HHI', record[:8])
            assert pointer == source and record[8:] == b'\xa5' * 4
            if source:
                assert bytes(ref.cpu.mem_read(source, len(data))) == data
            host_data = ctypes.create_string_buffer(data)
            host = String()
            init[kind](ctypes.byref(host), ctypes.addressof(host_data) if source else None)
            initializers.append(dict(ordinal=289 + kind, characters=count,
                original_length=length, original_maximum=maximum,
                host_length=host.length, host_maximum=host.maximum,
                host_matches=(length, maximum) == (host.length, host.maximum),
                caller_buffer_preserved=True, output_canary_preserved=True))

    # Executed character outputs are API facts, not an extracted private table.
    uppercase = [ref.call(316, value) & 255 for value in range(256)]
    comparisons = []

    def compare(label, left, right, insensitive, left_max=None, right_max=None):
        left_max = len(left) if left_max is None else left_max
        right_max = len(right) if right_max is None else right_max
        pointers = (0x10010003, 0x10030005)
        ref.cpu.mem_write(pointers[0], left + b'\x13\x57')
        ref.cpu.mem_write(pointers[1], right + b'\x24\x68')
        records = (struct.pack('<HHI', len(left), left_max, pointers[0]),
                   struct.pack('<HHI', len(right), right_max, pointers[1]))
        ref.cpu.mem_write(0x10000010, records[0])
        ref.cpu.mem_write(0x10000020, records[1])
        original = ref.call(279, 0x10000010, 0x10000020, insensitive,
                            instruction_limit=4000000) & 255
        assert bytes(ref.cpu.mem_read(pointers[0], len(left) + 2)) == left + b'\x13\x57'
        assert bytes(ref.cpu.mem_read(pointers[1], len(right) + 2)) == right + b'\x24\x68'
        assert bytes(ref.cpu.mem_read(0x10000010, 8)) == records[0]
        assert bytes(ref.cpu.mem_read(0x10000020, 8)) == records[1]
        buffers = (ctypes.create_string_buffer(left), ctypes.create_string_buffer(right))
        host_records = (String(len(left), left_max, ctypes.addressof(buffers[0])),
                        String(len(right), right_max, ctypes.addressof(buffers[1])))
        host = equal(ctypes.byref(host_records[0]), ctypes.byref(host_records[1]), insensitive)
        expected = (len(left) == len(right) and all(
            (uppercase[a] == uppercase[b] if insensitive & 255 else a == b)
            for a, b in zip(left, right)))
        assert original == int(expected), (label, original, expected)
        comparisons.append(dict(label=label, left=left.hex(), right=right.hex(),
            insensitive=insensitive, left_maximum=left_max, right_maximum=right_max,
            original=original, host=host, host_matches=original == host,
            input_records_and_bytes_preserved=True))

    for value in range(256):
        for flag in (0, 1, 255):
            compare(f'byte-{value:02x}-flag-{flag}', bytes([value]),
                    bytes([uppercase[value]]), flag, 0, 65535)
    for left, right in [(b'', b''), (b'a', b''), (b'', b'a'),
            (b'abc', b'ABC'), (b'a\0b', b'A\0B'), (b'a\0b', b'A\0C'),
            (b'prefix-tail', b'prefix'), (bytes(range(256)), bytes(uppercase)),
            (b'a' * 65535, b'A' * 65535), (b'a' * 65534 + b'b', b'A' * 65535)]:
        for flag in (0, 1, 0x100, 0x101):
            compare(f'counted-{len(comparisons)}', left, right, flag, 0, 0)
    return dict(snapshot_sha256=SNAPSHOT_SHA,
        reference_source_sha256=hashlib.sha256(Path(__file__).with_name(
            'native_io_device_reference.py').read_bytes()).hexdigest(),
        source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        initializers=initializers, uppercase=uppercase, comparisons=comparisons,
        hardware_operations=0, original_helpers_replaced=False,
        actual_thread_integration_qualified=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error('preserve existing observations; choose a new output path')
    result = observe(args.snapshot)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(initializers=len(result['initializers']),
        initializer_host_mismatches=sum(not row['host_matches'] for row in result['initializers']),
        uppercase_changes=[(value, upper) for value, upper in enumerate(result['uppercase']) if value != upper],
        comparisons=len(result['comparisons']),
        comparison_host_mismatches=sum(not row['host_matches'] for row in result['comparisons']))))
