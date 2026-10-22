"""Optional intro lifecycle and identity isolation, without original titles."""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_boot_launcher as boot


class StartupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / 'native').mkdir()
        (self.root / 'native/XboxStartup.exe').write_bytes(b'authored helper fixture')
        self.data = dict(startup=dict(default_enabled=True, executable='native/XboxStartup.exe'))
        self.catalog = SimpleNamespace(volume=75, setup=None, boot=None)
        self.log = io.StringIO()
        self.pack = self.root / 'fixture.pak'
        self.pack.write_bytes(b'authored fixture')

    def ready(self):
        return patch.object(boot, 'prepare', return_value=dict(status='ready', message='Ready', pack=self.pack))

    def test_title_default_and_saved_toggle_are_separate_from_console_profile(self):
        for enabled in (True, False):
            with self.subTest(enabled=enabled):
                data = dict(startup=dict(default_enabled=enabled))
                self.assertEqual(boot.read_choice(self.root, data), dict(enabled=enabled, kernel=''))
        boot.write_choice(self.root, dict(enabled=False, kernel='G:/Owned dumps/complete kernel.img'))
        self.assertFalse(boot.read_choice(self.root, self.data)['enabled'])
        self.assertFalse((self.root / 'data/console').exists())
        self.assertFalse((self.root / 'data/state').exists())

    def test_missing_short_and_unknown_inputs_preserve_direct_launch(self):
        choice = dict(enabled=True, kernel='')
        self.assertEqual(boot.prepare(self.root, self.data, choice)['status'], 'unavailable')
        for size in (5, 458752, 710720):
            kernel = self.root / 'authored.kernel'
            kernel.write_bytes(bytes(size))
            result = boot.prepare(self.root, self.data, dict(enabled=True, kernel=str(kernel)))
            self.assertEqual(result['status'], 'unavailable')
            self.assertIsNone(result['pack'])
            self.assertIn('launches directly', result['message'])
        self.assertFalse((self.root / 'data/console').exists())
        self.assertFalse((self.root / 'data/boot/assets').exists())

    def test_disabled_stage_does_not_start_helper(self):
        boot.write_choice(self.root, dict(enabled=False, kernel=''))
        with patch.object(boot.subprocess, 'Popen') as process:
            result = boot.run(self.root, self.data, self.catalog, {}, self.log)
        self.assertEqual(result['status'], 'disabled')
        process.assert_not_called()

    def test_complete_and_skip_are_distinct_and_automated_run_has_no_input_window(self):
        for status in ('completed', 'skipped'):
            child = Mock(returncode=0)
            child.communicate.return_value = (json.dumps(dict(schema='xboxllr.native-boot-run.v1',
                implementation='native-reconstruction', status=status)).encode(), None)
            with self.ready(), patch.object(boot.subprocess, 'Popen', return_value=child) as process:
                result = boot.run(self.root, self.data, self.catalog,
                    dict(XLLE_PILOT_INPUT='sdl', APPDATA='C:/forbidden'), self.log, automated=True)
            self.assertEqual(result['status'], status)
            args = process.call_args.args[0]
            self.assertIn('--headless', args)
            self.assertIn('--mute', args)
            self.assertNotIn('--realtime', args)
            env = process.call_args.kwargs['env']
            self.assertNotIn('XLLE_PILOT_INPUT', env)
            for key in ('TEMP', 'TMP', 'APPDATA', 'LOCALAPPDATA'):
                self.assertTrue(Path(env[key]).is_relative_to(self.root))
            child.terminate.assert_not_called()
            self.assertEqual(json.loads((self.root / 'data/boot/last-run.json').read_text())['status'], status)

    def test_owner_stage_keeps_audio_and_uses_only_chosen_volume(self):
        child = Mock(returncode=0)
        child.communicate.return_value = (b'{"schema":"xboxllr.native-boot-run.v1",'
            b'"implementation":"native-reconstruction","status":"completed"}', None)
        with self.ready(), patch.object(boot.subprocess, 'Popen', return_value=child) as process:
            boot.run(self.root, self.data, self.catalog, {}, self.log)
        command = process.call_args.args[0]
        self.assertNotIn('--headless', command)
        self.assertNotIn('--mute', command)
        self.assertEqual(command[-2:], ['--volume', '75'])

    def test_failure_or_bad_helper_report_is_nonfatal(self):
        for code, output in ((2, b'error'), (0, b'{}'), (0, b'[]'), (0, b'{"status":"completed"}')):
            child = Mock(returncode=code)
            child.communicate.return_value = (output, None)
            with self.ready(), patch.object(boot.subprocess, 'Popen', return_value=child):
                result = boot.run(self.root, self.data, self.catalog, {}, self.log, automated=True)
            self.assertEqual(result['status'], 'failed')
            self.assertIn('directly', result['message'])

    def test_timeout_terminates_only_helper_before_returning(self):
        child = Mock(returncode=-15)
        child.communicate.side_effect = [subprocess.TimeoutExpired('fixture', 25), (b'', None)]
        with self.ready(), patch.object(boot.subprocess, 'Popen', return_value=child):
            result = boot.run(self.root, self.data, self.catalog, {}, self.log, automated=True)
        self.assertEqual(result['status'], 'failed')
        child.terminate.assert_called_once_with()
        child.kill.assert_not_called()
        self.assertEqual(child.communicate.call_args.kwargs['timeout'], 2)

    def test_timeout_escalates_to_kill_and_waits_before_returning(self):
        child = Mock(returncode=-9)
        child.communicate.side_effect = [subprocess.TimeoutExpired('fixture', 25),
            subprocess.TimeoutExpired('fixture', 2), (b'', None)]
        with self.ready(), patch.object(boot.subprocess, 'Popen', return_value=child):
            result = boot.run(self.root, self.data, self.catalog, {}, self.log, automated=True)
        self.assertEqual(result['status'], 'failed')
        child.terminate.assert_called_once_with()
        child.kill.assert_called_once_with()
        self.assertEqual(child.communicate.call_count, 3)

    def test_invalid_settings_do_not_block_native_startup_or_rewrite_identity(self):
        path = self.root / 'data/boot-choice.txt'
        path.parent.mkdir()
        path.write_text('damaged choices')
        identity = self.root / 'data/console/eeprom.bin'
        identity.parent.mkdir()
        identity.write_bytes(b'owner console identity')
        result = boot.run(self.root, self.data, self.catalog, {}, self.log)
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(identity.read_bytes(), b'owner console identity')
        self.assertEqual(path.read_text(), 'damaged choices')

    @unittest.skipUnless(os.environ.get('XBOXLLR_BOOT_TEST_KERNEL') and
                         os.environ.get('XBOXLLR_BOOT_TEST_EXE'), 'requires private admitted input')
    def test_real_private_pack_preparation_and_native_handoff(self):
        import shutil
        shutil.copy2(os.environ['XBOXLLR_BOOT_TEST_EXE'], self.root / 'native/XboxStartup.exe')
        kernel = Path(os.environ['XBOXLLR_BOOT_TEST_KERNEL'])
        boot.write_choice(self.root, dict(enabled=True, kernel=str(kernel)))
        state = self.root / 'data/state/Partition1/UDATA/progress.sav'
        state.parent.mkdir(parents=True)
        state.write_bytes(b'owner progress remains private')
        before = boot.sha256(kernel.read_bytes())
        result = boot.run(self.root, self.data, self.catalog, dict(os.environ), self.log, automated=True)
        details = self.root / 'data/boot/helper.log'
        self.assertEqual(result['status'], 'completed', details.read_text(errors='replace') if details.exists() else result)
        self.assertEqual(state.read_bytes(), b'owner progress remains private')
        self.assertEqual(boot.sha256(kernel.read_bytes()), before)
        cache = self.root / 'data/boot/assets' / boot.PACK_SHA256 / 'xboot.pak'
        self.assertTrue(boot.valid_pack(cache))
        # Cached private data may be reused with no kernel/signing/HDD inputs.
        available = boot.prepare(self.root, self.data, dict(enabled=True, kernel=''))
        self.assertEqual(available['status'], 'ready')
        self.assertFalse((self.root / 'data/console').exists())


if __name__ == '__main__':
    unittest.main()
