#!/usr/bin/env python3
"""Build a pristine playable Windows alpha archive with corresponding source.

The native executable must already be built and validated. Never stage a used
player folder. This command does not publish or modify saves or existing output.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
from native_release_package import stage
from native_release_archive import archive
from native_release_attach import attach
from native_release_sources import package as dependency_package
from source_release_package import package as source_package, git
from native_release_runtime import sha

ROOT = Path(__file__).resolve().parents[1]


def main(argv=None, *, dashboard=False):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=ROOT if dashboard else None, required=not dashboard)
    parser.add_argument('--definition', default='titles/dashboard/title.toml' if dashboard else 'game.toml')
    parser.add_argument('--name', default='XboxDashboard' if dashboard else 'BlinxRecomp')
    parser.add_argument('--version', required=True)
    for name in ('runtime', 'cache', 'launcher', 'embed', 'boot-intro', 'renderer', 'ui', 'dsp', 'output', 'notes'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--runtime-dir', type=Path, action='append', default=[])
    args = parser.parse_args(argv)
    if not all(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', value) for value in (args.version, args.name)):
        parser.error('Use plain name/version identifiers')
    repo = args.repo.resolve(strict=True)
    if git(ROOT, 'status', '--porcelain').strip() or git(repo, 'status', '--porcelain').strip():
        raise ValueError('Commit release sources first')
    if args.output.exists() or args.output.is_symlink() or args.output.resolve().is_relative_to(repo) or args.output.resolve().is_relative_to(ROOT):
        raise ValueError('Choose a new output directory outside source checkouts')
    args.output.mkdir(parents=True)
    revision = git(ROOT, 'rev-parse', 'HEAD').decode().strip()
    source_revision = git(repo, 'rev-parse', 'HEAD').decode().strip()
    assets = args.output/'assets'; assets.mkdir()
    dependency = assets/('xboxllr-native-dependency-source-'+args.version+'.zip')
    dependency_record = dependency_package(args.renderer, args.ui, args.dsp, dependency)
    source = assets/(args.name+'-source-'+args.version+'.zip')
    source_record = source_package(repo, source, definition=args.definition)
    target = args.output/'stage'
    metadata = stage(repo/args.definition, args.runtime, args.launcher, args.cache,
        args.embed, args.runtime_dir, target, boot_intro=args.boot_intro)
    provenance = dict(schema='xboxllr.windows-build-provenance.v1',
        source_revision=source_revision, packaging_framework_revision=revision,
        runtime_sha256=sha(args.runtime), cache_sha256=sha(args.cache),
        launcher_sha256=sha(args.launcher), startup_sha256=sha(args.boot_intro),
        source_archive=source_record, dependency_source_archive=dependency_record,
        note='Original title bodies are generated privately from user-supplied input; see source docs/DEVELOPMENT.md.')
    for name, data in [('BUILD-PROVENANCE.json', (json.dumps(provenance,indent=2)+'\n').encode()),
                       ('RELEASE-NOTES.md', args.notes.read_bytes())]:
        (target/name).write_bytes(data); metadata['files'][name] = sha(target/name)
    metadata.update(source_revision=source_revision, build_provenance='BUILD-PROVENANCE.json')
    (target/'release.json').write_text(json.dumps(metadata,indent=2)+'\n')
    attach(target, [source], dependency, revision)
    result = archive(target, assets/(args.name+'-windows-x64-'+args.version+'.zip'))
    records = [result, source_record, dependency_record]
    (assets/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  '+r['file']+'\n' for r in records))
    (args.output/'artifacts.json').write_text(json.dumps(records,indent=2)+'\n')
    print(json.dumps(records,indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
