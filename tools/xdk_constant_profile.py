"""Emit private identities for the two audited complete constant upload APIs."""
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
        raise ValueError('private code belongs under build/')
    data = args.xbe.read_bytes()
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    if data[:4] != b'XBEH': raise ValueError('not an XBE')
    table = u32(0x120) - u32(0x104)
    lines = ['// XLLE_PRIVATE_GUEST_ARTIFACT', '#pragma once', '#include <cstdint>',
             'namespace xlle::xdk_constant_profile {']
    for count, entry, end, expected in [
        (1, 0x1480a0, 0x1480ff, '9809e9e583fae17ab938bace24739e20c8c4ef8785e783cf0972d58784823ab3'),
        (4, 0x148100, 0x1481a2, 'f49286399b206ce87d333305d07821a125b242b31d52e527aa388190dd9c4787')]:
        for i in range(u32(0x11c)):
            at = table + i * 56
            va, size, raw, raw_size = (u32(at + n) for n in (4, 8, 12, 16))
            if va <= entry and end <= va + min(size, raw_size):
                code = data[raw + entry - va:raw + end - va]
                break
        else: raise ValueError('API not backed by section bytes')
        if hashlib.sha256(code).hexdigest() != expected:
            raise ValueError('constant upload ABI requires another audit')
        lines += [f'constexpr uint32_t entry{count} = 0x{entry:x};',
                  f'constexpr uint8_t code{count}[] = {{' + ','.join(str(b) for b in code) + '};']
    lines += ['constexpr uint32_t cursor=0x153bd0, limit=0x153bd4, bank=0x156328, reserve=0x149550;', '}']
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(('\n'.join(lines)+'\n').encode())
    print('Verified complete constant upload functions, including real MakeSpace branches')

if __name__ == '__main__': main()
