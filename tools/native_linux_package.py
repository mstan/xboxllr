#!/usr/bin/env python3
"""Stage native Linux title payloads for tools/package_appimage.sh.

Inputs are built ELF applications and authored release files. Owned game,
firmware, console identity and generated game sources are never copied.
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tomllib
import zipfile

from native_release_package import ROOT, MODULES, copy_tree, copy_release_extras, sha


def elf(path):
    path = Path(path).resolve(strict=True)
    with path.open('rb') as stream:
        header = stream.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or header[18:20] != b'\x3e\x00':
        raise ValueError('Expected a native Linux x86_64 ELF: ' + str(path))
    return path


def checked_build(binary, cache):
    binary, cache = elf(binary), Path(cache).resolve(strict=True)
    if not binary.is_relative_to(cache.parent):
        raise ValueError('Runtime is outside its declared build')
    options = {}
    for line in cache.read_text().splitlines():
        if line.startswith(('PILOT_', 'CMAKE_BUILD_TYPE:')) and '=' in line:
            key, value = line.split('=', 1)
            options[key.split(':')[0]] = value
    required = dict(PILOT_NATIVE_OPTIMIZATION='OPTIMIZED', PILOT_SCHEDULING='NATIVE',
                    PILOT_IO_DEVICE='OFF', PILOT_FILES='NATIVE', PILOT_MEMORY='NATIVE',
                    PILOT_INPUT='NATIVE', PILOT_AUDIO='NATIVE', CMAKE_BUILD_TYPE='Release')
    if any(options.get(key) != value for key, value in required.items()):
        raise ValueError('Use an optimized native gameplay Release build')
    return binary, dict(executable_sha256=sha(binary), cache_sha256=sha(cache))


def title_metadata(source):
    if 'title' in source:
        return dict(source['title'])
    game = source['game']
    return dict(id='xbox-' + game['title_id'].lower(), name=game['name'], region=game['region'],
                game_id=game['title_id'], xbe_sha256=game['xbe_sha256'])


def stage(definition, runtime, cache, launcher, storage, output, revisions=(),
          source_archives=(), dependency_notices=None):
    if os.name != 'posix':
        raise ValueError('Stage AppImages on a native Linux filesystem (WSL is supported)')
    definition, output = Path(definition).resolve(strict=True), Path(output).absolute()
    if output.exists():
        raise ValueError('Choose a new staging destination; existing player data is preserved')
    source = tomllib.loads(definition.read_text())
    title = title_metadata(source)
    runtime, build = checked_build(runtime, cache)
    launcher, storage = elf(launcher), elf(storage)
    declared = source.get('revisions', {})
    if {key for key, *_ in revisions} != set(declared) or len(revisions) != len(declared):
        raise ValueError('Supply each declared native revision exactly once')
    records, binaries = [], [runtime]
    builds = {runtime.name: build}
    for key, binary, extra_cache in revisions:
        revision_file = (definition.parent / declared[key]['definition']).resolve(strict=True)
        if not revision_file.is_relative_to(definition.parent):
            raise ValueError('Revision definition leaves the title checkout')
        revision = tomllib.loads(revision_file.read_text())
        binary, proof = checked_build(binary, extra_cache)
        if revision.get('launcher') != source.get('launcher') or binary.name != revision['title']['executable']:
            raise ValueError('Revision launcher policy or executable identity differs')
        records.append(dict(id=key, title=title_metadata(revision), executable='native/' + binary.name))
        builds[binary.name] = proof
        binaries.append(binary)
    from native_revisions import entries
    entries(dict(title=title, revisions=records))
    output.mkdir(parents=True)
    native = output / 'native'
    native.mkdir()
    for binary in binaries:
        shutil.copy2(binary, native / binary.name)
        os.chmod(native / binary.name, 0o755)
    shutil.copy2(launcher, native / 'Launcher')
    os.chmod(native / 'Launcher', 0o755)
    shutil.copy2(storage, native / 'libxboxllr_storage.so')
    for base in (launcher.parent, runtime.parent):
        for folder in ('assets', 'third_party'):
            if (base / folder).is_dir():
                copy_tree(base / folder, native / folder)
        for name in ('ARTWORK-NOTICE.md', 'XBOX-CONTROLLER-ARTWORK.md'):
            if (base / name).is_file():
                shutil.copy2(base / name, native / name)
    artwork = definition.parent / 'assets/launcher/boxart.png'
    if artwork.is_file():
        (native / 'assets/img').mkdir(parents=True, exist_ok=True)
        shutil.copy2(artwork, native / 'assets/img/boxart.png')
        shutil.copy2(artwork.with_name('NOTICE.md'), native / 'ARTWORK-NOTICE.md')
    tools = output / 'tools'
    tools.mkdir()
    for module in (*MODULES, 'native_linux_entry'):
        shutil.copy2(ROOT / 'tools' / (module + '.py'), tools)
    notices = output / 'notices'
    notices.mkdir()
    for name in ('LICENSE', 'NOTICE', 'LICENSE-SCOPE.md', 'RIGHTS-NOTICE.md', 'CREDITS.md', 'THIRD_PARTY_ATTRIBUTION.md'):
        shutil.copy2(ROOT / name, notices / name)
    copy_tree(ROOT / 'LICENSES', notices / 'LICENSES')
    if not source_archives or dependency_notices is None:
        raise ValueError('Supply matching project/dependency source archives and native dependency notices')
    for archive in source_archives:
        archive = Path(archive).resolve(strict=True)
        with zipfile.ZipFile(archive) as zipped:
            if zipped.testzip() is not None or not any(name in zipped.namelist() for name in
                    ('SOURCE-RELEASE.json', 'DEPENDENCY-SOURCES.json')):
                raise ValueError('Expected a verified project or prepared-dependency source archive')
        destination = output / 'sources' / archive.name
        destination.parent.mkdir(exist_ok=True)
        if destination.exists():
            raise ValueError('Duplicate source archive name')
        shutil.copy2(archive, destination)
    copy_tree(Path(dependency_notices).resolve(strict=True), notices / 'native-dependencies')
    from native_input_mods import manifests as input_manifests
    from native_session_mods import manifests as session_manifests
    for path in input_manifests(source) + session_manifests(source):
        relative = path.relative_to(ROOT)
        (output / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, output / relative)
    mods = {}
    from native_title_switches import declarations, manifests as switch_manifests
    for package in ('xboxlle.enhancement.visuals', 'xboxlle.enhancement.frame-rate'):
        relative = Path('mods/preloaded/packages') / package / '1.0.0/manifest.toml'
        (output / relative).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, output / relative)
    wide = source.get('mods', {}).get('widescreen_manifest')
    candidates = [definition.parent / wide] if wide else list(
        (definition.parent / 'mods/preloaded/packages').glob('*/1.0.0/manifest.toml'))
    for path in candidates:
        package = tomllib.loads(path.read_text())
        if any(f['id'] == 'widescreen' for f in package.get('feature', [])):
            relative = Path('mods/preloaded/packages') / package['id'] / '1.0.0/manifest.toml'
            (output / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, output / relative)
            mods.update(widescreen_manifest=relative.as_posix(), widescreen_package=package['id'],
                        widescreen_plugin=package['plugin'][0]['id'])
    switches = declarations(source)
    if switches:
        for path in switch_manifests(definition, source):
            relative = path.relative_to(definition.parent)
            (output / relative).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, output / relative)
        mods['switches'] = switches
    hidden, choices = set(), {}
    for path in (ROOT / 'mods/ui-visibility.json', definition.parent / 'mods/ui-visibility.json'):
        if path.is_file():
            policy = json.loads(path.read_text())
            hidden.update(policy['hidden_features'])
            for key, values in policy['hidden_choices'].items():
                choices.setdefault(key, set()).update(values)
    (output / 'mods/ui-visibility.json').write_text(json.dumps(dict(version=1, hidden_features=sorted(hidden),
        hidden_choices={key: sorted(values) for key, values in choices.items()}), indent=2) + '\n')
    copy_release_extras(definition, source, output)
    launcher_options = dict(frame_modes=source.get('launcher', {}).get('frame_modes', []),
                            defaults=source.get('launcher', {}).get('defaults', {}))
    if source.get('launcher', {}).get('sharp_presentation'):
        launcher_options['sharp_presentation'] = True
    content_import = source.get('launcher', {}).get('image_import')
    if content_import is not None:
        if content_import != {'codec': 'tools/graffiti_native.py'} or title['game_id'] != '5345000A':
            raise ValueError('Unreviewed title image importer')
        shutil.copy2(definition.parent / 'tools/graffiti_native.py', tools / 'title_image_import.py')
        launcher_options['image_import'] = {'codec': 'tools/title_image_import.py'}
    app_id = 'xbox-' + title['game_id'].lower()
    (output / 'README-FIRST.txt').write_text(title['name'] + '\n\n'
        'Run the AppImage. Choose Skip for a new local console, then select your owned game files.\n'
        'An extracted .xbe requires the complete asset folder; ISO/XISO imports are prepared once.\n'
        'Original game/console files are supplied separately by you. Xbox Live is unavailable.\n'
        'Saves, settings and imports persist in ${XDG_DATA_HOME:-~/.local/share}/xboxllr/' + app_id + '/data.\n'
        'Keep that directory when replacing the AppImage. External selected saves/HDD images stay in place.\n'
        'Original saves require their original console signing identity. New local saves use a private local identity.\n'
        'Native Linux x86_64, Vulkan 1.3 graphics. No Wine or Proton runtime.\n'
        'The optional Xbox firmware startup animation is unavailable in this Linux build.\n'
        'Input recording is under Mods > Crash and input logs.\n'
        'After a crash, the message gives the path to a shareable diagnostic ZIP.\n'
        'Attach that ZIP to your bug report; do not upload PRIVATE-DO-NOT-SHARE.\n'
        'Matching project and prepared dependency source archives are inside sources/.\n'
        'See notices for project and third-party terms.\n')
    record = dict(schema='xboxllr.native-preview.v1', platform='linux-x86_64', application_id=app_id,
        title=title, revisions=records, executable='native/' + runtime.name, launcher_executable='native/Launcher',
        storage_library='native/libxboxllr_storage.so', startup={},
        capabilities=dict(visual_mods=True, startup_intro=False, session_replay_version=2), mods=mods,
        launcher=launcher_options, runtime=source.get('runtime', {}), builds=builds,
        framework_revision=subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        framework_dirty=bool(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain'], text=True).strip()),
        distribution='native-linux-appimage; original content supplied by user')
    record['files'] = {p.relative_to(output).as_posix(): sha(p) for p in output.rglob('*') if p.is_file()}
    (output / 'release.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('definition', 'runtime', 'cache', 'launcher', 'storage', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--revision', nargs=3, action='append', default=[], metavar=('ID', 'RUNTIME', 'CACHE'))
    parser.add_argument('--source-archive', type=Path, action='append', required=True)
    parser.add_argument('--dependency-notices', type=Path, required=True)
    args = parser.parse_args()
    result = stage(args.definition, args.runtime, args.cache, args.launcher, args.storage, args.output,
                   args.revision, args.source_archive, args.dependency_notices)
    print(json.dumps(dict(title=result['title']['name'], files=len(result['files']))))
