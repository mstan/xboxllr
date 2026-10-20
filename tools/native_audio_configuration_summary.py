#!/usr/bin/env python3
"""Summarize the passive native audio configuration JSONL capture offline.

Activation: set XLLE_PILOT_AUDIO_CONFIGURATION_CAPTURE to a new private file in
the title manifest's [runtime] configuration. The public native_title.py run
command removes inherited XLLE_PILOT_* variables before applying that manifest;
an inherited shell variable alone does not enable capture. The runtime opens
the file exclusively, flushes every record, and stops before record 32769.

Version 1 observes accepted configuration calls, not samples or complete voice
state. In particular it omits spatial/listener setters, deferred commits,
AddRef, Pause, natural completion, failed calls and caller addresses. This
report cannot certify spatial output or whole-title audio acceptance.
Version 1 uses the source-enforced native buffer interface offset of 28 bytes;
every buffer call must name that interface, including its first observation.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path


CREATE = 'IDirectSound_CreateSoundBuffer'
PREFIX = 'IDirectSoundBuffer_'
BUFFER_INTERFACE_OFFSET = 28
ARGUMENT_COUNTS = {CREATE: 4, **{PREFIX + name: count for name, count in (
    ('Release', 1), ('SetBufferData', 3), ('SetMixBins', 2),
    ('SetMixBinVolumes_8', 2), ('Play', 4), ('Stop', 1),
    ('SetCurrentPosition', 2), ('SetVolume', 2), ('SetHeadroom', 2),
    ('SetFrequency', 2), ('SetPitch', 2), ('SetLoopRegion', 3))}}
IDENTITY_FIELDS = ('base', 'flags', 'format', 'bits', 'channels', 'created_rate')
UINT_FIELDS = ('record', 'generation', 'result', 'base', 'data', 'bytes', 'flags',
               'format', 'bits', 'channels', 'created_rate', 'frequency', 'user_headroom')
SNAPSHOT_FIELDS = ('data', 'bytes', 'format', 'bits', 'channels', 'created_rate',
                   'frequency', 'volume', 'user_headroom', 'mix')
LIMITATIONS = [
    'No footer: the capture does not establish normal run completion or a complete attract cycle.',
    'Only successful selected calls are recorded; failures and guarded calls are absent.',
    'Spatial source/listener setters and deferred commits are not captured in version 1.',
    'Creation flags classify requested storage; they do not establish source mode or spatial output.',
    'AddRef, Pause, GetStatus and natural completion are absent; the last transport command is not playback state.',
    'Snapshots contain addresses and settings, not source bytes, rendered samples, audibility or output correctness.',
    'No caller address, title scene or input sequence is recorded; correlate those using separate run evidence.',
]


def _integer(value, name, maximum=0xffffffff, minimum=0):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError('invalid ' + name)


def _validate(row, number):
    if not isinstance(row, dict) or type(row.get('version')) is not int or row['version'] != 1:
        raise ValueError('unsupported audio configuration version')
    for key in UINT_FIELDS:
        _integer(row.get(key), key)
    _integer(row.get('time'), 'time', (1 << 64) - 1)
    _integer(row.get('volume'), 'volume', 0x7fffffff, -0x80000000)
    if row['record'] != number or number > 32768:
        raise ValueError('noncontiguous or excessive capture record')
    if not row['generation'] or not row['base'] or row['result'] & 0x80000000:
        raise ValueError('invalid generation/base or unsuccessful captured call')
    if row.get('time_domain') not in ('pit_ticks', 'qpc_100ns'):
        raise ValueError('unsupported capture time domain')
    entry = row.get('entry')
    if not isinstance(entry, str) or entry not in ARGUMENT_COUNTS:
        raise ValueError('entry is not selected by configuration capture version 1')
    args = row.get('arguments')
    if not isinstance(args, list) or len(args) != ARGUMENT_COUNTS[entry]:
        raise ValueError('incorrect captured argument count')
    for value in args:
        _integer(value, 'argument')
    mix = row.get('mix')
    if not isinstance(mix, list) or len(mix) > 32:
        raise ValueError('invalid mix snapshot')
    bins = set()
    for pair in mix:
        if not isinstance(pair, list) or len(pair) != 2:
            raise ValueError('invalid mix entry')
        _integer(pair[0], 'mix bin', 31)
        _integer(pair[1], 'mix volume', 0x7fffffff, -0x80000000)
        if pair[0] in bins:
            raise ValueError('duplicate mix bin')
        bins.add(pair[0])


def summarize(path, live=False):
    """Validate one capture and correlate stable generations, including reuse.

    --live accepts only an incomplete final line, never an interior damaged
    record. Read once so a concurrently appended file has an immutable prefix.
    """
    path = Path(path)
    data = path.read_bytes()
    captured_bytes = len(data)
    captured_sha256 = hashlib.sha256(data).hexdigest()
    omitted = 0
    if data and not data.endswith(b'\n'):
        if not live:
            raise ValueError('incomplete final capture record (use --live for an active capture)')
        end = data.rfind(b'\n') + 1
        omitted = len(data) - end
        data = data[:end]
    rows = []
    for number, line in enumerate(data.splitlines(), 1):
        try:
            row = json.loads(line)
        except (ValueError, UnicodeDecodeError) as error:
            raise ValueError('invalid JSON at capture line ' + str(number)) from error
        _validate(row, number)
        rows.append(row)
    if not rows:
        raise ValueError('capture has no complete records')
    report = summarize_rows(rows, omitted)
    report['capture'] = dict(path=str(path.resolve()), read_bytes=captured_bytes,
                             sha256=captured_sha256, analyzed_prefix_bytes=len(data),
                             analyzed_prefix_sha256=hashlib.sha256(data).hexdigest())
    return report


def summarize_rows(rows, omitted_trailing_bytes=0):
    """Analyze authored or parsed records; validate every record either way."""
    voices = {}
    live_bases = {}
    counts = Counter()
    domain = None
    previous_time = 0
    classifications = {name: dict(created=0, played_generations=0, successful_plays=0,
                                  successful_stops=0, final_releases=0)
                       for name in ('spatial', '2d')}
    for number, row in enumerate(rows, 1):
        _validate(row, number)
        if domain is not None and domain != row['time_domain']:
            raise ValueError('capture time domain changed')
        domain = row['time_domain']
        if row['time'] < previous_time:
            raise ValueError('capture clock moved backwards')
        previous_time = row['time']
        generation, base, entry = row['generation'], row['base'], row['entry']
        counts[entry] += 1
        if entry == CREATE:
            if generation != len(voices) + 1 or base in live_bases:
                raise ValueError('reused/out-of-order generation or live base')
            if row['flags'] & 0x20000 and not row['flags'] & 0x10:
                raise ValueError('mute-at-distance creation lacks spatial flag')
            kind = 'spatial' if row['flags'] & 0x20010 else '2d'
            if kind == 'spatial' and row['channels'] != 1:
                raise ValueError('spatial creation lacks mono storage')
            voice = dict(generation=generation, base=base, flags=row['flags'],
                         classification=kind, created_record=number, created_time=row['time'],
                         identity={key: row[key] for key in IDENTITY_FIELDS},
                         interface=None, event_counts=Counter(), plays=[], stops=[], releases=[],
                         explicit_stop_restarts=0, last_transport_command='created',
                         final_release_record=None)
            voices[generation] = voice
            live_bases[base] = generation
            classifications[kind]['created'] += 1
        elif generation not in voices or live_bases.get(base) != generation:
            raise ValueError('event has no matching live creation')
        voice = voices[generation]
        if any(row[key] != value for key, value in voice['identity'].items()):
            raise ValueError('voice identity or creation flags changed')
        if entry != CREATE:
            interface = row['arguments'][0]
            expected_interface = base + BUFFER_INTERFACE_OFFSET
            if expected_interface > 0xffffffff or interface != expected_interface:
                raise ValueError('buffer interface differs from base plus native offset 28')
            if voice['interface'] is not None and voice['interface'] != interface:
                raise ValueError('buffer interface changed within generation')
            voice['interface'] = interface
        voice['event_counts'][entry] += 1
        voice['last_snapshot'] = {key: row[key] for key in SNAPSHOT_FIELDS}
        voice['last_record'] = number
        kind_counts = classifications[voice['classification']]
        event = dict(record=number, time=row['time'], result=row['result'])
        if entry == PREFIX + 'Play':
            if not voice['plays']:
                kind_counts['played_generations'] += 1
            if voice['last_transport_command'] == 'stop' and voice['plays']:
                voice['explicit_stop_restarts'] += 1
            voice['plays'].append(dict(event, arguments=row['arguments'],
                                       configuration=voice['last_snapshot']))
            voice['last_transport_command'] = 'play'
            kind_counts['successful_plays'] += 1
        elif entry == PREFIX + 'Stop':
            voice['stops'].append(event)
            voice['last_transport_command'] = 'stop'
            kind_counts['successful_stops'] += 1
        elif entry == PREFIX + 'Release':
            voice['releases'].append(dict(event, remaining_references=row['result']))
            if row['result'] == 0:
                voice['final_release_record'] = number
                del live_bases[base]
                kind_counts['final_releases'] += 1
    if not voices:
        raise ValueError('capture has no complete records')
    for voice in voices.values():
        del voice['identity']
        voice['event_counts'] = dict(voice['event_counts'])
    return dict(schema='xboxlle.native-audio-configuration-summary.v1',
                partial=True, audio_acceptance=False, spatial_output_acceptance=False,
                records=sum(counts.values()), time_domain=domain,
                first_time=rows[0]['time'], last_time=previous_time,
                omitted_trailing_bytes=omitted_trailing_bytes,
                record_limit_reached=sum(counts.values()) == 32768,
                event_counts=dict(counts), classifications=classifications,
                live_generations=list(live_bases.values()), voices=list(voices.values()),
                spatial_paths=dict(source_setters='not captured', listener_setters='not captured',
                                   deferred_commits='not captured'), limitations=LIMITATIONS)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--live', action='store_true', help='Ignore an incomplete final line only')
    args = parser.parse_args()
    try:
        report = summarize(args.capture, args.live)
    except (OSError, ValueError) as error:
        parser.exit(2, str(error) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
