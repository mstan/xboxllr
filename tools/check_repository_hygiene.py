#!/usr/bin/env python3
"""Reject private assets and obvious secrets without printing matched contents.

Default: scan the Git index (exact bytes the next commit would contain).
--history: also scan every blob and filename reachable from local refs/reflogs.
This is a publication guard, not a replacement for a full secret scanner/review.
"""
import argparse
import pathlib
import re
import subprocess
import sys

PRIVATE_SUFFIXES = frozenset((
    '.bin .raw .rom .img .vhd .vhdx .vmdk .vdi .qcow .qcow2 .dmp .mem '
    '.pcap .pcapng .xbe .iso .xiso .zip .7z .rar .tar .gz .bz2 .xz .zst '
    '.pem .key .p12 .pfx .keystore .gpr .blockpack .jsonl .ppm .png .log'
).split())
PRIVATE_ROOTS = {'machine', 'private', 'secrets', 'dumps', 'captures', 'play'}
SECRET_PATTERNS = {
    'private key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----'),
    'provider token': re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,}|AKIA[A-Z0-9]{16}|xox[baprs]-[A-Za-z0-9-]{20,}|sk-(?:proj-)?[A-Za-z0-9_-]{30,})'),
    'literal credential': re.compile(rb'''(?im)(?:password|passwd|api[_-]?key|access[_-]?token|secret[_-]?key|hdd[_ -]?key|online[_ -]?key|eeprom[_ -]?key)["']?\s*[=:]\s*["']?[a-zA-Z0-9+/=_-]{12,}'''),
    'console identity': re.compile(rb'''(?im)(?:serial[_ -]?number|console[_ -]?id|mac[_ -]?address)["']?\s*[=:]\s*["']?[a-fA-F0-9:-]{10,}'''),
    'credential URL': re.compile(rb'''https?://[^\s/"']+:[^\s/@"']+@'''),
}


def private_path(path):
    p = pathlib.PurePosixPath(path.lower())
    name = p.name
    return bool(
        (p.parts and (p.parts[0] in PRIVATE_ROOTS or p.parts[0].startswith('private-audit')))
        or p.suffix in PRIVATE_SUFFIXES
        or any(part in {'bios', 'disc'} or part.endswith('.rep') for part in p.parts)
        or name == '.env' or name.startswith('.env.')
        or name == 'hardware.json' or name.startswith(('id_rsa', 'id_ed25519', 'console-identity'))
        or name == 'credentials' or name.startswith('credentials.')
        or (name.startswith('eeprom') and p.suffix in {'.json', '.txt', '.cfg', '.ini'})
        or name.endswith(('.report.json', '.argv.json', '.quiescence.json'))
    )


def content_findings(data):
    # The repository contains text source/metadata only. Renaming a dump is insufficient.
    if b'\0' in data:
        yield 'binary data', 1
    for category, pattern in SECRET_PATTERNS.items():
        for match in pattern.finditer(data):
            yield category, data.count(b'\n', 0, match.start()) + 1


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args])


def scan(root, history=False):
    objects = {}
    failures = set()
    for entry in git(root, 'ls-files', '--stage', '-z').split(b'\0'):
        if not entry:
            continue
        meta, path_bytes = entry.split(b'\t', 1)
        mode, oid, stage = meta.decode().split()
        path = path_bytes.decode('utf-8', 'replace')
        if private_path(path):
            failures.add((path, 'private asset path', 0))
        if stage != '0' or mode in {'160000', '120000'}:
            failures.add((path, 'unmerged entry, submodule or symlink requires review', 0))
            continue
        objects.setdefault(oid, path)
    if history:
        # Check names in EVERY tree: rev-list --objects only names each blob once,
        # which otherwise misses an identical blob moved into a forbidden path.
        commits = git(root, 'rev-list', '--all', '--reflog').decode().splitlines()
        for commit in commits:
            for entry in git(root, 'ls-tree', '-rz', commit).split(b'\0'):
                if not entry:
                    continue
                meta, path_bytes = entry.split(b'\t', 1)
                mode, kind, oid = meta.decode().split()
                path = path_bytes.decode('utf-8', 'replace')
                label = f'{commit[:12]}:{path}'
                if private_path(path):
                    failures.add((label, 'private asset path', 0))
                if kind == 'blob':
                    objects.setdefault(oid, label)
                elif kind == 'commit':
                    failures.add((label, 'submodule requires separate audit', 0))
    reader = subprocess.Popen(['git', '-C', str(root), 'cat-file', '--batch'],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for oid, path in objects.items():
            reader.stdin.write((oid + '\n').encode('ascii'))
            reader.stdin.flush()
            header = reader.stdout.readline().split()
            if len(header) != 3 or header[1] != b'blob':
                raise RuntimeError('Unable to read Git blob ' + oid)
            data = reader.stdout.read(int(header[2]))
            if reader.stdout.read(1) != b'\n':
                raise RuntimeError('Truncated Git object stream')
            for category, line in content_findings(data):
                failures.add((path, category, line))
    finally:
        reader.stdin.close()
        reader.stdout.close()
        reader.wait()
    for path, category, line in sorted(failures):
        print(f'{path}:{line}: {category} (contents redacted)')
    print(f'Repository hygiene: {len(objects)} unique blobs; {len(failures)} findings.')
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--history', action='store_true')
    parser.add_argument('--repo', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    return scan(args.repo, args.history)


if __name__ == '__main__':
    sys.exit(main())
