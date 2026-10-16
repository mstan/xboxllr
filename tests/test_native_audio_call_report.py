"""Checks for the passive retained-buffer lifecycle capture checkpoint."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from native_audio_call_report import retained_buffer_checkpoint


class RetainedBufferCheckpoint(unittest.TestCase):
    def checkpoint(self, operations, unfinished=b''):
        rows = [dict(schema='xboxlle.original-audio-calls.v1')]
        for identity, (method, obj, result, pcm, spdif) in enumerate(operations, 1):
            common = dict(id=identity, name='IDirectSoundBuffer_' + method,
                          ticks=identity, entry=1, address=1, thread=1,
                          fences=0, pcm_words=pcm, spdif_words=spdif)
            rows += [dict(common, phase='enter', args=[obj]),
                     dict(common, phase='return', eax=result)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'calls.jsonl'
            path.write_bytes(('\n'.join(map(json.dumps, rows))+'\n').encode()+unfinished)
            return retained_buffer_checkpoint(path, post_restart_frames=4)

    def sequence(self):
        return [('Play', 1, 0, 0, 0), ('Stop', 1, 0, 2, 2),
                ('Play', 1, 0, 4, 4), ('GetStatus', 1, 0, 12, 12)]

    def test_retained_buffer_needs_no_release_or_fence(self):
        result = self.checkpoint(self.sequence(), b'{"phase":')
        self.assertEqual(result['object'], 1)
        self.assertFalse(result['lifecycle_acceptance'])
        self.assertFalse(result['report']['milestones']['final_release_of_played_buffer'])

    def test_failed_stop_or_restart_cannot_trigger(self):
        for index in (1, 2):
            with self.subTest(index=index):
                rows = self.sequence()
                method, obj, _, pcm, spdif = rows[index]
                rows[index] = (method, obj, 0x80004005, pcm, spdif)
                self.assertIsNone(self.checkpoint(rows))

    def test_other_buffer_play_is_not_reuse(self):
        rows = self.sequence(); rows[2] = ('Play', 2, 0, 4, 4)
        self.assertIsNone(self.checkpoint(rows))

    def test_final_release_invalidates_pointer_identity(self):
        rows = self.sequence(); rows.insert(2, ('Release', 1, 0, 2, 2))
        self.assertIsNone(self.checkpoint(rows))

    def test_nonfinal_release_keeps_retained_object(self):
        rows = self.sequence(); rows.insert(2, ('Release', 1, 1, 2, 2))
        self.assertIsNotNone(self.checkpoint(rows))

    def test_both_streams_must_advance_after_restart(self):
        for pcm, spdif in ((11, 12), (12, 11)):
            with self.subTest(pcm=pcm, spdif=spdif):
                rows = self.sequence(); rows[-1] = ('GetStatus', 1, 0, pcm, spdif)
                self.assertIsNone(self.checkpoint(rows))

    def test_stop_or_release_cancels_post_restart_window(self):
        for method in ('Stop', 'Release'):
            with self.subTest(method=method):
                rows = self.sequence(); rows.insert(3, (method, 1, 0, 6, 6))
                self.assertIsNone(self.checkpoint(rows))

    def test_counter_reset_is_not_continued_output(self):
        rows = self.sequence(); rows.insert(3, ('GetStatus', 1, 0, 0, 0))
        with self.assertRaisesRegex(ValueError, 'counter moved backwards'):
            self.checkpoint(rows)


if __name__ == '__main__':
    unittest.main()
