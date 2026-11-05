"""Explicit prerelease-only XBE attempts and public, content-free fingerprints."""
import hashlib
import re
import struct
from pathlib import Path

MODE = 'same-title-attempt-v1'


def enabled(data):
    policy = data.get('_catalog', data).get('experimental_xbe')
    return isinstance(policy, dict) and policy.get('mode') == MODE


def fingerprint(raw):
    # Reuse the ordinary bounded section/certificate parser. No disassembly,
    # original bytes, signing seeds or source paths enter the public report.
    from aot_inventory import parse_xbe
    image = parse_xbe(raw)
    headers = struct.unpack_from('<I', raw, 0x108)[0]
    cert = struct.unpack_from('<I', raw, 0x118)[0] - image['base']
    return dict(sha256=image['sha256'], bytes=len(raw), title_id=f'{image["title_id"]:08X}',
        certificate_version=struct.unpack_from('<I', raw, cert + 0xac)[0],
        base=image['base'], entry=image['entry'], image_size=image['image_size'], header_bytes=headers,
        header_sha256=hashlib.sha256(raw[:headers]).hexdigest(),
        body_sha256=hashlib.sha256(raw[headers:]).hexdigest(),
        allowed_media=image['allowed_media'], region=image['region'],
        sections=[{key: section[key] for key in ('index', 'address', 'size', 'raw_offset', 'raw_size', 'flags', 'raw_sha256')}
                  for section in image['sections']], libraries=image['libraries'])


def inspect(data, digest, *, image=None, raw=None):
    if not enabled(data):
        return None
    if raw is None:
        if image is None:
            return None
        from native_disc import Disc, is_disc
        if is_disc(image):
            with Disc(image) as disc:
                raw = disc.xbe
        else:
            path = Path(image)
            if path.stat().st_size > 256 * 1024 * 1024:
                raise ValueError('Xbox executable exceeds the experimental size limit')
            raw = path.read_bytes()
    observed = fingerprint(raw)
    expected = data['title']['game_id'].upper()
    if observed['title_id'] != expected:
        raise ValueError('Wrong game selected; this experiment only attempts ' + data['title']['name'])
    if observed['sha256'] != digest:
        raise ValueError('Selected Xbox executable changed during inspection')
    reference = data['experimental_xbe']['reference']
    fields = ('base', 'entry', 'image_size', 'header_bytes')
    changed_layout = [key for key in fields if observed[key] != reference[key]]
    originals = {s['index']: s for s in reference['sections']}
    changed_sections = [s['index'] for s in observed['sections'] if s != originals.get(s['index'])]
    missing = sorted(set(originals) - {s['index'] for s in observed['sections']})
    return dict(mode=MODE, attempted_sha256=digest, compiled_sha256=data['title']['xbe_sha256'],
        observed=observed, reference=reference, changed_layout=changed_layout,
        changed_sections=changed_sections, missing_sections=missing,
        body_matches=observed['body_sha256'] == reference['body_sha256'],
        limitation='Attempts the existing compiled native program with this XBE. Different code or layouts may stop or behave incorrectly; compatibility is unverified.',
        save_policy='Separate experimental console and local saves; normal saves and external storage are not used.')


def input_sha256(data):
    digest = data.get('_experimental_input_sha256', data['title']['xbe_sha256'])
    if not re.fullmatch('[0-9a-f]{64}', digest):
        raise ValueError('Invalid experimental image identity')
    return digest
