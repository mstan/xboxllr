"""Optional presentation stage before a fresh native title, without console writes.

Only private boot settings/cache/logs are writable here. Original kernel inputs
are read-only; EEPROM, HDD ownership and game save identity are never consulted
or changed. Missing assets or helper failure preserve the normal direct launch.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

from native_boot_assets import AssetError, extract, read_kernel, sha256
from native_mod_launcher import atomic_json, decoded, encoded

PACK_BYTES = 72224
PACK_SHA256 = "6c6df777c291a447a2de2693e3edfc4a69e0c4151548c8e058b512669a09233f"
TIMEOUT_SECONDS = 25


def safe_path(root, relative):
    root = Path(root).resolve()
    path = root / relative
    if (not path.resolve().is_relative_to(root) or any(
            p.is_symlink() or getattr(p, 'is_junction', lambda: False)()
            for p in (path, *path.parents) if p.is_relative_to(root))):
        raise ValueError('Startup paths leave this application')
    return path


def default_enabled(data):
    return data.get('startup', {}).get('default_enabled') is True


def read_choice(root, data):
    path = safe_path(root, 'data/boot-choice.txt')
    if not path.exists():
        return dict(enabled=default_enabled(data), kernel='')
    if path.stat().st_size > 16384:
        raise ValueError('Startup settings are invalid')
    lines = path.read_text(encoding='ascii').splitlines()
    if (len(lines) != 4 or lines[0] != 'xboxllr-boot-choice 1' or lines[-1] != 'end'
            or lines[1] not in ('enabled 0', 'enabled 1') or not lines[2].startswith('kernel ')):
        raise ValueError('Startup settings are invalid')
    kernel = decoded(lines[2][7:])
    if len(kernel.encode('utf-8')) > 4095 or '\0' in kernel:
        raise ValueError('Startup kernel path is invalid')
    return dict(enabled=lines[1] == 'enabled 1', kernel=kernel)


def write_choice(root, choice):
    if (set(choice) != {'enabled', 'kernel'} or type(choice['enabled']) is not bool
            or not isinstance(choice['kernel'], str) or '\0' in choice['kernel']
            or len(choice['kernel'].encode('utf-8')) > 4095):
        raise ValueError('Invalid startup choice')
    path = safe_path(root, 'data/boot-choice.txt')
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = safe_path(root, 'data/boot-choice.txt.tmp')
    temporary.write_text(f'xboxllr-boot-choice 1\nenabled {int(choice["enabled"])}\n'
                         f'kernel {encoded(choice["kernel"])}\nend\n', encoding='ascii')
    temporary.replace(path)


def valid_pack(path):
    if not path.is_file() or path.stat().st_size != PACK_BYTES:
        return False
    return sha256(path.read_bytes()) == PACK_SHA256


def prepare(root, data, choice, *, fallback_kernel=''):
    """Return concise, nonfatal startup availability and a private pack path."""
    if not choice['enabled']:
        return dict(status='disabled', message='Startup intro is off.', pack=None)
    startup = data.get('startup', {})
    if not startup.get('executable'):
        return dict(status='unavailable', message='This package has no startup intro helper. The title launches directly.', pack=None)
    helper = safe_path(root, startup['executable'])
    if not helper.is_file():
        return dict(status='unavailable', message='Startup intro helper is unavailable. The title launches directly.', pack=None)
    pack = safe_path(root, 'data/boot/assets/' + PACK_SHA256 + '/xboot.pak')
    # An explicitly selected kernel is always validated, even if an older cache
    # exists. Clearing it permits the already prepared private pack to be reused.
    kernel = choice['kernel'] or ('' if valid_pack(pack) else fallback_kernel)
    if not kernel:
        if valid_pack(pack):
            return dict(status='ready', message='Startup data is ready.', pack=pack)
        return dict(status='unavailable', message='Add a compatible complete startup kernel dump in Settings for the intro. The title launches directly.', pack=None)
    try:
        content, manifest = extract(read_kernel(Path(kernel)))
    except (AssetError, OSError):
        return dict(status='unavailable', message='This kernel dump has no supported startup data. The title launches directly; a short save-signing dump is not enough.', pack=None)
    if not valid_pack(pack):
        destination = pack.parent
        if destination.exists():
            return dict(status='unavailable', message='Prepared startup data changed. The title launches directly; use a fresh setup to prepare it again.', pack=None)
        parent = safe_path(root, 'data/boot/assets')
        parent.mkdir(parents=True, exist_ok=True)
        candidate = Path(tempfile.mkdtemp(prefix='.prepare-', dir=parent))
        (candidate / 'xboot.pak').write_bytes(content)
        (candidate / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        candidate.rename(destination)
    return dict(status='ready', message='Startup data is ready.', pack=pack)


def ui_state(root, data, *, fallback_kernel=''):
    choice_path = ''
    try:
        choice_path = str(safe_path(root, 'data/boot-choice.txt'))
        choice = read_choice(root, data)
        availability = prepare(root, data, choice, fallback_kernel=fallback_kernel)
        last = safe_path(root, 'data/boot/last-run.json')
        if choice['enabled'] and last.is_file() and last.stat().st_size <= 8192:
            previous = json.loads(last.read_text(encoding='utf-8'))
            if isinstance(previous, dict) and previous.get('status') == 'failed':
                availability['message'] += ' The last intro could not finish; the title still launched.'
    except (ValueError, OSError, UnicodeError):
        choice = dict(enabled=False, kernel='')
        availability = dict(message='Startup settings or data are unavailable. The title launches directly.')
    return dict(**choice, status=availability['message'], choice_path=choice_path)


def run(root, data, catalog, environment, log, *, automated=False):
    """End/skip/failure all return to caller before the native title is spawned."""
    started = time.monotonic()
    report = dict(schema='xboxllr.startup-handoff.v1', implementation='native-reconstruction')
    try:
        choice = ({k: catalog.boot[k] for k in ('enabled', 'kernel')}
                  if getattr(catalog, 'boot', None) is not None else read_choice(root, data))
        available = prepare(root, data, choice,
            fallback_kernel=(getattr(catalog, 'setup', None) or {}).get('kernel', ''))
        report.update(status=available['status'], message=available['message'])
        if available['status'] == 'ready':
            helper = safe_path(root, data['startup']['executable'])
            env = {k: v for k, v in environment.items() if not k.startswith('XLLE_PILOT_')}
            for key, folder in [('TEMP', 'temp'), ('TMP', 'temp'),
                                ('APPDATA', 'appdata'), ('LOCALAPPDATA', 'localappdata')]:
                path = safe_path(root, 'data/boot/' + folder)
                path.mkdir(parents=True, exist_ok=True)
                env[key] = str(path)
            env['PYTHONDONTWRITEBYTECODE'] = '1'
            command = [str(helper), '--pack', str(available['pack']), '--volume', str(catalog.volume)]
            if automated:
                command.extend(['--headless', '--mute'])
            elif not catalog.volume:
                command.append('--mute')
            child = subprocess.Popen(command, cwd=safe_path(root, 'data/boot'), env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            try:
                output = child.communicate(timeout=TIMEOUT_SECONDS)[0]
            except subprocess.TimeoutExpired:
                child.terminate()
                try:
                    output = child.communicate(timeout=2)[0]
                except subprocess.TimeoutExpired:
                    child.kill()
                    output = child.communicate(timeout=2)[0]
                report.update(status='failed', message='Startup intro timed out. Launching the title directly.')
            else:
                try:
                    result = json.loads(output.decode('utf-8')) if len(output) <= 65536 else {}
                except (ValueError, UnicodeError):
                    result = {}
                if (child.returncode == 0 and isinstance(result, dict) and result.get('schema') == 'xboxllr.native-boot-run.v1'
                        and result.get('implementation') == 'native-reconstruction'
                        and result.get('status') in ('completed', 'skipped')):
                    report.update(status=result['status'], message='Startup intro ' + result['status'] + '.')
                else:
                    report.update(status='failed', message='Startup intro could not finish. Launching the title directly.')
            report['helper_exit_code'] = child.returncode
            private_log = safe_path(root, 'data/boot/helper.log')
            private_log.write_bytes(output[:65536])
    except (ValueError, OSError, UnicodeError, subprocess.SubprocessError):
        report.update(status='failed', message='Startup intro is unavailable. Launching the title directly.')
    report['elapsed_ms'] = round((time.monotonic() - started) * 1000, 3)
    log.write('[STARTUP] ' + report['message'] + '\n')
    log.flush()
    try:
        atomic_json(safe_path(root, 'data/boot/last-run.json'), report)
    except (ValueError, OSError):
        pass  # The optional stage may never prevent native-title startup.
    return report
