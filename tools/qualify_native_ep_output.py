#!/usr/bin/env python3
"""Independent PCM arithmetic and decoded AC-3 checks for original EP captures.

Requires numpy and caller-supplied FFmpeg/FFprobe. Private source, lookup, signal
and output artifacts stay under build/. No hardware or playback operations.
"""
import argparse
import hashlib
import json
import math
import struct
import subprocess
from pathlib import Path

import numpy as np

FREQUENCIES = (431, 653, 887, 1103, 1327, 83)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def signal(kind):
    # Non-periodic at the 256-sample boundary: stale or swapped input windows
    # cannot pass the full PCM comparison by replaying an identical block.
    result = bytearray()
    for frame in range(32):
        for frequency in FREQUENCIES:
            for sample in range(256):
                value = 0 if kind == 'zero' else round(524288 * math.sin(
                    2 * math.pi * frequency * (frame * 256 + sample) / 48000))
                result += struct.pack('<I', value & 0xffffff)
    return bytes(result)


def pcm_reference(raw, selector):
    samples = np.frombuffer(raw, dtype='<u4').astype(np.int64).reshape(32, 6, 256)
    samples = np.where(samples & 0x800000, samples - 0x1000000, samples)
    samples = samples.transpose(0, 2, 1).reshape(-1, 6)
    # Independently transcribed unchanged original coefficient stores and MACs:
    # selector0 P:032E..0378; selector1 P:032E..036B. Ordinary context has no
    # optional master scale. This arithmetic does not call the candidate DSP.
    weights = ([[0x7fffff, 0x5a8279, 0, 0x5a8279, 0],
                [0, 0x5a8279, 0x7fffff, 0, 0x5a8279]] if selector == 0 else
               [[0x400000, 0x5a8279, 0x400000, 0x2d413c, 0x2d413c]] * 2)
    wide = samples[:, :5] @ np.array(weights, dtype=np.int64).T
    rounded = np.rint(wide / (1 << 23)).astype(np.int64)
    return (np.clip(rounded, -(1 << 23), (1 << 23) - 1) >> 8).astype('<i2').tobytes()


def ac3_frames(stream):
    # IEC61937 AC-3 bursts: 8-byte little-endian preamble, bit count,
    # word-swapped AC-3 payload, then zero padding to the next 1536-frame burst.
    preamble = bytes.fromhex('72f81f4e')
    first = stream.find(preamble)
    assert first == 2996 and not any(stream[:first]), 'unexpected initial burst placement'
    frames = []
    positions = []
    for at in range(first, len(stream), 6144):
        assert stream[at:at+4] == preamble
        _, _, kind, bits = struct.unpack_from('<4H', stream, at)
        assert kind == 1 and bits == 20480
        payload = stream[at+8:at+8+bits//8]
        positions.append(at)
        if len(payload) != bits//8:
            assert len(payload) == 856 and len(frames) == 5 and at+8+len(payload) == len(stream)
            break  # Explicit final in-progress burst, never sent to the decoder.
        data = bytearray(payload)
        data[0::2], data[1::2] = payload[1::2], payload[0::2]
        assert data[:2] == b'\x0b\x77'
        assert not any(stream[at+8+bits//8:min(at+6144, len(stream))])
        frames.append(bytes(data))
    assert len(frames) == 5 and positions == [2996 + i*6144 for i in range(6)]
    return b''.join(frames), positions


def decoded_metrics(decoded, kind):
    samples = np.frombuffer(decoded, dtype='<f4').reshape(-1, 6).astype(np.float64)
    assert samples.shape == (7680, 6) and np.isfinite(samples).all()
    peak = np.max(np.abs(samples), axis=0)
    if kind == 'zero':
        # Lossy decoded silence may contain quantization/dither noise. Bound it
        # below 2^-13 full scale; do not demand bit-identical PCM silence.
        assert max(peak) < 2**-13
        return dict(frames=7680, peak=peak.tolist(), silence_ceiling=2**-13)
    # Decoder channel order: FL,FR,FC,LFE,SL,SR. Original GP/EP input order:
    # FL,FC,FR,SL,SR,LFE. Fit every authored frequency jointly, excluding one
    # initial AC-3 frame for encoder/filter startup. Phase is reported, not
    # accepted as a hardware latency or shared-clock measurement.
    order = (0, 2, 1, 5, 3, 4)
    time = np.arange(1536, 7680) / 48000
    columns = [np.ones(len(time))]
    for frequency in FREQUENCIES:
        columns.extend((np.sin(2*np.pi*frequency*time), np.cos(2*np.pi*frequency*time)))
    design = np.array(columns).T
    fit = np.linalg.lstsq(design, samples[1536:], rcond=None)[0]
    amplitudes = np.hypot(fit[1::2], fit[2::2])
    records = []
    for channel, source in enumerate(order):
        amplitude = float(amplitudes[source, channel])
        leakage = float(max(np.delete(amplitudes[:, channel], source)))
        gain_tolerance = 0.10 if channel == 3 else 0.01
        assert abs(amplitude / 0.0625 - 1) < gain_tolerance, (channel, amplitude)
        assert leakage < 0.0625 * 0.02, (channel, leakage)
        records.append(dict(channel=channel, source_channel=source, frequency=FREQUENCIES[source],
            amplitude=amplitude, relative_gain_tolerance=gain_tolerance,
            other_tone_peak=leakage, phase=float(np.arctan2(fit[2+2*source, channel], fit[1+2*source, channel]))))
    return dict(frames=7680, channels=records, other_tone_ceiling=0.0625*0.02)


def check(reference, input_path, kind, ffmpeg, ffprobe, output):
    reference, input_path, output = reference.resolve(), input_path.resolve(), output.resolve()
    assert 'build' in output.parts
    output.mkdir(parents=True, exist_ok=False)
    report = json.loads(reference.read_text())
    assert input_path.read_bytes() == signal(kind), 'signal differs from the independent authored input'
    assert report['ep_sustained_input_sha256'] == sha(input_path)
    assert len(report['lifetimes']) == 3
    pins = {str(p): sha(p) for p in (reference, input_path, Path(__file__).resolve(), ffmpeg, ffprobe)}
    pcm = []
    for selector, life in enumerate(report['lifetimes']):
        assert life['format_selector'] == selector and life['original_destructors_release_all_allocations']
        observed = life['ep_event_runs'][0]['observation']
        assert observed['clock_events'] == observed['original_buffer_toggles'] == 32
        for fifo in range(2):
            capture = reference.parent/f'original-ep-sustained-{selector}-fifo{fifo}.aot-private.bin'
            assert sha(capture) == life['sustained_fifo_sha256'][fifo]
            pins[str(capture)] = sha(capture)
            if selector < 2 and fifo == 0:
                assert capture.read_bytes() == pcm_reference(input_path.read_bytes(), selector), f'PCM selector{selector} mismatch'
                pcm.append(dict(selector=selector, stereo_frames=8192, exact_samples=16384))
    stream = (reference.parent/'original-ep-sustained-2-fifo1.aot-private.bin').read_bytes()
    encoded, positions = ac3_frames(stream)
    source, decoded = output/'complete-frames.aot-private.ac3', output/'decoded.aot-private.f32'
    source.write_bytes(encoded)
    command = [str(ffmpeg), '-hide_banner', '-v', 'error', '-xerror', '-err_detect', 'crccheck+explode',
               '-f', 'ac3', '-i', str(source), '-f', 'f32le', str(decoded)]
    run = subprocess.run(command, capture_output=True, text=True, timeout=30)
    (output/'decode.aot-private.log').write_text(run.stdout+run.stderr, newline='\n')
    assert run.returncode == 0 and not run.stderr, run.stderr
    probe = subprocess.run([str(ffprobe), '-v', 'error', '-f', 'ac3', '-show_streams', '-of', 'json', str(source)],
                           capture_output=True, text=True, timeout=30)
    assert probe.returncode == 0 and not probe.stderr
    metadata = json.loads(probe.stdout)['streams']
    assert len(metadata) == 1
    codec = metadata[0]
    assert (codec['codec_name'], codec['sample_rate'], codec['channels'], codec['channel_layout'], codec['bit_rate']) == ('ac3', '48000', 6, '5.1(side)', '640000')
    metrics = decoded_metrics(decoded.read_bytes(), kind)
    for path in (source, decoded):
        pins[str(path)] = sha(path)
    result = dict(issue='beads-xbox.1.113.9', kind=kind, pcm=pcm, ac3=metrics,
        complete_ac3_frames=5, burst_offsets=positions, final_in_progress_burst_excluded=True,
        decode_command=command, codec=codec, pins=pins,
        limits='Representative source-bound PCM and independent AC-3 decode/channel/gain evidence. Authored signals and clock; no original GP production, AC97/title consumption, hardware latency, exact LFE filter phase, reset or initial SECTL qualification. HLE unchanged.')
    (output/'qualified-output.aot-private.json').write_text(json.dumps(result, indent=2)+'\n', newline='\n')
    print(f'PASS: {kind}: exact32768 PCM samples,5 CRC-checked AC-3 frames,6 decoded channels; {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('zero', 'tones'), required=True)
    parser.add_argument('--write-input', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--ffmpeg', type=Path)
    parser.add_argument('--ffprobe', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.write_input:
        assert 'build' in args.write_input.resolve().parts
        with args.write_input.open('xb') as file:
            file.write(signal(args.kind))
    else:
        assert all((args.reference, args.input, args.ffmpeg, args.ffprobe, args.output))
        check(args.reference, args.input, args.kind, args.ffmpeg.resolve(), args.ffprobe.resolve(), args.output)
