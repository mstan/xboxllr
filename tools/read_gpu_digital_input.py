#!/usr/bin/env python3
"""Decode GPD1 active encoder-input pixels, before analog processing."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def decode(data):
    if len(data) < 320:
        raise ValueError('truncated GPD1 header')
    h = struct.unpack_from('<16I', data)
    if h[:3] != (0x31445047, 1, 320) or h[8] != 1 or h[14:] != (0, 2):
        raise ValueError('unsupported GPD1 version/model/stage')
    if h[3] not in (0, 1) or h[12] not in (0, 1) or h[13] not in (0, 1):
        raise ValueError('invalid GPD1 flags')
    width, height, pin_bytes, rgba_bytes = h[4:8]
    if len(data) != 320 + pin_bytes + rgba_bytes:
        raise ValueError('invalid GPD1 payload size')
    error = data[64:320].split(b'\0', 1)[0].decode('utf-8')
    if h[3]:
        if (width, height, pin_bytes, rgba_bytes, h[9], h[11], h[12]) != (
                640, 480, 640*480*4, 640*480*4, 12, 2560, 1) or error:
            raise ValueError('invalid copied digital profile')
        for falling, rising in struct.iter_unpack('<HH', data[320:320+pin_bytes]):
            if falling > 0xfff or rising > 0xfff:
                raise ValueError('invalid DDR word width')
    elif pin_bytes or rgba_bytes or h[9] != 0xffffffff or not error:
        raise ValueError('invalid failed digital copy')
    rgba = data[320+pin_bytes:]
    if any(a != 255 for a in rgba[3::4]):
        raise ValueError('invalid host preview alpha')
    rgb = bytearray(len(rgba)//4*3)
    for channel in range(3):
        rgb[channel::3] = rgba[channel::4]
    return dict(stage='active-encoder-digital-input', qualified=False,
                model='nominal-SD601-limited-range444', digital_pixels=bool(h[3]),
                complete_bus_waveform=False, analog_output=False,
                width=width, height=height, start=h[10], pitch=h[11],
                identity_dac=bool(h[12]), screen_off=bool(h[13]), error=error,
                sha256=hashlib.sha256(data).hexdigest()), bytes(rgb)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    metadata, rgb = decode(args.snapshot.read_bytes())
    with args.output.with_suffix('.json').open('x') as out:
        json.dump(metadata, out, indent=2)
        out.write('\n')
    if rgb:
        with args.output.with_suffix('.ppm').open('xb') as out:
            out.write(f'P6\n# Digital input preview; NOT analog output\n{metadata["width"]} {metadata["height"]}\n255\n'.encode()+rgb)
    print(json.dumps(metadata, indent=2))
