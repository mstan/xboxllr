"""Safe export content, real Windows file clipboard, and native crash handoff."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_crash_report as crash
import native_diagnostics as diagnostics


class CrashReportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.session = Path(self.temp.name)/'diagnostics/20261026-authored'
        self.private = self.session/'PRIVATE-DO-NOT-SHARE'
        self.private.mkdir(parents=True)
        (self.private/'eeprom.bin').write_bytes(b'authored-secret-console-identity')
        (self.private/'save-keys.bin').write_bytes(b'authored-secret-signing-keys')
        (self.private/'replay.json').write_text('{"environment":{"PATH":"C:/PrivateOwner/console"}}')
        (self.private/'game.log').write_text(
            '[FILE OPEN] name=C:/PrivateOwner/console/private-save\n'
            '[TEXTURE CREATE] header=d01bdf68 data=81469000 128x128 levels=1 format=11 bytes=32768\n'
            '[D3D BACKEND STOP] unqualified GPU texture download format\n'
            '[NATIVE STOP RECENT] 0014e730 00152960\n'
            '[FAULT SITE] pc=00007FF00420 module=C:\\PrivateOwner\\JSRFRecompUSA.exe base=00007FF00000 rva=420\n'
            '[FAULT GUEST] eax=00000000 ebx=12345678\n'
            '[NATIVE STOP RECENT] secret C:/PrivateOwner/console\n')
        data = diagnostics.INPUT_HEADER + diagnostics.INPUT_RECORD.pack(0, 10, 1, 0, 0, 0, bytes(22), bytes(10))
        (self.private/'inputs.bin').write_bytes(data)
        self.report = dict(schema=diagnostics.SCHEMA, title='Authored title', exit_code=2,
            stop='unqualified GPU texture download format', inputs=diagnostics.inspect_inputs(self.private/'inputs.bin'),
            selection=dict(features={'xboxlle.enhancement.visuals/resolution':dict(enabled=True,
                options=dict(height='1080', private='C:/PrivateOwner/console'))}),
            child_pid=42, private_field='authored-secret-signing-keys')
        (self.session/'report.json').write_text(json.dumps(self.report))

    def test_export_contains_diagnostic_failure_and_inputs_without_private_tree(self):
        output = crash.export_report(self.session, 'Chose SS on the graffiti screen')
        with zipfile.ZipFile(output) as archive:
            self.assertEqual(set(archive.namelist()), {'report.json','diagnostics.log','description.txt','README.txt','inputs.bin'})
            all_data = b''.join(archive.read(n) for n in archive.namelist())
            for secret in (b'PrivateOwner', b'authored-secret', b'private-save', b'PRIVATE-DO-NOT-SHARE'):
                self.assertNotIn(secret, all_data)
            self.assertIn(b'format=11', archive.read('diagnostics.log'))
            self.assertIn(b'00152960', archive.read('diagnostics.log'))
            self.assertIn(b'module=JSRFRecompUSA.exe base=00007FF00000 rva=420', archive.read('diagnostics.log'))
            self.assertIn(b'ebx=12345678', archive.read('diagnostics.log'))
            self.assertEqual(archive.read('inputs.bin'), (self.private/'inputs.bin').read_bytes())
            self.assertEqual(archive.read('description.txt'), b'Chose SS on the graffiti screen')
            public = json.loads(archive.read('report.json'))
            self.assertFalse(public['replay_available'])
            self.assertEqual(public['selection']['features']['xboxlle.enhancement.visuals/resolution']['options'],dict(height='1080'))
            self.assertEqual(public['stop'], self.report['stop'])
        again = crash.export_report(self.session, 'Updated note')
        self.assertEqual(again, output)
        with zipfile.ZipFile(again) as archive:
            self.assertEqual(archive.read('description.txt'), b'Updated note')
        self.assertEqual(list(output.parent.glob('*.partial')), [])

    def test_invalid_or_changed_input_is_not_attached(self):
        for data in (b'not an input recording', (self.private/'inputs.bin').read_bytes()+b'x',
                     diagnostics.INPUT_HEADER+diagnostics.INPUT_RECORD.pack(0, 11, 1, 0, 0, 0, bytes(22), bytes(10))):
            (self.private/'inputs.bin').write_bytes(data)
            with zipfile.ZipFile(crash.export_report(self.session)) as archive:
                self.assertNotIn('inputs.bin', archive.namelist())
                self.assertIn('input_attachment', json.loads(archive.read('report.json')))

    def test_dialog_and_clipboard_payload_preserve_unicode_and_structure(self):
        payload = crash.dropfiles(Path(self.temp.name)/'日本語 report.zip')
        self.assertEqual(struct.unpack('<IiiII',payload[:20]), (20,0,0,0,1))
        self.assertTrue(payload[20:].decode('utf-16-le').endswith('日本語 report.zip\0\0'))
        self.assertIn('Crash report'.encode('utf-16-le'), crash.dialog_template('Crash report'))
        self.assertIn('memory access', crash.failure_text(dict(exit_code=0xc0000005)))
        self.assertIn('unqualified GPU', crash.failure_text(self.report))

    @unittest.skipUnless(os.name == 'nt', 'Windows clipboard transport')
    def test_real_windows_file_clipboard_in_isolated_window_station(self):
        # A separate child/window station owns a separate clipboard. Never
        # replace the owner's current desktop clipboard during automated QA.
        script = r'''
import ctypes as c,sys
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from native_crash_report import copy_zip
u=c.WinDLL('user32',use_last_error=True)
u.CreateWindowStationW.argtypes=[c.c_wchar_p,c.c_uint,c.c_uint,c.c_void_p];u.CreateWindowStationW.restype=c.c_void_p
u.SetProcessWindowStation.argtypes=[c.c_void_p];u.SetProcessWindowStation.restype=c.c_int
station=u.CreateWindowStationW(None,0,0xf037f,None);assert station,c.WinError(c.get_last_error())
assert u.SetProcessWindowStation(station),c.WinError(c.get_last_error())
u.CreateDesktopW.argtypes=[c.c_wchar_p,c.c_wchar_p,c.c_void_p,c.c_uint,c.c_uint,c.c_void_p];u.CreateDesktopW.restype=c.c_void_p
u.SetThreadDesktop.argtypes=[c.c_void_p];u.SetThreadDesktop.restype=c.c_int
desktop=u.CreateDesktopW('report-test',None,None,0,0x10000000,None);assert desktop,c.WinError(c.get_last_error())
assert u.SetThreadDesktop(desktop),c.WinError(c.get_last_error())
u.CreateWindowExW.argtypes=[c.c_uint,c.c_wchar_p,c.c_wchar_p,c.c_uint,c.c_int,c.c_int,c.c_int,c.c_int,c.c_void_p,c.c_void_p,c.c_void_p,c.c_void_p]
u.CreateWindowExW.restype=c.c_void_p
window=u.CreateWindowExW(0,'STATIC','test',0,0,0,1,1,c.c_void_p(-3),None,None,None);assert window,c.WinError(c.get_last_error())
copy_zip(Path(sys.argv[2]),window)
u.OpenClipboard.argtypes=[c.c_void_p];u.GetClipboardData.argtypes=[c.c_uint];u.GetClipboardData.restype=c.c_void_p
assert u.OpenClipboard(window)
try:
    h=u.GetClipboardData(15);assert h
    shell=c.WinDLL('shell32');shell.DragQueryFileW.argtypes=[c.c_void_p,c.c_uint,c.c_wchar_p,c.c_uint]
    assert shell.DragQueryFileW(h,0xffffffff,None,0)==1
    path=c.create_unicode_buffer(32768);assert shell.DragQueryFileW(h,0,path,len(path))
    assert Path(path.value)==Path(sys.argv[2]).resolve(),path.value
finally:u.CloseClipboard()
# Exercise the real modal dialog, optional description, asynchronous ZIP
# preparation and Copy button on this same invisible desktop.
import json,threading,time,zipfile
from types import SimpleNamespace
from native_crash_report import show
session=SimpleNamespace(path=Path(sys.argv[3]),report=json.loads((Path(sys.argv[3])/'report.json').read_text()))
k=c.WinDLL('kernel32');thread=k.GetCurrentThreadId()
enumtype=c.WINFUNCTYPE(c.c_int,c.c_void_p,c.c_ssize_t)
u.EnumThreadWindows.argtypes=[c.c_uint,enumtype,c.c_ssize_t]
u.GetDlgItem.argtypes=[c.c_void_p,c.c_int];u.GetDlgItem.restype=c.c_void_p
u.GetDlgItemTextW.argtypes=[c.c_void_p,c.c_int,c.c_wchar_p,c.c_int]
u.SetDlgItemTextW.argtypes=[c.c_void_p,c.c_int,c.c_wchar_p]
u.PostMessageW.argtypes=[c.c_void_p,c.c_uint,c.c_size_t,c.c_ssize_t]
errors=[]
def drive():
    dialog=None
    try:
        assert u.SetThreadDesktop(desktop)
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            found=[]
            @enumtype
            def collect(hwnd,_):
                if u.GetDlgItem(hwnd,1001):found.append(hwnd)
                return 1
            u.EnumThreadWindows(thread,collect,0)
            if found:dialog=found[0];break
            time.sleep(.02)
        assert dialog,'Native crash dialog was not created'
        reason=c.create_unicode_buffer(1024);u.GetDlgItemTextW(dialog,101,reason,len(reason))
        assert 'unqualified GPU texture download format' in reason.value
        u.SetDlgItemTextW(dialog,103,'Chose SS from crash dialog')
        u.PostMessageW(dialog,0x111,1001,0)
        while time.monotonic()<deadline:
            status=c.create_unicode_buffer(1024);u.GetDlgItemTextW(dialog,105,status,len(status))
            if status.value.startswith('ZIP copied.'):break
            time.sleep(.02)
        else:raise AssertionError('Copy button did not complete: '+status.value)
        assert u.OpenClipboard(dialog)
        try:
            h=u.GetClipboardData(15);assert h
            out=c.create_unicode_buffer(32768);assert shell.DragQueryFileW(h,0,out,len(out))
            with zipfile.ZipFile(out.value) as z:assert z.read('description.txt')==b'Chose SS from crash dialog'
        finally:u.CloseClipboard()
    except BaseException as error:errors.append(repr(error))
    finally:
        if dialog:u.PostMessageW(dialog,0x111,2,0)
worker=threading.Thread(target=drive);worker.start();show(session);worker.join(15)
assert not worker.is_alive() and not errors,errors
print('PASS real CF_HDROP and crash dialog Copy button in isolated clipboard')
'''
        output = crash.export_report(self.session)
        child = subprocess.run([sys.executable,'-c',script,str(Path(crash.__file__).parent),str(output),str(self.session)],
                               text=True,capture_output=True,timeout=30)
        self.assertEqual(child.returncode,0,child.stdout+child.stderr)
        self.assertIn('PASS real CF_HDROP',child.stdout)


if __name__ == '__main__':
    unittest.main()
