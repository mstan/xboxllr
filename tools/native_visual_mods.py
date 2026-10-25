"""Resolve bundled visual features to the shared, data-only native mod ABI.

Title-specific camera/placement code is a statically reviewed registry. Packages
cannot supply code, and unsupported frame modes never silently become original.
"""
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def aspect_value(value):
    adaptive = value == 'adaptive'
    ratio = '16:9' if adaptive else value
    if not isinstance(ratio, str) or not re.fullmatch(r'[1-9][0-9]{0,4}:[1-9][0-9]{0,4}', ratio):
        raise ValueError('Aspect must be adaptive or numerator:denominator')
    n, d = map(int, ratio.split(':'))
    if max(n, d) > 65536 or not Fraction(4, 3) <= Fraction(n, d) <= Fraction(32, 9):
        raise ValueError('Aspect must be from 4:3 through 32:9')
    return Fraction(n, d), adaptive


def manifests(source, data):
    from native_input_mods import manifests as input_manifests
    paths = [ROOT / 'mods/preloaded/packages' / pid / '1.0.0/manifest.toml'
             for pid in ('xboxlle.enhancement.visuals', 'xboxlle.enhancement.frame-rate')]
    paths.extend(input_manifests(data))
    from native_session_mods import manifests as session_manifests
    paths.extend(session_manifests(data))
    wide = data.get('mods', {}).get('widescreen_manifest')
    if wide:
        paths.insert(0, (Path(source).parent / wide).resolve())
    from native_title_switches import manifests as switch_manifests
    paths.extend(switch_manifests(source, data))
    return paths


def resolve(source, data, output, *, aspect=None, height=0, supersample=1,
            antialiasing='original', anisotropy=1, frame_mode='original', frame_rate=60,
            registry_available=None):
    if (any(type(v) is not int for v in (height, supersample, anisotropy, frame_rate))
        or height not in (0, 1080, 1440, 1800, 2160) or supersample not in (1, 2, 3)
        or antialiasing not in ('original', 'fxaa') or anisotropy not in (1, 2, 4, 8, 16)
        or frame_rate not in (60, 120) or frame_mode not in ('original', 'interpolated', 'actual')):
        raise ValueError('Invalid visual mod option')
    if frame_mode != 'original' and frame_mode not in data.get('launcher', {}).get('frame_modes', []):
        raise ValueError('Frame mode is not qualified for this title')
    if registry_available is not None and type(registry_available) is not bool:
        raise ValueError('Invalid compiled visual registry capability')
    if not (data.get('mods', {}).get('registry') if registry_available is None else registry_available):
        raise ValueError('This title does not declare a trusted visual mod registry')
    sha = data['title']['xbe_sha256']
    ratio, adaptive = aspect_value(aspect) if aspect is not None else (Fraction(4, 3), False)
    canvas_height = height or 480
    canvas_width = (canvas_height * ratio.numerator + ratio.denominator - 1) // ratio.denominator if height else 640
    if max(canvas_width, canvas_height) * supersample > 16384:
        raise ValueError('Selected resolution/aspect/supersampling exceeds the native texture limit')
    operations, catalogs = [], {}
    selected = {tomllib.loads(p.read_text(encoding='utf-8'))['id']: p for p in manifests(source, data)}

    def catalog(pid, target, features, plugins):
        path = selected[pid]
        manifest = tomllib.loads(path.read_text(encoding='utf-8'))
        from native_revisions import supports_target
        targets_match = (supports_target(manifest.get('target'), target[0])
                         if len(target) == 1 and set(target[0]) == {'game_id', 'xbe_sha256'}
                         else manifest.get('target') == target)
        if (manifest.get('format_version') != 1 or manifest.get('id') != pid or manifest.get('version') != '1.0.0'
            or not targets_match or [f['id'] for f in manifest['feature']] != features
            or any(f['default_enabled'] is not False for f in manifest['feature']) or manifest.get('plugin') != plugins):
            raise ValueError('Unsupported bundled visual catalog: ' + pid)
        catalogs[pid] = hashlib.sha256(path.read_bytes()).hexdigest()

    if aspect is not None:
        mods = data.get('mods', {})
        pid, plugin = mods.get('widescreen_package'), mods.get('widescreen_plugin')
        if not pid or not plugin or pid not in selected or not re.fullmatch(r'[a-z][a-z0-9_.-]{0,94}', plugin):
            raise ValueError('This title does not declare a trusted widescreen plugin')
        catalog(pid, [dict(game_id=data['title']['game_id'], xbe_sha256=sha)], ['widescreen'],
                [dict(feature='widescreen', id=plugin)])
        operations.append(f'operation aspect 1 {plugin} {ratio.numerator} {ratio.denominator} {int(adaptive)}')
    if height or supersample != 1 or antialiasing != 'original' or anisotropy != 1:
        features = ['resolution', 'antialiasing', 'supersampling', 'filtering']
        catalog('xboxlle.enhancement.visuals', [dict(system='xbox', native_title_mod_abi=2)], features,
                [dict(feature=f, id='xboxlle.visuals.' + f) for f in features])
    if height: operations.append(f'operation resolution 1 {height}')
    if supersample != 1: operations.append(f'operation supersampling 1 {supersample}')
    if antialiasing != 'original': operations.append('operation antialiasing 1 1')
    if anisotropy != 1: operations.append(f'operation filtering 1 {anisotropy}')
    if frame_mode != 'original':
        catalog('xboxlle.enhancement.frame-rate', [dict(system='xbox', native_title_mod_abi=2)], ['frame-rate'],
                [dict(feature='frame-rate', id='xboxlle.frame-rate')])
        operations.append(f'operation frame-rate 1 {1 if frame_mode == "interpolated" else 2} {frame_rate}')
    plan = f'xlle-mod-plan 2\nidentity {sha}\n' + ''.join(op + '\n' for op in operations) + 'end\n'
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + '.tmp')
    temporary.write_text(plan, encoding='ascii', newline='\n')
    temporary.replace(output)
    result = dict(format_version=2, xbe_sha256=sha, operations=operations,
                  plan_sha256=hashlib.sha256(plan.encode('ascii')).hexdigest(), catalog_sha256=catalogs,
                  internal_canvas=[canvas_width * supersample, canvas_height * supersample])
    output.with_suffix(output.suffix + '.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def catalog_options(catalog, data):
    values = catalog.values()
    quality, frames = 'xboxlle.enhancement.visuals', 'xboxlle.enhancement.frame-rate'
    wide = data.get('mods', {}).get('widescreen_package')
    return dict(aspect=values.get((wide, 'widescreen', 'aspect')),
                height=int(values.get((quality, 'resolution', 'height'), '0')),
                supersample=int(values.get((quality, 'supersampling', 'axis'), '1')),
                antialiasing=values.get((quality, 'antialiasing', 'method'), 'original'),
                anisotropy=int(values.get((quality, 'filtering', 'anisotropy'), '1')),
                frame_mode=values.get((frames, 'frame-rate', 'mode'), 'original'),
                frame_rate=int(values.get((frames, 'frame-rate', 'rate'), '60')))
