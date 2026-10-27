"""Selections for statically linked title mods; no package code runs.

A switch feature is a checkbox, optionally with one enumerated choice that
makes its alternatives mutually exclusive. The title receives 0 when the
feature is off, 1 for a checkbox that is on, or the selected choice value.
"""
from pathlib import Path, PurePosixPath
import re
import tomllib
from native_revisions import supports_target

PREFIX = 'XLLE_PILOT_TITLE_MOD_'
CHOICE_VALUE = r'[a-z][a-z0-9-]{0,31}'


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


def title_target(data):
    """Native-title metadata uses [title]; Blinx's legacy profile uses [game]."""
    if 'title' in data:
        return dict(game_id=data['title']['game_id'], xbe_sha256=data['title']['xbe_sha256'])
    game = data['game']
    return dict(game_id=game['title_id'], xbe_sha256=game['xbe_sha256'])


def valid_choice(option):
    values, labels = option.get('values'), option.get('labels')
    return (option.get('type') == 'choice' and isinstance(values, list) and isinstance(labels, list) and
            2 <= len(values) <= 8 and len(labels) == len(values) and len(set(values)) == len(values) and
            all(isinstance(v, str) and re.fullmatch(CHOICE_VALUE, v) for v in values) and
            all(isinstance(label, str) and label.strip() for label in labels) and
            option.get('default') in values and not set(values) & {'0', '1'})


def manifests(source, data):
    root = Path(source).resolve().parent
    result = []
    switches = declarations(data)
    target = title_target(data) if switches else None
    for switch in switches:
        path = (root / switch['manifest']).resolve(strict=True)
        if not path.is_relative_to(root):
            raise ValueError('Title mod switch manifest leaves its title')
        catalog = tomllib.loads(path.read_text(encoding='utf-8'))
        features = [f for f in catalog.get('feature', []) if f.get('id') == switch['feature']]
        options = [o for o in catalog.get('option', []) if o.get('feature') == switch['feature']]
        if (catalog.get('format_version') != 1 or catalog.get('version') != '1.0.0' or
                catalog.get('id') != switch['package'] or not supports_target(catalog.get('target'), target) or
                len(features) != 1 or features[0].get('default_enabled') is not False or
                len(options) > 1 or (options and not valid_choice(options[0]))):
            raise ValueError('Unsupported bundled title mod switch')
        if path not in result:
            result.append(path)
    return result


def environment(catalog, data):
    selected = {(f['package'], f['id']): f for f in catalog.features}
    result = {}
    for switch in declarations(data):
        feature = selected.get((switch['package'], switch['feature']))
        if feature is None or type(feature['enabled']) is not bool:
            raise ValueError('Title mod switch is missing from its catalog')
        options = feature.get('options', [])
        if len(options) > 1:
            raise ValueError('Title mod switch has more than one choice')
        if not feature['enabled']:
            value = '0'
        elif options:
            value = options[0]['value']
            if not isinstance(value, str) or not re.fullmatch(CHOICE_VALUE, value) or value in ('0', '1'):
                raise ValueError('Invalid title mod switch choice')
        else:
            value = '1'
        result[PREFIX + switch['key']] = value
    return result
