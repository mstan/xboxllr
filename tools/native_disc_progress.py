"""Cancelable Windows Shell copy progress; no extra package or GUI toolkit.

IProgressDialog owns its modeless UI thread. The importer polls cancellation
between bounded copies. Other hosts use the same importer without this UI.
"""
import os
import ctypes as c
import time
import uuid
from native_disc import ImportCancelled


class Progress:
    def __init__(self, title):
        self.title = title
        self.pointer = c.c_void_p()
        self.ole = None
        self.last = 0

    def method(self, index, result, *arguments):
        table = c.cast(self.pointer, c.POINTER(c.POINTER(c.c_void_p))).contents
        return c.WINFUNCTYPE(result, c.c_void_p, *arguments)(table[index])

    def call(self, index, arguments, *values):
        status = self.method(index, c.c_long, *arguments)(self.pointer, *values)
        if status < 0:
            raise OSError(f'Import progress dialog failed (0x{status & 0xffffffff:08X})')

    def __enter__(self):
        if os.name != 'nt':
            return self
        self.ole = c.WinDLL('ole32')
        self.ole.CoInitializeEx.argtypes = [c.c_void_p, c.c_uint]
        self.ole.CoInitializeEx.restype = c.c_long
        self.initialized = self.ole.CoInitializeEx(None, 2) >= 0
        try:
            clsid = (c.c_ubyte * 16).from_buffer_copy(uuid.UUID('F8383852-FCD3-11D1-A6B9-006097DF5BD4').bytes_le)
            iid = (c.c_ubyte * 16).from_buffer_copy(uuid.UUID('EBBC7C04-315E-11D2-B62F-006097DF5BD4').bytes_le)
            self.ole.CoCreateInstance.argtypes = [c.c_void_p, c.c_void_p, c.c_uint, c.c_void_p, c.POINTER(c.c_void_p)]
            self.ole.CoCreateInstance.restype = c.c_long
            result = self.ole.CoCreateInstance(c.byref(clsid), None, 1, c.byref(iid), c.byref(self.pointer))
            if result < 0:
                raise OSError('Windows could not open the game import progress dialog')
            self.call(5, [c.c_wchar_p], self.title)
            self.call(10, [c.c_uint, c.c_wchar_p, c.c_int, c.c_void_p], 1, 'Preparing game files (first import only)', False, None)
            self.call(11, [c.c_wchar_p, c.c_void_p], 'Canceling import. Your disc image and saves are unchanged.', None)
            self.call(3, [c.c_void_p, c.c_void_p, c.c_uint, c.c_void_p], None, None, 2, None)
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def check(self):
        if self.pointer and self.method(7, c.c_int)(self.pointer):
            raise ImportCancelled()

    def update(self, current, total, name):
        self.check()
        now = time.monotonic()
        if self.pointer and (now - self.last >= .08 or current == total):
            self.call(9, [c.c_ulonglong, c.c_ulonglong], current, max(total, 1))
            self.call(10, [c.c_uint, c.c_wchar_p, c.c_int, c.c_void_p], 2, name, True, None)
            self.last = now

    def __exit__(self, *_):
        if self.pointer:
            try:
                self.call(4, [])
            finally:
                self.method(2, c.c_ulong)(self.pointer)
                self.pointer = c.c_void_p()
        if self.ole and self.initialized:
            self.ole.CoUninitialize()
            self.ole = None
