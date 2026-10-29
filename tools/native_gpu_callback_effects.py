"""Emit exact-image callback effects; original callback code remains unchanged.

The reviewed closure only qualifies the native QPC/checkpoint configuration
gated in graphics_callback_admission.h. Unknown images or changed closures emit
an unavailable certificate. Input manifest/inventory tampering remains fatal.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

from aot_inventory import parse_xbe

ROOT = Path(__file__).resolve().parent.parent
PROOFS = (ROOT / 'pilots/native-title/profiles/blinx-gpu-callback-effects.json',)
HEADER = 'pilot_gpu_callback_effects.h'


class Unqualified(ValueError):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest()


def checked_manifest(output):
    generated = output / 'gen'
    manifest = json.loads((output / 'generation.aot-private.json').read_text())
    actual = set()
    for path in generated.iterdir():
        if path.is_symlink() or (path.is_dir() and path.name != 'third_party'):
            raise ValueError('unexpected generated directory or symlink')
        if path.is_file():
            actual.add(path.name)
    if not manifest.get('files') or actual != set(manifest['files']):
        raise ValueError('generated inventory differs from its manifest')
    for name, expected in manifest['files'].items():
        if Path(name).name != name or digest((generated / name).read_bytes()) != expected:
            raise ValueError('changed generated artifact: ' + name)
    return manifest


def prove(blob, image, generated, manifest, proof):
    if proof['schema'] != 'gpu-callback-effects.v1' or proof['adapter'] != 'timestamp-context16.v1':
        raise Unqualified('unsupported effect adapter')
    if proof['write_stride'] != 16 or proof['write_bytes'] != 8 or len(proof['closure']) != 2 or proof['routine'] != proof['closure'][0]['address']:
        raise Unqualified('effect adapter shape differs')
    if proof['xbe_sha256'] != image['sha256']:
        raise Unqualified('image differs from reviewed proof')
    records = []
    units = [generated / name for name in sorted(manifest['files'])
             if re.fullmatch(r'recomp_(?:[0-9]{4}|entry_[0-9a-fA-F]{8})\.c', name)]
    sources = [(path.name, path.read_text(encoding='utf-8')) for path in units]
    for expected in proof['closure']:
        address, size = int(expected['address'], 0), expected['bytes']
        section = next((s for s in image['sections'] if s['address'] <= address and
                        address + size <= s['address'] + s['raw_size']), None)
        if section is None:
            raise Unqualified('closure lacks complete original backing')
        offset = section['raw_offset'] + address - section['address']
        if digest(blob[offset:offset + size]) != expected['original_sha256']:
            raise Unqualified('original closure changed')
        marker = 'void sub_%08X(void)' % address
        matches = [(name, text) for name, text in sources if marker in text]
        if len(matches) != 1 or matches[0][1].count(marker) != 1:
            raise Unqualified('missing or ambiguous complete generated closure')
        name, text = matches[0]
        start = text.index(marker)
        extent = 'Original: 0x%08X - 0x%08X (%d bytes,' % (address, address + size, size)
        if extent not in text[max(0, start - 300):start]:
            raise Unqualified('generated closure extent differs')
        end = text.find('\n/**', start)
        body = text[start:end if end >= 0 else len(text)].rstrip() + '\n'
        if digest(body.encode()) != expected['generated_body_sha256']:
            raise Unqualified('generated closure changed')
        records.append(dict(expected, generated_file=name))
    windows = [(s['address'], s['address'] + s['size']) for s in image['sections']
               if s['name'] == proof['write_section'] and s['flags'] & 1]
    base, size = int(proof['write_base'], 0), proof['write_bytes']
    if not any(low <= base < base + size <= high for low, high in windows):
        raise Unqualified('callback output lacks reviewed writable section')
    return dict(version=1, adapter=proof['adapter'], image_sha256=image['sha256'],
                routine=int(proof['routine'], 0), write_base=base,
                write_stride=proof['write_stride'], write_bytes=size,
                write_windows=windows, closure=records)


def header(record):
    lines = ['/* Generated callback effects. Original callback bodies unchanged. */',
             '#pragma once', '#define PILOT_GPU_CALLBACK_EFFECTS_VERSION %du' % record['version']]
    if record['version'] == 1:
        lines += ['#define PILOT_GPU_CALLBACK_ROUTINE 0x%08xu' % record['routine'],
                  '#define PILOT_GPU_CALLBACK_WRITE_BASE 0x%08xu' % record['write_base'],
                  'static int pilot_gpu_callback_write_section(uint32_t base,uint32_t bytes)',
                  '{return ' + ' || '.join('(base>=0x%xu && (uint64_t)base+bytes<=0x%xull)' % window
                                         for window in record['write_windows']) + ';}']
    return '\n'.join(lines) + '\n'


def emit(xbe, output):
    output = Path(output)
    manifest = checked_manifest(output)
    blob = Path(xbe).read_bytes()
    image = parse_xbe(blob)
    if image['sha256'] != manifest['sha256']:
        raise ValueError('original image and generated manifest differ')
    record = dict(version=0, image_sha256=image['sha256'], reason='no reviewed callback effect proof')
    for path in PROOFS:
        proof = json.loads(path.read_text())
        if proof['xbe_sha256'] != image['sha256']:
            continue
        try:
            record = prove(blob, image, output / 'gen', manifest, proof)
        except Unqualified as error:
            record['reason'] = str(error)
        record['proof_sha256'] = digest(path.read_bytes())
        break
    destination = output / 'gen' / HEADER
    content = header(record).encode()
    if not destination.exists() or destination.read_bytes() != content:
        destination.write_bytes(content)
    manifest['gpu_callback_effects'] = record
    manifest['files'][HEADER] = digest(content)
    (output / 'generation.aot-private.json').write_text(json.dumps(manifest, indent=2) + '\n', newline='\n')
    checked_manifest(output)
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(emit(args.xbe, args.output), indent=2))
