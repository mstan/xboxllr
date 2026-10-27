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
MODULES = ('native_content_import', 'native_texture_replacements', 'native_release_runtime', 'native_revisions', 'native_boot_launcher', 'native_boot_assets', 'native_diagnostics', 'native_crash_report', 'replay_native_diagnostic', 'native_session_diagnostics', 'replay_native_session', 'native_session_mods', 'native_mod_launcher', 'native_visual_mods', 'native_input_mods', 'native_title_switches',
           'native_save_keys', 'native_console_settings', 'create_eeprom', 'aot_inventory', 'native_storage', 'xbe_identity',
           'native_disc', 'native_disc_progress')
# Narrow native VP build closure. No machine/CPU executable, original input,
# generated title body or private fixture is part of the startup source kit.
BOOT_SHARED_SOURCES = (
    'cmake/ApuRate.cmake', 'cmake/ApuAdpcm.cmake',
    'LICENSES/libsamplerate-BSD.txt', 'src/third_party/libsamplerate/README.md',
    'src/third_party/dr_wav/dr_wav.h', 'src/third_party/dr_wav/LICENSE',
    'src/third_party/dr_wav/README.md',
    *(f'src/machine/{name}' for name in (
        'apu.hpp', 'apu_adpcm.c', 'apu_adpcm.h', 'apu_clock.hpp',
        'apu_startup_config.hpp', 'apu_dsp_host.hpp', 'apu_gp_dma.hpp',
        'apu_dsp_layout.hpp', 'apu_envelope_curve.hpp', 'apu_dls2_lpf.hpp',
        'apu_signal_model.hpp', 'apu_ssl.hpp', 'apu_observation.hpp',
        'apu_mix_word.hpp', 'mmio_device.hpp', 'io_device.hpp',
        'apu_rate.cpp', 'apu_envelope.cpp', 'apu_voice_quantum.cpp', 'apu_filter.cpp')),
)


def startup_source_files():
    """Explicit shared closure plus immediate authored/core source directories."""
    files = [ROOT / name for name in ('LICENSE', 'NOTICE', 'LICENSE-SCOPE.md',
             'tools/native_boot_assets.py', 'docs/NATIVE-BOOT-INTRO.md', *BOOT_SHARED_SOURCES)]
    for folder in ('pilots/native-boot', 'src/third_party/xbox-boot-re',
                   'src/third_party/xbox-boot-re/core/include', 'src/third_party/xbox-boot-re/core/src'):
        files.extend(p for p in (ROOT / folder).iterdir() if p.is_file() and
                     (p.suffix.lower() in ('.c', '.cpp', '.h', '.md', '.txt') or p.name == 'LICENSE'))
    result = sorted(set(files))
    if any(not p.is_file() or p.is_symlink() or
           getattr(p, 'is_junction', lambda: False)() for p in result):
        raise ValueError('Startup sources must be present ordinary files')
    return result


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


def copy_release_extras(definition, source, output):
    """Copy only explicitly declared authored extras, never a user's art folder."""
    names = source.get('release', {}).get('extras', [])
    if (not isinstance(names, list) or len(names) > 32 or not all(isinstance(name, str) for name in names)
            or len(set(names)) != len(names)):
        raise ValueError('Invalid release extras list')
    base = Path(definition).resolve().parent
    total = 0
    checked = []
    for name in names:
        if not isinstance(name, str) or '\\' in name or ':' in name or '\0' in name:
            raise ValueError('Invalid release extra path')
        relative = Path(name)
        if (relative.is_absolute() or len(relative.parts) < 2 or relative.parts[0] != 'extras'
                or '..' in relative.parts or relative.suffix.lower() not in ('.png', '.md', '.txt')):
            raise ValueError('Release extras must be authored images/notices under extras/')
        path = base
        for part in relative.parts:
            path = path / part
            if path.is_symlink() or getattr(path, 'is_junction', lambda: False)():
                raise ValueError('Release extras must be ordinary files')
        if not path.is_file() or not path.resolve().is_relative_to(base):
            raise ValueError('Missing release extra')
        total += path.stat().st_size
        if total > 16 * 1024 * 1024:
            raise ValueError('Release extras exceed the 16 MiB budget')
        checked.append((path, relative))
    for path, relative in checked:
        target = Path(output) / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    return names


def stage(definition, runtime, launcher, cache, embed, runtime_dirs, output, *, boot_intro, revisions=()):
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
    intro_default = source.get('launcher', {}).get('startup_intro') is True
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
    extra_runtimes = []
    revision_records = []
    declared = source.get('revisions', {})
    if {entry[0] for entry in revisions} != set(declared) or len(revisions) != len(declared):
        raise ValueError('Supply each declared native revision build exactly once')
    for key, extra_runtime, extra_cache in revisions:
        extra_runtime, extra_cache = Path(extra_runtime), Path(extra_cache)
        relative = Path(declared[key]['definition'])
        revision_file = (definition.parent / relative).resolve()
        if relative.is_absolute() or not revision_file.is_relative_to(definition.parent.resolve()):
            raise ValueError('Revision definition leaves the title checkout')
        revision = tomllib.loads(revision_file.read_text(encoding='utf-8'))
        receipt = json.loads((extra_cache.parent / 'native-title-build.json').read_text(encoding='utf-8'))
        if (receipt.get('title') != revision['title']['id'] or
                receipt.get('executable_sha256') != sha(extra_runtime)):
            raise ValueError('Native revision executable differs from its build receipt')
        extra_options = {}
        for line in extra_cache.read_text(encoding='utf-8').splitlines():
            if line.startswith('PILOT_') and ':INTERNAL=' not in line:
                name, value = line.split('=', 1)
                extra_options[name.split(':')[0]] = value
        if (not extra_runtime.resolve().is_relative_to(extra_cache.parent.resolve()) or
                extra_runtime.name != revision['title']['executable'] + '.exe' or
                extra_options.get('PILOT_NATIVE_OPTIMIZATION') != 'OPTIMIZED' or
                extra_options.get('PILOT_IO_DEVICE') != 'OFF' or
                extra_options.get('PILOT_SCHEDULING') != 'NATIVE' or
                bool(extra_options.get('PILOT_TITLE_MOD_SOURCE')) != bool(options.get('PILOT_TITLE_MOD_SOURCE')) or
                revision.get('launcher') != source.get('launcher')):
            raise ValueError('Native revision build differs from the qualified package policy')
        extra_runtimes.append(extra_runtime)
        revision_records.append(dict(id=key, title=revision['title'], executable='native/' + extra_runtime.name))
    from native_revisions import entries
    entries(dict(title=title, revisions=revision_records))
    boot_intro = Path(boot_intro)
    boot_module = boot_intro.with_name('XboxStartup.dll')
    if not boot_module.is_file():
        raise ValueError('Build the same-window startup module beside its helper')
    launch_stub = launcher.with_name('xboxllr_start.exe')
    if not launch_stub.is_file():
        raise ValueError('Build the windowed release entry point beside the launcher')
    closure = inventory([runtime, *extra_runtimes, launcher, boot_intro, boot_module, launch_stub], runtime_dirs)
    if not closure['complete']:
        raise ValueError('Missing Windows dependencies: ' + str(closure['missing']))
    # No output is created until metadata and all PE inputs have been checked.
    output.mkdir(parents=True)
    native = output / 'native'
    native.mkdir()
    for record in closure['files']:
        origin = Path(record['source']).resolve()
        if origin == launch_stub.resolve():
            shutil.copy2(record['source'], output / 'Start Game.exe')
            continue
        name = ('Launcher.exe' if origin == launcher.resolve() else
                'XboxStartup.exe' if origin == boot_intro.resolve() else record['name'])
        shutil.copy2(record['source'], native / name)
    for folder in ('assets', 'third_party'):
        if (launcher.parent / folder).is_dir():
            copy_tree(launcher.parent / folder, native / folder)
    if (runtime.parent / 'third_party').is_dir():
        copy_tree(runtime.parent / 'third_party', native / 'third_party')
    if (boot_intro.parent / 'third_party').is_dir():
        copy_tree(boot_intro.parent / 'third_party', native / 'third_party')
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
    # Self-contained authored startup source and retained MIT notice accompany
    # the helper. Original pack/kernel/sample/model bytes are never copied.
    boot_source = output / 'source/native-startup'
    for path in startup_source_files():
        target = boot_source / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    (notices / 'xbox-boot-re').mkdir()
    shutil.copy2(ROOT / 'src/third_party/xbox-boot-re/LICENSE', notices / 'xbox-boot-re/LICENSE')
    mods = {}
    available = bool(options.get('PILOT_TITLE_MOD_SOURCE'))
    # Startup is a launcher presentation mod, available without a visual registry.
    from native_boot_launcher import MOD_PACKAGE
    relative = Path('mods/preloaded/packages') / MOD_PACKAGE / '1.0.0/manifest.toml'
    (output / relative).parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / relative, output / relative)
    from native_input_mods import manifests as input_manifests
    from native_session_mods import manifests as session_manifests
    for path in input_manifests(source) + session_manifests(source):
        relative = path.relative_to(ROOT)
        (output / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, output / relative)
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
    from native_title_switches import declarations as switch_declarations, manifests as switch_manifests
    switches = switch_declarations(source)
    if switches:
        if not available:
            raise ValueError('Title switches require a compiled trusted registry')
        for path in switch_manifests(definition, source):
            relative = path.relative_to(definition.resolve().parent)
            (output / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, output / relative)
        mods['switches'] = switches
    command = '@echo off\r\nsetlocal\r\nstart "" "%~dp0Start Game.exe" %*\r\n'
    (output / 'Start Game.cmd').write_bytes(command.encode('ascii'))
    diagnostic_command = command.replace('setlocal\r\n', 'setlocal\r\nset "XBOXLLR_DIAGNOSTICS=1"\r\n')
    (output / 'Start with Diagnostics.cmd').write_bytes(diagnostic_command.encode('ascii'))
    (output / 'README-FIRST.txt').write_text(
        title['name'] + '\n\nOpen Start Game.exe. The first-launch popup asks about optional Xbox files.\n'
        'Choose Skip for a new local console and automatic private save storage.\n'
        'That choice is remembered. Original saves may need their original console files;\n'
        'new local saves are not guaranteed to work on a real Xbox. Xbox Live is unavailable.\n\n'
        'Choose your game files on the main screen: select an Xbox .iso/.xiso or supported extracted .xbe.\n'
        'Disc images are imported once with progress and Cancel, then the prepared game files are reused.\n'
        'Keep data/discs across upgrades to avoid another import. Imports need extra space beside this build.\n'
        'Extracted .xbe files still need their complete game asset folder.\n'
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
        'If the game stops unexpectedly, the crash report window explains the recorded failure.\n'
        'Add an optional description and choose Copy diagnostic ZIP to copy a shareable file.\n'
        'Use Open report folder if your chat does not accept pasted files. The ZIP excludes\n'
        'console keys and saved games; private reproduction material stays local.\n\n'
        'Project software is GPL-3.0-or-later; third-party terms remain applicable.\n'
        'Original software, artwork and trademarks remain the property of their\n'
        'respective rights holders. See notices/RIGHTS-NOTICE.md and notices/LICENSE.\n'
        'Source and build information accompany the download on its release page.\n', encoding='utf-8')
    with (output / 'README-FIRST.txt').open('a', encoding='utf-8') as readme:
        if revision_records:
            readme.write('\nSupported original revisions are selected automatically by the executable hash.\n'
                'Default console profiles and saves are separate for each revision. Switching\n'
                'revisions preserves the existing profile and progress; keep the whole data folder.\n')
        readme.write('\nStartup intro: optional under Settings. Default for this title: ' + ('on' if intro_default else 'off') + '.\n'
            'Mods > Xbox Startup > Skip startup animation goes straight to the title.\n'
            'The skip mod is off by default, so Dashboard plays the full sequence.\n'
            'Startup and the title use one window, preserving its position and size.\n'
            'It uses startup data from your compatible complete kernel dump, prepared privately.\n'
            'A short save-signing dump is not enough. Missing/unsupported startup data launches\n'
            'the title directly. No boot assets are included, and console identity/saves stay unchanged.\n'
            'This is a native animation reconstruction, not full firmware execution.\n')
    content_import = source.get('launcher', {}).get('image_import')
    if content_import is not None:
        if content_import != {'codec': 'tools/graffiti_native.py'} or title.get('game_id') != '5345000A':
            raise ValueError('Unreviewed title image-import declaration')
        codec = definition.parent / 'tools/graffiti_native.py'
        if codec.is_symlink() or not codec.is_file(): raise ValueError('Missing authored title image importer')
        shutil.copy2(codec, output / 'tools/title_image_import.py')
    copy_release_extras(definition, source, output)
    files = {p.relative_to(output).as_posix(): sha(p) for p in output.rglob('*') if p.is_file()}
    record = dict(schema='xboxllr.native-preview.v1', title=title, revisions=revision_records,
        executable='native/' + runtime.name, launcher_executable='native/Launcher.exe',
        startup=dict(executable='native/XboxStartup.exe', module='native/XboxStartup.dll',
                     default_enabled=intro_default),
        capabilities=dict(visual_mods=available, startup_intro=True, session_replay_version=2), mods=mods,
        launcher=dict(frame_modes=source.get('launcher', {}).get('frame_modes', []),
                      defaults=source.get('launcher', {}).get('defaults', {})),
        runtime=source.get('runtime', {}), files=files,
        framework_revision=subprocess.check_output(['C:/Program Files/Git/cmd/git.exe', '-C', str(ROOT),
            'rev-parse', 'HEAD'], text=True).strip(),
        framework_dirty=bool(subprocess.check_output(['C:/Program Files/Git/cmd/git.exe', '-C', str(ROOT),
            'status', '--porcelain'], text=True).strip()),
        distribution='windows-prerelease; original content supplied by user')
    if content_import is not None:
        record['launcher']['image_import'] = {'codec': 'tools/title_image_import.py'}
    if source.get('launcher', {}).get('sharp_presentation'):
        record['launcher']['sharp_presentation'] = True
    (output / 'release.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('definition', 'runtime', 'launcher', 'cache', 'embed', 'output', 'boot-intro'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--revision', nargs=3, action='append', default=[], metavar=('ID', 'RUNTIME', 'CACHE'))
    args = parser.parse_args()
    result = stage(args.definition, args.runtime, args.launcher, args.cache, args.embed,
                   args.runtime_dir, args.output, boot_intro=args.boot_intro, revisions=args.revision)
    print(json.dumps(dict(title=result['title']['name'], files=len(result['files']))))
