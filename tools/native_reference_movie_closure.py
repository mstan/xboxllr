#!/usr/bin/env python3
"""Review one finalized reference movie batch, without another title run.

Pixel-source matching names movie phases independently of file-open logging.
One audio alignment is used for each actual Play lifetime. A numeric pass still
requires the retained scene review for fresh demo reentry; this tool never
updates the family burndown or treats diagnostic termination as guest teardown.
"""
import os
os.environ.update(OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1', MKL_NUM_THREADS='1')
import argparse
import hashlib
import importlib.util
import json
import math
import re
import struct
from fractions import Fraction
from pathlib import Path
import numpy as np
from scipy.signal import correlate, resample_poly

LIMITS = dict(waveform_absolute_rmse_max=.02, waveform_correlation_min=.99,
              av_absolute_p95_seconds_max=2/29.94, video_rmse_max=1.,
              video_source_separation_min=1., video_frame_separation_min=.25,
              distinctive_standard_deviation_min=10., audio_anchor_second=10,
              audio_anchor_search_seconds=.1, amplitude_gain=1.,
              natural_opening_seconds_min=74., natural_final_frame=2263,
              start_to_stop_seconds_max=1.,
              source_pitch_rational_phase_error_frames_max=.01,
              source_pitch_rational_denominator_max=65536,
              ring_byte_mismatches_max=0, repeated_movie_source_bytes_max=0)


def identity(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(4*1024*1024), b''):
            digest.update(block)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest.hexdigest())


def read_movie(path):
    rows = []
    with path.open('rb') as stream:
        while header := stream.read(88):
            if len(header) != 88:
                raise ValueError('partial movie header')
            words = struct.unpack('<14I', header[:56])
            draw, present, serial, model = struct.unpack('<4Q', header[56:])
            raw = stream.read(words[11])
            if (words[0] != 0x434d4c58 or words[1] != 2 or words[7] != 16 or
                    words[11] != words[8]*words[9] or len(raw) != words[11] or present < draw):
                raise ValueError('invalid movie record')
            if rows and (model < rows[-1]['model'] or serial <= rows[-1]['serial']):
                raise ValueError('backwards movie timeline')
            rows.append(dict(model=model, qpc=present, serial=serial,
                             grid=np.frombuffer(raw, dtype=np.uint8).reshape(words[9], words[8]).copy()))
    return rows


def match_movies(rows, reference):
    choices = [[] for _ in rows]
    pins = []
    for name in ('microsoft', 'artoon001', 'op01', 'h_title02a'):
        metadata = reference/(name+'.json'); gray = metadata.with_suffix('.gray')
        meta = json.loads(metadata.read_text()); w, h = meta['grid_width'], meta['grid_height']
        timestamps = [float(frame['best_effort_timestamp_time']) for frame in meta['frames']
                      if frame.get('best_effort_timestamp_time') is not None]
        if (not timestamps or any(not math.isfinite(value) or value < 0 for value in timestamps) or
                any(a > b for a,b in zip(timestamps, timestamps[1:]))):
            raise ValueError('reference movie timestamps are absent or invalid: '+name)
        # Some decoded final frames have no PTS. The last recorded timestamp
        # bounds the media end from below; do not invent a final-frame time.
        end_pts_lower_bound = timestamps[-1]
        refs = np.fromfile(gray, dtype=np.uint8).reshape(-1, w*h).astype(np.float32)
        norms = np.sum(refs*refs, axis=1)
        pins.extend((identity(metadata), identity(gray)))
        for begin in range(0, len(rows), 32):
            batch = rows[begin:begin+32]
            samples = np.array([row['grid'][:h, :w].ravel() for row in batch], dtype=np.float32)
            distances = np.maximum(np.sum(samples*samples, axis=1)[:, None] + norms[None, :] - 2*samples@refs.T, 0)/(w*h)
            best = np.argmin(distances, axis=1)
            seconds = np.partition(distances, 1, axis=1)[:, 1]
            for at, frame in enumerate(best):
                timestamp = meta['frames'][int(frame)].get('best_effort_timestamp_time')
                choices[begin+at].append(dict(source=name, frame=int(frame), pts=None if timestamp is None else float(timestamp),
                                              rmse=float(np.sqrt(distances[at, frame])), second_rmse=float(np.sqrt(seconds[at])),
                                              std=float(samples[at].std()), reference_frames=len(refs),
                                              reference_end_pts_lower_bound=end_pts_lower_bound,
                                              final_rmse=float(np.sqrt(distances[at, -1]))))
    accepted = []
    for index, candidates in enumerate(choices):
        candidates.sort(key=lambda item: item['rmse']); first, other = candidates[:2]
        if (first['std'] >= LIMITS['distinctive_standard_deviation_min'] and first['rmse'] < LIMITS['video_rmse_max'] and
                other['rmse']-first['rmse'] >= LIMITS['video_source_separation_min']):
            accepted.append(dict(record=index, model=rows[index]['model'], qpc=rows[index]['qpc'], **first))
    # A genuine title/other-source transition separates opening lifetimes.
    # Identical frames held within a movie are never treated as a new lifetime.
    phases = []
    for row in accepted:
        if not phases or phases[-1]['source'] != row['source']:
            phases.append(dict(source=row['source'], matches=[]))
        phases[-1]['matches'].append(row)
    for phase in phases:
        matched = phase['matches']; phase.update(first_model=matched[0]['model'], last_model=matched[-1]['model'],
                                               frame_min=min(row['frame'] for row in matched),
                                               frame_max=max(row['frame'] for row in matched),
                                               final_min_rmse=min(row['final_rmse'] for row in matched))
    return phases, pins


def encoded_playback(samples, requested_frequency, source_sample_rate):
    # Actual original constructor/SetFrequency packing is independently bound
    # by native_audio_default_reference.py: 44100 -> -501, 48000 -> 0.
    # A capture's frequency is the request, not the resulting source clock.
    # Never estimate a rate from the candidate waveform or realign windows.
    pitches = {44100: -501, 48000: 0}
    if requested_frequency not in pitches:
        raise ValueError('unsupported observed movie DMA frequency')
    pitch = pitches[requested_frequency]
    effective = 48000*math.exp2(pitch/4096)
    ratio = 48000*requested_frequency/(source_sample_rate*effective)
    rational = Fraction(ratio).limit_denominator(LIMITS['source_pitch_rational_denominator_max'])
    phase_error = len(samples)*abs(float(rational)-ratio)
    if phase_error > LIMITS['source_pitch_rational_phase_error_frames_max']:
        raise ValueError('source-derived resampling ratio exceeds phase bound')
    converted = samples if rational == 1 else resample_poly(samples, rational.numerator, rational.denominator, axis=0)
    return converted, dict(requested_frequency=requested_frequency, encoded_pitch=pitch,
                           encoded_source_rate_hz=effective, source_sample_rate=source_sample_rate,
                           output_rate=48000,
                           output_per_input=[rational.numerator,rational.denominator],
                           maximum_rational_phase_error_frames=phase_error, fitted_rate=False)


def align_audio(voice, source, gp):
    if tuple(voice['shape'][:2]) != (2, 16) or len(voice['source_bytes_by_rate']) != 1:
        raise ValueError('movie voice needs one observed stereo PCM16 DMA rate')
    rate = int(next(iter(voice['source_bytes_by_rate'])))
    caller = np.frombuffer(source, dtype='<i2').reshape(-1, 2).astype(np.float64)/32768
    expected, playback = encoded_playback(caller, rate, rate)
    anchor = LIMITS['audio_anchor_second']*48000; width = 48000
    estimate = voice['play_frames'][0]; radius = round(LIMITS['audio_anchor_search_seconds']*48000)
    lo = max(0, estimate+anchor-radius); hi = min(len(gp), estimate+anchor+width+radius)
    needle = expected[anchor:anchor+width, 0]; needle = needle-needle.mean()
    if len(needle) != width or hi-lo < width:
        raise ValueError('complete declared audio anchor unavailable')
    hay = np.asarray(gp[lo:hi, 0], dtype=np.float64)
    dots = correlate(hay, needle, mode='valid', method='fft')
    sums = np.r_[0, np.cumsum(hay)]; squares = np.r_[0, np.cumsum(hay*hay)]
    variance = np.maximum(squares[width:]-squares[:-width]-(sums[width:]-sums[:-width])**2/width, 1e-20)
    scores = dots/np.sqrt(variance*np.dot(needle, needle)); best = int(scores.argmax())
    return lo+best-anchor, expected, float(scores[best]), playback


def bind_opening_lifetimes(openings, voices, epoch, press):
    required, later = [], []
    for phase in openings:
        start_frame = round((phase['first_model']/1e7-epoch)*48000)
        candidates = [voice for voice in voices if len(voice['play_frames']) == 1 and
                      abs(voice['play_frames'][0]-start_frame) < 48000]
        if len(candidates) != 1:
            raise ValueError('movie source does not identify one actual Play lifetime')
        voice = candidates[0]
        # A fixed diagnostic may reach another opening after the declared
        # interruption/reentry. Its termination is not that new voice's Stop.
        # Keep its data; it cannot replace a required pre-Start lifetime.
        if epoch+voice['play_frames'][0]/48000 > press:
            later.append(dict(voice=voice,
                video={key:value for key,value in phase.items() if key!='matches'},
                no_source_rereads=voice['repeated_before_another_write_bytes']==0,
                excluded_from_required_lifetimes=True,
                reason='actual Play begins after the declared Start interruption'))
        else:
            if not voice['destroyed'] or not voice['stop_frames']:
                raise ValueError('opening lacks actual Stop/destroy')
            required.append((phase, voice))
    return required, later


def classify_movie_lifetimes(phases, voices, epoch, press, release):
    """Classify actual retirement, never duration alone or diagnostic cutoff.

    A source-derived remaining media interval must exceed the time left until
    Stop plus the existing A/V uncertainty. Merely stopping near Start at EOS
    cannot establish interruption. The immediate independently named successor
    must be title, not another normally chained logo/movie.
    """
    if release <= press:
        raise ValueError('Start release must follow actual delivery')
    movies = [phase for phase in phases if phase['source'] in ('microsoft', 'artoon001', 'op01')]
    required, later = bind_opening_lifetimes(movies, voices, epoch, press)
    natural, interrupted, earlier = [], [], []
    uncertainty = LIMITS['av_absolute_p95_seconds_max']
    for phase, voice in required:
        # Successful idempotent Stop calls are all recorded. The first retires
        # playback; subsequent real calls remain evidence, not new lifetimes.
        stops = voice['stop_frames']
        if (not stops or stops[0] < voice['play_frames'][0] or
                any(a > b for a,b in zip(stops, stops[1:]))):
            raise ValueError('movie Stop records are absent or unordered')
        play = epoch+voice['play_frames'][0]/48000
        stop = epoch+voice['stop_frames'][0]/48000
        duration = stop-play
        index = next(i for i, candidate in enumerate(phases) if candidate is phase)
        successor = phases[index+1] if index+1 < len(phases) else None
        completed = (phase['source'] == 'op01' and
                     duration >= LIMITS['natural_opening_seconds_min'] and
                     phase['frame_max'] == LIMITS['natural_final_frame'] and
                     phase['final_min_rmse'] < LIMITS['video_rmse_max'])
        if completed and stop < press:
            if (successor is None or successor['source'] != 'h_title02a' or
                    successor['first_model']/1e7 < stop-uncertainty):
                raise ValueError('completed opening lacks actual source-to-title transition')
            natural.append((phase, voice))
            continue
        if play < press <= stop:
            points = [point for point in phase['matches'] if point['pts'] is not None and
                      point['second_rmse']-point['rmse'] >= LIMITS['video_frame_separation_min']]
            truncated = bool(points) and all(
                play-uncertainty <= point['model']/1e7 <= stop+uncertainty and
                0 <= point['pts'] < point['reference_end_pts_lower_bound'] and
                point['reference_end_pts_lower_bound']-point['pts'] >
                stop-point['model']/1e7+uncertainty for point in points)
            direct_title = (successor is not None and successor['source'] == 'h_title02a' and
                            press < successor['first_model']/1e7 and
                            stop-uncertainty <= successor['first_model']/1e7)
            if (stop-press > LIMITS['start_to_stop_seconds_max'] or
                    phase['first_model']/1e7 > press or
                    phase['final_min_rmse'] < LIMITS['video_rmse_max'] or
                    phase['frame_max'] >= phase['matches'][0]['reference_frames']-1 or
                    not truncated or not direct_title):
                raise ValueError('Start-overlapping movie lacks source truncation/direct title transition')
            interrupted.append((phase, voice))
        else:
            earlier.append((phase, voice))
    if len(natural) != 2 or len(interrupted) != 1:
        raise ValueError('requires two completed op01 lifetimes and one unique actual movie interruption')
    return natural, interrupted, earlier, later


def evaluate(folder, reference, output):
    output.mkdir(); (output/'declared-limits.aot-private.json').write_text(json.dumps(LIMITS, indent=2)+'\n')
    manifest_path = folder/'run.aot-private.json'; manifest = json.loads(manifest_path.read_text())
    if manifest['stop_lines'] or manifest['termination_reason'] != 'successful-bounded-capture-end':
        raise ValueError('batch is not a finalized zero-stop bounded observation')
    verified = []
    for old in manifest['artifacts']:
        actual = identity(Path(old['path']))
        if actual != old:
            raise ValueError('artifact changed: '+old['path'])
        verified.append(actual)
    log_path = folder/'title.log'; log = log_path.read_text(errors='replace')
    if ('[SCHEDULING] LLE original thread/event/timer namespace' not in log or
            '[REFERENCE TIME] stopped input:' not in log):
        raise ValueError('required original scheduling/common-clock route absent')
    deliveries = [dict(row=int(match[0]), scheduled_ms=int(match[1]), elapsed_ms=int(match[2]), digital=match[3])
                  for match in re.findall(r'\[INPUT REPLAY DELIVERED\] row=(\d+) scheduled_ms=(\d+) elapsed_ms=(\d+) digital=([0-9a-f]+)', log)]
    if [row['digital'] for row in deliveries] != ['0000', '0010', '0000']:
        raise ValueError('actual Start/release delivery absent')
    if not all(a['elapsed_ms'] < b['elapsed_ms'] for a,b in zip(deliveries, deliveries[1:])):
        raise ValueError('actual input deliveries are not strictly ordered')
    frozen_inspector = folder/'source/native_audio_reference_capture.py'
    spec = importlib.util.spec_from_file_location('frozen_reference_capture', frozen_inspector)
    capture = importlib.util.module_from_spec(spec); spec.loader.exec_module(capture)
    gp_path = folder/'capture/engine-1.gp.f32'
    audio, sources, records = capture.inspect(folder/'capture/engine-1.dma.bin', log=log_path)
    if gp_path.stat().st_size % 24:
        raise ValueError('partial six-channel GP output')
    gp = np.memmap(gp_path, dtype='<f4', mode='r').reshape(-1, 6)
    if not np.isfinite(gp).all():
        raise ValueError('nonfinite GP output')
    endings = re.findall(r'\[REFERENCE CAPTURE END\] engine=1 epoch_ticks=\d+ frames=(\d+)', log)
    if len(endings) != 1 or int(endings[0]) != len(gp):
        raise ValueError('GP output frame count differs from finalization')
    epoch = audio['epoch_pit_ticks']/1125000
    press = deliveries[1]['elapsed_ms']/1000
    movie_rows = read_movie(folder/'movie.bin'); phases, references = match_movies(movie_rows, reference)
    comparisons = []
    naturals, interruptions, earlier, later_openings = classify_movie_lifetimes(
        phases, audio['voices'], epoch, press, deliveries[2]['elapsed_ms']/1000)
    for natural, (phase, voice) in [(True, item) for item in naturals]+[(False, item) for item in interruptions]:
        stopped = voice['stop_frames'][0]; duration = (stopped-voice['play_frames'][0])/48000
        row = dict(voice=voice, video={key: value for key, value in phase.items() if key != 'matches'},
                   consumed_model_seconds=duration, natural=natural,
                   no_source_rereads=voice['repeated_before_another_write_bytes']==0)
        if natural:
            offset, expected, score, playback = align_audio(voice, sources[voice['id']], gp)
            # Compare the full independent media span, including the quiet tail.
            # DMA prefetch beyond media EOS is not invented output correctness.
            media_path = reference/'op01.f32'; media_meta = media_path.with_suffix('.json')
            media = np.fromfile(media_path, dtype='<f4').reshape(-1, 2).astype(np.float64)
            # The independent file is nominal 48000-Hz media. Apply the same
            # original, source-derived pitch to its nominal source timeline.
            media, media_playback = encoded_playback(media, playback['requested_frequency'],48000)
            count = len(media)
            if offset < 0 or count > len(expected) or offset+count > stopped or offset+count > len(gp):
                raise ValueError('full declared media span not present before actual Stop')
            actual = np.asarray(gp[offset:offset+count, :2], dtype=np.float64)
            error = actual-expected[:count]; independent = actual-media
            rmse = np.sqrt(np.mean(error*error, axis=0)).tolist()
            corr = [float(np.corrcoef(actual[:,ch], expected[:count,ch])[0,1]) for ch in range(2)]
            media_rmse = np.sqrt(np.mean(independent*independent, axis=0)).tolist()
            media_corr = [float(np.corrcoef(actual[:,ch], media[:,ch])[0,1]) for ch in range(2)]
            zero = epoch+offset/48000
            av = [point['model']/1e7-zero-point['pts'] for point in phase['matches'] if point['pts'] is not None and
                  point['second_rmse']-point['rmse'] >= LIMITS['video_frame_separation_min']]
            if len(av) < 100:
                raise ValueError('insufficient independently distinctive opening timing samples')
            av95 = float(np.percentile(np.abs(av), 95))
            row.update(fixed_offset_frames=offset, one_anchor_per_play=True, anchor_correlation=score,
                       playback=playback, independent_media_playback=media_playback,
                       compared_frames=count, caller_rmse=rmse, caller_correlation=corr,
                       independent_media_rmse=media_rmse, independent_media_correlation=media_corr,
                       independent_media=[identity(media_path), identity(media_meta)], av_absolute_p95_seconds=av95,
                       av_samples=len(av), numeric_pass=max(rmse+media_rmse)<=LIMITS['waveform_absolute_rmse_max'] and
                       min(corr+media_corr)>=LIMITS['waveform_correlation_min'] and av95<=LIMITS['av_absolute_p95_seconds_max'])
            references.extend(row['independent_media'])
        comparisons.append(row)
    natural = [row for row in comparisons if row['natural']]
    interrupted = [row for row in comparisons if not row['natural']]
    checks = dict(exact_source_ring_bytes=audio['ring_byte_mismatches']==0,
                  two_natural_openings=len(natural)==2,
                  distinctive_final_picture=all(row['video']['frame_max']==LIMITS['natural_final_frame'] and
                                               row['video']['final_min_rmse']<LIMITS['video_rmse_max'] for row in natural),
                  natural_waveform_and_av=all(row['numeric_pass'] for row in natural),
                  movie_no_rereads=all(row['no_source_rereads'] for row in comparisons+later_openings) and
                                  all(voice['repeated_before_another_write_bytes']==0 for _, voice in earlier),
                  actual_interruption=len(interrupted)==1 and epoch+interrupted[0]['voice']['play_frames'][0]/48000<press<=
                                      epoch+interrupted[0]['voice']['stop_frames'][0]/48000 and
                                      epoch+interrupted[0]['voice']['stop_frames'][0]/48000-press<=LIMITS['start_to_stop_seconds_max'],
                  subsequent_title=bool(interrupted) and any(phase['source']=='h_title02a' and
                                      phase['first_model']/1e7>press for phase in phases))
    configuration = [json.loads(line) for line in (folder/'configuration.jsonl').read_text().splitlines()]
    if not configuration or [row['record'] for row in configuration] != list(range(1, len(configuration)+1)):
        raise ValueError('configuration census is missing or discontinuous')
    census = dict(records=len(configuration), generations=len({row['generation'] for row in configuration}),
                  modes=[dict(format=mode[0], bits=mode[1], channels=mode[2], created_rate=mode[3], frequency=mode[4],
                              volume=mode[5], user_headroom=mode[6], mix=json.loads(mode[7])) for mode in sorted({
                              (row['format'],row['bits'],row['channels'],row['created_rate'],row['frequency'],row['volume'],
                               row['user_headroom'],json.dumps(row['mix'])) for row in configuration})],
                  entries=sorted({row['entry'] for row in configuration}))
    result = dict(numeric_result='PASS' if all(checks.values()) else 'FAIL', checks=checks, limits=LIMITS,
                  scope='Finalized original-scheduling/reference-APU movie evidence; native audio/input/graphics callers remain selected',
                  family_qualified=False, fresh_demo_scene_review_required=True, diagnostic_termination_is_guest_teardown=False,
                  manifest=identity(manifest_path), analysis=identity(Path(__file__)), verified_artifacts=verified,
                  reference_inputs=references, audio=audio, opening_lifetimes=comparisons, deliveries=deliveries,
                  required_opening_window=dict(actual_play_not_after_start_seconds=press,
                      natural_lifetimes=2,interrupted_lifetimes=1),
                  later_opening_lifetimes=later_openings,
                  earlier_movie_lifetimes=[dict(voice=voice, video={key:value for key,value in phase.items() if key!='matches'})
                                           for phase,voice in earlier],
                  movie_records=len(movie_rows), phases=[{key:value for key,value in phase.items() if key!='matches'} for phase in phases],
                  configuration_census=census, present_files=[identity(path) for path in sorted(folder.glob('present-*.ppm'))],
                  pending=['Confirm fresh demo gameplay after third Start/release from retained scene captures.',
                           'Review final configuration census against the reached original-stage inventory.',
                           'Root reconciles retained original thread/IRQ/FP proofs before family closure.'], hardware_operations=0)
    result_path=output/'review.aot-private.json';result_path.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(result=result['numeric_result'],checks=checks,report=identity(result_path)),indent=2))
    return int(not all(checks.values()))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path);parser.add_argument('reference',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    return evaluate(args.run,args.reference,args.output)


if __name__=='__main__':
    raise SystemExit(main())
