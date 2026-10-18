#!/usr/bin/env python3
"""Compare portable XcSHA with original kernel execution and independent hashlib.

Authored messages only. Validate copied contexts, prefix preservation, all raw
context bytes and count rollover without a title process, output device or HDD.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import time

from native_save_keys import OriginalCrypto


def run(kernel, compiler, output):
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    wrapper = output / 'sha-wrapper.c'
    wrapper.write_text('''#include "sha1.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API __attribute__((visibility("default")))
#endif
API void sha_init(unsigned char *c){pilot_sha_init(c);}
API void sha_update(unsigned char *c,const unsigned char *p,uint32_t n){pilot_sha_update(c,p,n);}
API void sha_final(unsigned char *c,unsigned char *d){pilot_sha_final(c,d);}
''')
    library = output / ('sha.dll' if os.name == 'nt' else 'sha.so')
    subprocess.run([str(compiler), '-shared', '-fPIC', '-O2', '-std=c11', '-Wall',
                    '-Wextra', '-Werror', '-I' + str(root / 'pilots/native-title'),
                    str(wrapper), '-o', str(library)], check=True, capture_output=True)
    lib = ctypes.CDLL(str(library.resolve()))
    lib.sha_init.argtypes = [ctypes.c_void_p]
    lib.sha_update.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32]
    lib.sha_final.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    original = OriginalCrypto(kernel.read_bytes())
    ctx, data, digest = 0x30000100, 0x30001000, 0x30000200
    start = time.monotonic()
    checks = 0
    for length in [0, 1, 3, 55, 56, 63, 64, 65, 127, 128, 129, 2048]:
        message = bytes((i*19+7) & 255 for i in range(length))
        for chunk in [1, 7, 64, 513, 2048]:
            native = ctypes.create_string_buffer(b'\xa5' * 116, 116)
            original.cpu.mem_write(ctx, b'\xa5' * 116)
            original.call(original.export(335), ctx)
            lib.sha_init(native)
            assert bytes(native) == bytes(original.cpu.mem_read(ctx, 116)), 'init ABI'
            for offset in range(0, length, chunk):
                part = message[offset:offset+chunk]
                original.cpu.mem_write(data, part)
                original.call(original.export(336), ctx, data, len(part))
                lib.sha_update(native, part, len(part))
                wanted = bytes(original.cpu.mem_read(ctx, 116))
                assert bytes(native) == wanted, ('update', length, chunk, offset,
                    [i for i, (a,b) in enumerate(zip(bytes(native),wanted)) if a != b][:12])
                checks += 1
            # Caller copies the complete context; no hidden host handle may be required.
            clone = ctypes.create_string_buffer(bytes(native), 116)
            actual = ctypes.create_string_buffer(20)
            original.call(original.export(337), ctx, digest)
            lib.sha_final(clone, actual)
            expected = hashlib.sha1(message).digest()
            assert bytes(actual) == bytes(original.cpu.mem_read(digest,20)) == expected, ('digest',length,chunk)
            wanted = bytes(original.cpu.mem_read(ctx,116))
            assert bytes(clone) == wanted, ('final context',length,chunk,
                [i for i,(a,b) in enumerate(zip(bytes(clone),wanted)) if a != b][:16])
            checks += 1
    for shift in [0,1,2,3]:
        part=bytes((i*13+3)&255 for i in range(193))
        native=ctypes.create_string_buffer(b'\xa5'*116,116);lib.sha_init(native)
        original.cpu.mem_write(ctx,bytes(native));original.cpu.mem_write(data+shift,part)
        source=ctypes.create_string_buffer(b'\0'*shift+part)
        original.call(original.export(336),ctx,data+shift,len(part))
        lib.sha_update(native,ctypes.byref(source,shift),len(part))
        assert bytes(native)==bytes(original.cpu.mem_read(ctx,116)), ('input alignment',shift)
        checks+=1
    # A byte-count carry must not become the donor's differently ordered bit count.
    native = ctypes.create_string_buffer(b'\xa5' * 116, 116)
    lib.sha_init(native)
    struct.pack_into('<II', native, 44, 0x12345678, 0xfffffff0)
    original.cpu.mem_write(ctx,bytes(native));part=bytes(range(32));original.cpu.mem_write(data,part)
    original.call(original.export(336),ctx,data,len(part));lib.sha_update(native,part,len(part))
    assert bytes(native)==bytes(original.cpu.mem_read(ctx,116)), 'count rollover'
    receipt = dict(passed=True, original_calls_compared=checks+1, vectors=60,
        seconds=round(time.monotonic()-start,3), original_raw_context_bytes=116,
        copied_contexts=True, count_rollover=True, independent_oracle='Python hashlib SHA1',
        header_sha256=hashlib.sha256((root/'pilots/native-title/sha1.h').read_bytes()).hexdigest(),
        library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
        scope='XcSHA context/digest/partition/copy contract; no title/save/gameplay acceptance')
    (output/'result.aot-private.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['kernel','compiler','output']:parser.add_argument('--'+name,type=Path,required=True)
    a=parser.parse_args();print(json.dumps(run(a.kernel,a.compiler,a.output)))
