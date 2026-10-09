"""Qualify the NV20 Z24S8 decoder adaptation against pinned, unmodified envytools.

This is donor-equivalence evidence, not a fresh Xbox hardware measurement.
The original Xbox register shape selects this codec; silicon tag layout and
the hardware compressor's representation choices need separate qualification.
No console is contacted and no replacement reference functions are generated.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys

REVISION = 'f102b82381f3f11cee113d16374c87091db039d9'
HARNESS = r'''
#include "nvhw/vram.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    FILE* in=fopen(argv[1],"rb");FILE* out=fopen(argv[2],"wb");
    if(!in || !out)return 2;
    unsigned char block[64];size_t n;
    while((n=fread(block,1,16,in))==16) {
        memset(block+16,0,48);
        comp_decompress(0x20,1,block,1);
        if(fwrite(block,1,64,out)!=64)return 2;
    }
    int failed=n!=0 || ferror(in) || fclose(out);
    fclose(in);return failed?2:0;
}
'''


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--envytools', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--git', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    repo, output = args.envytools.resolve(), args.output.resolve()
    if 'build' not in output.parts:
        raise ValueError('generated evidence must remain under build/')
    output.parent.mkdir(parents=True, exist_ok=True)
    work = output.parent / 'donor-build'
    work.mkdir(exist_ok=True)
    environment = dict(os.environ)
    environment['PATH'] = str(args.cc.resolve().parent) + os.pathsep + environment.get('PATH', '')

    def run(command, cwd=work):
        result = subprocess.run(list(map(str, command)), cwd=cwd, env=environment,
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f'command failed: {command}\n{result.stdout}{result.stderr}')
        return result.stdout.strip()

    revision = run([args.git, '-C', repo, 'rev-parse', 'HEAD'])
    if revision != REVISION:
        raise ValueError('reference checkout is not the pinned envytools revision')
    relevant = ['nvhw/comp.c', 'nvhw/chipset.c', 'include/nvhw', 'ssot']
    if run([args.git, '-C', repo, 'status', '--porcelain', '--', *relevant]):
        raise ValueError('reference inputs have local modifications')
    for module in ('fb', 'bus', 'gpu'):
        run([sys.executable, '-m', 'ssot.gen_c', 'ssot.' + module, work, work / 'include'], cwd=repo)
    harness = work / 'reference.c'
    harness.write_text(HARNESS, encoding='utf-8')
    reference = work / 'reference.exe'
    run([args.cc.resolve(), '-std=c11', '-O2', '-I', repo / 'include', '-I', work / 'include',
         repo / 'nvhw/comp.c', repo / 'nvhw/chipset.c', harness,
         *(work / 'cgen' / (m + '.c') for m in ('fb', 'bus', 'gpu')), '-o', reference])
    rng = random.Random(0x2A20261008)
    blocks = [bytes(16), bytes([255]) * 16]
    blocks += [(1 << bit).to_bytes(16, 'little') for bit in range(128)]
    blocks += [rng.randbytes(16) for _ in range(20000)]
    inputs = work / 'blocks.bin'
    inputs.write_bytes(b''.join(blocks))
    expected, actual = work / 'donor.bin', work / 'adapted.bin'
    run([reference, inputs, expected])
    run([args.fixture.resolve(), '--decode-blocks', inputs, actual])
    wanted, observed = expected.read_bytes(), actual.read_bytes()
    if len(wanted) != len(blocks) * 64 or len(observed) != len(wanted):
        raise ValueError('decoder output length differs from the complete corpus')
    mismatch = sum(a != b for a, b in zip(wanted, observed))
    # Check authored encoder output using the ORIGINAL decoder, not merely an
    # encode/decode round trip through two functions that may share a mistake.
    planes = []
    for i in range(4096):
        base, stencil = rng.randrange(1 << 24), rng.randrange(256)
        dx, dy = rng.randrange(-16384, 16384), rng.randrange(-16384, 16384)
        pixels = []
        for y in range(4):
            for x in range(4):
                residual = 0 if i < 128 or (x, y) in ((1, 1), (2, 1), (1, 2)) else rng.randrange(-16, 16)
                z = (base + dx * (x - 1) + dy * (y - 1) + residual) & 0xffffff
                pixels.append((z << 8) | stencil)
        planes.append(struct.pack('<16I', *pixels))
    encoder_input, encoder_output = work / 'planes.bin', work / 'encoded.bin'
    encoder_input.write_bytes(b''.join(planes))
    run([args.fixture.resolve(), '--encode-blocks', encoder_input, encoder_output])
    encoded_bytes = encoder_output.read_bytes()
    if len(encoded_bytes) != len(planes) * 20:
        raise ValueError('encoder output length differs from complete corpus')
    sizes = {8: 0, 16: 0}
    packets = []
    for i in range(len(planes)):
        packet = encoded_bytes[i * 20:(i + 1) * 20]
        size, = struct.unpack_from('<I', packet)
        if size not in sizes:
            raise ValueError('known representable plane was not compressed')
        sizes[size] += 1
        packets.append(packet[4:])
    packet_input, decoded_planes = work / 'packets.bin', work / 'decoded-planes.bin'
    packet_input.write_bytes(b''.join(packets))
    run([reference, packet_input, decoded_planes])
    if decoded_planes.read_bytes() != encoder_input.read_bytes():
        raise ValueError('original decoder did not recover every encoded input pixel')
    manifest = dict(version=2, scope='NV20 non-MS little-endian Z24S8 donor decoder and authored encoder equivalence',
                    hardware_qualification=False, reference_revision=revision,
                    original_sources={p: digest(repo / p) for p in ('nvhw/comp.c', 'nvhw/chipset.c')},
                    fixture_sha256=digest(args.fixture), compiler_sha256=digest(args.cc),
                    blocks=len(blocks), compared_pixels=len(blocks) * 16,
                    input_sha256=digest(inputs), expected_sha256=digest(expected),
                    actual_sha256=digest(actual), mismatching_bytes=mismatch,
                    encoder_blocks=len(planes), encoded_sizes=sizes,
                    encoder_input_sha256=digest(encoder_input), encoded_sha256=digest(encoder_output),
                    original_decoded_sha256=digest(decoded_planes))
    output.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(blocks=len(blocks), compared_pixels=len(blocks) * 16,
                          mismatching_bytes=mismatch, encoder_blocks=len(planes), encoded_sizes=sizes,
                          manifest=str(output))))
    if mismatch:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
