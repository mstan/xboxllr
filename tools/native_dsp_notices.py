#!/usr/bin/env python3
"""Collect verbatim notices for the pinned DSP FFI's locked Windows dependencies.

Cargo's normal dependency closure can include conservatively unified features;
this inventory is not a claim that every listed crate is linked into the binary.
Build/dev tools remain in donor.json's separate full dependency inventory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def notice_files(root):
    prefixes = ('license', 'licence', 'copying', 'notice', 'copyright', 'authors')
    return sorted(p for p in root.rglob('*')
                  if p.is_file() and p.name.lower().startswith(prefixes))


def vcs_revision(root):
    path = root / '.cargo_vcs_info.json'
    return json.loads(path.read_text())['git']['sha1'] if path.exists() else None


def prepare(source, cargo):
    source = source.resolve()
    if 'build' not in source.parts:
        raise ValueError('notices must accompany a private prepared DSP source')
    donor = json.loads((source / 'donor.json').read_text())
    if digest(source / 'Cargo.lock') != donor['sources']['Cargo.lock']:
        raise ValueError('Cargo.lock no longer matches the prepared donor')
    target = 'x86_64-pc-windows-msvc'
    metadata = json.loads(subprocess.check_output([
        str(cargo), 'metadata', '--offline', '--locked', '--format-version', '1',
        '--filter-platform', target, '--manifest-path', str(source / 'Cargo.toml')], cwd=source))
    packages = {p['id']: p for p in metadata['packages']}
    nodes = {n['id']: n for n in metadata['resolve']['nodes']}
    pending = [p['id'] for p in packages.values() if p['name'] == 'dsp56300-emu-ffi']
    if len(pending) != 1:
        raise ValueError('expected one DSP FFI package')
    selected = set()
    while pending:
        key = pending.pop()
        if key in selected:
            continue
        selected.add(key)
        pending.extend(d['pkg'] for d in nodes[key]['deps']
                       if any(k['kind'] is None for k in d['dep_kinds']))
    output = source / 'third_party'
    output.mkdir(exist_ok=True)
    records = []
    for key in sorted(selected):
        package = packages[key]
        root = Path(package['manifest_path']).parent
        origin = package['name'] + '-' + package['version']
        files = notice_files(root)
        if package['license_file']:
            explicit = (root / package['license_file']).resolve()
            if explicit not in files:
                files.append(explicit)
        if package['source'] is None:
            # Workspace crates inherit the pinned donor's root MIT license.
            if not root.is_relative_to(source):
                raise ValueError('unexpected external path dependency: ' + origin)
            files, root = [source / 'LICENSE'], source
            notice_origin = donor['repository'] + '/tree/' + donor['revision']
        else:
            notice_origin = origin
        if not files:
            # Some Cranelift crates omit the monorepo LICENSE from their crate
            # archive. Recover it from a sibling with the identical VCS commit
            # and declared terms; record the actual source, never invent text.
            revision = vcs_revision(root)
            for sibling in packages.values():
                sibling_root = Path(sibling['manifest_path']).parent
                if (revision and sibling['license'] == package['license']
                        and vcs_revision(sibling_root) == revision
                        and (sibling_root / 'LICENSE').is_file()):
                    files, root = [sibling_root / 'LICENSE'], sibling_root
                    notice_origin = sibling['name'] + '-' + sibling['version'] + '@' + revision
                    break
        if not files:
            raise ValueError('no upstream license text for ' + origin)
        copies = []
        for path in files:
            relative = path.resolve().relative_to(root.resolve())
            dest = output / origin / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(path.read_bytes())
            copies.append(dict(path=dest.relative_to(output).as_posix(), sha256=digest(dest)))
        records.append(dict(name=package['name'], version=package['version'],
                            license=package['license'], authors=package['authors'],
                            repository=package['repository'], source=package['source'],
                            notice_origin=notice_origin, notices=copies))
    rustc = cargo.with_name('rustc.exe')
    sysroot = Path(subprocess.check_output([str(rustc), '--print', 'sysroot'], cwd=source, text=True).strip())
    rust_notice = sysroot / 'share/doc/rust/COPYRIGHT-library.html'
    (output / rust_notice.name).write_bytes(rust_notice.read_bytes())
    record = dict(target=target, cargo_lock_sha256=digest(source / 'Cargo.lock'),
                  scope='normal dependency closure, conservative Cargo feature unification; build/dev inventory in donor.json',
                  packages=records,
                  rust=dict(version=subprocess.check_output([str(rustc), '-Vv'], cwd=source, text=True),
                            notice=rust_notice.name, sha256=digest(rust_notice)))
    (output / 'inventory.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    lines = ['# DSP dependency credits', '',
             'Verbatim upstream notices for the locked Windows DSP FFI normal dependency closure.',
             'Cargo may include extra features; this is not a list of proven linked objects.',
             'Build/dev tools are separately inventoried in donor.json.', '',
             '| Package | Declared terms | Notices |', '| --- | --- | --- |']
    for p in records:
        links = ', '.join('[' + Path(n['path']).name + '](' + n['path'] + ')' for n in p['notices'])
        lines.append('| ' + p['name'] + ' ' + p['version'] + ' | ' + str(p['license']) + ' | ' + links + ' |')
    lines += ['', '[Rust standard-library notices](COPYRIGHT-library.html).',
              'Exact origins, authors, source URLs and notice hashes: [inventory.json](inventory.json).', '']
    (output / 'README.md').write_text('\n'.join(lines), encoding='utf-8')
    return dict(packages=len(records), notices=sum(len(p['notices']) for p in records))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--cargo', type=Path, default=Path.home() / '.cargo/bin/cargo.exe')
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.cargo)))
