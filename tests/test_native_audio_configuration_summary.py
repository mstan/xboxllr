"""Authored evidence checks for passive native configuration captures."""
import json
import hashlib
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_audio_configuration_summary import CREATE, PREFIX, summarize, summarize_rows


def event(record, name='CreateSoundBuffer', generation=1, base=4096, flags=0x40000,
          result=0, **changes):
    name = CREATE if name == 'CreateSoundBuffer' else PREFIX + name
    arguments = {CREATE: [256, 512, 768, 0], PREFIX + 'Play': [base + 28, 0, 0, 1],
                 PREFIX + 'SetBufferData': [base + 28, 8192, 128],
                 PREFIX + 'SetFrequency': [base + 28, 24000]}.get(name, [base + 28])
    row = dict(version=1, record=record, generation=generation, time_domain='qpc_100ns',
               time=record * 100, entry=name, result=result, base=base, data=8192,
               bytes=128, flags=flags, format=1, bits=16, channels=1,
               created_rate=22050, frequency=22050, volume=-200, user_headroom=0,
               mix=[[0, 0], [1, -100]], arguments=arguments)
    row.update(changes)
    return row


class ConfigurationSummary(unittest.TestCase):
    def test_spatial_and_2d_play_classification_and_configuration(self):
        rows = [event(1), event(2, 'CreateSoundBuffer', 2, 5000, 0x40110),
                event(3, 'SetFrequency', frequency=24000),
                event(4, 'Play', frequency=24000),
                event(5, 'Play', 2, 5000, 0x40110)]
        result = summarize_rows(rows)
        self.assertEqual(result['classifications']['2d']['successful_plays'], 1)
        self.assertEqual(result['classifications']['spatial']['played_generations'], 1)
        self.assertEqual(result['voices'][0]['plays'][0]['configuration']['frequency'], 24000)
        self.assertEqual(result['voices'][1]['flags'], 0x40110)
        self.assertFalse(result['spatial_output_acceptance'])
        self.assertEqual(result['spatial_paths']['deferred_commits'], 'not captured')

    def test_nonfinal_release_stop_restart_and_base_reuse(self):
        rows = [event(1), event(2, 'Play'), event(3, 'Release', result=1),
                event(4, 'Stop'), event(5, 'Play'), event(6, 'Release'),
                event(7, generation=2), event(8, 'Play', generation=2)]
        result = summarize_rows(rows)
        first, second = result['voices']
        self.assertEqual(first['explicit_stop_restarts'], 1)
        self.assertEqual(first['final_release_record'], 6)
        self.assertEqual(second['explicit_stop_restarts'], 0)
        self.assertEqual(result['live_generations'], [2])
        self.assertEqual(result['classifications']['2d']['played_generations'], 2)

    def test_stopping_other_voice_is_not_a_restart(self):
        result = summarize_rows([event(1), event(2, generation=2, base=5000),
                                 event(3, 'Play'), event(4, 'Stop', 2, 5000),
                                 event(5, 'Play')])
        self.assertEqual(result['voices'][0]['explicit_stop_restarts'], 0)

    def test_reject_uncreated_released_or_reused_live_identity(self):
        cases = [[event(1, 'Play')], [event(1), event(2, 'Release'), event(3, 'Play')],
                 [event(1), event(2, generation=2)],
                 [event(1), event(2, generation=1, base=5000)]]
        for rows in cases:
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                summarize_rows(rows)

    def test_failed_or_guarded_call_cannot_be_counted_as_success(self):
        for name in ('Play', 'Release'):
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'unsuccessful'):
                summarize_rows([event(1), event(2, name, result=0x80004005)])

    def test_identity_flags_and_interface_changes_are_rejected(self):
        for change in (dict(flags=0x40110), dict(channels=2), dict(base=5000),
                       dict(arguments=[9999, 0, 0, 0])):
            rows = [event(1), event(2, 'Play'), event(3, 'Play', **change)]
            with self.subTest(change=change), self.assertRaises(ValueError):
                summarize_rows(rows)

    def test_first_buffer_call_must_use_source_bound_interface(self):
        for arguments in ([9999, 0, 0, 1], [4096, 0, 0, 1], [0, 0, 0, 1]):
            with self.subTest(arguments=arguments), self.assertRaisesRegex(ValueError, 'native offset 28'):
                summarize_rows([event(1), event(2, 'Play', arguments=arguments)])

    def test_buffer_interface_addition_cannot_wrap_32_bits(self):
        base = 0xfffffff0
        with self.assertRaisesRegex(ValueError, 'native offset 28'):
            summarize_rows([event(1, base=base),
                            event(2, 'Play', base=base, arguments=[12, 0, 0, 1])])

    def test_sequence_clock_domain_schema_and_arguments_are_checked(self):
        for change in (dict(record=3), dict(time=0), dict(time_domain='pit_ticks'),
                       dict(version=True), dict(arguments=[4124]), dict(result=True),
                       dict(mix=[[0, 0], [0, 1]]), dict(generation=3)):
            row = event(2, 'Play'); row.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                summarize_rows([event(1), row])

    def test_unknown_spatial_event_is_not_silently_treated_as_captured(self):
        row = event(2, 'Play'); row['entry'] = PREFIX + 'SetPosition'
        with self.assertRaisesRegex(ValueError, 'not selected'):
            summarize_rows([event(1), row])

    def test_creation_requires_admitted_spatial_storage(self):
        for changes in (dict(flags=0x20000), dict(flags=0x10, channels=2)):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                summarize_rows([event(1, **changes)])

    def test_live_prefix_is_explicit_and_interior_corruption_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.jsonl'
            complete = (json.dumps(event(1)) + '\n').encode()
            path.write_bytes(complete + b'{"version":')
            with self.assertRaisesRegex(ValueError, 'incomplete final'):
                summarize(path)
            result = summarize(path, live=True)
            self.assertEqual(result['records'], 1)
            self.assertEqual(result['omitted_trailing_bytes'], 11)
            self.assertEqual(result['capture']['analyzed_prefix_sha256'],
                             hashlib.sha256(complete).hexdigest())
            self.assertTrue(result['partial'])
            path.write_bytes(complete + b'{bad}\n')
            with self.assertRaisesRegex(ValueError, 'invalid JSON'):
                summarize(path, live=True)

    def test_nonempty_complete_capture_is_required(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.jsonl'
            for data in (b'', b'{'):
                path.write_bytes(data)
                with self.subTest(data=data), self.assertRaises(ValueError):
                    summarize(path, live=True)

    def test_play_snapshot_is_not_overwritten_by_later_configuration(self):
        result = summarize_rows([event(1), event(2, 'Play'),
                                 event(3, 'SetBufferData', data=9000, bytes=256)])
        voice = result['voices'][0]
        self.assertEqual(voice['plays'][0]['configuration']['data'], 8192)
        self.assertEqual(voice['last_snapshot']['data'], 9000)
        self.assertEqual(voice['last_transport_command'], 'play')
        self.assertFalse(result['audio_acceptance'])

    def test_cli_reports_authored_capture_and_rejects_damage(self):
        tool = Path(__file__).resolve().parents[1] / 'tools/native_audio_configuration_summary.py'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'capture.jsonl'
            path.write_text('\n'.join(json.dumps(row) for row in
                                     [event(1, flags=0x40110), event(2, 'Play', flags=0x40110)])
                            + '\n', encoding='utf-8')
            command = [sys.executable, '-B', str(tool), str(path)]
            result = subprocess.run(command, capture_output=True, text=True, check=True)
            report = json.loads(result.stdout)
            self.assertEqual(report['classifications']['spatial']['successful_plays'], 1)
            self.assertFalse(report['audio_acceptance'])
            path.write_text('{broken}\n', encoding='utf-8')
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn('invalid JSON', result.stderr)


if __name__ == '__main__':
    unittest.main()
