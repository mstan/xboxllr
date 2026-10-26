"""Portable-layout native preview startup; all writable state stays beside it.

Fresh local saves need no original console dumps. Original signing imports and
optional existing HDD images retain explicit admission and identity ownership.
ISO/XISO imports are private reusable game files, independent of console saves.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import secrets
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid
from contextlib import contextmanager, ExitStack

from native_mod_launcher import Catalog, atomic_json, encoded, decoded
from native_visual_mods import catalog_options, manifests, resolve
from native_save_keys import prepare as prepare_keys
from native_save_keys import prepare_virtual
from native_console_settings import prepare as prepare_settings
from create_eeprom import generate_eeprom, read_eeprom_file, validate_eeprom
from native_revisions import hashes as revision_hashes, select as select_revision, profile_relative
from native_disc import prepare_game, image_digest, ImportCancelled


def sha(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def inside(root, relative):
    relative = Path(relative)
    if relative.is_absolute() or '..' in relative.parts or ':' in str(relative):
        raise ValueError('Invalid package path')
    path = root / relative
    if (any(part.is_symlink() or getattr(part, 'is_junction', lambda: False)() for part in (path, *path.parents)
            if part.is_relative_to(root)) or not path.resolve().is_relative_to(root.resolve())):
        raise ValueError('Package path leaves its directory')
    return path


def writable_environment(root):
    data = inside(root, 'data')
    for key, name in [('TEMP', 'temp'), ('TMP', 'temp'),
                      ('LOCALAPPDATA', 'localappdata'), ('APPDATA', 'appdata')]:
        path = inside(root, 'data/' + name)
        path.mkdir(parents=True, exist_ok=True)
        os.environ[key] = str(path)
    os.environ['PYTHONDONTWRITEBYTECODE'] = '1'
    return data


def descriptor(root, *, verify=True):
    data = json.loads((root / 'release.json').read_text(encoding='utf-8'))
    if data.get('schema') != 'xboxllr.native-preview.v1':
        raise ValueError('Unsupported preview package')
    if verify:
        for name, expected in data['files'].items():
            if sha(inside(root, name)) != expected:
                raise ValueError('Package file changed: ' + name)
    return data


def checked_image(data, image):
    image = Path(image).resolve(strict=True)
    if not image.is_file() or image.stat().st_size > 256 * 1024 * 1024:
        raise ValueError('Choose the supported extracted Xbox executable')
    select_revision(data.get('_catalog', data), sha(image), image=image)
    return image


def prepare_profile(root, data, image, kernel=None, imported_eeprom=None, *, virtual_root=None):
    """Publish only a complete new profile. Existing saves/identity are untouched."""
    image = checked_image(data, prepare_game(root, data, image))
    data = select_revision(data.get('_catalog', data), sha(image))
    destination = inside(root, profile_relative(data) + '/console')
    if destination.exists() or destination.is_symlink():
        raise ValueError('A console profile already exists; keep it and its saves')
    # Validate every supplied private input before creating output files.
    from native_save_keys import kernel_key
    kernel_bytes = None
    if kernel:
        kernel = Path(kernel).resolve(strict=True)
        if kernel.stat().st_size > 1024 * 1024:
            raise ValueError('Unreviewed original kernel')
        kernel_bytes = kernel.read_bytes()
        kernel_key(kernel_bytes)
    elif imported_eeprom and virtual_root is None:
        raise ValueError('Using an original console for existing saves also needs its signing source. Select a kernel dump in Settings, or use the default new local console.')
    eeprom = read_eeprom_file(Path(imported_eeprom)) if imported_eeprom else generate_eeprom('r2')
    checked = validate_eeprom(eeprom)
    if not checked.ok:
        raise ValueError('Imported EEPROM is invalid: ' + checked.reason)
    destination.parent.mkdir(parents=True, exist_ok=True)
    candidate = Path(tempfile.mkdtemp(prefix='.console-setup-', dir=destination.parent))
    with (candidate / 'eeprom.bin').open('xb') as output:
        output.write(eeprom)
    if kernel_bytes is not None:
        prepare_keys(image.read_bytes(), kernel_bytes, eeprom, candidate / 'save-keys.bin')
        signing = 'original-source-hmac'
    else:
        seed = virtual_root if virtual_root is not None else secrets.token_bytes(32)
        prepare_virtual(image.read_bytes(), eeprom, candidate / 'save-keys.bin', root=seed)
        (candidate / 'virtual-signing-root.bin').write_bytes(seed)
        signing = 'virtual-local-v1'
    prepare_settings(eeprom, candidate / 'console-settings.bin')
    atomic_json(candidate / 'profile.json', dict(schema='xboxllr.console-preview.v1',
        xbe_sha256=data['title']['xbe_sha256'], eeprom_sha256=hashlib.sha256(eeprom).hexdigest(),
        eeprom_source='imported' if imported_eeprom else 'generated-r2',
        signing=signing, save_keys_sha256=sha(candidate / 'save-keys.bin')))
    # No recursive replacement, migration, regeneration or save deletion.
    candidate.rename(destination)
    return destination


def shared_identity(storage, image, kernel=None, imported_eeprom=None):
    """Select/create a shared native HDD folder without replacing user data."""
    storage, image = Path(storage), Path(image).resolve(strict=True)
    if not storage.is_dir() or storage.is_symlink() or getattr(storage, 'is_junction', lambda: False)():
        raise ValueError('Choose an ordinary local folder for shared storage.')
    storage = storage.resolve(strict=True)
    if str(storage).startswith('\\\\') or image.is_relative_to(storage) or storage.is_relative_to(image.parent):
        raise ValueError('Choose a separate local storage folder, outside the game files.')
    console = storage / '.xboxllr-console'
    if console.exists():
        record = json.loads((console / 'identity.json').read_text(encoding='utf-8'))
        if record.get('schema') != 'xboxllr.shared-console.v1' or sha(console / 'eeprom.bin') != record['eeprom_sha256']:
            raise ValueError('Shared storage identity is invalid; no save data was changed.')
        if imported_eeprom and sha(Path(imported_eeprom)) != record['eeprom_sha256']:
            raise ValueError('This EEPROM does not match the shared storage. Its existing identity is preserved.')
        original = record['signing'] == 'original-source-hmac'
        if original:
            kernel = kernel or record.get('kernel_source')
            if not kernel:
                raise ValueError('This shared console needs its original signing source. Select it in Settings.')
        elif kernel:
            raise ValueError('This shared console uses local saves. Keep its identity or choose separate empty storage.')
        seed = (console / 'virtual-signing-root.bin').read_bytes() if not original else None
        if seed is not None and hashlib.sha256(seed).hexdigest() != record['root_sha256']:
            raise ValueError('Shared storage signing identity changed; existing saves are preserved.')
        return kernel, console / 'eeprom.bin', seed
    if any(p.name != 'session.lock' for p in storage.iterdir()):
        raise ValueError('This folder already contains files without a matching xboxllr console identity. Choose an empty folder; existing data will not be overwritten.')
    if imported_eeprom and not kernel:
        raise ValueError('Select the matching kernel backup when importing an original EEPROM.')
    if kernel:
        from native_save_keys import kernel_key
        kernel_key(Path(kernel).read_bytes())
    eeprom = read_eeprom_file(Path(imported_eeprom)) if imported_eeprom else generate_eeprom('r2')
    checked = validate_eeprom(eeprom)
    if not checked.ok:
        raise ValueError('The selected EEPROM is invalid: ' + checked.reason)
    candidate = Path(tempfile.mkdtemp(prefix='.console-setup-', dir=storage))
    (candidate / 'eeprom.bin').write_bytes(eeprom)
    seed = None if kernel else secrets.token_bytes(32)
    record = dict(schema='xboxllr.shared-console.v1', eeprom_sha256=hashlib.sha256(eeprom).hexdigest(),
                  signing='original-source-hmac' if kernel else 'virtual-local-v1')
    if seed is not None:
        (candidate / 'virtual-signing-root.bin').write_bytes(seed)
        record['root_sha256'] = hashlib.sha256(seed).hexdigest()
    else:
        record['kernel_source'] = str(Path(kernel).resolve())
    atomic_json(candidate / 'identity.json', record)
    candidate.rename(console)
    return kernel, console / 'eeprom.bin', seed


def read_console_choice(path):
    if not path.exists():
        return dict(acknowledged=False, kernel='', eeprom='', storage='')
    if path.stat().st_size > 32768:
        raise ValueError('Console setup settings are invalid.')
    lines = path.read_text(encoding='ascii').splitlines()
    version = 1 if lines and lines[0] == 'xboxllr-console-choice 1' else 2 if lines and lines[0] == 'xboxllr-console-choice 2' else 0
    if not version or len(lines) != (6 if version == 1 else 7) or lines[-1] != 'end':
        raise ValueError('Console setup settings are incomplete.')
    values = dict(line.split(' ', 1) for line in lines[1:-1])
    keys = {'acknowledged', 'kernel', 'eeprom', 'storage'} | ({'hdd'} if version == 2 else set())
    if set(values) != keys or values['acknowledged'] not in ('0', '1'):
        raise ValueError('Console setup settings are invalid.')
    return dict(acknowledged=values['acknowledged'] == '1', **{k: decoded(values[k]) for k in keys-{'acknowledged'}})


def disk_identity(root, disk, kernel=None, eeprom=None, *, prepared=None):
    """Admit actual image bytes and a retained original-compatible signer.

    Original save formats still verify their own signatures in the title. The
    disk does not reveal which EEPROM originally signed every proprietary save;
    do not claim that metadata inspection proves that relationship.
    """
    from native_storage import Storage
    from native_save_keys import kernel_key
    disk = Path(disk)
    if disk.is_symlink() or not disk.is_file() or str(disk.resolve()).startswith('\\\\'):
        raise ValueError('Choose an existing local raw or QCOW2 HDD image file.')
    disk = disk.resolve(strict=True)
    with Storage(inside(root, 'native/xboxllr_storage.dll'), disk) as mounted:
        report, identity = mounted.inspect(), mounted.identity()
    # Only metadata/identity lives beside the selected image; game saves stay
    # inside its actual FATX partitions. Never manufacture an image here.
    owner = disk.with_name(disk.name + '.xboxllr')
    if owner.exists() and (not owner.is_dir() or owner.is_symlink() or
            getattr(owner, 'is_junction', lambda: False)()):
        raise ValueError('The HDD console profile must be an ordinary directory.')
    record_path = owner / 'identity.json'
    record = json.loads(record_path.read_text()) if record_path.exists() else None
    if record:
        if (record.get('schema') != 'xboxllr.image-console.v1' or
                record.get('disk_identity') != identity or record.get('signing') != 'original-source-hmac' or
                sha(owner / 'eeprom.bin') != record.get('eeprom_sha256')):
            raise ValueError('This disk or its console identity changed. Its existing saves were preserved.')
        if eeprom and sha(Path(eeprom)) != record['eeprom_sha256']:
            raise ValueError('The EEPROM does not match this disk profile. Existing saves were preserved.')
        eeprom = owner / 'eeprom.bin'
        kernel = kernel or record.get('kernel_source')
    if prepared:
        profile_record = json.loads((prepared / 'profile.json').read_text())
        if (not record or profile_record['signing'] != 'original-source-hmac' or
                sha(prepared / 'eeprom.bin') != record['eeprom_sha256']):
            raise ValueError('This prepared save identity does not match the selected HDD image.')
    else:
        if not kernel or not eeprom:
            raise ValueError('An existing HDD image needs your kernel dump and matching EEPROM. Select both in Settings, or leave the HDD image empty for new local saves.')
        kernel_key(Path(kernel).read_bytes())
        content = read_eeprom_file(Path(eeprom))
        checked = validate_eeprom(content)
        if not checked.ok:
            raise ValueError('EEPROM rejected: ' + checked.reason)
    if not record:
        owner.mkdir(exist_ok=True)
        if any(owner.iterdir()):
            raise ValueError('The HDD profile folder already contains unrecognized data. Nothing was replaced.')
        with (owner / 'eeprom.bin').open('xb') as output:
            output.write(content)
        record = dict(schema='xboxllr.image-console.v1', disk_identity=identity,
            eeprom_sha256=hashlib.sha256(content).hexdigest(), signing='original-source-hmac',
            kernel_source=str(Path(kernel).resolve()))
        atomic_json(record_path, record)
        eeprom = owner / 'eeprom.bin'
    return dict(path=str(disk), owner=owner, identity=identity, report=report,
                kernel=kernel, eeprom=eeprom)


def verify_profile(root, data):
    profile = inside(root, profile_relative(data) + '/console')
    record = json.loads((profile / 'profile.json').read_text(encoding='utf-8'))
    if (record.get('schema') != 'xboxllr.console-preview.v1' or
            record.get('xbe_sha256') != data['title']['xbe_sha256'] or
            sha(profile / 'eeprom.bin') != record['eeprom_sha256'] or
            sha(profile / 'save-keys.bin') != record['save_keys_sha256']):
        raise ValueError('Console identity changed; preserve this profile and select its matching files')
    blob = (profile / 'save-keys.bin').read_bytes()
    if (len(blob) != 360 or blob[:8] != b'XLLEKEY1' or
            blob[8:40].hex() != record['xbe_sha256'] or
            blob[40:72].hex() != record['eeprom_sha256']):
        raise ValueError('Signing inputs do not match this console/title')
    settings = (profile / 'console-settings.bin').read_bytes()
    if len(settings) != 44 or settings[:8] != b'XLLESET1' or settings[8:40].hex() != record['eeprom_sha256']:
        raise ValueError('Console settings do not match this identity')
    return profile


def message(title, text, flags=0):
    if os.name != 'nt':
        raise ValueError(text)
    return ctypes.windll.user32.MessageBoxW(None, text, title, flags)


@contextmanager
def session_lock(data):
    """One setup/game writer per profile; never delete another process's lock."""
    with (data / 'session.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        if os.name == 'nt':
            import msvcrt
            try:
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            except OSError as error:
                raise ValueError('This preview is already open; use its existing window') from error
        else:
            import fcntl
            try:
                fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as error:
                raise ValueError('This preview is already open') from error
        try:
            yield
        finally:
            lock.seek(0)
            if os.name == 'nt':
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock.fileno(), fcntl.LOCK_UN)


def choose_eeprom():
    from ctypes import wintypes as w
    class OpenFile(ctypes.Structure):
        _fields_ = [('size', w.DWORD), ('owner', w.HWND), ('instance', w.HINSTANCE),
            ('filter', w.LPCWSTR), ('custom', w.LPWSTR), ('custom_max', w.DWORD),
            ('filter_index', w.DWORD), ('file', w.LPWSTR), ('file_max', w.DWORD),
            ('file_title', w.LPWSTR), ('file_title_max', w.DWORD), ('initial', w.LPCWSTR),
            ('title', w.LPCWSTR), ('flags', w.DWORD), ('offset', w.WORD), ('extension', w.WORD),
            ('default_extension', w.LPCWSTR), ('data', ctypes.c_ssize_t), ('hook', ctypes.c_void_p),
            ('template', w.LPCWSTR), ('reserved', ctypes.c_void_p), ('reserved_word', w.DWORD),
            ('flags_ex', w.DWORD)]
    selected = ctypes.create_unicode_buffer(32768)
    request = OpenFile()
    request.size = ctypes.sizeof(request)
    request.filter = 'Xbox EEPROM (*.bin)\0*.bin\0All files\0*.*\0\0'
    request.file = ctypes.cast(selected, w.LPWSTR)
    request.file_max = len(selected)
    request.title = 'Import your existing Xbox EEPROM (original file is preserved)'
    request.flags = 0x1000 | 0x800 | 0x8  # file/path must exist; do not change cwd
    return Path(selected.value) if ctypes.windll.comdlg32.GetOpenFileNameW(ctypes.byref(request)) else None


def launch_game(root, data, image, profile, catalog, *, automated_seconds=None, storage=None, hdd=None):
    image = checked_image(data, image)
    data = select_revision(data.get('_catalog', data), sha(image))
    if profile != verify_profile(root, data):
        raise ValueError('Selected native revision does not match its console profile')
    state = Path(storage) if storage is not None else inside(root, profile_relative(data) + '/state')
    state.mkdir(parents=True, exist_ok=True)
    # Developer probe/replay/state overrides must not leak into an owner run.
    env = {key: value for key, value in os.environ.items() if not key.startswith('XLLE_PILOT_')}
    env.update(data['runtime'])
    env.update(XLLE_PILOT_SAVE_KEYS_FILE=str(profile / 'save-keys.bin'),
               XLLE_PILOT_CONSOLE_SETTINGS_FILE=str(profile / 'console-settings.bin'),
               XLLE_PILOT_VISIBLE='1', XLLE_PILOT_AUDIO_OUTPUT_MUTE='0', RECOMP_VOLUME=str(catalog.volume))
    if data['title'].get('name'):
        env['XLLE_PILOT_WINDOW_TITLE'] = data['title']['name']
    if hdd:
        env.update(XLLE_PILOT_HDD_IMAGE=hdd['path'], XLLE_PILOT_HDD_IDENTITY=hdd['identity'])
    for name in ('SDL_AUDIODRIVER', 'SDL_AUDIO_DRIVER'):
        env.pop(name, None)
    player = catalog.player
    if player['source'] in ('none', 'keyboard'):
        env['XLLE_PILOT_INPUT'] = player['source']
    elif player['source'] == 'gamepad':
        env.update(XLLE_PILOT_INPUT='sdl', XLLE_PILOT_INPUT_GUID=player['guid'])
    options = catalog_options(catalog, data)
    from native_input_mods import environment as input_mod_environment
    env.update(input_mod_environment(catalog))
    from native_title_switches import environment as title_mod_environment
    env.update(title_mod_environment(catalog, data))
    if data['capabilities']['visual_mods']:
        plan = root / 'data' / 'mods.plan'
        resolve(root / 'release.json', data, plan, **options, registry_available=True)
        env['XLLE_PILOT_MOD_PLAN'] = str(plan)
    if automated_seconds is not None:
        env.update(XLLE_PILOT_VISIBLE='0', XLLE_PILOT_AUDIO_OUTPUT_MUTE='1', RECOMP_VOLUME='0', SDL_AUDIODRIVER='dummy')
    command = [str(inside(root, data['executable'])), str(image),
               str(profile / 'eeprom.bin'), str(image.parent), str(state)]
    from native_diagnostics import Session
    from native_session_mods import enabled as session_mod_enabled
    input_capture = session_mod_enabled(catalog) or os.environ.get('XBOXLLR_DIAGNOSTICS') == '1'
    full_capture = data.get('capabilities', {}).get('session_replay_version') == 2 and os.environ.get('XBOXLLR_FULL_REPLAY') == '1'
    if full_capture:
        from native_session_diagnostics import FullSession
        session = FullSession(root, data, command[0], image, profile, state, catalog.selection(), env, hdd=hdd)
    else:
        session = Session(root, data, command[0], image, profile, state, catalog.selection(), env,
            enabled=input_capture, hdd=hdd)
    log = session.log_path.open('w', encoding='utf-8')
    with log:
        # Only this launch's startup/native children participate. The original
        # final boot image survives until native first-present acknowledgement.
        from native_boot_launcher import Handoff, run as run_startup
        with Handoff(root, log) as startup:
            run_startup(root, data, catalog, env, log, automated=automated_seconds is not None,
                        handoff=startup if automated_seconds is None else None, title_image=image)
            env.update(startup.environment)
            try:
                child = subprocess.Popen(command, cwd=state, env=env, stdout=log, stderr=log,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            except OSError:
                session.finish(None, reason='launch-failed')
                raise
            session.started(child)
            startup.title_started(child)
            if automated_seconds is not None:
                try:
                    code = child.wait(timeout=automated_seconds)
                except subprocess.TimeoutExpired:
                    child.terminate()
                    child.wait()
                    session.finish(child.returncode, reason='automated-time-limit')
                    return 0
            else:
                code = child.wait()
            from native_boot_launcher import finish_in_title
            finish_in_title(root, log)
            session.finish(code)
            return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--start-minimized', action='store_true')
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--image', type=Path)
    parser.add_argument('--kernel', type=Path)
    parser.add_argument('--eeprom', type=Path)
    parser.add_argument('--verify-package', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    writable = writable_environment(root)
    data = descriptor(root)
    if args.verify_package:
        print('Preview package hashes verified.')
        return 0
    if args.prepare_only:
        if not args.image:
            parser.error('--prepare-only requires --image')
        with session_lock(writable):
            prepare_profile(root, data, args.image, args.kernel, args.eeprom)
            verify_profile(root, select_revision(data, image_digest(args.image)))
        return 0
    with session_lock(writable):
        return interactive(root, data, writable, args.start_minimized)


def launcher_manifests(root, data):
    from native_boot_launcher import manifests as startup_manifests
    from native_input_mods import manifests as input_manifests
    from native_session_mods import manifests as session_manifests
    paths = (manifests(root / 'release.json', data) if data['capabilities']['visual_mods']
             else input_manifests(data))
    return paths + session_manifests(data) + startup_manifests(data)


def interactive(root, data, writable, minimized):
    config_path = writable / 'setup.json'
    config = json.loads(config_path.read_text(encoding='utf-8')) if config_path.exists() else {}
    os.environ['XLLE_LAUNCHER_LOW_POWER'] = '1'
    if minimized:
        os.environ['XLLE_LAUNCHER_START_MINIMIZED'] = '1'
    while True:
        catalog_data = data.get('_catalog', data)
        data = catalog_data
        if config.get('image'):
            try:
                data = select_revision(catalog_data, image_digest(config['image']))
            except (ValueError, OSError):
                pass  # Keep the launcher available to choose another supported XBE.
        ready = inside(root, profile_relative(data) + '/console').exists()
        if ready:
            verify_profile(root, data)
        choice_path = writable / 'console-choice.txt'
        choice = read_console_choice(choice_path)
        from native_boot_launcher import ui_state as startup_ui, manifests as startup_manifests
        from native_input_mods import manifests as input_manifests
        catalogs = launcher_manifests(root, data)
        catalog = Catalog(catalogs,
            writable / 'mod-selection.json', visibility=[root / 'mods/ui-visibility.json'],
            frame_modes=data.get('launcher', {}).get('frame_modes', []),
            defaults=data.get('launcher', {}).get('defaults', {}),
            boot=startup_ui(root, data, fallback_kernel=choice['kernel']),
            setup=dict(required=False, kernel=choice['kernel'], acknowledged=choice['acknowledged'],
                       eeprom=choice['eeprom'], storage=choice['storage'], hdd=choice.get('hdd',''), locked=ready, choice_path=str(choice_path)))
        run = writable / 'launcher' / uuid.uuid4().hex
        result, image = catalog.run(inside(root, data['launcher_executable']), run,
            title=data['title']['name'], region=data['title'].get('region', ''),
            sha=revision_hashes(catalog_data), image=config.get('image', ''), game_id=data['title'].get('game_id'))
        config = dict(image=image)
        atomic_json(config_path, config)
        if catalog.boot is not None and result in (0, 1):
            from native_boot_launcher import write_choice as write_startup_choice
            try:
                write_startup_choice(root, {k: catalog.boot[k] for k in ('enabled', 'kernel')})
            except (ValueError, OSError):
                pass  # An optional intro setting cannot block the native title.
        os.environ.pop('XLLE_LAUNCHER_START_MINIMIZED', None)
        if result != 0:
            return 0
        try:
            image = prepare_game(root, catalog_data, image)
            checked_image(data, image)
            data = select_revision(catalog_data, sha(Path(image)))
            ready = inside(root, profile_relative(data) + '/console').exists()
            choice = read_console_choice(choice_path)
            binding = inside(root, profile_relative(data) + '/console/storage.json')
            selected = json.loads(binding.read_text()) if ready and binding.exists() else dict(path=choice['storage'],hdd=choice.get('hdd',''))
            storage, hdd_path = selected['path'], selected.get('hdd','')
            with ExitStack() as locks:
                hdd = None
                if hdd_path:
                    # Image priority is explicit. Never use the folder after
                    # an image admission failure or a busy-image error.
                    hdd = disk_identity(root,hdd_path,choice['kernel'] or None,choice['eeprom'] or None,
                        prepared=inside(root, profile_relative(data) + '/console') if ready else None)
                    locks.enter_context(session_lock(hdd['owner']))
                    storage = str(hdd['owner']/'state')
                elif storage:
                    # The shared writer lease covers setup and the whole game.
                    storage = str(Path(storage).resolve(strict=True))
                    locks.enter_context(session_lock(Path(storage)))
                if not ready:
                    kernel, eeprom, seed = choice['kernel'] or None, choice['eeprom'] or None, None
                    if hdd:
                        kernel,eeprom = hdd['kernel'],hdd['eeprom']
                    elif storage:
                        kernel, eeprom, seed = shared_identity(storage, image, kernel, eeprom)
                    profile = prepare_profile(root, data, image, kernel, eeprom, virtual_root=seed)
                    atomic_json(profile / 'storage.json', dict(path=storage or '',hdd=hdd['path'] if hdd else ''))
                profile = verify_profile(root, data)
                if storage and not hdd and sha(Path(storage) / '.xboxllr-console/eeprom.bin') != sha(profile / 'eeprom.bin'):
                    raise ValueError('Shared storage belongs to another console. Its existing saves were preserved.')
                return launch_game(root, data, image, profile, catalog, storage=storage or None,hdd=hdd)
        except ImportCancelled:
            continue
        except (ValueError, OSError, KeyError) as error:
            message(data['title']['name'] + ' setup', str(error), 0x10)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError) as error:
        message('xboxllr setup', str(error), 0x10)
        raise SystemExit(2)
