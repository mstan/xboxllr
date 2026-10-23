"""Data-only camera mods at the common host-controller report boundary.

Capture records the mapped report; strict diagnostic replay already contains
that report and must not invert it again. The selection belongs to the normal
diagnostic session identity, like the selected controller.
"""
from pathlib import Path
import tomllib

PACKAGE = 'xboxlle.enhancement.camera'


def manifest():
    return Path(__file__).resolve().parents[1] / 'mods/preloaded/packages' / PACKAGE / '1.0.0/manifest.toml'


def manifests(data):
    """Expose camera controls only for an explicitly admitted original title.

    The input implementation remains reusable. The data-only mod catalog owns
    its qualified title/revision list; a shared launcher is not qualification.
    """
    title = data.get('game', data.get('title', {}))
    identity = dict(game_id=title.get('title_id', title.get('game_id')),
                    xbe_sha256=title.get('xbe_sha256'))
    if not all(isinstance(value, str) and value for value in identity.values()):
        return []
    path = manifest()
    # Unadmitted packages deliberately omit this manifest entirely.
    if not path.is_file():
        return []
    catalog = tomllib.loads(path.read_text(encoding='utf-8'))
    return [path] if identity in catalog.get('target', []) else []


def environment(catalog):
    selected = {f['id']: f['enabled'] for f in catalog.features if f['package'] == PACKAGE}
    return dict(XLLE_PILOT_CAMERA_INVERT_X=str(int(selected.get('invert-horizontal', False))),
                XLLE_PILOT_CAMERA_INVERT_Y=str(int(selected.get('invert-vertical', False))))
