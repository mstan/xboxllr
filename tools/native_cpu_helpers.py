"""Prepare attributed MIT native CPU helpers without changing translated bodies."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / 'src/third_party/xboxrecomp_cpu'
BRIDGE = ROOT / 'pilots/native-title/native_cpu_helpers.h'
PIN = '1409a7d7801d3e931fb1074be6104209ddd9a33e'
HEADER_SHA256 = '16891854eb3821c582c3f02cfd391bae8df9e59c655e9cb3be2ae95e6256a732'
LICENSE_SHA256 = 'de6601cea3a6bf2d821b6b89f59b9d6e719cc017e530bcb4bbe2fcf7306e6dda'
LEGACY_SHA256 = '7af6dd4a797f4cc86b4872d5fce7fd25bcf533425e36f0a565d25bba54ac28ff'
END = '#endif /* RECOMP_TYPES_H */'
PORT_DECLARATIONS = (
    '\n/* Real byte port read; unsupported ports stop explicitly. */\nuint8_t pilot_cpu_in8(uint16_t port);\n',
    '\n/* Real byte port write; unsupported ports stop explicitly. */\nvoid pilot_cpu_out8(uint16_t port, uint8_t value);\n',
    '\n/* Publish prior stores on the coherent CPU/device memory bus. */\nvoid pilot_cpu_wbinvd(void);\n',
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('unexpected pinned CPU helper structure')
    return text.replace(old, new)


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def write_owned(path, data):
    # Replace the directory entry rather than altering a possibly hard-linked
    # input. Temporary files stay beside the explicitly selected private output.
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.cpu-helper-', delete=False) as file:
        file.write(data)
        temporary = Path(file.name)
    temporary.replace(path)


def ordinary_entries(directory, names):
    for name in names:
        if linked(Path(directory) / name):
            raise ValueError('linked entries are not supported in a private preparation')
    return ()


def prepared_header():
    original = (UPSTREAM / 'recomp_types.h').read_bytes()
    if sha(original) != HEADER_SHA256 or sha((UPSTREAM / 'LICENSE').read_bytes()) != LICENSE_SHA256:
        raise ValueError('MIT CPU helper source/license differs from the reviewed revision')
    text = original.decode('utf-8')
    text = replace_once(text, 'void recomp_icall_not_code_log(uint32_t va);',
                        'void recomp_icall_not_code_log(uint32_t va, uint32_t saved_esp);')
    lines = text.splitlines(keepends=True)
    for name, stack, kind in [('RECOMP_ICALL', 'g_esp + 4u', 1),
                              ('RECOMP_ICALL_SAFE', '(saved_esp)', 1),
                              ('RECOMP_ICALL_SAFE_AT', '(saved_esp)', 1),
                              ('RECOMP_ITAIL', None, 2)]:
        indexes = [i for i, line in enumerate(lines) if line.startswith('#define ' + name + '(')]
        if len(indexes) != 1:
            raise ValueError('missing or ambiguous CPU dispatch macro: ' + name)
        start = end = indexes[0]
        while lines[end].rstrip().endswith('\\'):
            end += 1
        body = ''.join(lines[start:end + 1])
        body = replace_once(body, 'recomp_icall_fail_log(_va);',
                            f'pilot_recomp_indirect_failure(_va, {kind}u);')
        if stack is not None:
            body = replace_once(body, 'recomp_icall_not_code_log(_va);',
                                f'recomp_icall_not_code_log(_va, {stack});')
        lines[start:end + 1] = body.splitlines(keepends=True)
    text = ''.join(lines)
    text = re.sub(r'extern volatile (uint(?:32|64)_t g_icall_(?:trace(?:_idx)?|count)\b)',
                  r'extern RECOMP_TLS volatile \1', text)
    return text + '\n' + BRIDGE.read_text(encoding='utf-8')


def install(directory):
    """Replace only a recognized generic header; retain verified authored hooks."""
    from native_thread_checkpoints import HEADER as checkpoints
    from native_interrupt_lowering import HEADER as interrupts
    directory = Path(directory)
    path = directory / 'recomp_types.h'
    notices = directory / 'third_party/sp00nznet-cpu-helpers'
    # Validate all mutation targets before the first write. In particular, an
    # existing notice directory must not redirect writes to another workspace.
    for target in (directory, directory / 'third_party', notices):
        if linked(target) or (target.exists() and not target.is_dir()):
            raise ValueError('CPU helper output requires ordinary directories')
    for target in (path, notices / 'LICENSE', notices / 'PROVENANCE.md'):
        if linked(target) or (target.exists() and not target.is_file()):
            raise ValueError('CPU helper output requires ordinary files')
    old = path.read_text(encoding='utf-8')
    new = prepared_header()
    if old.startswith(new):
        suffix = old[len(new):]
    else:
        if old.count(END) != 1:
            raise ValueError('unrecognized native CPU helper boundary')
        prefix, suffix = old.split(END)
        prefix += END + '\n'
        prefix = re.sub(r'extern RECOMP_TLS volatile (uint(?:32|64)_t g_icall_(?:trace(?:_idx)?|count)\b)',
                        r'extern volatile \1', prefix)
        if sha(prefix.encode()) != LEGACY_SHA256:
            raise ValueError('unreviewed CPU helper variant; use a fresh preparation')
        # The newline belongs to the original template, not an authored hook.
        if not suffix.startswith('\n'):
            raise ValueError('malformed native CPU helper suffix')
        suffix = suffix[1:]
    remaining = suffix
    for extension in (checkpoints, interrupts, *PORT_DECLARATIONS):
        if remaining.count(extension) > 1:
            raise ValueError('duplicated native CPU helper extension')
        remaining = remaining.replace(extension, '')
    if remaining.strip():
        raise ValueError('unreviewed native CPU helper extension')
    write_owned(path, (new + suffix).encode('utf-8'))
    notices.mkdir(parents=True, exist_ok=True)
    write_owned(notices / 'LICENSE', (UPSTREAM / 'LICENSE').read_bytes())
    write_owned(notices / 'PROVENANCE.md', (UPSTREAM / 'README.md').read_bytes())
    return dict(version=1, repository='https://github.com/sp00nznet/xboxrecomp',
                revision=PIN, upstream_header_sha256=HEADER_SHA256,
                bridge_sha256=sha(BRIDGE.read_bytes()), license='MIT upstream; authored bridge retains project scope')


def clone_preparation(title, source, destination):
    """Publish a verified upgraded copy; never mutate/overwrite the source."""
    import native_title as n
    title, data = n.definition(title)
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if 'build' not in destination.parts:
        raise ValueError('private generated output requires a build directory')
    if destination.exists() or destination.is_relative_to(source) or source.is_relative_to(destination):
        raise ValueError('use a new, separate generated output directory')
    if linked(source / 'gen'):
        raise ValueError('linked generated directory is not a private preparation')
    manifest = n.check_generated(source, data['title'])
    source_identity = n.preparation_identity(title, data, source)
    n.require_receipt(source / 'native-title-preparation.json', source_identity)
    destination.parent.mkdir(parents=True, exist_ok=True)
    candidate = Path(tempfile.mkdtemp(prefix='.cpu-helper-candidate-', dir=destination.parent))
    shutil.copytree(source, candidate, dirs_exist_ok=True, symlinks=True, ignore=ordinary_entries)
    record = install(candidate / 'gen')
    manifest['runtime_helpers'] = record
    manifest['files']['recomp_types.h'] = n.digest(candidate / 'gen/recomp_types.h')
    (candidate / 'generation.aot-private.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    prepared = n.preparation_identity(title, data, candidate)
    (candidate / 'native-title-preparation.json').write_text(json.dumps(prepared, indent=2) + '\n', encoding='utf-8')
    n.require_receipt(candidate / 'native-title-preparation.json', n.preparation_identity(title, data, candidate))
    if n.preparation_identity(title, data, source) != source_identity:
        raise ValueError('source preparation changed during CPU helper cloning')
    # Both paths resolve beneath the explicitly chosen destination parent. A
    # failed candidate remains private for diagnosis; no recursive cleanup.
    if candidate.resolve().parent != destination.parent or destination.exists():
        raise ValueError('CPU helper publication target changed')
    candidate.rename(destination)
    return dict(output=str(destination), runtime_helpers=record,
                generated_header_sha256=n.digest(destination / 'gen/recomp_types.h'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit-header', action='store_true', help='Emit the ROM-free helper header for component builds')
    parser.add_argument('--title', type=Path)
    parser.add_argument('--source-generation', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.emit_header:
        if args.title or args.source_generation:
            parser.error('header-only emission does not consume a title or preparation')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(prepared_header().encode('utf-8'))
    else:
        if not args.title or not args.source_generation:
            parser.error('cloning a preparation requires --title and --source-generation')
        print(json.dumps(clone_preparation(args.title, args.source_generation, args.output), indent=2))
