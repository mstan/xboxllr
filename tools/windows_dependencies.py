"""Read-only Windows x64 import-closure preflight shared by all title releases.

PE layout: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
Only declared directories are inspected, one level deep. No PATH/System32 walk.
This checks imported DLLs, not licensing, explicit LoadLibrary calls or assets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

# Windows 10+ platform components. VC++/MinGW/SDL/XAudio redist DLLs are not
# platform exemptions: the package must supply those when actually imported.
SYSTEM_DLLS = frozenset("""
advapi32 avrt bcrypt bcryptprimitives cfgmgr32 combase comctl32 comdlg32 crypt32 d2d1 d3d11
d3dcompiler_47 dbghelp dwmapi dwrite dxgi gdi32 hid imm32 iphlpapi kernel32
kernelbase mpr msacm32 msvcrt ntdll ole32 oleaut32 opengl32 powrprof
propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi user32 userenv
uxtheme version winhttp wininet winmm wintrust ws2_32 wtsapi32 xaudio2_9
xinput1_4
""".split())
NAME = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_.+-]*\.dll', re.I)
API_SET = re.compile(r'(?:api|ext)-ms-win-[a-z0-9-]+\.dll', re.I)
MAX_IMAGE = 256 * 1024 * 1024


def system_dll(name):
    return name.lower().removesuffix('.dll') in SYSTEM_DLLS or bool(API_SET.fullmatch(name))


class Image:
    def __init__(self, data):
        self.data = data
        if len(data) < 64 or data[:2] != b'MZ':
            raise ValueError('not a DOS/PE image')
        pe = self.get('<I', 0x3c)[0]
        if self.block(pe, 4) != b'PE\0\0':
            raise ValueError('invalid PE signature')
        machine, count, _, _, _, optional_size, _ = self.get('<HHIIIHH', pe + 4)
        if machine != 0x8664:
            raise ValueError('Windows release requires x64 dependencies')
        optional = pe + 24
        self.block(optional, optional_size)
        if optional_size < 112 or self.get('<H', optional)[0] != 0x20b:
            raise ValueError('Windows x64 release requires PE32+')
        self.base = self.get('<Q', optional + 24)[0]
        self.headers = self.get('<I', optional + 60)[0]
        directories = self.get('<I', optional + 108)[0]
        if directories > (optional_size - 112) // 8:
            raise ValueError('data directory exceeds optional header')
        self.directories = [self.get('<II', optional + 112 + i * 8) for i in range(directories)]
        if not 1 <= count <= 96:
            raise ValueError('invalid section count')
        self.sections = []
        for i in range(count):
            at = optional + optional_size + i * 40
            self.block(at, 40)
            _, rva, raw_size, raw_at = self.get('<IIII', at + 8)
            self.block(raw_at, raw_size)
            self.sections.append((rva, raw_size, raw_at))

    def block(self, at, size):
        if at < 0 or size < 0 or at > len(self.data) - size:
            raise ValueError('truncated PE structure')
        return self.data[at:at + size]

    def get(self, form, at):
        return struct.unpack(form, self.block(at, struct.calcsize(form)))

    def offset(self, rva, size):
        if not 0 <= rva <= 0xffffffff or rva + size > 0x100000000:
            raise ValueError('invalid PE RVA')
        candidates = []
        if rva + size <= self.headers:
            candidates.append(rva)
        candidates += [raw + rva - start for start, length, raw in self.sections
                       if start <= rva and rva + size <= start + length]
        if len(candidates) != 1:
            raise ValueError('unmapped or ambiguous PE RVA')
        self.block(candidates[0], size)
        return candidates[0]

    def name(self, rva):
        result = bytearray()
        for i in range(256):
            value = self.data[self.offset(rva + i, 1)]
            if value == 0:
                try:
                    return result.decode('ascii')
                except UnicodeDecodeError as error:
                    raise ValueError('non-ASCII DLL name') from error
            result.append(value)
        raise ValueError('unterminated DLL name')

    def imports(self, *, delayed=False):
        index, stride = (13, 32) if delayed else (1, 20)
        if index >= len(self.directories):
            return []
        rva, size = self.directories[index]
        if rva == size == 0:
            return []
        if not rva or size < stride or size // stride > 4096:
            raise ValueError('invalid import directory extent')
        names = set()
        for at in range(0, size - stride + 1, stride):
            fields = self.get('<' + 'I' * (stride // 4), self.offset(rva + at, stride))
            if not any(fields):
                return sorted(names)
            if delayed:
                if fields[0] & ~1:
                    raise ValueError('unsupported delay-import attributes')
                name_rva = fields[1] if fields[0] & 1 else fields[1] - self.base
            else:
                name_rva = fields[3]
            name = self.name(name_rva).lower()
            if not NAME.fullmatch(name):
                raise ValueError('import must name a plain DLL file')
            names.add(name)
        raise ValueError('unterminated import directory')


def inspect(path):
    path = Path(path)
    if path.stat().st_size > MAX_IMAGE:
        raise ValueError('PE exceeds the release inventory size limit')
    data = path.read_bytes()
    if len(data) > MAX_IMAGE:
        raise ValueError('PE exceeds the release inventory size limit')
    image = Image(data)
    return dict(name=path.name, source=str(path.resolve()), sha256=hashlib.sha256(data).hexdigest(),
                imports=image.imports(), delay_imports=image.imports(delayed=True))


def inventory(entries, directories=()):
    entries = [Path(p).resolve(strict=True) for p in entries]
    if not entries:
        raise ValueError('declare at least one executable or explicit runtime DLL')
    roots = list(dict.fromkeys([p.parent for p in entries] + [Path(p).resolve(strict=True) for p in directories]))
    candidates = {}
    for root in roots:
        if not root.is_dir():
            raise ValueError('dependency search root must be a directory')
        for path in root.iterdir():
            if path.suffix.lower() == '.dll' and path.is_file():
                candidates.setdefault(path.name.lower(), []).append(path.resolve())
    records, pending, missing, platform, inspected = {}, list(entries), set(), set(), {}

    def selected(path):
        if path not in inspected:
            inspected[path] = inspect(path)
        return inspected[path]

    while pending:
        path = pending.pop(0)
        key = path.name.lower()
        record = selected(path)
        if key in records:
            if records[key]['sha256'] != record['sha256']:
                raise ValueError('conflicting package basename: ' + key)
            continue
        records[key] = record
        for kind in ('imports', 'delay_imports'):
            for name in record[kind]:
                if system_dll(name):
                    platform.add(name)
                    continue
                choices = list(dict.fromkeys(candidates.get(name, [])))
                if not choices:
                    missing.add((key, name, kind))
                    continue
                fingerprints = {selected(p)['sha256'] for p in choices}
                if len(fingerprints) != 1:
                    raise ValueError('ambiguous DLL versions in declared search roots: ' + name)
                pending.append(choices[0])
    return dict(schema='xboxllr.windows-imports.v1', platform='windows-x64', system_policy='windows-10-plus',
                complete=not missing, files=[records[k] for k in sorted(records)],
                system_imports=sorted(platform),
                missing=[dict(importer=a, name=b, kind=c) for a, b, c in sorted(missing)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--entry', type=Path, action='append', required=True,
                        help='Executable, or an explicitly loaded runtime DLL; repeatable')
    parser.add_argument('--search-dir', type=Path, action='append', default=[])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not args.output.name.endswith('.aot-private.json'):
        parser.error('inventory includes build paths; use an .aot-private.json output')
    try:
        report = inventory(args.entry, args.search_dir)
    except (ValueError, OSError) as error:
        parser.exit(2, str(error) + '\n')
    if args.output.is_symlink() or (args.output.exists() and any(
            args.output.samefile(record['source']) for record in report['files'])):
        parser.error('inventory output must not replace or alias an input binary')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(complete=report['complete'], files=len(report['files']), missing=report['missing'])))
    return 0 if report['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
