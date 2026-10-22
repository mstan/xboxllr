"""Authored private sessions: crash durability, safe reproduction and rejection."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_diagnostics as diagnostics
import replay_native_diagnostic as replay_tool


class DiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'package'
        self.root.mkdir()
        (self.root / 'game.exe').write_bytes(b'authored executable fixture')
        (self.root / 'release.json').write_text('{}')
        self.image = self.base / 'media/game.xbe'
        self.image.parent.mkdir()
        self.image.write_bytes(b'authored image')
        self.profile = self.root / 'data/console'
        self.profile.mkdir(parents=True)
        for name in ('eeprom.bin', 'save-keys.bin', 'console-settings.bin'):
            (self.profile / name).write_bytes(b'private-' + name.encode())
        self.state = self.root / 'data/state'
        self.state.mkdir()
        self.progress = self.state / 'Partition1/UDATA/fixture/progress'
        self.progress.parent.mkdir(parents=True)
        self.progress.write_bytes(b'original progress')
        self.data = dict(title=dict(name='Authored title', xbe_sha256=diagnostics.digest(self.image)),
            executable='game.exe', schema='xboxllr.native-preview.v1', files={}, runtime={},
            framework_revision='a'*40, framework_dirty=False)

    def session(self, **kwargs):
        env = dict(XLLE_PILOT_SAVE_KEYS_FILE=str(self.profile / 'save-keys.bin'),
                   XLLE_PILOT_INPUT='keyboard')
        session = diagnostics.Session(self.root, self.data, self.root / 'game.exe', self.image,
            self.profile, self.state, {}, env, **kwargs)
        return session, env

    @staticmethod
    def inputs(session, records=None):
        if records is None:
            records = [(0, 20, 1, 0, 0, 0, b'\x00\x00\xff'+bytes(19), bytes(10)),
                       (1, 20, 2, 0, 0, 0, bytes([255])*22, bytes(10)),
                       (2, 22, 3, 0, 0, 0x12340056, bytes(22), bytes(10))]
        path = session.private / 'inputs.bin'
        path.write_bytes(diagnostics.INPUT_HEADER + b''.join(diagnostics.INPUT_RECORD.pack(*r) for r in records))
        return path

    def test_crash_logs_retained_across_launches_public_report_excludes_keys(self):
        first, env = self.session(enabled=True)
        self.inputs(first)
        first.log_path.write_text('[PILOT STOP] undiscovered direct target at 000403c0; entries=999\nprivate-path=G:/Secrets/console\n[DIAGNOSTIC INPUTS FINAL] records=3 flushed=1\n')
        first.finish(2)
        second, _ = self.session()
        self.assertNotEqual(first.path, second.path)
        self.assertTrue(first.log_path.exists())
        report = json.loads((first.path / 'report.json').read_text())
        self.assertEqual(report['exit_code'], 2)
        self.assertIn('000403c0', report['stop'])
        self.assertTrue(report['replay_available'])
        self.assertTrue(report['input_delivery_complete'])
        self.assertNotIn('G:/Secrets', (first.path / 'report.json').read_text())
        self.assertFalse(any(p.name in ('save-keys.bin', 'eeprom.bin') for p in first.path.iterdir()))
        self.assertEqual(self.progress.read_bytes(), b'original progress')

    def test_partial_input_and_changed_snapshot_rejected(self):
        session, _ = self.session(enabled=True)
        path = self.inputs(session)
        path.write_bytes(path.read_bytes() + b'x')
        with self.assertRaisesRegex(ValueError, 'partial'):
            diagnostics.inspect_session(session.path)
        self.inputs(session)
        (session.private / 'initial-state/Partition1/UDATA/fixture/progress').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError, 'snapshot changed'):
            diagnostics.inspect_session(session.path)

    def test_writer_failure_is_not_reported_as_complete_reproduction(self):
        session, _ = self.session(enabled=True)
        self.inputs(session)
        session.log_path.write_text('[DIAGNOSTIC INPUTS INCOMPLETE] bounded writer queue overflow; gameplay continues\n')
        session.finish(0)
        self.assertFalse(session.report['replay_available'])
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            diagnostics.inspect_session(session.path)

    def test_abrupt_tail_is_unconfirmed_and_public_errors_hide_original_paths(self):
        session, _ = self.session(enabled=True)
        self.inputs(session)
        session.log_path.write_text('host abruptly stopped')
        session.finish(-1)
        self.assertFalse(session.report['input_delivery_complete'])
        private_path = 'G:/PrivateConsole/unique-secret-console.bin'
        with patch.object(diagnostics, 'snapshot_tree', side_effect=OSError(private_path)):
            failed, _ = self.session(enabled=True)
        self.assertNotIn(private_path, (failed.path / 'report.json').read_text())
        self.assertIn(private_path, (failed.private / 'capture-errors.txt').read_text())

    def test_rejects_nonmonotonic_sequences_reserved_data_and_path_escape(self):
        session, _ = self.session(enabled=True)
        for row in [(1, 0, 1, 0, 0, 0, bytes(22), bytes(10)),
                    (0, 0, 9, 0, 0, 0, bytes(22), bytes(10)),
                    (0, 0, 1, 4, 0, 0, bytes(22), bytes(10)),
                    (0, 0, 1, 0, 0, 0, bytes(22), b'x'+bytes(9))]:
            with self.subTest(row=row):
                self.inputs(session, [row])
                with self.assertRaises(ValueError):
                    diagnostics.inspect_session(session.path)
        self.inputs(session)
        record = session.replay
        record['state_files'] = {'../../escape': dict(size=0, sha256='0'*64)}
        diagnostics.durable_json(session.private / 'replay.json', record)
        with self.assertRaisesRegex(ValueError, 'bundle path'):
            diagnostics.inspect_session(session.path)

    def test_snapshot_budget_rejection_and_hdd_do_not_copy_or_change_saves(self):
        with self.assertRaisesRegex(ValueError, 'budget'):
            diagnostics.snapshot_tree(self.state, self.base / 'too-large', limit=1)
        self.assertFalse((self.base / 'too-large').exists())
        session, env = self.session(enabled=True, hdd={'path': 'private.qcow2'})
        self.assertFalse((session.private / 'initial-state').exists())
        self.assertFalse(session.report['replay_available'])
        self.assertEqual(session.report['initial_snapshot_reason'], 'hdd-image-not-snapshotted')
        self.assertIn('XLLE_PILOT_DIAGNOSTIC_RECORD', env)

    def test_reproduction_uses_cloned_state_muted_audio_and_rejects_other_build(self):
        session, env = self.session(enabled=True)
        self.inputs(session)
        session.log_path.write_text('fixture')
        session.finish(2)
        work = self.base / 'reproduction'
        with patch('native_release_runtime.descriptor', return_value=self.data), \
                patch.object(replay_tool.subprocess, 'Popen') as launch:
            launch.return_value.wait.return_value = 4
            result = replay_tool.reproduce(session.path, self.root, self.image, work)
            args = launch.call_args.args[0]
            actual = launch.call_args.kwargs['env']
            self.assertEqual(args[-1], str(work / 'state'))
            self.assertEqual(args[2], str(work / 'console/eeprom.bin'))
            self.assertEqual(actual['XLLE_PILOT_AUDIO_OUTPUT_MUTE'], '1')
            self.assertEqual(actual['XLLE_PILOT_INPUT'], 'diagnostic-replay')
            self.assertEqual(actual['TEMP'], str(work / 'temp'))
            self.assertEqual(result['exit_code'], 4)
            self.assertFalse(result['deterministic_machine_replay'])
        self.assertEqual(self.progress.read_bytes(), b'original progress')
        (self.root / 'game.exe').write_bytes(b'other build')
        with patch('native_release_runtime.descriptor', return_value=self.data), self.assertRaisesRegex(ValueError, 'identical'):
            replay_tool.reproduce(session.path, self.root, self.image, self.base / 'bad')
        self.assertFalse((self.base / 'bad').exists())

    def test_transient_cache_and_original_dashboard_content_not_snapshotted(self):
        for partition in ('Partition2', 'Partition3', 'Partition4', 'Partition5'):
            path = self.state / partition / 'fixture'
            path.parent.mkdir()
            path.write_bytes(b'not included in a persistent save capture')
        (self.state / 'native-cache-v1.bin').write_bytes(b'cache manifest')
        session, _ = self.session(enabled=True)
        self.assertEqual(list(session.replay['state_files']), ['Partition1/UDATA/fixture/progress'])
        self.assertEqual(session.replay['state_policy'], 'persistent-partition1-only-cold-cache')

    def test_running_report_survives_parent_interruption_without_claiming_crash(self):
        session, _ = self.session()
        class Child:
            pid = 123
        session.started(Child())
        running = json.loads((session.path / 'report.json').read_text())
        self.assertEqual(running['state'], 'running')
        self.assertNotIn('exit_code', running)


if __name__ == '__main__':
    unittest.main()
