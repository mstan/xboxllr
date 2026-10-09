"""Compare the maintained tile layout with a pinned public Xbox hardware capture.

The layout fixture models BlitBeyondWidth's copy/clip/readback sequence.
The command fixture executes it through PFIFO and the 2D command engine.
Neither is a full GPU/CPU-aperture integration pass.
No console is contacted, expected pixels generated, or capture modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image

GOLDEN_REVISION = '06b041d6a50c060b4974d93617405f86a03a0b8f'
GOLDEN_SHA256 = 'd5e8cfbd7cd6c8a229236c38fd50c4decafef145391f3f0bdc6c34018051a513'
GOLDEN_URL = ('https://raw.githubusercontent.com/abaire/nxdk_pgraph_tests_golden_results/'
              + GOLDEN_REVISION + '/results/Image_blit/BlitBeyondWidth.png')
TEST_REVISION = '706a41e61188d3b08e5b1bb2a32b74ad062a883d'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--golden', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--mode', choices=('layout', 'commands'), default='layout')
    args = parser.parse_args()
    if digest(args.golden) != GOLDEN_SHA256:
        raise ValueError('hardware capture differs from the pinned original PNG')
    if 'build' not in args.output.resolve().parts:
        raise ValueError('qualification output must remain under build/')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    raw_path = args.output.with_suffix('.rgba')
    result = subprocess.run([str(args.fixture.resolve()), '--hardware-rgba', str(raw_path.resolve())],
                            capture_output=True, text=True, timeout=60, check=False)
    if result.returncode:
        raise RuntimeError('tile fixture failed: ' + result.stdout + result.stderr)
    with Image.open(args.golden) as image:
        if image.size != (640, 480):
            raise ValueError('unexpected hardware capture geometry')
        expected = image.convert('RGBA').tobytes()
    actual = raw_path.read_bytes()
    if len(actual) != len(expected):
        raise ValueError('fixture output has the wrong size')
    # Fixed rectangles outside the test's explanatory text overlay. Include
    # all 1024 final guard pixels; compare RGBA exactly, without image filters,
    # tolerance fitting, alpha removal or exclusions based on mismatches.
    rectangles = [(0, 0, 640, 20), (0, 280, 640, 200)]
    count = mismatches = identity_mismatches = 0
    for x0, y0, width, height in rectangles:
        for y in range(y0, y0 + height):
            for x in range(x0, x0 + width):
                offset = (y * 640 + x) * 4
                wanted = expected[offset:offset + 4]
                mismatches += actual[offset:offset + 4] != wanted
                # Negative control: the same fixture with linear storage.
                index = y * 640 + x + 1024
                if index >= 640 * 480:
                    linear = bytes([204] * 4)
                else:
                    sy, sx = divmod(index, 640)
                    blue = sy * 255 // 512
                    linear = bytes((255 - blue, sx * 255 // 1024, blue, 255))
                identity_mismatches += linear != wanted
                count += 1
    manifest = dict(
        version=2, mode=args.mode,
        scope=('uncompressed pitch-2560 bank-1 ' +
               ('PFIFO/2D copy, clipping and tile read/write' if args.mode == 'commands' else 'tile layout') +
               '; not complete GPU integration'),
        golden_url=GOLDEN_URL, golden_revision=GOLDEN_REVISION, golden_sha256=GOLDEN_SHA256,
        test_source_revision=TEST_REVISION,
        test_source='src/tests/image_blit_tests.cpp:TestBlitPastWidth',
        fixture_sha256=digest(args.fixture), rgba_sha256=digest(raw_path),
        rectangles_xywh=rectangles, compared_pixels=count, mismatched_pixels=mismatches,
        linear_negative_control_mismatches=identity_mismatches,
        fixture_output=result.stdout.strip(),
    )
    args.output.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(manifest, indent=2))
    return 1 if mismatches or not identity_mismatches else 0


if __name__ == '__main__':
    raise SystemExit(main())
