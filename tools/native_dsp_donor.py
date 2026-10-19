#!/usr/bin/env python3
"""Prepare the pinned MIT DSP core with fail-closed unsupported-feature guards.

The donor and Cargo outputs stay private. No title assets or GPL emulator code
are inputs. Cargo's locked dependency metadata accompanies the source hashes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from native_dsp_sen import FILES as SEN_FILES, patch as resident_sen_patch

PIN = 'c60aeedb6a6014850204c71e53bc0cf5b3104222'
GIT = 'C:/Program Files/Git/cmd/git.exe'


def preparation_identity():
    directory = Path(__file__).resolve().parent
    return dict(schema='xboxllr.dsp-prepared-source.v1', generators={
        name: hashlib.sha256((directory / name).read_bytes()).hexdigest()
        for name in ('native_dsp_donor.py', 'native_dsp_sen.py')})


def guarded(name, text):
    replacements = {
        'crates/emu/src/core.rs': [(
            '        self.check_unimplemented_modes_slow();',
            '        panic!("Unqualified Xbox DSP mode: SR={:06x}, OMR={:06x}", sr, omr);')],
        'crates/emu/src/emit/mod.rs': [(
            'Instruction::Reset => self.emit_reset(),',
            'Instruction::Reset => panic!("Unqualified Xbox DSP RESET"),'), (
            'Instruction::Debug => self.emit_nop_like(1),',
            'Instruction::Debug => panic!("Unqualified Xbox DSP DEBUG"),'), (
            'Instruction::Debugcc { .. } => {\n                self.set_inst_len(1);\n                self.set_cycles(5);\n            }',
            'Instruction::Debugcc { .. } => panic!("Unqualified Xbox DSP DEBUGcc"),'), (
            'Instruction::Wait => self.emit_wait(),',
            'Instruction::Wait => panic!("Unqualified Xbox DSP WAIT/peripheral scheduling"),')],
    }
    for old, new in replacements.get(name, []):
        if text.count(old) != 1:
            raise ValueError('donor guard no longer matches: ' + name)
        text = text.replace(old, new)
    return resident_sen_patch(name, text)


def prepare(donor, output, cargo):
    donor, output = donor.resolve(), output.resolve()
    if 'build' not in output.parts or donor == output or donor in output.parents:
        raise ValueError('prepared source must be a separate private build directory')
    def git(*args):
        return subprocess.check_output([GIT, '-C', str(donor), *args])
    if git('rev-parse', 'HEAD').decode().strip() != PIN:
        raise ValueError('unreviewed DSP revision')
    git('diff', '--exit-code', 'HEAD', '--')
    output.mkdir(parents=True, exist_ok=True)
    hashes, patched = {}, {}
    for name in git('ls-files', '-z').decode().split('\0'):
        if not name:
            continue
        # Copy tracked source only; .git and build products never cross over.
        blob = (donor / name).read_bytes()
        hashes[name] = hashlib.sha256(blob).hexdigest()
        if name in SEN_FILES:
            blob = guarded(name, blob.decode()).encode()
            patched[name] = hashlib.sha256(blob).hexdigest()
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob)
    metadata = json.loads(subprocess.check_output([
        str(cargo), 'metadata', '--locked', '--format-version', '1',
        '--manifest-path', str(output / 'Cargo.toml')], cwd=output))
    dependencies = [dict(name=p['name'], version=p['version'], license=p['license'],
                         license_file=p['license_file'], source=p['source'])
                    for p in metadata['packages']]
    record = dict(repository='https://github.com/mborgerson/dsp56300', revision=PIN,
                  license='MIT', sources=hashes, guarded=patched,
                  preparation=preparation_identity(),
                  dependencies=dependencies,
                  policy='Bounded hardware-resident SEN supports logical SP=SC depths0..14 without extension-memory access; spill/refill/underflow, direct task/configuration writes, explicit stack ports, early loop abort and interrupts stop. Other unsupported arithmetic/memory modes and RESET/DEBUG/WAIT stop; full stack extension and cache/bus timing remain unqualified.')
    (output / 'donor.json').write_text(json.dumps(record, indent=2)+'\n', newline='\n')
    from native_dsp_notices import prepare as prepare_notices
    prepare_notices(output, cargo)
    return dict(revision=PIN, copied=len(hashes), guarded=len(patched), dependencies=len(dependencies))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--donor', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cargo', type=Path, default=Path.home()/'.cargo/bin/cargo.exe')
    args = parser.parse_args()
    print(json.dumps(prepare(args.donor, args.output, args.cargo)))
