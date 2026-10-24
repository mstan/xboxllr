"""Exact native executable selection; each revision retains its own saves."""
from copy import deepcopy
import re

from xbe_identity import unsupported_image_message


def entries(data):
    data = data.get('_catalog', data)
    if len(data.get('revisions', [])) > 31:
        raise ValueError('Too many native revisions')
    result = [dict(id='', title=data['title'], executable=data.get('executable'))]
    seen = {data['title']['xbe_sha256']}
    ids = set()
    for revision in data.get('revisions', []):
        key = revision.get('id', '')
        title = revision.get('title', {})
        digest = title.get('xbe_sha256', '')
        if (not re.fullmatch(r'[a-z0-9][a-z0-9_-]*', key) or key in ids or
                not re.fullmatch(r'[0-9a-f]{64}', digest) or digest in seen or
                title.get('game_id') != data['title'].get('game_id') or
                not isinstance(revision.get('executable'), str)):
            raise ValueError('Invalid native revision catalog')
        seen.add(digest)
        ids.add(key)
        result.append(revision)
    return result


def hashes(data):
    return [entry['title']['xbe_sha256'] for entry in entries(data)]


def select(data, digest, *, image=None):
    data = data.get('_catalog', data)
    for entry in entries(data):
        if entry['title']['xbe_sha256'] == digest:
            selected = deepcopy(data)
            selected['title'] = deepcopy(entry['title'])
            if entry['executable'] is not None:
                selected['executable'] = entry['executable']
            selected['_revision'] = entry['id']
            # Preserve the original catalog when selecting again after a UI switch.
            selected['_catalog'] = data.get('_catalog', data)
            return selected
    raise ValueError(unsupported_image_message(image, data['title']) if image is not None else
                     'This Xbox executable is not a supported original revision')


def profile_relative(data):
    key = data.get('_revision', '')
    if key and not re.fullmatch(r'[a-z0-9][a-z0-9_-]*', key):
        raise ValueError('Invalid native revision profile')
    return 'data/revisions/' + key if key else 'data'


def supports_target(targets, identity):
    if not isinstance(targets, list) or not targets or len(targets) > 32:
        return False
    seen = set()
    for target in targets:
        if (not isinstance(target, dict) or set(target) != {'game_id', 'xbe_sha256'} or
                not re.fullmatch(r'[0-9A-Fa-f]{8}', str(target['game_id'])) or
                not re.fullmatch(r'[0-9a-f]{64}', str(target['xbe_sha256']))):
            return False
        key = target['game_id'], target['xbe_sha256']
        if key in seen:
            return False
        seen.add(key)
    return identity in targets
