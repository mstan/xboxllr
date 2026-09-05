#!/usr/bin/env python3
"""Compare two frame-series PPM sets pixel by pixel (beads-xbox.1.15.7).

PERF-METHODOLOGY.md section 3 was rewritten on 2026-09-05: the acceptance
standard is BEHAVIORAL FAITHFULNESS, not byte-exactness.  A byte comparison
answers "did anything move"; it does not answer "would a human see it".  This
tool answers the second question, which is what a GPU executor has to be judged
on, because float32 interpolation on a GPU is not obliged to reproduce the
software rasterizer's `double` arithmetic bit for bit.

For each checkpoint it reports:
  * differing pixel count and percentage
  * maximum and mean absolute channel delta over the differing pixels
  * the histogram of channel deltas (how many are +/-1, +/-2, ...)
  * the bounding box of the difference, which is what separates "LSB noise
    spread over the whole image" from "one object is in the wrong place"
  * the largest 4-connected component of differing pixels, which is what
    separates "scattered rounding" from "a missing or misplaced primitive"

With --png-dir it also writes a diff image per checkpoint: the reference in
grey, differing pixels tinted by magnitude (blue = 1 LSB, green = 2-4,
yellow = 5-16, red = >16).  Those are the images a human reads.

Exit code 0 when every checkpoint is within the tolerance, 1 otherwise.
PNG writing uses only the standard library (zlib + struct).
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import zlib
from collections import deque


def read_ppm(path):
    """Read a binary P6 PPM.  Returns (width, height, bytes)."""
    with open(path, 'rb') as handle:
        data = handle.read()
    if not data.startswith(b'P6'):
        raise ValueError(f'{path}: not a binary P6 PPM')
    fields = []
    index = 2
    while len(fields) < 3:
        while index < len(data) and data[index : index + 1].isspace():
            index += 1
        if data[index : index + 1] == b'#':
            while index < len(data) and data[index] != 0x0A:
                index += 1
            continue
        start = index
        while index < len(data) and not data[index : index + 1].isspace():
            index += 1
        fields.append(int(data[start:index]))
    index += 1  # single whitespace after maxval
    width, height, maxval = fields
    if maxval != 255:
        raise ValueError(f'{path}: maxval {maxval} is not supported')
    pixels = data[index : index + width * height * 3]
    if len(pixels) != width * height * 3:
        raise ValueError(f'{path}: truncated pixel data')
    return width, height, pixels


def write_png(path, width, height, rgb):
    """Minimal RGB8 PNG writer (stdlib only)."""
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)  # filter type 0
        raw += rgb[y * stride : (y + 1) * stride]
    def chunk(tag, payload):
        return (struct.pack('>I', len(payload)) + tag + payload +
                struct.pack('>I', zlib.crc32(tag + payload) & 0xFFFFFFFF))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    with open(path, 'wb') as handle:
        handle.write(b'\x89PNG\r\n\x1a\n')
        handle.write(chunk(b'IHDR', header))
        handle.write(chunk(b'IDAT', zlib.compress(bytes(raw), 6)))
        handle.write(chunk(b'IEND', b''))


def tint(delta):
    """Colour ramp for a per-pixel maximum channel delta."""
    if delta <= 1:
        return (60, 90, 255)
    if delta <= 4:
        return (40, 220, 80)
    if delta <= 16:
        return (255, 220, 40)
    return (255, 40, 40)


def largest_component(mask, width, height):
    """Size and bounding box of the largest 4-connected run of set pixels."""
    seen = bytearray(len(mask))
    best = 0
    best_box = None
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        queue = deque([start])
        seen[start] = 1
        size = 0
        x0 = x1 = start % width
        y0 = y1 = start // width
        while queue:
            here = queue.popleft()
            size += 1
            hx = here % width
            hy = here // width
            if hx < x0: x0 = hx
            if hx > x1: x1 = hx
            if hy < y0: y0 = hy
            if hy > y1: y1 = hy
            if hx > 0 and mask[here - 1] and not seen[here - 1]:
                seen[here - 1] = 1
                queue.append(here - 1)
            if hx + 1 < width and mask[here + 1] and not seen[here + 1]:
                seen[here + 1] = 1
                queue.append(here + 1)
            if hy > 0 and mask[here - width] and not seen[here - width]:
                seen[here - width] = 1
                queue.append(here - width)
            if hy + 1 < height and mask[here + width] and not seen[here + width]:
                seen[here + width] = 1
                queue.append(here + width)
        if size > best:
            best = size
            best_box = (x0, y0, x1, y1)
    return best, best_box


def compare(reference_path, candidate_path, png_path=None):
    rw, rh, rp = read_ppm(reference_path)
    cw, ch, cp = read_ppm(candidate_path)
    if (rw, rh) != (cw, ch):
        raise ValueError(
            f'{candidate_path}: {cw}x{ch} does not match reference {rw}x{rh}')
    pixels = rw * rh
    mask = bytearray(pixels)
    histogram = {}
    differing = 0
    max_delta = 0
    delta_sum = 0
    x0, y0, x1, y1 = rw, rh, -1, -1
    diff_rgb = bytearray(pixels * 3) if png_path else None
    for index in range(pixels):
        base = index * 3
        dr = abs(rp[base] - cp[base])
        dg = abs(rp[base + 1] - cp[base + 1])
        db = abs(rp[base + 2] - cp[base + 2])
        delta = dr if dr > dg else dg
        if db > delta:
            delta = db
        if diff_rgb is not None and delta == 0:
            grey = (rp[base] * 77 + rp[base + 1] * 151 + rp[base + 2] * 28) >> 10
            diff_rgb[base] = grey
            diff_rgb[base + 1] = grey
            diff_rgb[base + 2] = grey
        if delta == 0:
            continue
        differing += 1
        mask[index] = 1
        delta_sum += dr + dg + db
        if delta > max_delta:
            max_delta = delta
        histogram[delta] = histogram.get(delta, 0) + 1
        px = index % rw
        py = index // rw
        if px < x0: x0 = px
        if px > x1: x1 = px
        if py < y0: y0 = py
        if py > y1: y1 = py
        if diff_rgb is not None:
            r, g, b = tint(delta)
            diff_rgb[base] = r
            diff_rgb[base + 1] = g
            diff_rgb[base + 2] = b
    component, component_box = (0, None)
    if differing:
        component, component_box = largest_component(mask, rw, rh)
    if diff_rgb is not None:
        write_png(png_path, rw, rh, diff_rgb)
    return {
        'width': rw,
        'height': rh,
        'pixels': pixels,
        'differing': differing,
        'max_delta': max_delta,
        'mean_delta': (delta_sum / (differing * 3)) if differing else 0.0,
        'histogram': histogram,
        'box': (x0, y0, x1, y1) if differing else None,
        'largest_component': component,
        'component_box': component_box,
    }


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-prefix', required=True,
                        help='frame-series prefix of the reference run')
    parser.add_argument('--candidate-prefix', required=True,
                        help='frame-series prefix of the candidate run')
    parser.add_argument('--png-dir', default='',
                        help='write one diff PNG per checkpoint into this dir')
    parser.add_argument('--max-channel-delta', type=int, default=1,
                        help='largest per-channel delta accepted (default 1)')
    parser.add_argument('--max-differing-pct', type=float, default=0.5,
                        help='largest differing-pixel percentage accepted')
    parser.add_argument('--max-component', type=int, default=64,
                        help='largest connected differing region accepted; a '
                             'big one means structure moved, not rounding')
    args = parser.parse_args(argv)

    ref_dir = os.path.dirname(os.path.abspath(args.reference_prefix)) or '.'
    ref_base = os.path.basename(args.reference_prefix)
    cand_dir = os.path.dirname(os.path.abspath(args.candidate_prefix)) or '.'
    cand_base = os.path.basename(args.candidate_prefix)
    suffixes = sorted(
        name[len(ref_base):] for name in os.listdir(ref_dir)
        if name.startswith(ref_base) and name.endswith('.ppm'))
    if not suffixes:
        print(f'no PPMs found for prefix {args.reference_prefix}',
              file=sys.stderr)
        return 2
    if args.png_dir:
        os.makedirs(args.png_dir, exist_ok=True)

    print(f'{"checkpoint":<34}{"differing":>12}{"pct":>9}{"maxd":>6}'
          f'{"meand":>8}{"maxrun":>8}  verdict')
    failures = 0
    compared = 0
    for suffix in suffixes:
        reference = os.path.join(ref_dir, ref_base + suffix)
        candidate = os.path.join(cand_dir, cand_base + suffix)
        if not os.path.exists(candidate):
            print(f'{suffix:<34}{"MISSING":>12}')
            failures += 1
            continue
        png = None
        if args.png_dir:
            png = os.path.join(args.png_dir,
                               'diff' + suffix.replace('.ppm', '.png'))
        stats = compare(reference, candidate, png)
        compared += 1
        pct = 100.0 * stats['differing'] / stats['pixels']
        ok = (stats['max_delta'] <= args.max_channel_delta and
              pct <= args.max_differing_pct and
              stats['largest_component'] <= args.max_component)
        if not ok:
            failures += 1
        print(f'{suffix:<34}{stats["differing"]:>12}{pct:>9.4f}'
              f'{stats["max_delta"]:>6}{stats["mean_delta"]:>8.3f}'
              f'{stats["largest_component"]:>8}  {"ok" if ok else "REVIEW"}')
        if stats['differing']:
            hist = ' '.join(f'{d}:{n}' for d, n in
                            sorted(stats['histogram'].items())[:12])
            print(f'{"":<34}  box={stats["box"]} component_box='
                  f'{stats["component_box"]} deltas[{hist}]')
    print(f'\ncompared={compared} failures={failures} '
          f'tolerance: max_channel_delta<={args.max_channel_delta} '
          f'differing_pct<={args.max_differing_pct} '
          f'largest_component<={args.max_component}')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
