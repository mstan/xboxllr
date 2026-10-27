"""Native crash feedback and bounded, shareable diagnostics without console secrets."""
from __future__ import annotations

import ctypes as c
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import threading
import time
import uuid
import zipfile

from native_diagnostics import bounded_json, inspect_inputs, safe_relative

LOG_LIMIT = 8 * 1024 * 1024
MARKERS = {'PILOT STOP', 'D3D BACKEND STOP', 'PILOT AUDIO STOP', 'PILOT DSP STOP',
           'PILOT INPUT STOP', 'NATIVE STOP CONTEXT', 'NATIVE STOP RECENT',
           'NATIVE EXCEPTION', 'NATIVE EXCEPTION STACK', 'NATIVE EXCEPTION RECENT',
           'DIAGNOSTIC INPUTS FINAL', 'DIAGNOSTIC INPUTS INCOMPLETE',
           'TEXTURE CREATE', 'TEXTURE UPLOAD', 'D3D STATE', 'D3D TEXTURE STATE',
           'FAULT', 'FAULT GUEST', 'HOST LANGUAGE EXCEPTION'}
PUBLIC_KEYS = ('schema', 'started_utc', 'finished_utc', 'state', 'title', 'xbe_sha256',
    'executable_sha256', 'framework_revision', 'framework_dirty', 'package_manifest_sha256',
    'system', 'exit_code', 'exit_reason', 'stop', 'input_delivery_complete', 'inputs')


def failure_text(report):
    stop = report.get('stop')
    if stop:
        return 'The game stopped because of a runtime error.\r\n\r\nDetails: ' + str(stop)[:240]
    code = report.get('exit_code')
    if isinstance(code, int) and code & 0xffffffff == 0xc0000005:
        return 'The game encountered a memory access error. The diagnostic ZIP can help identify where it happened.'
    return 'The game stopped unexpectedly.\r\n\r\nExit code: ' + (f'0x{code & 0xffffffff:08X}' if isinstance(code, int) else 'unavailable')


def export_report(session, notes=''):
    """Allowlisted metadata/log markers and validated fixed-size input records.

    Never walk the private session tree or include replay environments, EEPROM,
    signing keys, native saves, raw logs or original game files.
    """
    session = Path(session).resolve(strict=True)
    report = bounded_json(safe_relative(session, 'report.json'))
    public = {key: report[key] for key in PUBLIC_KEYS if key in report}
    # Preserve the rendering/input configuration needed to interpret a report,
    # admitting catalog identifiers and scalar choices rather than arbitrary
    # custom paths or unknown fields from a remembered selection.
    selection = report.get('selection', {})
    public['selection'] = dict(features={})
    if isinstance(selection, dict):
        for name, feature in list(selection.get('features', {}).items())[:256]:
            if not re.fullmatch(r'[a-z0-9_.-]{1,100}/[a-z0-9_.-]{1,100}', name) or not isinstance(feature, dict):
                continue
            choices = {key: value for key, value in feature.get('options', {}).items()
                if re.fullmatch(r'[a-z0-9_.-]{1,80}', key) and isinstance(value, (str, int, bool))
                and re.fullmatch(r'[A-Za-z0-9_.:+-]{0,80}', str(value))}
            public['selection']['features'][name] = dict(enabled=bool(feature.get('enabled')), options=choices)
        player = selection.get('player1', {})
        if isinstance(player, dict) and player.get('source') in ('auto', 'none', 'keyboard', 'gamepad'):
            public['selection']['player1'] = dict(source=player['source'])
            if re.fullmatch(r'[0-9a-f]{32}', str(player.get('guid', ''))):
                public['selection']['player1']['guid'] = player['guid']
    public['shareable_report'] = True
    public['replay_available'] = False
    public['limitation'] = 'Inputs and selected diagnostics only; no initial saves or console identity. This ZIP is not a complete replay snapshot.'
    private = safe_relative(session, 'PRIVATE-DO-NOT-SHARE')
    lines = []
    log = safe_relative(private, 'game.log')
    if log.is_file():
        with log.open('rb') as stream:
            stream.seek(max(0, log.stat().st_size - LOG_LIMIT))
            # An allowlist of numeric/status diagnostics excludes file paths,
            # arbitrary original file contents and console/profile log records.
            for line in stream.read(LOG_LIMIT).decode('utf-8', 'replace').splitlines():
                frame = re.fullmatch(r'(\[(?:FAULT SITE|HOST LANGUAGE FRAME)\] (?:index=\d+ )?pc=[0-9a-fA-F]+ module=)(.*?)( (?:base=[0-9a-fA-F]+ )?rva=[0-9a-fA-F]+)', line)
                if frame:
                    module = frame[2].replace('\\', '/').rsplit('/', 1)[-1]
                    if not re.fullmatch(r'[A-Za-z0-9_. -]{1,160}', module):
                        module = 'unknown'
                    lines.append(frame[1]+module+frame[3])
                    continue
                match = re.fullmatch(r'\[([A-Z0-9 _]+)\] ([A-Za-z0-9 _(),=:+.*-]{1,2048})', line)
                if match and match[1] in MARKERS:
                    lines.append(line)
    inputs = safe_relative(private, 'inputs.bin')
    include_inputs = False
    if inputs.is_file():
        try:
            actual = inspect_inputs(inputs)
            if report.get('inputs') and actual != report['inputs']:
                raise ValueError('Input recording changed')
            include_inputs = True
        except (OSError, ValueError):
            public['input_attachment'] = 'Unavailable: input recording is incomplete or invalid.'
    # This folder contains only explicitly shareable exports, separate from
    # the local private reproduction material and copied console identities.
    output_dir = safe_relative(session.parent, 'crash-reports')
    output_dir.mkdir(exist_ok=True)
    output = safe_relative(output_dir, session.name + '.zip')
    temporary = output.with_name(output.name + '.' + uuid.uuid4().hex + '.partial')
    try:
        with zipfile.ZipFile(temporary, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=3) as archive:
            archive.writestr('report.json', json.dumps(public, indent=2)+'\n')
            archive.writestr('diagnostics.log', '\n'.join(lines)+'\n')
            archive.writestr('description.txt', str(notes)[:4000])
            archive.writestr('README.txt', 'Shareable crash report. Contains selected diagnostics, optional description and validated controller inputs. Console keys, native saves and raw logs are excluded.\n')
            if include_inputs:
                archive.write(inputs, 'inputs.bin')
        with zipfile.ZipFile(temporary) as archive:
            if archive.testzip() is not None:
                raise OSError('Diagnostic ZIP verification failed')
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return output


def dropfiles(path):
    # DROPFILES is five 32-bit fields on both Win32 and Win64; the Unicode
    # file list has a second terminating NUL, including for a single file.
    return struct.pack('<IiiII', 20, 0, 0, 0, 1) + (str(Path(path).absolute())+'\0\0').encode('utf-16-le')


def copy_zip(path, owner):
    """Place a real Shell file copy on the clipboard, not a path string."""
    if os.name != 'nt':
        raise OSError('File clipboard requires Windows')
    path = Path(path).resolve(strict=True)
    if not path.is_file():
        raise OSError('Diagnostic ZIP is unavailable')
    user, kernel = c.WinDLL('user32', use_last_error=True), c.WinDLL('kernel32', use_last_error=True)
    user.OpenClipboard.argtypes = [c.c_void_p]; user.OpenClipboard.restype = c.c_int
    user.EmptyClipboard.restype = c.c_int
    user.SetClipboardData.argtypes = [c.c_uint, c.c_void_p]; user.SetClipboardData.restype = c.c_void_p
    user.RegisterClipboardFormatW.argtypes = [c.c_wchar_p]; user.RegisterClipboardFormatW.restype = c.c_uint
    kernel.GlobalAlloc.argtypes = [c.c_uint, c.c_size_t]; kernel.GlobalAlloc.restype = c.c_void_p
    kernel.GlobalLock.argtypes = [c.c_void_p]; kernel.GlobalLock.restype = c.c_void_p
    kernel.GlobalUnlock.argtypes = [c.c_void_p]
    kernel.GlobalFree.argtypes = [c.c_void_p]; kernel.GlobalFree.restype = c.c_void_p
    for attempt in range(5):
        if user.OpenClipboard(owner):
            break
        if attempt == 4:
            raise OSError('The clipboard is busy. Please try Copy again.')
        time.sleep(.03)
    try:
        if not user.EmptyClipboard():
            raise c.WinError(c.get_last_error())
        for format_id, data in ((15, dropfiles(path)),
                (user.RegisterClipboardFormatW('Preferred DropEffect'), struct.pack('<I', 1))):
            if not format_id:
                raise c.WinError(c.get_last_error())
            handle = kernel.GlobalAlloc(0x42, len(data))
            if not handle:
                raise MemoryError('Could not allocate the file clipboard')
            try:
                pointer = kernel.GlobalLock(handle)
                if not pointer:
                    raise c.WinError(c.get_last_error())
                c.memmove(pointer, data, len(data)); kernel.GlobalUnlock(handle)
                if not user.SetClipboardData(format_id, handle):
                    raise c.WinError(c.get_last_error())
                handle = None  # Windows now owns this movable allocation.
            finally:
                if handle:
                    kernel.GlobalFree(handle)
    finally:
        user.CloseClipboard()


def dialog_template(title):
    """Standard dialog units/font provide DPI scaling and normal keyboard access."""
    def text(value): return (value+'\0').encode('utf-16-le')
    controls = [
        (100, 0x82, 12, 9, 336, 18, 0, 'The game stopped unexpectedly'),
        (101, 0x81, 12, 31, 336, 49, 0xA00844, ''),
        (102, 0x82, 12, 88, 336, 12, 0, 'What were you doing just before it stopped? (optional)'),
        (103, 0x81, 12, 103, 336, 52, 0xA11044, ''),
        (104, 0x82, 12, 162, 336, 24, 0, 'The ZIP includes diagnostics and controller inputs. Console keys and saved games are excluded.'),
        (105, 0x82, 12, 190, 336, 22, 0, 'Copy the ZIP to paste it into a chat, or open its folder to attach it.'),
        (1001, 0x80, 12, 220, 115, 18, 0x10001, '&Copy diagnostic ZIP'),
        (1002, 0x80, 134, 220, 100, 18, 0x10000, '&Open report folder'),
        (2, 0x80, 291, 220, 57, 18, 0x10000, 'Close')]
    data = bytearray(struct.pack('<IIHhhhh', 0x80c808c0, 0, len(controls), 0, 0, 360, 250))
    data += b'\0\0\0\0'+text(title)+struct.pack('<H', 9)+text('Segoe UI')
    for identity, atom, x, y, width, height, style, label in controls:
        data += bytes((-len(data)) % 4)
        data += struct.pack('<IIhhhhH', 0x50000000|style, 0, x, y, width, height, identity)
        data += struct.pack('<HH', 0xffff, atom)+text(label)+b'\0\0'
    return bytes(data)


def show(session):
    if os.name != 'nt':
        return
    user = c.WinDLL('user32', use_last_error=True)
    user.SetDlgItemTextW.argtypes = [c.c_void_p, c.c_int, c.c_wchar_p]
    user.GetDlgItemTextW.argtypes = [c.c_void_p, c.c_int, c.c_wchar_p, c.c_int]
    user.GetDlgItem.argtypes = [c.c_void_p, c.c_int]; user.GetDlgItem.restype = c.c_void_p
    user.EnableWindow.argtypes = [c.c_void_p, c.c_int]
    user.EndDialog.argtypes = [c.c_void_p, c.c_ssize_t]
    user.PostMessageW.argtypes = [c.c_void_p, c.c_uint, c.c_size_t, c.c_ssize_t]
    user.SendDlgItemMessageW.argtypes = [c.c_void_p, c.c_int, c.c_uint, c.c_size_t, c.c_ssize_t]
    callback_type = c.WINFUNCTYPE(c.c_ssize_t, c.c_void_p, c.c_uint, c.c_size_t, c.c_ssize_t)
    user.DialogBoxIndirectParamW.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, callback_type, c.c_ssize_t]
    user.DialogBoxIndirectParamW.restype = c.c_ssize_t
    state = dict(busy=False, closed=False, output=None, error=None)
    complete = 0x8000 + 71

    def prepare(window, notes):
        try:
            state['output'] = export_report(session.path, notes)
        except Exception:
            state['error'] = 'Could not prepare the ZIP. Check that the game folder is writable and has free space, then try Copy again.'
        if not state['closed']:
            user.PostMessageW(window, complete, 0, 0)

    @callback_type
    def callback(window, message, wparam, lparam):
        if message == 0x110:  # WM_INITDIALOG
            user.SetDlgItemTextW(window, 100, session.report.get('title', 'The game')+' stopped unexpectedly')
            user.SetDlgItemTextW(window, 101, failure_text(session.report))
            user.SendDlgItemMessageW(window, 103, 0xc5, 4000, 0)  # EM_LIMITTEXT
            user.EnableWindow(user.GetDlgItem(window, 1002), False)
            return 1
        if message == 0x10 or (message == 0x111 and wparam & 0xffff == 2):
            state['closed'] = True; user.EndDialog(window, 0); return 1
        if message == 0x111 and wparam & 0xffff == 1001 and not state['busy']:
            notes = c.create_unicode_buffer(4001); user.GetDlgItemTextW(window, 103, notes, len(notes))
            state.update(busy=True, error=None)
            user.EnableWindow(user.GetDlgItem(window, 1001), False)
            user.SetDlgItemTextW(window, 105, 'Preparing your diagnostic ZIP...')
            threading.Thread(target=prepare, args=(window, notes.value), daemon=False).start()
            return 1
        if message == complete:
            state['busy'] = False
            user.EnableWindow(user.GetDlgItem(window, 1001), True)
            if state['output']:
                user.EnableWindow(user.GetDlgItem(window, 1002), True)
            status = state['error']
            if not status:
                try:
                    copy_zip(state['output'], window)
                    status = 'ZIP copied. Paste it into your chat. If file pasting is unavailable, use Open report folder.'
                except (OSError, MemoryError):
                    status = 'The ZIP is ready, but the clipboard is busy. Try Copy again, or use Open report folder.'
            user.SetDlgItemTextW(window, 105, status)
            return 1
        if message == 0x111 and wparam & 0xffff == 1002 and state['output']:
            try:
                subprocess.Popen(['explorer.exe', '/select,'+str(state['output'])])
            except OSError:
                user.SetDlgItemTextW(window, 105, 'Could not open the folder. The ZIP is still saved; try Copy again.')
            return 1
        return 0

    template = c.create_string_buffer(dialog_template(session.report.get('title', 'Game')+' - Crash report'))
    if user.DialogBoxIndirectParamW(None, template, None, callback, 0) == -1:
        user.MessageBoxW.argtypes = [c.c_void_p, c.c_wchar_p, c.c_wchar_p, c.c_uint]
        user.MessageBoxW(None, failure_text(session.report), 'Game stopped', 0x10)
