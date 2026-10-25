"""Complete storage baselines, sealed journals, isolated replay and refusal cases."""
import json
import gzip
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import native_session_diagnostics as session
import replay_native_session as replay
from native_diagnostics import digest


def actor_file(path, identity, actor, observations):
    with gzip.open(path, 'wb') as output:
        output.write(session.HEADER.pack(session.MAGIC, 2, actor, bytes.fromhex(identity)))
        for sequence, (tag, argument, payload) in enumerate(observations):
            first = struct.pack('<QIIQ', sequence, tag, len(payload), argument)
            output.write(first + struct.pack('<II', zlib.crc32(payload, zlib.crc32(first)), 0) + payload)


class FullSessionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'package'
        self.root.mkdir()
        (self.root / 'game.exe').write_bytes(b'authored native executable fixture')
        (self.root / 'release.json').write_text('{}')
        self.image = self.base / 'media/default.xbe'
        self.image.parent.mkdir()
        self.image.write_bytes(b'authored game identity')
        self.profile = self.root / 'data/console'
        self.profile.mkdir(parents=True)
        for name in ('eeprom.bin', 'save-keys.bin', 'console-settings.bin'):
            (self.profile / name).write_bytes(b'private console fixture')
        self.state = self.root / 'data/state'
        self.state.mkdir()
        (self.state / 'Partition3/empty-cache-directory').mkdir(parents=True)
        for name, data in [('Partition1/UDATA/progress', b'native progress'),
                           ('Partition3/cache', b'warm cache'), ('utility-cache.json', b'{"fixture":true}')]:
            path = self.state / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.data = dict(schema='xboxllr.native-preview.v1', executable='game.exe',
                         title=dict(name='Authored title', xbe_sha256=digest(self.image)),
                         capabilities=dict(session_replay_version=2), files={}, runtime={}, revisions=[],
                         framework_revision='a'*40, framework_dirty=False)

    def capture(self, *, hdd=None):
        env = {'XLLE_PILOT_INPUT': 'keyboard'}
        recorded = session.FullSession(self.root, self.data, self.root / 'game.exe', self.image,
                                       self.profile, self.state, {}, env, hdd=hdd)
        self.assertEqual(recorded.report['initial_snapshot'], 'complete')
        return recorded, env

    def seal(self, captured, *, final=True):
        folder = captured.private / 'journal'
        identity = captured.report['executable_sha256']
        actor_file(folder / 'actor-00000000.xlr', identity, 0, [
            (12, 1, struct.pack('<QQQQ', 1, 0x1111, 0x2222, 16384)),
            (15, 2, b'authored native stop')])
        actor_file(folder / 'actor-fffffffe.xlr', identity, session.SCHEDULE_ACTOR, [
            (session.TURN_TAG, 0, session.TURN.pack(0, 1, 0, 0)),
            (session.TURN_TAG, 0, session.TURN.pack(0, 6, 100, 0x1234))])
        captured.log_path.write_text('[D3D BACKEND STOP] authored native stop\n' +
            ('[SESSION REPLAY FINAL] records=4 written=4 complete=1\n' if final else ''))
        captured.finish(2)

    def test_complete_baseline_and_preserved_progress(self):
        captured, env = self.capture()
        self.assertEqual(env['XLLE_PILOT_SESSION_RECORD'], str(captured.private / 'journal'))
        self.assertEqual((captured.private / 'initial-state/Partition3/cache').read_bytes(), b'warm cache')
        self.assertTrue((captured.private / 'initial-state/utility-cache.json').is_file())
        self.assertTrue((captured.private / 'initial-state/Partition3/empty-cache-directory').is_dir())
        self.seal(captured)
        report, metadata, info = session.inspect_session(captured.path)
        self.assertTrue(report['journal_delivery_complete'])
        self.assertTrue(report['replay_available'])
        self.assertEqual((info['frames'], info['turns'], info['records']), (1, 2, 4))
        self.assertEqual(report['stop'], 'authored native stop')
        self.assertEqual((self.state / 'Partition1/UDATA/progress').read_bytes(), b'native progress')
        (captured.private / 'initial-state/Partition3/cache').write_bytes(b'altered')
        with self.assertRaisesRegex(ValueError, 'baseline changed'):
            session.inspect_session(captured.path)

    def test_unsealed_crash_never_claims_complete_replay(self):
        captured, _ = self.capture()
        self.seal(captured, final=False)
        self.assertFalse(captured.report['replay_available'])
        with self.assertRaisesRegex(ValueError, 'sealed terminal'):
            session.inspect_session(captured.path)

    def test_all_observations_and_terminal_seal_are_integrity_checked(self):
        captured, _ = self.capture()
        self.seal(captured)
        path = captured.private / 'journal/actor-00000000.xlr'
        original = path.read_bytes()
        raw = gzip.decompress(original)
        for bad in (raw[:-1], raw[:48+12] + b'\xff'*4 + raw[48+16:], raw[:-1] + b'X'):
            path.write_bytes(gzip.compress(bad))
            with self.assertRaises(ValueError):
                session.inspect_session(captured.path)
        for bad in (original[:-6],original[:-8]+bytes([original[-8]^1])+original[-7:]):
            path.write_bytes(bad)
            with self.assertRaisesRegex(ValueError,'Compressed journal'):
                session.inspect_session(captured.path)
        path.write_bytes(original)
        self.assertTrue(session.inspect_session(captured.path)[2]['terminal_complete'])

    def test_hdd_is_an_independent_sparse_snapshot(self):
        disk = self.base / 'owned.img'
        disk.write_bytes(b'volume' + bytes(3 * 1024 * 1024) + b'last')
        captured, _ = self.capture(hdd=dict(path=str(disk), identity='b'*64))
        self.assertEqual(digest(captured.private / 'initial-hdd.img'), digest(disk))
        self.seal(captured)
        self.assertEqual(session.inspect_session(captured.path)[1]['hdd']['size'], disk.stat().st_size)
        disk.write_bytes(b'owner progress changed later')
        self.assertTrue(session.inspect_session(captured.path)[2]['terminal_complete'])

    def test_storage_failure_is_explicit_and_never_arms_partial_capture(self):
        env = {}
        with patch.object(session.shutil, 'disk_usage', return_value=type('Disk', (), {'free': 0})()):
            captured = session.FullSession(self.root, self.data, self.root / 'game.exe', self.image,
                                           self.profile, self.state, {}, env)
        self.assertEqual(captured.report['initial_snapshot'], 'unavailable')
        self.assertNotIn('XLLE_PILOT_SESSION_RECORD', env)
        self.assertTrue((captured.private / 'capture-errors.txt').is_file())

    def test_replay_isolated_storage_muted_environment_and_verified_exit(self):
        captured, _ = self.capture()
        self.seal(captured)
        work = self.base / 'replay'
        observed = {}

        class Child:
            def __init__(child, command, **kwargs):
                observed.update(command=command, **kwargs)
                kwargs['stdout'].write('[SESSION REPLAY VERIFIED] terminal boundary and all observations match\n')
                kwargs['stdout'].flush()
            def wait(child, timeout=None):
                return 2

        import native_release_runtime
        with patch.object(native_release_runtime, 'descriptor', return_value=self.data), \
             patch.object(replay.subprocess, 'Popen', Child):
            result = replay.reproduce(captured.path, self.root, self.image, work)
        self.assertTrue(result['verified'])
        self.assertEqual(result['exit_code'], 2)
        self.assertEqual(observed['cwd'], work / 'state')
        self.assertEqual(observed['env']['RECOMP_VOLUME'], '0')
        self.assertEqual(observed['env']['XLLE_PILOT_INPUT'], 'none')
        self.assertTrue((work / 'state/Partition3/empty-cache-directory').is_dir())
        (work / 'state/Partition1/UDATA/progress').write_bytes(b'replayed progress')
        self.assertEqual((self.state / 'Partition1/UDATA/progress').read_bytes(), b'native progress')
        with self.assertRaises(ValueError):
            replay.reproduce(captured.path, self.root, self.image, self.root / 'unsafe')
        with self.assertRaises(ValueError):
            replay.reproduce(captured.path, self.root, self.image, self.base / 'new', seconds=float('nan'))

    def test_replay_environment_drops_private_paths_and_remote_control(self):
        work = self.base / 'env'
        work.mkdir()
        env = replay.replay_environment(dict(runtime={'XLLE_PILOT_DEBUG_PORT': '1'}), {
            'XLLE_PILOT_HDD_IMAGE': str(self.base / 'owned.img'),
            'XLLE_PILOT_DEBUG_PORT': '1234', 'XLLE_PILOT_INPUT': 'sdl',
            'XLLE_PILOT_INPUT_GUID': 'fixture', 'RECOMP_VOLUME': '100'}, work)
        self.assertNotIn('XLLE_PILOT_HDD_IMAGE', env)
        self.assertNotIn('XLLE_PILOT_DEBUG_PORT', env)
        self.assertEqual(env['XLLE_PILOT_INPUT'], 'none')
        self.assertEqual(env['RECOMP_VOLUME'], '0')


if __name__ == '__main__':
    unittest.main()
