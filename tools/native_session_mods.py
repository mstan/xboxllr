"""The session recorder is a bundled launch policy, never a game-code plugin."""
from pathlib import Path
import tomllib

PACKAGE = 'xboxllr.diagnostics'
FEATURE = 'session-replay'


def manifests(data):
    title = data.get('game', data.get('title', {}))
    target = dict(game_id=title.get('title_id', title.get('game_id')),
                  xbe_sha256=title.get('xbe_sha256'))
    path = Path(__file__).resolve().parents[1] / 'mods/preloaded/packages' / PACKAGE / '1.0.0/manifest.toml'
    if path.is_file() and target in tomllib.loads(path.read_text(encoding='utf-8')).get('target', []):
        return [path]
    return []


def enabled(catalog):
    return any(feature['package'] == PACKAGE and feature['id'] == FEATURE and feature['enabled']
               for feature in catalog.features)
