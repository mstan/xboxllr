"""Independently read actual bounded stage journals, including incomplete cases."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def read_trace(path):
    raw = path.read_bytes()
    header = struct.unpack_from('<8s4I5Q', raw)
    magic, version, size, status, flags, lifetime, epoch, first, payload_bytes, count = header
    if (magic, version, size, status) != (b'XLLESTG1', 1, 64, 1) or flags & ~15 or not lifetime:
        raise ValueError('invalid stage header')
    if len(raw) != size+payload_bytes+24:
        raise ValueError('partial stage framing')
    at = size; rows = []; previous = epoch
    for sequence in range(1, count+1):
        if at+32 > size+payload_bytes:
            raise ValueError('partial stage record')
        kind, voice, words, length, ticks, serial = struct.unpack_from('<4I2Q', raw, at)
        at += 32
        if (kind not in range(1, 9) or serial != sequence or ticks < previous or
                words > 64 or length > 65536 or at+words*4+length > size+payload_bytes):
            raise ValueError('invalid stage record')
        metadata = struct.unpack_from('<'+'I'*words, raw, at); at += words*4
        payload = raw[at:at+length]; at += length; previous = ticks
        rows.append(dict(kind=kind, voice=voice, ticks=ticks, sequence=serial, metadata=metadata, data=payload))
    if at != size+payload_bytes or struct.unpack_from('<8s2Q', raw, at) != (b'XLLEEND1', payload_bytes, count):
        raise ValueError('invalid stage footer')
    if bool(count) != bool(flags & 1) or (count and first != rows[0]['ticks']):
        raise ValueError('invalid source trigger')
    return dict(flags=flags, lifetime=lifetime, epoch=epoch, first=first, records=rows)


def check(program, output):
    assert 'build' in output.resolve().parts and not output.exists()
    output.mkdir(parents=True)
    results = []
    for mode, flags, count in (('normal', 1, 2), ('window', 9, 2), ('capacity', 3, 12), ('invalid', 5, 1)):
        result = subprocess.run([str(program.resolve()), mode, str(output.resolve())], capture_output=True, timeout=15)
        log = (result.stdout+result.stderr).decode(errors='replace')
        (output/(mode+'.log')).write_text(log, encoding='utf-8')
        assert result.returncode == 0, log
        path = output/(mode+'.bin.partial'); trace = read_trace(path)
        assert not (output/(mode+'.bin')).exists()
        assert (trace['flags'], trace['lifetime'], trace['epoch'], trace['first']) == (flags, 123, 456, 600)
        assert len(trace['records']) == count
        for index, row in enumerate(trace['records']):
            assert row['voice'] == 96 and row['metadata'] == (11, 22, 33, 44, 55, 66, 77)
            assert row['data'] == struct.pack('<6f', .25, -.5, 0, 1, -1, .125)
            assert row['kind'] == (3 if mode in ('normal', 'window') and index == 1 else 1)
            assert row['ticks'] == (600 if index == 0 else 700 if mode != 'capacity' else 600+index)
        results.append(dict(mode=mode, flags=flags, records=count, path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    for mode, flags, count in (('retained', 1, 2), ('retained-window', 9, 2),
                               ('retained-capacity', 3, 12), ('retained-untriggered', 0, 0)):
        result = subprocess.run([str(program.resolve()), mode, str(output.resolve())], capture_output=True, timeout=15)
        log = (result.stdout+result.stderr).decode(errors='replace')
        (output/(mode+'.log')).write_text(log, encoding='utf-8')
        assert result.returncode == 0, log
        path = output/(mode+'.bin.partial'); trace = read_trace(path)
        assert (trace['flags'], trace['lifetime'], trace['epoch'], trace['first']) == (flags, 123, 456, 600 if count else 0)
        assert len(trace['records']) == count
        for index, row in enumerate(trace['records']):
            assert row['voice'] == 96 and row['metadata'] == (11, 22, 33, 44, 55, 66, 77)
            assert row['data'] == struct.pack('<6f', .25, -.5, 0, 1, -1, .125)
            assert row['kind'] == (6 if index == 0 else 1)
        results.append(dict(mode=mode, flags=flags, records=count, path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    report = dict(status='bounded-stage-journal-persistence-validated', cases=results,
                  limits='Storage uses a 1024-byte test capacity; production uses64MiB initial/256MiB retained, each at most4guest seconds. No title, hardware, audio accuracy or guest teardown qualification.')
    (output/'stage-trace-check.aot-private.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('PASS: eight prefix/retained cases; same-object lifetime trigger, exact bytes, time/capacity/untriggered distinctions and immutable persistence')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--program', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    check(**vars(parser.parse_args()))
