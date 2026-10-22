#!/usr/bin/env python3
"""Admission through the same image/filesystem implementation used by titles.

Opening for inspection is read-only and denies concurrent writers. This never
creates/formats/converts a disk, derives keys, or claims arbitrary saves match
the selected console. Identity/signing admission belongs to console setup.
"""
import ctypes as c
import hashlib
import struct
from pathlib import Path


class Storage:
    def __init__(self, library, image, *, writable=False):
        self.api = c.CDLL(str(Path(library).resolve(strict=True)))
        signatures = {
            'open': (c.c_void_p, [c.c_char_p, c.c_int, c.c_char_p, c.c_size_t]),
            'destroy': (c.c_uint32, [c.c_void_p]),
            'bytes': (c.c_uint64, [c.c_void_p]),
            'is_qcow2': (c.c_int, [c.c_void_p]),
            'flush': (c.c_uint32, [c.c_void_p]),
            'read_blocks': (c.c_uint32, [c.c_void_p, c.c_uint64, c.c_void_p, c.c_uint32]),
            'capacity': (c.c_uint32, [c.c_void_p, c.c_uint, c.POINTER(c.c_uint64),
                c.POINTER(c.c_uint64), c.POINTER(c.c_uint32)]),
        }
        for name, (result, args) in signatures.items():
            fn = getattr(self.api, 'xllr_storage_' + name)
            fn.restype, fn.argtypes = result, args
        error = c.create_string_buffer(1024)
        self.handle = self.api.xllr_storage_open(str(Path(image).resolve(strict=True)).encode('utf-8'),
            writable, error, len(error))
        if not self.handle:
            raise ValueError(error.value.decode('utf-8', 'replace') or 'HDD image admission failed')

    def close(self):
        if self.handle:
            status = self.api.xllr_storage_destroy(self.handle)
            self.handle = None
            if status:
                raise OSError(f'HDD image flush/close failed ({status:08x})')

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def inspect(self):
        partitions = []
        for index in range(1, 6):
            total, free, unit = c.c_uint64(), c.c_uint64(), c.c_uint32()
            status = self.api.xllr_storage_capacity(self.handle, index, c.byref(total), c.byref(free), c.byref(unit))
            if status:
                raise ValueError(f'HDD partition {index} admission failed ({status:08x})')
            partitions.append(dict(partition=index, cluster_bytes=unit.value,
                total_clusters=total.value, available_clusters=free.value))
        return dict(format='qcow2' if self.api.xllr_storage_is_qcow2(self.handle) else 'raw',
            bytes=self.api.xllr_storage_bytes(self.handle), partitions=partitions,
            disk_structure_verified=True)

    def identity(self):
        # Stable volume identity, not a hash of mutable save contents. Native
        # mounting rechecks this under its exclusive writer handle.
        data = bytearray(struct.pack('<Q', self.api.xllr_storage_bytes(self.handle)))
        for offset in (0xabe80000,0x8ca80000,0x80000,0x2ee80000,0x5dc80000):
            part = c.create_string_buffer(16)
            status = self.api.xllr_storage_read_blocks(self.handle, offset, part, 16)
            if status:
                raise ValueError(f'Cannot read HDD volume identity ({status:08x})')
            data.extend(part.raw)
        return hashlib.sha256(data).hexdigest()
