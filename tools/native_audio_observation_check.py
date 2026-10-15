"""Check actual serialized observation exits and retained dual-stream files."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def pin(path):
    return dict(path=str(path.resolve()), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def check(program, snapshot, output):
    assert 'build' in output.resolve().parts and not output.exists()
    output.mkdir(parents=True)
    rows = []
    for mode in ('spdif-observation', 'spdif-observation-failure'):
        run = subprocess.run([str(program.resolve()), mode, str(snapshot.resolve()),
                              str(output.resolve())], capture_output=True, timeout=15)
        text = (run.stdout+run.stderr).decode(errors='replace')
        (output/(mode+'.log')).write_text(text)
        assert run.returncode == 0, text
        expected_status = 'error' if mode.endswith('failure') else 'ok'
        assert '[OBSERVATION STOP] partial=1 status='+expected_status in text, text
        assert 'ticks=66000 codec_frame=11 pcm_bytes=1024 pcm_stamps=1 spdif_bytes=1024 spdif_stamps=1 owned=1' in text
        if expected_status == 'error':
            assert 'PASS failed PCM persistence still finalizes digital prefix' in text
        digital = list(output.glob(mode+'-[0-9]*.bin.partial'))
        assert len(digital) == 1
        analog = digital[0].with_name(digital[0].name.removesuffix('.partial')+'.pcm.partial')
        for path, magic, multiplier, offset, kind in (
                (analog, b'XLLEAC97', 17, 1, 0),
                (digital[0], b'XLLESPD1', 31, 7, 0x02000000)):
            assert not path.with_suffix('').exists(), 'Incomplete capture published as complete'
            data = path.read_bytes()
            assert len(data) == 80+48+1024+32
            header = struct.unpack_from('<8s6I6Q', data)
            assert header[:7] == (magic, 1, 80, 48000, 2, 16, 1)
            assert header[7] and header[8:11] == (60000, 1024, 1)
            stamp = struct.unpack_from('<5Q2I', data, 80)
            assert stamp == (header[7], 60000, 66000, 11, 0, 1024, kind)
            assert data[128:1152] == bytes((i*multiplier+offset)&255 for i in range(1024))
            assert struct.unpack_from('<8s2Q2I', data, 1152) == (b'XLLEEND1', 1024, 1, 1, 0)
            rows.append(dict(mode=mode, kind=magic.decode(), incomplete=True, pin=pin(path)))
    report = dict(status='dual-output-observation-stop-validated', outputs=rows,
                  pins=[pin(program), pin(Path(__file__))],
                  limits='Two actual serialized owner exits; no title, playback or hardware. '
                         'Injected PCM persistence failure remains a failure even when bytes are present. '
                         'No device close/reset/retirement is claimed.')
    (output/'observation-check.aot-private.json').write_text(json.dumps(report, indent=2)+'\n')
    print('PASS: normal observation exit and persistence failure retain both exact incomplete output prefixes; no new device work or time')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('program', 'snapshot', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    check(**vars(parser.parse_args()))
