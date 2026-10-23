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
import uuid

from native_boot_assets import (AssetError, extract, read_kernel, read_title_logo,
                                sha256, TITLE_LOGO_SHA256)
from native_mod_launcher import atomic_json, decoded, encoded

PACK_BYTES = 73120
PACK_SHA256 = "e97b240aa8f0d20ce70d8c9ee2e4f3a150ca75808ba01c412c487335a7807dca"
LEGACY_PACK_SHA256 = "6c6df777c291a447a2de2693e3edfc4a69e0c4151548c8e058b512669a09233f"
TIMEOUT_SECONDS = 25
HANDOFF_SECONDS = 55


class _Events:
    """Exclusive per-launch Win32 event ownership; no global or window lookup."""
    def __init__(self):
        if os.name != 'nt':
            raise OSError('Startup presentation handoff is available on Windows only')
        import ctypes
        from ctypes import wintypes as w
        self.api = ctypes.WinDLL('kernel32', use_last_error=True)
        self.api.CreateEventW.argtypes = [w.LPVOID, w.BOOL, w.BOOL, w.LPCWSTR]
        self.api.CreateEventW.restype = w.HANDLE
        self.api.SetEvent.argtypes = [w.HANDLE]
        self.api.SetEvent.restype = w.BOOL
        self.api.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
        self.api.WaitForSingleObject.restype = w.DWORD
        self.api.CloseHandle.argtypes = [w.HANDLE]
        self.api.CloseHandle.restype = w.BOOL
        prefix = 'Local\\xboxllr-boot-' + uuid.uuid4().hex
        self.names = {key: prefix + '-' + key for key in ('ready', 'presented', 'cancel')}
        self.handles = {}
        try:
            for key, name in self.names.items():
                ctypes.set_last_error(0)
                handle = self.api.CreateEventW(None, True, False, name)
                if not handle:
                    raise OSError('Cannot create private startup event')
                self.handles[key] = handle
                if ctypes.get_last_error() == 183:  # Never connect to an existing launch.
                    raise OSError('Startup event identity already exists')
        except BaseException:
            self.close()
            raise

    def signaled(self, key):
        result = self.api.WaitForSingleObject(self.handles[key], 0)
        if result not in (0, 258):
            raise OSError('Private startup event wait failed')
        return result == 0

    def signal(self, key):
        if not self.api.SetEvent(self.handles[key]):
            raise OSError('Private startup event signal failed')

    def close(self):
        for handle in self.handles.values():
            self.api.CloseHandle(handle)
        self.handles.clear()


class Handoff:
    """Retain only our boot child until our native child's first presentation.

    The title process is never terminated here. A title failure, skip, timeout,
    launcher exception or owner exit retires only this launch's helper.
    """
    def __init__(self, root, log):
        self.root, self.log = root, log
        self.events = self.child = None
        self.report = None
        self.ready = False
        self.started = time.monotonic()

    def __enter__(self):
        return self

    def arguments(self):
        self.events = _Events()
        return [
            '--handoff-ready', self.events.names['ready'],
            '--handoff-presented', self.events.names['presented'],
            '--handoff-cancel', self.events.names['cancel'], '--handoff-parent', str(os.getpid())]

    def wait_ready(self, child):
        self.child = child
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while child.poll() is None:
            if self.events.signaled('ready'):
                self.ready = True
                return True
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(child.args, TIMEOUT_SECONDS)
            time.sleep(.01)
        return False

    @property
    def environment(self):
        return ({'XLLE_PILOT_BOOT_PRESENT_EVENT': self.events.names['presented']}
                if self.ready else {})

    def title_started(self, title):
        if not self.ready:
            return
        deadline = time.monotonic() + HANDOFF_SECONDS
        reason = 'title-failed-before-present'
        while True:
            if self.events.signaled('presented'):
                reason = 'title-presented'
                break
            if title.poll() is not None:
                break
            if self.child.poll() is not None:
                reason = 'helper-exited-before-present'
                break
            if time.monotonic() >= deadline:
                reason = 'title-present-timeout'
                break
            time.sleep(.01)
        self.close(reason)

    def close(self, reason='title-launch-cancelled'):
        try:
            if self.child is not None:
                if self.child.poll() is None and self.events and reason != 'title-presented':
                    try:
                        self.events.signal('cancel')
                    except OSError:
                        pass  # Still reap our helper if its event became invalid.
                try:
                    output = self.child.communicate(timeout=2)[0]
                except subprocess.TimeoutExpired:
                    self.child.terminate()
                    try:
                        output = self.child.communicate(timeout=2)[0]
                    except subprocess.TimeoutExpired:
                        self.child.kill()
                        output = self.child.communicate(timeout=2)[0]
                if self.report is not None:
                    try:
                        result = json.loads(output.decode('utf-8')) if len(output) <= 65536 else {}
                    except (ValueError, UnicodeError):
                        result = {}
                    success = (self.child.returncode == 0 and isinstance(result, dict)
                        and result.get('schema') == 'xboxllr.native-boot-run.v1'
                        and result.get('implementation') == 'native-reconstruction'
                        and result.get('status') == 'completed'
                        and result.get('handoff') == 'title-presented' and reason == 'title-presented')
                    skipped = (self.child.returncode == 0 and isinstance(result, dict)
                        and result.get('schema') == 'xboxllr.native-boot-run.v1'
                        and result.get('implementation') == 'native-reconstruction'
                        and result.get('handoff') == 'skipped')
                    self.report.update(status='completed' if success else 'skipped' if skipped else 'failed',
                        handoff='skipped' if skipped else reason, helper_exit_code=self.child.returncode,
                        elapsed_ms=round((time.monotonic()-self.started)*1000, 3),
                        message=('Startup intro handed its final image to the title.' if success else
                                 'Startup final image was skipped.' if skipped else
                                 'Startup final-image handoff ended: ' + reason + '.'))
                    safe_path(self.root, 'data/boot/helper.log').write_bytes(output[:65536])
                    atomic_json(safe_path(self.root, 'data/boot/last-run.json'), self.report)
                    self.log.write('[STARTUP] ' + self.report['message'] + '\n')
                    self.log.flush()
        except (OSError, ValueError, subprocess.SubprocessError):
            pass  # Optional presentation must not stop the native title.
        finally:
            self.child = None
            self.ready = False
            if self.events:
                self.events.close()
                self.events = None

    def __exit__(self, *unused):
        self.close()


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
        stale = safe_path(root, 'data/boot/assets/' + LEGACY_PACK_SHA256 + '/xboot.pak').is_file()
        message = ('The older startup cache lacks the required original after-image. Re-select the same compatible complete kernel in Settings to prepare it again. ' if stale else
                   'Add a compatible complete startup kernel dump in Settings for the intro. ')
        return dict(status='unavailable', message=message + 'The title launches directly.', pack=None)
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


def run(root, data, catalog, environment, log, *, automated=False, handoff=None, title_image=None):
    """Optionally retain our final image; skip/failure preserve direct startup."""
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
            if title_image is not None:
                # Explicit original title source, independent of modified4983's
                # embedded Xecuter2 variant. Failure never swaps variants silently.
                logo = read_title_logo(Path(title_image))
                path = safe_path(root, 'data/boot/title-logo/' + TITLE_LOGO_SHA256 + '.rle')
                path.parent.mkdir(parents=True, exist_ok=True)
                if path.exists() and (not path.is_file() or path.stat().st_size != len(logo) or path.read_bytes() != logo):
                    raise ValueError('Prepared original title logo changed')
                if not path.exists():
                    with path.open('xb') as output:
                        output.write(logo)
                command.extend(['--title-logo', str(path)])
                report['after_image_source'] = 'selected-title-xbe-microsoft'
            else:
                report['after_image_source'] = 'embedded-firmware-xecuter2'
            report['firmware_profile'] = 'modified-4983-xecuter2'
            if automated:
                command.extend(['--headless', '--mute'])
            elif not catalog.volume:
                command.append('--mute')
            if handoff is not None:
                command.extend(handoff.arguments())
            # Only the helper's bounded JSON result belongs on stdout. Native
            # APU diagnostics may exceed a pipe buffer before the ready event;
            # a separate private file cannot block readiness or corrupt JSON.
            # Popen gives the child its own redirect handle. Close our handle
            # immediately, including while that child retains its final image.
            diagnostics = safe_path(root, 'data/boot/helper-stderr.log')
            with diagnostics.open('wb') as diagnostic_log:
                child = subprocess.Popen(command, cwd=safe_path(root, 'data/boot'), env=env,
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=diagnostic_log,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            report['diagnostic_log'] = 'data/boot/helper-stderr.log'
            try:
                if handoff is not None and handoff.wait_ready(child):
                    report.update(status='awaiting-title', message='Startup final image is ready for the native title.')
                    handoff.report = report
                    output = None
                else:
                    output = child.communicate(timeout=TIMEOUT_SECONDS)[0]
                    if handoff is not None:
                        handoff.child = None
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
                    result = json.loads(output.decode('utf-8')) if output is not None and len(output) <= 65536 else {}
                except (ValueError, UnicodeError):
                    result = {}
                if output is None:
                    pass  # The owned helper remains until title_started/close.
                elif (child.returncode == 0 and isinstance(result, dict) and result.get('schema') == 'xboxllr.native-boot-run.v1'
                        and result.get('implementation') == 'native-reconstruction'
                        and result.get('status') in ('completed', 'skipped')):
                    report.update(status=result['status'], message='Startup intro ' + result['status'] + '.')
                else:
                    report.update(status='failed', message='Startup intro could not finish. Launching the title directly.')
            report['helper_exit_code'] = child.returncode
            private_log = safe_path(root, 'data/boot/helper.log')
            if output is not None:
                private_log.write_bytes(output[:65536])
    except (ValueError, OSError, UnicodeError, subprocess.SubprocessError):
        if handoff is not None:
            handoff.close('startup-failed')
        report.update(status='failed', message='Startup intro is unavailable. Launching the title directly.')
    report['elapsed_ms'] = round((time.monotonic() - started) * 1000, 3)
    log.write('[STARTUP] ' + report['message'] + '\n')
    log.flush()
    try:
        atomic_json(safe_path(root, 'data/boot/last-run.json'), report)
    except (ValueError, OSError):
        pass  # The optional stage may never prevent native-title startup.
    return report
