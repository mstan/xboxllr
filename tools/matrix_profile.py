"""Generate the private profile for the audited dashboard matrix multiply."""
import argparse
import hashlib
import struct
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--xbe', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if not any(p.startswith('build') for p in args.output.resolve().parts):
        raise ValueError('private code must remain in a build directory')
    data = args.xbe.read_bytes()
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    if data[:4] != b'XBEH':
        raise ValueError('not an XBE')
    entry, length = 0xcfff1, 0xe3
    for i in range(u32(0x11c)):
        at = u32(0x120) - u32(0x104) + i * 56
        va, size, raw, raw_size = (u32(at + j) for j in (4, 8, 12, 16))
        if va <= entry and entry + length <= va + min(size, raw_size):
            code = data[raw + entry - va:raw + entry - va + length]
            break
    else:
        raise ValueError('audited function absent')
    if hashlib.sha256(code).hexdigest() != '7045ca54660b7851e8281e7e4e478343850b97b2f370d8d3b08cfe34e15b2463':
        raise ValueError('function needs a new ABI audit')
    lines = ['// XLLE_PRIVATE_GUEST_ARTIFACT', '#pragma once', '#include <cstdint>',
             'namespace xlle::matrix_profile {', f'constexpr uint32_t entry = 0x{entry:x};',
             'constexpr uint8_t code[] = {', ','.join(f'0x{x:02x}' for x in code), '};', '}']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('Verified matrix multiply stdcall(out,left,right), RET 12, alias-safe inputs')


if __name__ == '__main__':
    main()
