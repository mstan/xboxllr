"""Assemble a pristine Windows preview; never bundle owned setup inputs.

This stager is not a license clearance or GitHub publication operation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tomllib
import zipfile

from windows_dependencies import inventory

ROOT = Path(__file__).resolve().parents[1]
MODULES = ('native_release_runtime', 'native_diagnostics', 'replay_native_diagnostic', 'native_mod_launcher', 'native_visual_mods',
           'native_save_keys', 'native_console_settings', 'create_eeprom', 'aot_inventory', 'native_storage')


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def copy_tree(source, destination):
    source, destination = Path(source), Path(destination)
    for path in source.rglob('*'):
        if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
            raise ValueError('Stage ordinary files only')
        if path.is_file():
            target = destination / path.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def stage(definition, runtime, launcher, cache, embed, runtime_dirs, output):
    definition, runtime, launcher, cache, embed, output = map(Path,
        (definition, runtime, launcher, cache, embed, output))
    if output.exists() or output.is_symlink():
        raise ValueError('Choose a new empty staging destination; existing data is preserved')
    source = tomllib.loads(definition.read_text(encoding='utf-8'))
    if 'title' in source:
        title = dict(source['title'])
    elif 'game' in source:
        game = source['game']
        title = dict(id='xbox-' + game['title_id'].lower(), name=game['name'], region=game['region'],
                     game_id=game['title_id'], xbe_sha256=game['xbe_sha256'])
    else:
        raise ValueError('Unsupported title metadata')
    options = {}
    for line in cache.read_text(encoding='utf-8').splitlines():
        if line.startswith('PILOT_') and ':INTERNAL=' not in line:
            key, value = line.split('=', 1)
            options[key.split(':')[0]] = value
    if (options.get('PILOT_NATIVE_OPTIMIZATION') != 'OPTIMIZED' or
            options.get('PILOT_IO_DEVICE') != 'OFF' or options.get('PILOT_SCHEDULING') != 'NATIVE'):
        raise ValueError('Only the optimized native gameplay build belongs in this preview')
    if not runtime.resolve().is_relative_to(cache.parent.resolve()):
        raise ValueError('Runtime is outside its declared build')
    closure = inventory([runtime, launcher], runtime_dirs)
    if not closure['complete']:
        raise ValueError('Missing Windows dependencies: ' + str(closure['missing']))
    # No output is created until metadata and all PE inputs have been checked.
    output.mkdir(parents=True)
    native = output / 'native'
    native.mkdir()
    for record in closure['files']:
        name = 'Launcher.exe' if Path(record['source']).resolve() == launcher.resolve() else record['name']
        shutil.copy2(record['source'], native / name)
    for folder in ('assets', 'third_party'):
        if (launcher.parent / folder).is_dir():
            copy_tree(launcher.parent / folder, native / folder)
    if (runtime.parent / 'third_party').is_dir():
        copy_tree(runtime.parent / 'third_party', native / 'third_party')
    for name in ('ARTWORK-NOTICE.md', 'XBOX-CONTROLLER-ARTWORK.md'):
        if (launcher.parent / name).is_file():
            shutil.copy2(launcher.parent / name, native / name)
    artwork = definition.parent / 'assets/launcher/boxart.png'
    if artwork.is_file():
        shutil.copy2(artwork, native / 'assets/img/boxart.png')
        shutil.copy2(artwork.with_name('NOTICE.md'), native / 'ARTWORK-NOTICE.md')
    python = output / 'python'
    python.mkdir()
    with zipfile.ZipFile(embed) as archive:
        for entry in archive.infolist():
            target = python / entry.filename
            if not target.resolve().is_relative_to(python.resolve()) or entry.is_dir():
                raise ValueError('Unsafe embedded Python archive')
            target.write_bytes(archive.read(entry))
    pth_files = list(python.glob('python*._pth'))
    if len(pth_files) != 1:
        raise ValueError('Unsupported embedded Python layout')
    pth = pth_files[0]
    pth.write_text(pth.stem + '.zip\n.\n../tools\n', encoding='ascii')
    tools = output / 'tools'
    tools.mkdir()
    for module in MODULES:
        shutil.copy2(ROOT / 'tools' / (module + '.py'), tools)
    notices = output / 'notices'
    notices.mkdir()
    for name in ('LICENSE', 'NOTICE', 'LICENSE-SCOPE.md', 'RIGHTS-NOTICE.md', 'CREDITS.md', 'THIRD_PARTY_ATTRIBUTION.md'):
        shutil.copy2(ROOT / name, notices / name)
    if (ROOT / 'LICENSES').is_dir():
        copy_tree(ROOT / 'LICENSES', notices / 'LICENSES')
    mods = {}
    available = bool(options.get('PILOT_TITLE_MOD_SOURCE'))
    if available:
        for package in ('xboxlle.enhancement.visuals', 'xboxlle.enhancement.frame-rate'):
            relative = Path('mods/preloaded/packages') / package / '1.0.0/manifest.toml'
            (output / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, output / relative)
        wide = source.get('mods', {}).get('widescreen_manifest')
        candidates = [definition.parent / wide] if wide else list(
            (definition.parent / 'mods/preloaded/packages').glob('*/1.0.0/manifest.toml'))
        for path in candidates:
            package = tomllib.loads(path.read_text(encoding='utf-8'))
            if any(feature['id'] == 'widescreen' for feature in package.get('feature', [])):
                relative = Path('mods/preloaded/packages') / package['id'] / '1.0.0/manifest.toml'
                (output / relative).parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, output / relative)
                mods = dict(widescreen_manifest=relative.as_posix(), widescreen_package=package['id'],
                            widescreen_plugin=package['plugin'][0]['id'])
        policies = [ROOT / 'mods/ui-visibility.json', definition.parent / 'mods/ui-visibility.json']
        hidden = set()
        choices = {}
        for path in policies:
            if path.is_file():
                policy = json.loads(path.read_text())
                hidden.update(policy['hidden_features'])
                for key, values in policy['hidden_choices'].items():
                    choices.setdefault(key, set()).update(values)
        (output / 'mods/ui-visibility.json').write_text(json.dumps(dict(version=1,
            hidden_features=sorted(hidden), hidden_choices={key: sorted(v) for key, v in choices.items()}), indent=2)+'\n')
    command = '@echo off\r\nsetlocal\r\n"%~dp0python\\pythonw.exe" "%~dp0tools\\native_release_runtime.py" %*\r\n'
    (output / 'Start Game.cmd').write_bytes(command.encode('ascii'))
    diagnostic_command = command.replace('setlocal\r\n', 'setlocal\r\nset "XBOXLLR_DIAGNOSTICS=1"\r\n')
    (output / 'Start with Diagnostics.cmd').write_bytes(diagnostic_command.encode('ascii'))
    (output / 'README-FIRST.txt').write_text(
        title['name'] + '\n\nOpen Start Game.cmd. The first-launch popup asks about optional Xbox files.\n'
        'Choose Skip for a new local console and automatic private save storage.\n'
        'That choice is remembered. Original saves may need their original console files;\n'
        'new local saves are not guaranteed to work on a real Xbox. Xbox Live is unavailable.\n\n'
        'Choose your game files on the main screen: select the supported extracted .xbe.\n'
        'Keep the whole game asset folder. Direct ISO import is not implemented yet.\n'
        'Game files and original console files are supplied separately by you.\n\n'
        'Optional Kernel dump, EEPROM (console identity), HDD image and Save folder are under Settings.\n'
        'An existing raw/QCOW2 HDD image takes priority. Without an image, a chosen folder\n'
        'is used; leave both blank for automatic private storage for this game only.\n'
        'No HDD image is generated. A selected image needs a valid kernel dump and matching\n'
        'EEPROM. Disk and selected signing inputs are checked before use. Original saves\n'
        'still need their matching identity and must pass the game\'s own verification.\n'
        'Close xemu or other games before sharing an image. Invalid/busy images report\n'
        'an error instead of silently switching to folder saves.\n'
        'New identity starts with empty local saves. Previous previews/saves are untouched.\n'
        'Select your controller and optional Mods in the Xbox-themed launcher.\n'
        'Setup settings, console identity and saves live in this package\'s data folder.\n'
        'Launcher keyboard bindings are stored beside Launcher.exe.\n'
        'Keep that data folder, selected save storage and any <image>.xboxllr profile across upgrades.\n'
        'Do not copy either into a release archive.\n\n'
        'Project software is GPL-3.0-or-later; third-party terms remain applicable.\n'
        'Original software, artwork and trademarks remain the property of their\n'
        'respective rights holders. See notices/RIGHTS-NOTICE.md and notices/LICENSE.\n'
        'Source and build information accompany the download on its release page.\n', encoding='utf-8')
    files = {p.relative_to(output).as_posix(): sha(p) for p in output.rglob('*') if p.is_file()}
    record = dict(schema='xboxllr.native-preview.v1', title=title,
        executable='native/' + runtime.name, launcher_executable='native/Launcher.exe',
        capabilities=dict(visual_mods=available), mods=mods,
        launcher=dict(frame_modes=source.get('launcher', {}).get('frame_modes', []),
                      defaults=source.get('launcher', {}).get('defaults', {})),
        runtime=source.get('runtime', {}), files=files,
        framework_revision=subprocess.check_output(['C:/Program Files/Git/cmd/git.exe', '-C', str(ROOT),
            'rev-parse', 'HEAD'], text=True).strip(),
        framework_dirty=bool(subprocess.check_output(['C:/Program Files/Git/cmd/git.exe', '-C', str(ROOT),
            'status', '--porcelain'], text=True).strip()),
        distribution='windows-prerelease; original content supplied by user')
    (output / 'release.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('definition', 'runtime', 'launcher', 'cache', 'embed', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    args = parser.parse_args()
    result = stage(args.definition, args.runtime, args.launcher, args.cache, args.embed,
                   args.runtime_dir, args.output)
    print(json.dumps(dict(title=result['title']['name'], files=len(result['files']))))
