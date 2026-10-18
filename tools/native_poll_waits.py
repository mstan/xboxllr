"""Bind reviewed guest-callback polls to an optional native host-idle service.

The original signed comparison, flag snapshots, back edge and scheduler
checkpoint remain authoritative. This never writes a polled value or declares
completion. A separate build leaves the original spin loop intact.
"""
import hashlib
import json
from pathlib import Path
import re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_EAX

BODY = re.compile(r'^void (sub_[0-9A-F]{8})\(void\)\n\{\n.*?^\}\n', re.M | re.S)
DECLARATION = 'void pilot_native_poll_wait(void);\n'


def number(value):
    return int(value, 0) if isinstance(value, str) else int(value)


def digest(contract):
    return hashlib.sha256(json.dumps(contract, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate(blob, image, contract):
    if contract.get('version') != 1 or contract['xbe_sha256'] != image['sha256']:
        raise ValueError('poll contract version/image mismatch')
    counter = number(contract['counter'])
    if not 0 <= counter <= 0x4000000-4 or counter % 4:
        raise ValueError('poll counter must be aligned ordinary guest RAM')
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True

    def code(record):
        start, end = number(record['start']), number(record['end'])
        section = next((s for s in image['sections'] if s['flags'] & 4 and
                        s['address'] <= start < end <= s['address'] + s['raw_size']), None)
        if not section or end-start > 1024:
            raise ValueError('poll evidence outside bounded executable bytes')
        offset = section['raw_offset'] + start-section['address']
        data = blob[offset:offset+end-start]
        if offset < 0 or len(data) != end-start:
            raise ValueError('poll evidence outside input file')
        if hashlib.sha256(data).hexdigest() != record['sha256']:
            raise ValueError('poll evidence bytes changed')
        result = list(decoder.disasm(data, start))
        if sum(i.size for i in result) != len(data):
            raise ValueError('incomplete poll evidence decoding')
        return result

    def memory(operand):
        return (operand.type == X86_OP_MEM and not operand.mem.base and
                not operand.mem.index and not operand.mem.segment and
                operand.mem.disp == counter and operand.size == 4)

    def eax(operand):
        return operand.type == X86_OP_REG and operand.reg == X86_REG_EAX

    publisher = code(contract['publisher'])
    if (len(publisher) != 4 or [i.mnemonic for i in publisher] != ['mov', 'inc', 'mov', 'jmp'] or
        not eax(publisher[0].operands[0]) or not memory(publisher[0].operands[1]) or
        not eax(publisher[1].operands[0]) or not memory(publisher[2].operands[0]) or
        not eax(publisher[2].operands[1]) or publisher[3].operands[0].type != X86_OP_IMM):
        raise ValueError('unreviewed guest counter publisher')
    registration = code(contract['registration'])
    if (len(registration) != 2 or [i.mnemonic for i in registration] != ['push', 'call'] or
        any(i.operands[0].type != X86_OP_IMM for i in registration) or
        registration[0].operands[0].imm != number(contract['publisher']['start']) or
        registration[1].operands[0].imm != number(contract['export']['start'])):
        raise ValueError('unreviewed callback registration')
    export = code(contract['export'])
    if not export or export[-1].mnemonic != 'ret' or export[-1].op_str != '4':
        raise ValueError('unreviewed callback registration export')
    if not contract['loops'] or len(contract['loops']) > 64:
        raise ValueError('poll contract requires a bounded nonempty loop inventory')
    seen = set()
    for record in contract['loops']:
        pc = number(record['start'])
        if pc in seen:
            raise ValueError('duplicate poll loop')
        seen.add(pc)
        instructions = code(record)
        if (len(instructions) != 2 or [i.mnemonic for i in instructions] != ['cmp', 'jl'] or
            not memory(instructions[0].operands[0]) or not eax(instructions[0].operands[1]) or
            instructions[1].operands[0].type != X86_OP_IMM or instructions[1].operands[0].imm != pc):
            raise ValueError('poll must contain only the original signed RAM comparison and back edge')


def transform(source, loops, counter):
    for record in loops:
        pc = number(record['start'])
        symbol = f'sub_{number(record["function"]):08X}'
        matches = [m for m in BODY.finditer(source) if m[1] == symbol]
        if len(matches) != 1:
            raise ValueError('poll requires exactly one generated owner: ' + symbol)
        match = matches[0]
        before = (f'loc_{pc:08X}: ;\n    PILOT_THREAD_CHECKPOINT();\n'
                  f'    _fa = (uint32_t)(MEM32(0x{counter:X})) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;\n'
                  f'    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp MEM32(0x{counter:X}), eax (32-bit) */\n'
                  f'    if (CMP_L(_fas, _fbs)) goto loc_{pc:08X}; /* jl: less (signed <) */')
        old_branch = before.splitlines()[-1]
        after = before[:-len(old_branch)] + (
            '    if (CMP_L(_fas, _fbs)) {\n'
            f'        /* PILOT native poll wait v1: {pc:08X}. */\n'
            '#ifdef PILOT_NATIVE_POLL_WAIT\n        pilot_native_poll_wait();\n#endif\n'
            f'        goto loc_{pc:08X};\n    }}')
        body = match[0]
        if body.count(before) == 1 and after not in body:
            body = body.replace(before, after)
        elif before in body or body.count(after) != 1:
            raise ValueError('generated poll/checkpoint shape changed: ' + hex(pc))
        source = source[:match.start()] + body + source[match.end():]
    if DECLARATION not in source:
        include = '#include <math.h>\n'
        if source.count(include) != 1 or source.index(include) > BODY.search(source).start():
            raise ValueError('unreviewed generated poll include boundary')
        source = source.replace(include, include + DECLARATION, 1)
    return source


def apply(blob, image, directory, contract):
    validate(blob, image, contract)
    directory = Path(directory)
    sources = {p: p.read_text(encoding='utf-8') for p in sorted(directory.glob('recomp_*.c'))}
    staged = {}
    for record in contract['loops']:
        symbol = f'sub_{number(record["function"]):08X}'
        owners = [p for p, source in sources.items() if any(m[1] == symbol for m in BODY.finditer(source))]
        if len(owners) != 1:
            raise ValueError('ambiguous/missing poll owner: ' + symbol)
        path = owners[0]
        staged[path] = transform(staged.get(path, sources[path]), [record], number(contract['counter']))
    names = sorted(p.name for p in staged)
    if any(not re.fullmatch(r'recomp_[0-9]+\.c', name) for name in names):
        raise ValueError('poll must belong to an ordinary generated CPU unit')
    for path, source in staged.items():
        if source != sources[path]:
            path.write_text(source, encoding='utf-8', newline='\n')
    (directory/'pilot_poll_wait_units.json').write_text(json.dumps(names) + '\n', encoding='utf-8')
    return dict(version=1, contract=contract, contract_sha256=digest(contract), units=names)
