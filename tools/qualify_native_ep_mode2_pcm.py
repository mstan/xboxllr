#!/usr/bin/env python3
"""Independent fixed-context PCM2 arithmetic over retained original EP captures.

No candidate DSP execution, fitted coefficients, timing/gain search, hardware or
playback. Caller supplies the original private program and captured artifacts.
The program hash and full context restrict this oracle to the audited route.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from native_audio_stage_trace_check import read_trace


PROGRAM_SHA256 = '6ed8f14b4113024b85bd1243df47ffd6dc923289831bfdf2f40f2abc78aced5d'
CONTEXT = (24, 1, 1, 0, 0, 7, 1, 31, 9, 0, 1, 1, 0, 0, 1, 239, 0, 6663308, 2, 0, 0, 0, 0, 0)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def pin(path):
    raw = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(raw), sha256=sha(raw))


def signed(word):
    assert 0 <= word <= 0xffffff
    return word - 0x1000000 if word & 0x800000 else word


def clip24(value):
    return max(-8388608, min(8388607, value))


def round23(product):
    # Convergent fractional rounding, with no host floating-point dependency.
    value, low = divmod(product, 1 << 23)
    return clip24(value + (low > (1 << 22) or (low == (1 << 22) and value & 1)))


class Mode2Pcm:
    def __init__(self, program, context):
        assert sha(program) == PROGRAM_SHA256 and tuple(context) == CONTEXT
        words = struct.unpack('<754I', program)

        def word(address):
            return signed(words[address - 0x300] & 0xffffff)

        # Bootstrap P:0055..0057 passes X:0BC0 unchanged to P:0304. Program
        # P:03D4..03E3 reads context[7], clamps 1..31, then indexes P:0305.
        # For this context the literal selected word is P:0324 = 607000;
        # do not replace that surprising source value with intended unity.
        self.gain = word(0x305 + CONTEXT[7])
        # X:0608 is explicitly zero. P:0593..05AB selects the second filter
        # bank: front Y:002D (P:0560), surround Y:0041 (P:0574).
        self.front = [[word(0x560 + stage * 5 + i) for i in range(5)] for stage in range(4)]
        self.rear = [[word(0x574 + stage * 5 + i) for i in range(5)] for stage in range(5)]
        # Per section: y[n-1], y[n-2], x[n-1], x[n-2], Q48 residue.
        # The first-call branch P:058D clears the entire 78-word state.
        self.histories = [[[0] * 5 for _ in range(count)] for count in (4, 4, 5)]

    @staticmethod
    def cascade(samples, coefficients, histories):
        # P:05CA..05F1 implements cascaded sections with residue feedback.
        # Each multiply is fractional (factor two in the 56-bit accumulator).
        # Paired c0/c3 MACs double those terms. MOV to X truncates/limits; B
        # retains the discarded fraction across samples and frame boundaries.
        for (c0, c1, c2, c3, c4), state in zip(coefficients, histories):
            y1, y2, x1, x2, residue = state
            result = []
            for x in samples:
                total = 2 * (-2 * c0 * y1 - c1 * y2 + c2 * x + 2 * c3 * x1 + c4 * x2) + residue
                assert -(1 << 55) <= total < (1 << 55), 'outside audited accumulator range'
                y = clip24(total >> 24)
                residue = total - (y << 24)
                y1, y2, x1, x2 = y, y1, x, x1
                result.append(y)
            state[:] = y1, y2, x1, x2, residue
            samples = result
        return samples

    def state_words(self):
        # X:063D..0670 histories, X:0671..068A high/low residue pairs.
        words = [value & 0xffffff for group in self.histories for state in group for value in state[:4]]
        words += [value for group in self.histories for state in group
                  for value in ((state[4] >> 24) & 0xffffff, state[4] & 0xffffff)]
        assert len(words) == 78
        return struct.pack('<78I', *words)

    def process(self, planes):
        assert len(planes) == 5 and all(len(plane) == 256 for plane in planes)
        assert all(-8388608 <= value < 8388608 for plane in planes for value in plane)
        # P:043A scales all five input planes before the PCM transform.
        channels = [[round23(sample * self.gain) for sample in plane] for plane in planes]
        # P:0469..047E combines SL/SR. P:047F..04A6 adds center to FL/FR;
        # the 0x5a827a coefficient is distinct from selector0's 0x5a8279.
        left = [round23((channels[0][i] << 23) + 0x5a827a * channels[1][i]) for i in range(256)]
        right = [round23((channels[2][i] << 23) + 0x5a827a * channels[1][i]) for i in range(256)]
        surround = [round23(0x5a827a * (channels[3][i] + channels[4][i])) for i in range(256)]
        left = self.cascade(left, self.front, self.histories[0])
        right = self.cascade(right, self.front, self.histories[1])
        surround = self.cascade(surround, self.rear, self.histories[2])
        # P:04EC..050A adds the filtered surround with opposite signs. FIFO0
        # packs the high sixteen bits of each actual 24-bit sample, L then R.
        output = [value for l, r, s in zip(left, right, surround)
                  for value in (round23((l << 23) + 0x5a827a * s) >> 8,
                                round23((r << 23) - 0x5a827a * s) >> 8)]
        return struct.pack('<512h', *output)


def prepared_binding(reference, program):
    data = json.loads(reference.read_text(encoding='utf-8-sig'))
    lifetime = next(item for item in data['lifetimes'] if item['format_selector'] == 2)
    folder = reference.parent
    ram = (folder / 'original-startup-ram-2.aot-private.bin').read_bytes()
    registers = (folder / 'original-startup-registers-2.aot-private.bin').read_bytes()
    ownership = (folder / 'original-startup-ownership-2.aot-private.bin').read_bytes()
    assert sha(ram) == lifetime['ram_sha256'] and sha(registers) == lifetime['registers_sha256']
    assert sha(ownership) == lifetime['ownership_sha256']
    regs = dict(struct.iter_unpack('<2I', registers))
    owners = list(struct.iter_unpack('<2I', ownership))

    def physical(at, size):
        assert any(at >= base and at + size <= base + length for base, length in owners)
        assert at + size <= len(ram)
        return ram[at:at + size]

    def scratch(at, size):
        out = bytearray()
        while size:
            assert at // 4096 < regs[0xfe8020dc]
            page = struct.unpack('<I', physical(regs[0xfe802048] + (at // 4096) * 8, 4))[0]
            part = min(size, 4096 - at % 4096)
            out += physical(page + at % 4096, part)
            at += part
            size -= part
        return bytes(out)

    assert scratch(0xc00, len(program)) == program
    assert struct.unpack('<24I', scratch(7422 * 4, 96)) == CONTEXT
    return data, lifetime


def fixture_check(reference, input_path, program):
    data, lifetime = prepared_binding(reference, program)
    raw = input_path.read_bytes()
    assert sha(raw) == data['ep_sustained_input_sha256'] and len(raw) == 32 * 6 * 256 * 4
    samples = [signed(item[0]) for item in struct.iter_unpack('<I', raw)]
    capture = reference.parent / 'original-ep-sustained-2-fifo0.aot-private.bin'
    actual = capture.read_bytes()
    assert sha(actual) == lifetime['sustained_fifo_sha256'][0]
    model = Mode2Pcm(program, CONTEXT)
    expected = bytearray()
    for frame in range(32):
        expected += model.process([samples[(frame * 6 + ch) * 256:(frame * 6 + ch + 1) * 256] for ch in range(5)])
    assert actual == expected, f'{reference.parent.name}: selector2 PCM differs'
    return dict(reference=pin(reference), input=pin(input_path), output=pin(capture),
                frames=8192, exact_samples=16384, persistent_filter_sections=13)


def live_check(path, program):
    trace = read_trace(path)
    assert trace['flags'] == 1, 'requires a complete recorded prefix without journal errors/truncation'
    model = Mode2Pcm(program, CONTEXT)
    pending = None
    have_context = False
    blocks = states = contexts = 0
    output_hash = hashlib.sha256()
    for row in trace['records']:
        kind, metadata, raw = row['kind'], row['metadata'], row['data']
        if kind == 6:
            address, = metadata
            if address == 7422 * 4:
                assert struct.unpack('<24I', raw) == CONTEXT
                have_context = True
                contexts += 1
            if address == 0xc00 and len(raw) == len(program):
                assert have_context and raw == program and pending is None
                pending = dict(planes={}, state=False, ring_half=None)
            elif pending is not None:
                if address == 7446 * 4 and len(raw) == 78 * 4:
                    # First measured state must be zero; thereafter derive every
                    # state from prior observed input, never reseed from output.
                    assert not pending['state'] and raw == model.state_words(), 'filter state continuity differs'
                    pending['state'] = True
                    states += 1
                elif 0x10000 <= address < 0x12800 and len(raw) == 1024:
                    # Each channel has a 0x800-byte ring; consecutive 256-word
                    # events consume alternating 0x400-byte halves.
                    half = (address - 0x10000) % 0x800
                    assert half in (0, 0x400)
                    assert pending['ring_half'] in (None, half)
                    pending['ring_half'] = half
                    channel = (address - 0x10000) // 0x800
                    assert channel not in pending['planes']
                    pending['planes'][channel] = [signed(v[0]) for v in struct.iter_unpack('<I', raw)]
        elif kind == 7 and metadata[:2] == (0, 1):
            assert pending is not None and pending['state'] and set(pending['planes']) == set(range(5))
            expected = model.process([pending['planes'][channel] for channel in range(5)])
            assert raw == expected, f'live PCM block {blocks} differs at tick {row["ticks"]}'
            output_hash.update(raw)
            blocks += 1
            pending = None
    assert pending is None and blocks and states == blocks and contexts
    return dict(trace=pin(path), contexts=contexts, exact_blocks=blocks, frames=blocks * 256,
                exact_samples=blocks * 512, exact_state_loads=states,
                initial_filter_state='observed zero', output_sha256=output_hash.hexdigest(),
                limits='Captured prefix only; state content checks do not establish physical ownership or codec timing.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True, type=Path)
    parser.add_argument('--fixture', nargs=2, action='append', type=Path, required=True,
                        metavar=('REFERENCE_JSON', 'INPUT_BIN'))
    parser.add_argument('--trace', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert not args.output.exists()
    program = args.program.read_bytes()
    model = Mode2Pcm(program, CONTEXT)
    report = dict(status='selector2-pcm-captured-domain-qualified', program=pin(args.program),
                  tool=pin(Path(__file__)), context=CONTEXT, gain_word=hex(model.gain),
                  fixtures=[fixture_check(reference, signal, program) for reference, signal in args.fixture],
                  live=live_check(args.trace, program), hardware_operations=0, playback=False,
                  family_qualified=False,
                  limits='Fixed original program/context and captured signal domain; no full-title, active reset, FE ordering, analog ring or timing acceptance.')
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'PASS: selector2 PCM {len(report["fixtures"])} retained fixtures, '
          f'{report["live"]["exact_blocks"]} live blocks and state loads; exact samples, no fitted parameters')


if __name__ == '__main__':
    main()
