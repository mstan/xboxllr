"""Generic recomp-ui build and owner preference handoff for native titles."""
from pathlib import Path
import os
import subprocess
import sys

import native_launcher_ui
from native_mod_launcher import Catalog
from native_visual_mods import manifests, catalog_options

ROOT = Path(__file__).resolve().parents[1]


def binary(build):
    suffix = '.exe' if os.name == 'nt' else ''
    paths = [Path(build) / 'launcher/Release' / ('xlle_launcher' + suffix),
             Path(build) / 'launcher' / ('xlle_launcher' + suffix)]
    return next((p for p in paths if p.is_file()), paths[0])


def boot_binary(build):
    return Path(build) / 'boot/Release/xboxllr_boot_intro.exe'


def build_boot(build, cmake, generator, jobs):
    """Shared Windows presentation helper; no proprietary input is needed."""
    if os.name != 'nt':
        return None
    directory = Path(build) / 'boot'
    command = [str(cmake), '-S', str(ROOT / 'pilots/native-boot'), '-B', str(directory), '-G', generator]
    if generator.startswith('Visual Studio'):
        command.extend(['-A', 'x64'])
    else:
        command.append('-DCMAKE_BUILD_TYPE=Release')
    subprocess.run(command, check=True)
    subprocess.run([str(cmake), '--build', str(directory), '--config', 'Release', '--parallel', str(jobs)], check=True)
    path = boot_binary(build) if generator.startswith('Visual Studio') else directory / 'xboxllr_boot_intro.exe'
    if not path.is_file():
        raise ValueError('Startup helper build produced no executable')
    return path


def build_launcher(source, data, paths, build, cmake, generator, jobs):
    config = data.get('launcher', {})
    if config.get('ui_revision') != native_launcher_ui.REVISION:
        raise ValueError('Declare the reviewed launcher UI revision')
    for key in ('launcher_ui', 'sdl2_dir'):
        if key not in paths or not paths[key].is_dir():
            raise ValueError('Missing private launcher dependency: ' + key)
    git = 'C:/Program Files/Git/cmd/git.exe' if os.name == 'nt' else 'git'
    prepared = Path(build) / 'launcher-ui'
    native_launcher_ui.prepare(paths['launcher_ui'], prepared, git)
    directory = Path(build) / 'launcher'
    command = [str(cmake), '-S', str(ROOT / 'pilots/native-launcher'), '-B', str(directory), '-G', generator,
               '-DXLLE_UI_SOURCE=' + str(prepared), '-DSDL2_DIR=' + str(paths['sdl2_dir']),
               '-DPython3_EXECUTABLE=' + sys.executable,
               '-DXLLE_LAUNCHER_BOXART=', '-DXLLE_LAUNCHER_ARTWORK_NOTICE=']
    if generator.startswith('Visual Studio'):
        command += ['-A', 'x64']
    else:
        command.append('-DCMAKE_BUILD_TYPE=Release')
    for key, option in [('boxart', 'XLLE_LAUNCHER_BOXART'), ('artwork_notice', 'XLLE_LAUNCHER_ARTWORK_NOTICE')]:
        if key in config:
            command.append('-D' + option + '=' + str((Path(source).parent / config[key]).resolve()))
    subprocess.run(command, check=True)
    subprocess.run([str(cmake), '--build', str(directory), '--config', 'Release', '--parallel', str(jobs)], check=True)
    build_boot(build, cmake, generator, jobs)
    return binary(build)


def select(source, data, paths, build, run, *, force=False):
    if not binary(build).is_file():
        raise ValueError('Build the native launcher before requesting it')
    root = Path(source).parent
    catalog = Catalog(manifests(source, data), paths.get('mod_selection', root / 'local/mod-selection.json'),
                      visibility=[ROOT / 'mods/ui-visibility.json', root / 'mods/ui-visibility.json'],
                      frame_modes=data.get('launcher', {}).get('frame_modes', []),
                      defaults=data.get('launcher', {}).get('defaults', {}))
    result, image = 0, str(paths['xbe'])
    if force or not catalog.skip_launcher:
        result, image = catalog.run(binary(build), run, title=data['title']['name'],
                                    region=data['title'].get('region', ''), sha=data['title']['xbe_sha256'], image=paths['xbe'],
                                    game_id=data['title'].get('game_id'))
    if result != 0:
        return None
    player = catalog.player
    environment = dict(RECOMP_VOLUME=str(catalog.volume))
    from native_input_mods import environment as input_mod_environment
    environment.update(input_mod_environment(catalog))
    from native_title_switches import environment as title_mod_environment
    environment.update(title_mod_environment(catalog, data))
    if player['source'] in ('none', 'keyboard'):
        environment['XLLE_PILOT_INPUT'] = player['source']
    elif player['source'] == 'gamepad':
        environment.update(XLLE_PILOT_INPUT='sdl', XLLE_PILOT_INPUT_GUID=player['guid'])
    return dict(image=Path(image), environment=environment, options=catalog_options(catalog, data))
