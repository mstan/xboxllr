#!/usr/bin/env python3
"""Insert host-lock-free suspension points into private generated guest loops.

Function entry is checked by recomp_trace_enter. Backward local branches need
their own points: a worker may otherwise spin indefinitely without a call.
No guest instruction, register, branch condition or title address is replaced.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

BODY = re.compile(r'^void \w+\(void\)\n\{\n.*?^\}\n', re.M | re.S)
LABEL = re.compile(r'^(loc_[0-9A-Fa-f]+): ;$', re.M)
JUMP = re.compile(r'\bgoto (loc_[0-9A-Fa-f]+);')
CHECK = '    PILOT_THREAD_CHECKPOINT();\n'
HEADER = '''
/* Guest scheduler safe points: never suspend a host thread holding host locks. */
#ifndef PILOT_THREAD_CHECKPOINT
extern RECOMP_TLS volatile uint32_t *g_pilot_suspend_count;
void pilot_thread_checkpoint(void);
#ifdef PILOT_SESSION_RUNTIME
extern int g_pilot_session_enabled;
extern RECOMP_TLS uint64_t g_pilot_session_checkpoints,g_pilot_session_deadline;
#define PILOT_THREAD_CHECKPOINT() do { \\
    if (g_pilot_session_enabled) { \\
        ++g_pilot_session_checkpoints; \\
        if (g_pilot_session_checkpoints>=g_pilot_session_deadline || \\
            (g_pilot_suspend_count && *g_pilot_suspend_count)) pilot_thread_checkpoint(); \\
    } else if (g_pilot_suspend_count && *g_pilot_suspend_count) pilot_thread_checkpoint(); \\
} while (0)
#else
#define PILOT_THREAD_CHECKPOINT() do { \\
    if (g_pilot_suspend_count && *g_pilot_suspend_count) pilot_thread_checkpoint(); \\
} while (0)
#endif
#endif
'''


def instrument(code):
    """Return rewritten C and total loop targets, including existing points."""
    total = 0

    def body(match):
        nonlocal total
        source = match.group()
        labels = {m[1]: m for m in LABEL.finditer(source)}
        targets = set()
        for jump in JUMP.finditer(source):
            if jump[1] not in labels:
                raise ValueError('branch target missing within function: ' + jump[1])
            if labels[jump[1]].start() < jump.start():
                targets.add(jump[1])
        total += len(targets)
        for label in sorted((labels[t] for t in targets), key=lambda m: m.end(), reverse=True):
            after = label.end() + 1
            if not source[after:].startswith(CHECK):
                source = source[:after] + CHECK + source[after:]
        return source

    return BODY.sub(body, code), total


def apply(directory):
    # Validate all functions before writing any artifacts.
    changes = []; total = 0
    for path in sorted(directory.glob('recomp_*.c')):
        code, count = instrument(path.read_text(encoding='utf-8'))
        changes.append((path, code)); total += count
    header = directory / 'recomp_types.h'
    text = header.read_text(encoding='utf-8')
    if '#ifndef PILOT_THREAD_CHECKPOINT' in text:
        # The instrumentation footer is appended by this module. Upgrade its
        # entire versioned contract instead of retaining an older macro.
        start=text.index('/* Guest scheduler safe points:')
        directive=text.index('#ifndef PILOT_THREAD_CHECKPOINT',start)
        depth=0; end=None
        for match in re.finditer(r'^#(if(?:n?def)?|endif)\b[^\n]*\n?',text[directive:],re.M):
            depth += -1 if match[1]=='endif' else 1
            if depth==0:
                end=directive+match.end();break
        if end is None:raise ValueError('unterminated checkpoint header')
        text=text[:start].rstrip()+'\n'+HEADER+text[end:]
    else:
        text+=HEADER
    changes.append((header, text))
    for path, code in changes:
        if path.read_bytes()!=code.encode('utf-8'):
            path.write_text(code, encoding='utf-8', newline='\n')
    return dict(version=2, loop_targets=total, function_entry='recomp_trace_enter', session_checkpoints=True)


def upgrade(output):
    output = output.resolve()
    if 'build' not in output.parts:
        raise ValueError('generated output must remain under private build/')
    path = output / 'generation.aot-private.json'
    manifest = json.loads(path.read_text())
    generated = output / 'gen'
    for name, digest in manifest['files'].items():
        if hashlib.sha256((generated / name).read_bytes()).hexdigest() != digest:
            raise ValueError('changed generated artifact: ' + name)
    manifest['thread_checkpoints'] = apply(generated)
    manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(generated.iterdir()) if p.is_file()}
    path.write_text(json.dumps(manifest, indent=2) + '\n', newline='\n')
    return manifest['thread_checkpoints']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    print(json.dumps(upgrade(parser.parse_args().output)))
