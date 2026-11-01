#!/usr/bin/env python3
"""Stage one XboxLLR beta release containing Dashboard and build-tool downloads.

Runs the existing component packagers and collects their verified assets in one
directory. Native binaries must already be validated. Existing output is kept.
This command does not change GitHub or compile/run an application.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

from native_release_runtime import sha

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tool-launcher', type=Path, required=True, help='Built xboxllr.exe')
    parser.add_argument('--capstone-wheel', type=Path, required=True)
    for name in ('runtime', 'cache', 'launcher', 'embed', 'boot-intro', 'renderer', 'ui', 'dsp', 'notes'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    parser.add_argument('--runtime-framework-revision', required=True)
    parser.add_argument('--runtime-source-revision', required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists() or args.output.is_symlink() or output.is_relative_to(ROOT):
        raise ValueError('Choose a new release output outside the source checkout')
    output.mkdir(parents=True)
    common = ['--version', args.version]
    dashboard = [sys.executable, str(ROOT/'tools/release_dashboard.py'), *common,
                 '--output', str(output/'dashboard')]
    for name in ('runtime', 'cache', 'launcher', 'embed', 'boot_intro', 'renderer', 'ui', 'dsp', 'notes',
                 'runtime_framework_revision', 'runtime_source_revision'):
        dashboard += ['--'+name.replace('_', '-'), str(getattr(args, name))]
    for path in args.runtime_dir:
        dashboard += ['--runtime-dir', str(path)]
    toolkit = [sys.executable, str(ROOT/'tools/release_toolkit.py'), *common,
        '--output', str(output/'toolkit'), '--launcher', str(args.tool_launcher),
        '--embed', str(args.embed), '--capstone-wheel', str(args.capstone_wheel)]
    for command in (dashboard, toolkit):
        subprocess.run(command, check=True)
    assets = output/'assets'; assets.mkdir()
    records = []
    for component in ('dashboard', 'toolkit'):
        for record in json.loads((output/component/'artifacts.json').read_text()):
            source = output/component/'assets'/record['file']
            destination = assets/record['file']
            if destination.exists() or sha(source) != record['sha256']:
                raise ValueError('Conflicting or changed release asset')
            shutil.copy2(source, destination)
            if sha(destination) != record['sha256']:
                raise ValueError('Collected release asset differs')
            records.append(dict(record, component=component))
    (assets/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['file']+'\n' for r in records))
    (output/'artifacts.json').write_text(json.dumps(records, indent=2)+'\n')
    print('One XboxLLR beta release: publish the combined assets/ directory.')


if __name__ == '__main__':
    main()
