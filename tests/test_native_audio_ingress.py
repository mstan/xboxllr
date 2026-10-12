"""Independent source-ownership and actual-loop observation contract."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('ingress', ROOT / 'tools/native_audio_ingress.py')
ingress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ingress)


def record(kind, offset=0, payload=b'', *, voice=9, pointer=0x1234,
           capacity=32, start=8, end=24, looping=1, playing=1, time=0):
    return ingress.HEADER.pack(0x50414c58, 2, kind, 80, time, voice, pointer,
        48000, 1, 16, capacity, offset, len(payload), start, end, looping, playing) + payload


class IngressLoopContract(unittest.TestCase):
    def inspect(self, records, identity=9, window=2, overflow=0):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            data = b''.join(records)
            capture = folder / 'ingress'
            capture.write_bytes(data)
            log = folder / 'log'
            log.write_text(f'[AUDIO INGRESS] bytes=17 overflow=0 window=1\n'
                f'[AUDIO INGRESS] bytes={len(data)} overflow={overflow} window=2\n')
            return ingress.inspect(capture, log, identity, window)

    def snapshot(self, **kwargs):
        return record(3, payload=bytes(range(kwargs.get('capacity', 32))), **kwargs)

    def test_actual_partial_wrap(self):
        payload = bytes(range(20, 24)) + bytes(range(8, 16))
        report, pcm = self.inspect([self.snapshot(), record(2, 20, payload)])
        self.assertEqual(report['ring_byte_mismatches'], 0)
        self.assertEqual(pcm, payload)
        self.assertEqual(report['inventory'][0]['loop_states'][0]['end'], 24)

    def test_preloop_prefix_and_postloop_seek(self):
        for offset, payload in ((4, bytes(range(4, 24)) + bytes(range(8, 12))),
                                (28, bytes(range(28, 32)) + bytes(range(8, 16)))):
            with self.subTest(offset=offset):
                report, _ = self.inspect([self.snapshot(), record(2, offset, payload)])
                self.assertEqual(report['ring_byte_mismatches'], 0)

    def test_small_loop_repeats_within_one_real_chunk(self):
        payload = bytes(range(10, 12)) + bytes(range(8, 12)) * 10
        report, pcm = self.inspect([self.snapshot(end=12), record(2, 10, payload, end=12)])
        self.assertEqual(report['ring_byte_mismatches'], 0)
        self.assertEqual(len(pcm), 42)  # More than the owned capacity, not an OOB read.

    def test_caller_write_wraps_at_capacity(self):
        change = b'ABCDWXYZ'
        report, pcm = self.inspect([self.snapshot(), record(1, 28, change),
            record(2, 28, b'ABCD' + bytes(range(8, 12)))])
        self.assertEqual(report['ring_byte_mismatches'], 0)
        self.assertEqual(pcm[:4], b'ABCD')

    def test_loop_mode_and_span_changes_are_observed(self):
        records = [self.snapshot(), record(2, 20, bytes(range(20, 24)) + bytes(range(8, 12))),
            record(4, start=0, end=32, looping=0),
            record(2, 24, bytes(range(24, 32)), start=0, end=32, looping=0),
            record(4, start=4, end=12, looping=1),
            record(2, 8, bytes(range(8, 12)) + bytes(range(4, 8)), start=4, end=12)]
        report, _ = self.inspect(records)
        self.assertEqual(report['ring_byte_mismatches'], 0)
        self.assertEqual(report['inventory'][0]['controls'], 2)
        self.assertEqual(len(report['inventory'][0]['loop_states']), 3)

    def test_silent_full_capacity_assumption_is_a_byte_failure(self):
        report, _ = self.inspect([self.snapshot(start=0, end=32),
            record(2, 20, bytes(range(20, 24)) + bytes(range(8, 12)), start=0, end=32)])
        self.assertEqual(report['ring_byte_mismatches'], 4)

    def test_control_change_without_observation_rejects(self):
        with self.assertRaisesRegex(ValueError, 'without a captured control record'):
            self.inspect([self.snapshot(), record(2, 0, b'\0\1', looping=0)])

    def test_late_snapshot_preserves_span_and_live_caller_bytes(self):
        initial = bytearray(range(32)); initial[8:12] = b'ABCD'
        report, pcm = self.inspect([record(3, payload=initial), record(2, 22, b'\26\27ABCD')])
        self.assertEqual(report['ring_byte_mismatches'], 0)
        self.assertEqual(pcm, b'\26\27ABCD')

    def test_new_lifetime_does_not_inherit_old_snapshot(self):
        with self.assertRaisesRegex(ValueError, 'without captured caller ownership'):
            self.inspect([self.snapshot(voice=8), record(5, voice=8), record(2, 0, b'\0\0')])

    def test_pointer_reuse_requires_actual_retirement(self):
        with self.assertRaisesRegex(ValueError, 'before captured lifetime retirement'):
            self.inspect([self.snapshot(voice=8), self.snapshot()])
        report, _ = self.inspect([self.snapshot(voice=8), record(5, voice=8),
                                 self.snapshot(), record(2, 0, b'\0\1')])
        self.assertEqual(report['ring_byte_mismatches'], 0)

    def test_retired_id_rejects_further_work(self):
        with self.assertRaisesRegex(ValueError, 'retired buffer lifetime'):
            self.inspect([self.snapshot(), record(5), record(2, 0, b'\0\1')])

    def test_unknown_bytes_and_partial_initialization_reject(self):
        for records in ([record(2, 0, b'\0\0')],
                        [record(1, 0, b'AB'), record(2, 8, b'\0\0')]):
            with self.assertRaisesRegex(ValueError, 'without captured caller ownership'):
                self.inspect(records)

    def test_missing_and_invalid_metadata_reject(self):
        v1 = __import__('struct').pack('<4I3Q6I', 0x50414c58, 1, 3, 64, 0, 9,
                                     0x1234, 48000, 1, 16, 32, 0, 32) + bytes(32)
        with self.assertRaisesRegex(ValueError, 'lacks actual loop metadata'):
            self.inspect([v1])
        for kwargs in ({'start': 24, 'end': 24}, {'end': 34}, {'start': 9}, {'looping': 2}):
            with self.assertRaisesRegex(ValueError, 'actual loop metadata'):
                self.inspect([self.snapshot(**kwargs)])

    def test_finite_submission_cannot_wrap(self):
        with self.assertRaisesRegex(ValueError, 'finite submission wraps'):
            self.inspect([self.snapshot(looping=0), record(2, 28, b'ABCDEFGH', looping=0)])

    def test_full_capacity_loop_still_passes(self):
        report, pcm = self.inspect([self.snapshot(start=0, end=32),
            record(2, 28, bytes(range(28, 32)) + bytes(range(4)), start=0, end=32)])
        self.assertEqual(report['ring_byte_mismatches'], 0)
        self.assertEqual(len(pcm), 8)

    def test_snapshot_overflow_and_ambiguous_window_reject(self):
        with self.assertRaisesRegex(ValueError, 'complete owned ring'):
            self.inspect([record(3, payload=b'AB')])
        for kwargs in ({'overflow': 1}, {'window': None}):
            with self.assertRaisesRegex(ValueError, 'finished with zero overflow'):
                self.inspect([self.snapshot()], **kwargs)


if __name__ == '__main__':
    unittest.main()
