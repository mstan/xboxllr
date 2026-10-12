import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
import numpy as np

spec = importlib.util.spec_from_file_location('closure', Path(__file__).resolve().parents[1]/'tools/native_reference_movie_closure.py')
closure = importlib.util.module_from_spec(spec)
spec.loader.exec_module(closure)


def movie(source, start, stop, final=False, media_end=100):
    point = dict(model=round((stop-.1)*1e7), pts=stop-start-.1,
                 reference_end_pts_lower_bound=media_end, reference_frames=2264,
                 second_rmse=2., rmse=0.)
    phase = dict(source=source, first_model=round(start*1e7), last_model=point['model'],
                 frame_max=2263 if final else 100, final_min_rmse=0. if final else 2., matches=[point])
    voice = dict(play_frames=[round(start*48000)], stop_frames=[round(stop*48000)],
                 destroyed=True, repeated_before_another_write_bytes=0)
    return phase, voice


def title(at):
    return dict(source='h_title02a', first_model=round(at*1e7))


class MovieReferenceMetadataTest(unittest.TestCase):
    def references(self, folder, frames):
        selected = None
        for index, name in enumerate(('microsoft', 'artoon001', 'op01', 'h_title02a')):
            refs = np.array([[[10+index*10, 200-index*10], [30+j*15, 240-j*15]]
                             for j in range(3)], dtype=np.uint8)
            (folder/(name+'.json')).write_text(json.dumps(dict(grid_width=2, grid_height=2, frames=frames)))
            refs.tofile(folder/(name+'.gray'))
            if name == 'op01': selected = refs
        return selected

    def test_missing_final_timestamp_retains_conservative_tail_bound(self):
        # Actual op01/artoon001 metadata have an untimed decoded final frame.
        # Matching still names that frame; the preceding PTS is only a bound.
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            refs = self.references(folder, [{'best_effort_timestamp_time': '0'},
                                           {'best_effort_timestamp_time': '.5'}, {}])
            rows = [dict(grid=refs[frame], model=frame*1000000, qpc=frame)
                    for frame in (0, 2)]
            phases, _ = closure.match_movies(rows, folder)
        self.assertEqual(len(phases), 1)
        self.assertEqual(phases[0]['source'], 'op01')
        self.assertEqual(phases[0]['frame_max'], 2)
        self.assertIsNone(phases[0]['matches'][-1]['pts'])
        self.assertEqual(phases[0]['matches'][-1]['reference_end_pts_lower_bound'], .5)

    def test_absent_nonfinite_or_backwards_reference_time_rejected(self):
        for times in ((None, None, None), ('0', 'nan', None), ('0', '-1', None), ('1', '0', None)):
            with self.subTest(times=times), tempfile.TemporaryDirectory() as temporary:
                folder = Path(temporary)
                self.references(folder, [{} if value is None else {'best_effort_timestamp_time': value}
                                         for value in times])
                with self.assertRaises(ValueError):
                    closure.match_movies([], folder)


class MovieClassificationTest(unittest.TestCase):
    def fixture(self, source='op01', start=210, stop=290.2, final=False, media_end=100):
        first, v1 = movie('op01', 10, 86, True)
        second, v2 = movie('op01', 110, 186, True)
        skipped, v3 = movie(source, start, stop, final, media_end)
        return [first, title(86.2), second, title(186.2), skipped, title(stop+.1)], [v1,v2,v3]

    def classify(self, phases, voices, press=290, release=291):
        return closure.classify_movie_lifetimes(phases, voices, 0, press, release)

    def test_late_op01_interruption_is_not_natural_by_duration(self):
        phases, voices = self.fixture()
        natural, interrupted, _, _ = self.classify(phases, voices)
        self.assertEqual(len(natural), 2)
        self.assertEqual(interrupted[0][0]['source'], 'op01')

    def test_known_logos_can_be_independently_truncated(self):
        for source in ('microsoft', 'artoon001'):
            with self.subTest(source=source):
                phases, voices = self.fixture(source, start=280, media_end=20)
                self.assertEqual(self.classify(phases, voices)[1][0][0]['source'], source)

    def test_natural_end_near_start_is_not_interruption(self):
        for source in ('microsoft', 'artoon001', 'op01'):
            with self.subTest(source=source):
                phases, voices = self.fixture(source, final=True, media_end=80.2)
                with self.assertRaises(ValueError):
                    self.classify(phases, voices)

    def test_unskippable_logo_chains_to_next_movie(self):
        phases, voices = self.fixture('microsoft', start=280, media_end=20)
        phases[-1] = dict(source='artoon001', first_model=2904000000)
        with self.assertRaises(ValueError):
            self.classify(phases, voices)

    def test_source_tail_insufficient_despite_no_final_frame_match(self):
        phases, voices = self.fixture(media_end=80.2)
        with self.assertRaises(ValueError):
            self.classify(phases, voices)

    def test_actual_retirement_delivery_and_transition_required(self):
        for change in ('destroy', 'stop', 'release', 'late_stop', 'early_title'):
            with self.subTest(change=change):
                phases, voices = self.fixture()
                release = 291
                if change == 'destroy': voices[-1]['destroyed'] = False
                if change == 'stop': voices[-1]['stop_frames'] = []
                if change == 'release': release = 290
                if change == 'late_stop': voices[-1]['stop_frames'] = [292*48000]
                if change == 'early_title': phases[-1] = title(289.9)
                with self.assertRaises(ValueError):
                    self.classify(phases, voices, release=release)

    def test_title_or_gameplay_is_not_a_movie(self):
        for source in ('h_title02a', 'gameplay'):
            phases, voices = self.fixture(source)
            with self.assertRaises(ValueError):
                self.classify(phases, voices)

    def test_ambiguous_play_binding_rejected(self):
        phases, voices = self.fixture()
        voices.append(dict(voices[-1]))
        with self.assertRaises(ValueError):
            self.classify(phases, voices)

    def test_two_natural_openings_require_final_picture_and_title(self):
        for change in ('picture', 'transition', 'unordered_title'):
            phases, voices = self.fixture()
            if change == 'picture': phases[0]['frame_max'] = 2200
            elif change == 'transition': phases[1]['source'] = 'gameplay'
            else: phases[1] = title(85)
            with self.assertRaises(ValueError):
                self.classify(phases, voices)

    def test_legitimate_delayed_title_and_sparse_picture_are_allowed(self):
        phases, voices = self.fixture()
        phases[1] = title(90)
        phases[3] = title(190)
        phases[-1] = title(300)
        point = phases[-2]['matches'][0]
        point['model'] = 2880000000
        point['pts'] = 78
        phases[-2]['last_model'] = point['model']
        natural, interrupted, _, _ = self.classify(phases, voices)
        self.assertEqual((len(natural),len(interrupted)), (2,1))

    def test_multiple_start_overlapping_movies_rejected(self):
        phases, voices = self.fixture()
        other, voice = movie('artoon001', 289, 290.3, media_end=10)
        phases.extend([other, title(290.4)])
        voices.append(voice)
        with self.assertRaises(ValueError):
            self.classify(phases, voices)

    def test_held_presentation_after_stop_needs_in_playback_source_evidence(self):
        phases, voices = self.fixture()
        movie_phase = phases[-2]
        held = dict(movie_phase['matches'][0], model=2908000000)
        movie_phase['matches'].append(held)
        movie_phase['last_model'] = held['model']
        phases[-1] = title(291)
        self.assertEqual(len(self.classify(phases, voices)[1]), 1)
        # A post-retirement picture alone does not prove the source was
        # truncated while the actual voice was still playing.
        movie_phase['matches'] = [held]
        with self.assertRaises(ValueError):
            self.classify(phases, voices)

    def test_idempotent_stops_retain_first_playback_retirement(self):
        phases, voices = self.fixture()
        # audio_reference.cpp records kind 5 on each successful Stop, including
        # a second Stop before Release while the voice is already stopped.
        for voice in voices:
            voice['stop_frames'].append(voice['stop_frames'][0]+2400)
        natural, interrupted, _, _ = self.classify(phases, voices)
        self.assertEqual(len(natural), 2)
        self.assertEqual(interrupted[0][1]['stop_frames'], [13929600,13932000])
        voices[-1]['stop_frames'].reverse()
        with self.assertRaises(ValueError):
            self.classify(phases, voices)

    def test_earlier_logo_and_later_unretired_opening_retained(self):
        phases, voices = self.fixture()
        logo, voice = movie('microsoft', 0, 5, True, 5)
        phases[:0] = [logo, title(5.1)]
        voices.insert(0, voice)
        later, voice = movie('op01', 310, 320)
        voice['destroyed'] = False
        voice['stop_frames'] = []
        phases.append(later)
        voices.append(voice)
        natural, interrupted, earlier, after = self.classify(phases, voices)
        self.assertEqual((len(natural),len(interrupted),len(earlier),len(after)), (2,1,1,1))
        self.assertFalse(after[0]['voice']['destroyed'])
        self.assertEqual(after[0]['voice']['stop_frames'], [])


if __name__ == '__main__':
    unittest.main()
