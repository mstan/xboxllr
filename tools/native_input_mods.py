"""Data-only camera mods at the common host-controller report boundary.

Capture records the mapped report; strict diagnostic replay already contains
that report and must not invert it again. The selection belongs to the normal
diagnostic session identity, like the selected controller.
"""
from pathlib import Path

PACKAGE = 'xboxlle.enhancement.camera'


def manifest():
    return Path(__file__).resolve().parents[1] / 'mods/preloaded/packages' / PACKAGE / '1.0.0/manifest.toml'


def environment(catalog):
    selected = {f['id']: f['enabled'] for f in catalog.features if f['package'] == PACKAGE}
    return dict(XLLE_PILOT_CAMERA_INVERT_X=str(int(selected.get('invert-horizontal', False))),
                XLLE_PILOT_CAMERA_INVERT_Y=str(int(selected.get('invert-vertical', False))))
