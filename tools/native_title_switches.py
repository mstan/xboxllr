"""Checkbox selections for statically linked title mods; no package code runs."""
from pathlib import Path, PurePosixPath
import re
import tomllib
from native_revisions import supports_target

PREFIX = 'XLLE_PILOT_TITLE_MOD_'


def declarations(data):
    switches = data.get('mods', {}).get('switches', [])
    if not isinstance(switches, list):
        raise ValueError('Invalid title mod switches')
    seen = set()
    for switch in switches:
        if (not isinstance(switch, dict) or set(switch) != {'manifest', 'package', 'feature', 'key'} or
                any(not isinstance(v, str) for v in switch.values()) or
                not re.fullmatch(r'[A-Z][A-Z0-9_]{0,63}', switch['key']) or
                not re.fullmatch(r'[a-z][a-z0-9_.-]{0,94}', switch['package']) or
                not re.fullmatch(r'[a-z][a-z0-9-]{0,63}', switch['feature'])):
            raise ValueError('Invalid title mod switch declaration')
        expected = f"mods/preloaded/packages/{switch['package']}/1.0.0/manifest.toml"
        if switch['manifest'] != expected or PurePosixPath(expected).is_absolute():
            raise ValueError('Title mod switch must use its bundled manifest')
        identity = switch['package'] + '/' + switch['feature']
        if switch['key'] in seen or identity in seen:
            raise ValueError('Duplicate title mod switch')
        seen.update((switch['key'], identity))
    return switches


def manifests(source, data):
    root = Path(source).resolve().parent
    result = []
    for switch in declarations(data):
        path = (root / switch['manifest']).resolve(strict=True)
        if not path.is_relative_to(root):
            raise ValueError('Title mod switch manifest leaves its title')
        catalog = tomllib.loads(path.read_text(encoding='utf-8'))
        target = [dict(game_id=data['title']['game_id'], xbe_sha256=data['title']['xbe_sha256'])]
        features = [f for f in catalog.get('feature', []) if f.get('id') == switch['feature']]
        if (catalog.get('format_version') != 1 or catalog.get('version') != '1.0.0' or
                catalog.get('id') != switch['package'] or not supports_target(catalog.get('target'), target[0]) or
                len(features) != 1 or features[0].get('default_enabled') is not False or
                any(o.get('feature') == switch['feature'] for o in catalog.get('option', []))):
            raise ValueError('Unsupported bundled title mod switch')
        if path not in result:
            result.append(path)
    return result


def environment(catalog, data):
    selected = {(f['package'], f['id']): f['enabled'] for f in catalog.features}
    result = {}
    for switch in declarations(data):
        key = (switch['package'], switch['feature'])
        if key not in selected or type(selected[key]) is not bool:
            raise ValueError('Title mod switch is missing from its catalog')
        result[PREFIX + switch['key']] = str(int(selected[key]))
    return result
