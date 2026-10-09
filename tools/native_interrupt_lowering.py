#!/usr/bin/env python3
"""Lower verified interrupt, byte port and cache-publication instructions.

Only reviewed straight-line MOV/PUSH/NOP shadow instructions are supported.
Other STI sites retain the explicit unsupported fault. No address is singled
out; original bytes and the pinned donor's actual statement must both match.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from aot_inventory import parse_xbe
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

SITE = re.compile(r'RECOMP_UNIMPL\("(cli|sti|in al, dx|out dx, al|wbinvd)", (0x[0-9A-Fa-f]+)u\); /\* TODO: \1 \*/')
REGS = {'eax', 'ecx', 'edx', 'ebx', 'esi', 'edi'}
HEADER = '''
/* Original CPU interrupt flag effects, including STI's following instruction. */
void pilot_cpu_cli(void);
void pilot_cpu_sti(void);
void pilot_cpu_sti_complete(void);
'''


def shadow_statement(insn):
    if insn.mnemonic == 'mov':
        parts = insn.op_str.split(', ')
        if len(parts) == 2 and set(parts) <= REGS:
            return f'{parts[0]} = {parts[1]};'
    if insn.mnemonic == 'push' and insn.op_str in REGS:
        return f'PUSH32(esp, {insn.op_str});'
    if insn.mnemonic == 'nop':
        return '/* nop */'
    return None


def lower(code, read):
    disassembler = Cs(CS_ARCH_X86, CS_MODE_32)
    edits, rows = [], []
    for site in SITE.finditer(code):
        kind, address = site[1], int(site[2], 16)
        instructions = list(disassembler.disasm(read(address, 16), address))
        expected=kind.split(' ',1)[0]
        expected_size = 2 if kind == 'wbinvd' else 1
        if not instructions or instructions[0].mnemonic != expected or instructions[0].size != expected_size or (
                expected in ('in','out') and instructions[0].op_str!=kind.split(' ',1)[1]):
            raise ValueError('interrupt instruction differs from original bytes')
        end = site.end()
        if kind == 'in al, dx':
            replacement = 'SET_LO8(eax, pilot_cpu_in8(LO16(edx)));'
        elif kind == 'out dx, al':
            replacement = 'pilot_cpu_out8(LO16(edx), LO8(eax));'
        elif kind == 'cli':
            replacement = 'pilot_cpu_cli();'
        elif kind == 'wbinvd':
            replacement = 'pilot_cpu_wbinvd();'
        else:
            following = instructions[1] if len(instructions) > 1 else None
            statement = shadow_statement(following) if following else None
            match = re.match(r'\s*' + re.escape(statement), code[end:]) if statement else None
            if not match:
                rows.append(dict(address=address, opcode=kind, supported=False,
                                 reason='unqualified following instruction or generated boundary'))
                continue
            end += match.end()
            replacement = 'pilot_cpu_sti();' + match.group() + '\n    pilot_cpu_sti_complete();'
        edits.append((site.start(), end, replacement))
        rows.append(dict(address=address, opcode=kind, supported=True))
    for start, end, replacement in reversed(edits):
        code = code[:start] + replacement + code[end:]
    return code, rows


def apply(directory, xbe):
    blob = xbe.read_bytes()
    image = parse_xbe(blob)

    def read(address, size):
        section = next((s for s in image['sections'] if s['flags'] & 4 and
                        s['address'] <= address and address + size <= s['address'] + s['raw_size']), None)
        if section is None:
            raise ValueError('interrupt instruction outside executable original bytes')
        offset = section['raw_offset'] + address - section['address']
        return blob[offset:offset + size]

    changes, rows = [], []
    for path in sorted(directory.glob('*.c')):
        original = path.read_text(encoding='utf-8')
        code, sites = lower(original, read)
        rows.extend(sites)
        if code != original:
            changes.append((path, code))
    header = directory / 'recomp_types.h'
    text = header.read_text(encoding='utf-8')
    if 'void pilot_cpu_cli(void);' not in text:
        text += HEADER
    if 'uint8_t pilot_cpu_in8(uint16_t port);' not in text:
        text += '\n/* Real byte port read; unsupported ports stop explicitly. */\nuint8_t pilot_cpu_in8(uint16_t port);\n'
    if 'void pilot_cpu_out8(uint16_t port, uint8_t value);' not in text:
        text += '\n/* Real byte port write; unsupported ports stop explicitly. */\nvoid pilot_cpu_out8(uint16_t port, uint8_t value);\n'
    if 'void pilot_cpu_wbinvd(void);' not in text:
        text += '\n/* Publish prior stores on the coherent CPU/device memory bus. */\nvoid pilot_cpu_wbinvd(void);\n'
    if text != header.read_text(encoding='utf-8'):
        changes.append((header, text))
    for path, code in changes:
        path.write_text(code, encoding='utf-8', newline='\n')
    return dict(version=4, xbe_sha256=image['sha256'], sites=rows)


def upgrade(xbe, output):
    if 'build' not in output.resolve().parts:
        raise ValueError('private generated output must remain under build/')
    manifest_path = output / 'generation.aot-private.json'
    manifest = json.loads(manifest_path.read_text())
    if hashlib.sha256(xbe.read_bytes()).hexdigest() != manifest['sha256']:
        raise ValueError('original image differs from generated inventory')
    directory = output / 'gen'
    for name, digest in manifest['files'].items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
            raise ValueError('changed generated artifact: ' + name)
    result = apply(directory, xbe)
    previous = manifest.get('interrupt_flags', {}).get('sites', [])
    by_address = {row['address']: row for row in previous + result['sites']}
    result['sites'] = sorted(by_address.values(), key=lambda row: row['address'])
    manifest['interrupt_flags'] = result
    manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in sorted(directory.iterdir()) if p.is_file()}
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    print(json.dumps(upgrade(args.xbe, args.output)))
