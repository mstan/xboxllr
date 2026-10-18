"""Diagnostic continuation profile for the existing pinned Phobos C output.

This is a deliberately restricted post-lowering pass, not an instruction lifter.
The donor still disassembles/lifts every guest instruction. Unsupported source
or opcode shapes fail preparation before any output changes. Default preparation
never calls this pass. No title/service/device resumability is implied.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import re

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

VERSION = 1
BODY = re.compile(r'void (\w+)\(void\)\n\{\n(.*?)^\}\n', re.M | re.S)
ORIGINAL = re.compile(r'Original: (0x[0-9A-Fa-f]+) - (0x[0-9A-Fa-f]+)')
TRACE = re.compile(r'RECOMP_TRACE_ENTER\("\w+", (0x[0-9A-Fa-f]+)\);')
LABEL = re.compile(r'^(loc_([0-9A-Fa-f]+)): ;$', re.M)
CALL = re.compile(r'RECOMP_(?:ABI_CALL(?:_POP)?|ICALL_SAFE_AT)\(([^,]+),[^;]+\);')
DECL = re.compile(r'\b(uint32_t|int32_t|int)\s+([A-Za-z_]\w*\s*=\s*[^;]+);')
FIELDS = {'_fa': ('uint32_t', 'fa'), '_fb': ('uint32_t', 'fb'),
          '_fas': ('int32_t', 'fas'), '_fbs': ('int32_t', 'fbs'),
          '_flags': ('int', 'flags'), 'ebp': ('uint32_t', 'ebp'),
          '_icall_esp': ('uint32_t', 'icall_esp'),
          '_icall_target': ('uint32_t', 'icall_target')}
OPS = {'mov', 'push', 'pop', 'cmp', 'test', 'add', 'sub', 'inc', 'dec',
       'jmp', 'jne', 'je', 'jl', 'jge', 'ret', 'call', 'nop'}
GENERATED_CALLS = {'RECOMP_TRACE_ENTER', 'RECOMP_ABI_CALL', 'RECOMP_ABI_CALL_POP',
                   'RECOMP_ICALL_SAFE_AT', 'PILOT_THREAD_CHECKPOINT',
                   'PUSH32', 'POP32', 'MEM8', 'MEM16', 'MEM32', 'MEM64',
                   'LO8', 'HI8', 'LO16', 'SET_LO8', 'SET_HI8', 'SET_LO16',
                   'CMP_EQ', 'CMP_NE', 'CMP_L', 'CMP_LE', 'CMP_G', 'CMP_GE', 'if'}


def lower_sources(sources: dict[str, str], read):
    """Return rewritten sources, schema header and manifest without mutations.

    `read(guest_address, length)` supplies actual original executable bytes.
    Names/addresses come from donor output; none are fixture-specific.
    """
    rows, changes, identities = [], {}, []
    functions = {}
    for filename, source in sorted(sources.items()):
        edits = []
        for match in BODY.finditer(source):
            name, body = match[1], match[2]
            meta = list(ORIGINAL.finditer(source[:match.start()]))
            if not meta or not TRACE.search(body):
                raise ValueError('continuation profile requires donor bounds and entry trace')
            start, end = (int(v, 16) for v in meta[-1].groups())
            trace = TRACE.search(body)
            if int(trace[1], 16) != start or start in functions:
                raise ValueError('duplicate or mismatched continuation function')
            functions[start] = name
            raw = read(start, end-start)
            insns = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(raw, start))
            if not insns or insns[-1].address+insns[-1].size != end:
                raise ValueError('continuation instruction coverage')
            if any(i.mnemonic not in OPS for i in insns):
                raise ValueError('opcode outside diagnostic continuation subset')
            if any(i.mnemonic == 'jmp' and not (
                    i.op_str.startswith('0x') and start <= int(i.op_str, 16) < end)
                    for i in insns):
                raise ValueError('tail/indirect jump outside continuation subset')
            identities.append(raw)
            if 'RECOMP_UNIMPL' in body or 'setjmp' in body or 'longjmp' in body:
                raise ValueError('unsupported donor body in continuation profile')
            lexical = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', '', body, flags=re.S)
            called = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', lexical))
            if called-GENERATED_CALLS:
                raise ValueError('unhandled generated helper/transfer: '+str(sorted(called-GENERATED_CALLS)))
            transfer_macros = set(re.findall(r'\bRECOMP_[A-Z0-9_]+', lexical))
            if transfer_macros-GENERATED_CALLS:
                raise ValueError('unhandled generated transfer macro')

            declared = set()
            def declaration(m):
                for part in m[2].split(','):
                    variable = part.split('=', 1)[0].strip()
                    if variable not in FIELDS or FIELDS[variable][0] != m[1]:
                        raise ValueError('unsupported continuation local: ' + variable)
                    declared.add(variable)
                # Keep the original assignments in place. A restored activation
                # jumps past its prologue/argument preparation, so they do not
                # overwrite saved lowering state or repeat guest pushes.
                return m[2] + ';'
            body = DECL.sub(declaration, body)
            if re.search(r'\b(?:uint\w*_t|int\w*_t|size_t|ptrdiff_t|int|char|short|long|'
                         r'unsigned|signed|double|float|static|struct|union|enum|_Bool)\s+[\w*]', body):
                raise ValueError('unhandled automatic storage in continuation body')
            token, sites, body_edits = 0, [], []
            for label in LABEL.finditer(body):
                if int(label[2], 16) not in {i.address for i in insns}:
                    raise ValueError('resume label is not an original instruction')
                token += 1
                row = dict(function=start, token=token, guest_address=int(label[2], 16),
                           is_call=0, direct_target=0)
                sites.append((row, label[1]))
                body_edits.append((label.end(), label.end(),
                    f'\n    pilot_cont_at_label(_cont, {token}u, 0x{row["guest_address"]:08x}u);'))
            calls = list(CALL.finditer(body))
            if len(calls) != sum(i.mnemonic == 'call' for i in insns):
                raise ValueError('unhandled donor call shape')
            for call in calls:
                prefix = body[body.rfind('\n', 0, call.start())+1:call.start()]
                pushed = re.search(r'PUSH32\(esp, (0x[0-9A-Fa-f]+)u\);\s*$', prefix)
                if not pushed:
                    raise ValueError('call has no verified guest return push')
                return_pc = int(pushed[1], 16)
                original = next((i for i in insns if i.mnemonic == 'call' and
                                 i.address+i.size == return_pc), None)
                if original is None:
                    raise ValueError('call return PC differs from original instruction')
                indirect = call.group().startswith('RECOMP_ICALL')
                target = call[1].strip()
                direct = 0 if indirect else int(target.rstrip('uU'), 0)
                if indirect and target != '_icall_target':
                    raise ValueError('indirect target must have persisted donor capture')
                if not indirect and (not original.op_str.startswith('0x') or
                                     int(original.op_str, 16) != direct):
                    raise ValueError('direct call differs from original bytes')
                token += 1
                label = f'cont_call_{start:08x}_{token}'
                row = dict(function=start, token=token, guest_address=return_pc,
                           is_call=1, direct_target=direct)
                sites.append((row, label))
                body_edits.append((call.start(), call.start(),
                    f'\n{label}: ;\n    _cont->token={token}u; _cont->callee={target};\n'
                    '    pilot_cont_validate_call(_cont);\n    '))
            if not sites or not re.search(r'\breturn;', body):
                raise ValueError('continuation function lacks labels/return')
            # Reject unwrapped calls/tail transfers, rather than drop an activation.
            remainder = CALL.sub('', body)
            if re.search(r'\bsub_[0-9A-Fa-f]+\s*\(', remainder):
                raise ValueError('unwrapped generated call')
            for a, b, value in sorted(body_edits, reverse=True):
                body = body[:a] + value + body[b:]
            for variable in sorted(declared, key=len, reverse=True):
                body = re.sub(r'\b'+variable+r'\b', '_cont->'+FIELDS[variable][1], body)
            # A single statement must remain a single statement, including
            # after an unbraced if. Never turn a conditional return unconditional.
            body = re.sub(r'\breturn;', '{ pilot_cont_leave(); return; }', body)
            switch = '\n'.join(f'    case {r["token"]}u: goto {label};' for r, label in sites)
            body = (f'    PilotContFrame *_cont=pilot_cont_enter(0x{start:08x}u);\n'
                    '    if (!_cont) return;\n    switch (_cont->token) {\n'
                    f'    case 0: break;\n{switch}\n'
                    '    default: pilot_cont_fault("invalid active continuation token"); return;\n'
                    '    }\n' + body)
            rows.extend(r for r, _ in sites)
            edits.append((match.start(2), match.end(2), body))
        # Only donor translation units participate, never hand-authored bindings.
        if not edits:
            raise ValueError('non-donor translation unit in diagnostic profile: ' + filename)
        rewritten = source
        for a, b, value in reversed(edits):
            rewritten = rewritten[:a] + value + rewritten[b:]
        changes[filename] = '#include "continuation_diagnostic.h"\n' + rewritten
    for row in rows:
        if row['direct_target'] and row['direct_target'] not in functions:
            raise ValueError('external service call outside continuation subset')
    if not functions:
        raise ValueError('no donor functions in continuation profile')
    support = Path(__file__).resolve().parents[1]/'pilots/native-title'
    implementation = Path(__file__).read_bytes() + (support/'continuation_diagnostic.h').read_bytes() + (support/'continuation_diagnostic.c').read_bytes()
    identity = hashlib.sha256(
        b'XLLE diagnostic continuation v1\0' + b''.join(identities) +
        json.dumps(sources, sort_keys=True).encode() +
        json.dumps(rows, sort_keys=True).encode() + implementation).digest()
    table = '\n'.join('    {%du,%du,%du,%du,%du},' % tuple(r.values()) for r in rows)
    header = ('#ifndef PILOT_CONTINUATION_SCHEMA_H\n#define PILOT_CONTINUATION_SCHEMA_H\n'
              '#include "continuation_diagnostic.h"\n'
              f'static const PilotContSite pilot_cont_sites[]={{\n{table}\n}};\n'
              'static const PilotContSchema pilot_cont_schema={\n    {' +
              ','.join(str(v) for v in identity) + '},\n    pilot_cont_sites,\n'
              '    sizeof(pilot_cont_sites)/sizeof(pilot_cont_sites[0])\n};\n#endif\n')
    return changes, header, dict(version=VERSION, diagnostic_only=True,
                                identity=identity.hex(), sites=rows)


def apply(directory: Path, xbe: Path):
    from aot_inventory import parse_xbe
    blob = xbe.read_bytes()
    image = parse_xbe(blob)
    def read(address, size):
        section = next((s for s in image['sections'] if s['flags'] & 4 and
                        s['address'] <= address and address+size <= s['address']+s['raw_size']), None)
        if section is None:
            raise ValueError('continuation outside original executable bytes')
        offset = section['raw_offset']+address-section['address']
        return blob[offset:offset+size]
    paths = sorted(directory.glob('recomp_[0-9]*.c'))
    changes, header, manifest = lower_sources({p.name: p.read_text(encoding='utf-8') for p in paths}, read)
    if not paths:
        raise ValueError('no generated continuation bodies')
    for name, code in changes.items():
        (directory/name).write_text(code, encoding='utf-8', newline='\n')
    (directory/'pilot_continuation_schema.h').write_text(header, encoding='utf-8', newline='\n')
    return manifest
