#!/usr/bin/env python3
"""Bind a closed, exact-image offline network lifecycle and guarded consumers."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM
from native_binding import Binding, validate_body


def original_bytes(context, address, size):
    sections = [s for s in context.image['sections'] if s['address'] <= address
                and address + size <= s['address'] + s['raw_size']]
    if len(sections) != 1:
        raise ValueError('network proof outside stored original image')
    section = sections[0]
    offset = section['raw_offset'] + address - section['address']
    return context.blob[offset:offset+size]


def decode(context, address, size, begin, end, strict=True):
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.skipdata = not strict
    instructions = list(decoder.disasm(original_bytes(context, address, size), address))
    if sum(i.size for i in instructions) != size:
        raise ValueError(f'incomplete network body decoding at {address:08x}')
    refs = []
    detail = Cs(CS_ARCH_X86, CS_MODE_32); detail.detail = True; detail.skipdata = not strict
    for instruction in detail.disasm(original_bytes(context, address, size), address):
        if not instruction.id:  # Non-admitted discovery ranges can contain data.
            continue
        for operand in instruction.operands:
            target = None
            if operand.type == X86_OP_MEM and not operand.mem.base and not operand.mem.index:
                target = operand.mem.disp & 0xffffffff
            elif operand.type == X86_OP_IMM:
                target = operand.imm & 0xffffffff
            if target is not None and begin <= target < end:
                refs.append([instruction.address, target])
    return sorted({int(i.op_str or '0', 0) for i in instructions if i.mnemonic == 'ret'}), refs


def bind(xbe, output, symbols, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('network', profile, adapter='xonline-offline-lifecycle-v1')
    if set(proof) != {'adapter', 'globals', 'consumer_range', 'calls', 'callback_table', 'untranslated_callbacks', 'set_last_error'}:
        raise ValueError('unsupported network proof fields')
    globals_ = proof['globals']
    if (not isinstance(globals_, dict) or set(globals_) != {'online_owner', 'online_gate', 'network_owner', 'network_gate'}
            or any(type(a) is not int or not 0 < a <= 0xfffffffc or a % 4 for a in globals_.values())
            or len(set(globals_.values())) != 4):
        raise ValueError('invalid network owner globals')
    extent = proof['consumer_range']
    if (not isinstance(extent, list) or len(extent) != 2 or any(type(v) is not int for v in extent)
            or not 0 < extent[0] < extent[1] <= 0x100000000 or extent[1]-extent[0] > 4096
            or not all(extent[0] <= a and a+4 <= extent[1] for a in globals_.values())):
        raise ValueError('invalid network consumer extent')
    begin, end = extent
    functions = {int(f['start'], 16): f for f in context.functions}
    if len(functions) != len(context.functions):
        raise ValueError('duplicate generated network function entries')
    calls, seen, lifecycle = [], set(), set()
    for entry in proof['calls']:
        validate_body(entry)
        if set(entry) != {'address', 'bytes', 'sha256', 'returns', 'references', 'action'}:
            raise ValueError('invalid network entry proof fields')
        address = entry['address']; action = entry['action']
        if type(address) is not int or address in seen or address not in functions or action not in ('startup', 'cleanup', 'guard'):
            raise ValueError('invalid network entry/action')
        function = functions[address]
        if function.get('section') == '.data' or not context.matches_body(function, entry):
            raise ValueError('unreviewed network original body')
        returns, refs = decode(context, address, entry['bytes'], begin, end)
        if returns != entry['returns'] or refs != entry['references']:
            raise ValueError('network return/global operand proof differs')
        if action != 'guard':
            expected = [4] if action == 'startup' else [0]
            if action in lifecycle or returns != expected or not any(target == globals_['online_owner'] for _, target in refs):
                raise ValueError('incomplete or unreviewed network lifecycle ABI')
            lifecycle.add(action)
        seen.add(address)
        calls.append(dict(entry, symbol=function['name']))
    if lifecycle != {'startup', 'cleanup'}:
        raise ValueError('missing network lifecycle entry')
    # A profile cannot omit another discovered original global consumer while
    # admitting startup. The exact image/body identity remains the proof basis.
    consumers = set()
    for address, function in functions.items():
        if function.get('section') == '.data':
            continue
        _, refs = decode(context, address, function['size'], begin, end, strict=False)
        if refs:
            consumers.add(address)
    if not consumers <= seen:
        raise ValueError('unprotected original network global consumer')
    # Check every byte alignment too: undecodable discovery ranges or a stored
    # literal alias must not silently bypass the decoded-consumer admission.
    starts = sorted(a for a, f in functions.items() if f.get('section') != '.data')
    for section in context.image['sections']:
        data = context.blob[section['raw_offset']:section['raw_offset']+section['raw_size']]
        for target in globals_.values():
            offset = data.find(struct.pack('<I', target))
            while offset >= 0:
                address = section['address']+offset
                index = bisect.bisect_right(starts, address)-1
                owner = starts[index] if index >= 0 else None
                if owner not in seen or address+4 > int(functions[owner]['end'], 16):
                    raise ValueError('unprotected original network global literal/alias')
                offset = data.find(struct.pack('<I', target), offset+1)
    table = proof['callback_table']
    if (not isinstance(table, dict) or set(table) != {'address', 'entries', 'publisher', 'instruction', 'owner_offset'}
            or len(table['entries']) != 4 or len(set(table['entries'])) != 4
            or any(type(a) is not int or not 0 < a <= 0xffffffff for a in table['entries'])
            or table['publisher'] not in seen or table['owner_offset'] != 0x264):
        raise ValueError('unreviewed original network callback table')
    if original_bytes(context, table['address'], 16) != struct.pack('<4I', *table['entries']):
        raise ValueError('original network callback table differs')
    publisher = functions[table['publisher']]
    detail = Cs(CS_ARCH_X86, CS_MODE_32); detail.detail = True
    publish = [i for i in detail.disasm(original_bytes(context, table['publisher'], publisher['size']), table['publisher'])
               if i.address == table['instruction']]
    if (len(publish) != 1 or publish[0].mnemonic != 'mov' or len(publish[0].operands) != 2
            or publish[0].operands[0].type != X86_OP_MEM or publish[0].operands[0].mem.disp != table['owner_offset']
            or not publish[0].operands[0].mem.base or publish[0].operands[0].mem.index
            or publish[0].operands[1].type != X86_OP_IMM or publish[0].operands[1].imm != table['address']):
        raise ValueError('original network callback publisher role differs')
    absent = set()
    for entry in proof['untranslated_callbacks']:
        validate_body(entry); address = entry['address']
        if (set(entry) != {'address', 'bytes', 'sha256', 'returns'} or address in absent or address in functions
                or hashlib.sha256(original_bytes(context, address, entry['bytes'])).hexdigest() != entry['sha256']
                or decode(context, address, entry['bytes'], begin, end)[0] != entry['returns']):
            raise ValueError('unprotected or changed original callback entry')
        # If it becomes translated, require an explicit new guard admission.
        dispatch = context.generated / 'recomp_dispatch.c'
        if dispatch.exists() and re.search(r'\b0x0*'+format(address, 'x')+r'u?\b', dispatch.read_text(), re.I):
            raise ValueError('unprotected original callback dispatch entry')
        absent.add(address)
    if set(table['entries']) != (seen & set(table['entries'])) | absent:
        raise ValueError('unprotected original callback table entry')
    error = context.symbol('SetLastError', 4)
    if (proof['set_last_error'].get('stack_bytes') != 4 or not context.matches_body(error, proof['set_last_error'])
            or decode(context, int(error['start'], 16), error['size'], begin, end)[0] != [4]):
        raise ValueError('unreviewed network thread LastError helper')
    calls.sort(key=lambda c: c['address'])
    binding = dict(xbe_sha256=context.image['sha256'], adapter=proof['adapter'], calls=calls,
                   globals=globals_, consumer_range=extent, callback_table=table,
                   untranslated_callbacks=proof['untranslated_callbacks'], set_last_error=proof['set_last_error'])
    header = ['#pragma once', '#define PILOT_NETWORK_ABI_VERSION 1u',
              'enum { PILOT_NETWORK_ACTION_GUARD, PILOT_NETWORK_ACTION_STARTUP, PILOT_NETWORK_ACTION_CLEANUP };',
              'enum PilotNetworkEntry {']
    header += [f'    PILOT_NETWORK_ENTRY_{c["address"]:08X},' for c in calls]
    header += ['    PILOT_NETWORK_COUNT', '};',
               'static const struct { const char *name; unsigned address, action, stack_bytes; } pilot_network_calls[] = {']
    for call in calls:
        action = call['action'].upper()
        header.append(f'    {{"{call["symbol"]}", 0x{call["address"]:08x}u, PILOT_NETWORK_ACTION_{action}, {4 if action == "STARTUP" else 0}u}},')
    header += ['};']
    wrapper = ['#include "pilot_network_bindings.h"',
               'extern void pilot_network_dispatch(unsigned, void (*)(void));', f'extern void {error["name"]}(void);']
    replacements = {}
    for call in calls:
        symbol = call['symbol']
        if not re.fullmatch(r'sub_[0-9A-Fa-f]{8}', symbol):
            raise ValueError('invalid generated network symbol')
        original = f'pilot_original_network_{call["address"]:08x}'
        replacements[symbol] = original
        wrapper += [f'extern void {original}(void);', f'void {symbol}(void) {{', '#if defined(PILOT_NETWORK_OFFLINE) && PILOT_NETWORK_OFFLINE',
                    f'    pilot_network_dispatch(PILOT_NETWORK_ENTRY_{call["address"]:08X}, {error["name"]});',
                    '#else', f'    {original}();', '#endif', '}']
    texts = {'pilot_network_bindings.h': '\n'.join(header)+'\n', 'pilot_network_binding.c': '\n'.join(wrapper)+'\n'}
    record = output / 'network-binding.aot-private.json'
    if (context.generated / 'pilot_network_binding.c').exists():
        if (json.loads(record.read_text()) != binding or context.manifest.get('network_binding') != binding
                or any((context.generated / name).read_text() != text for name, text in texts.items())):
            raise ValueError('existing network binding differs')
        return binding
    found, edits = set(), {}
    def rename(match):
        name = match[1]
        if name not in replacements:
            return match[0]
        if name in found:
            raise ValueError('duplicate original network generated body')
        found.add(name)
        return 'void '+replacements[name]+'(void)\n{'
    for path in context.generated.glob('recomp_*.c'):
        source = path.read_text(encoding='utf-8')
        updated = re.sub(r'void (sub_[0-9A-Fa-f]{8})\(void\)\n\{', rename, source)
        if source != updated:
            edits[path] = updated
    if found != set(replacements):
        raise ValueError('missing original network generated body')
    for path, source in edits.items():
        path.write_text(source, encoding='utf-8', newline='\n')
    for name, source in texts.items():
        (context.generated / name).write_text(source, encoding='utf-8', newline='\n')
    record.write_text(json.dumps(binding, indent=2)+'\n', encoding='utf-8', newline='\n')
    context.manifest['network_binding'] = binding
    context.manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest, indent=2)+'\n', encoding='utf-8', newline='\n')
    return binding


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'output', 'symbols'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--profile', type=Path)
    args = parser.parse_args()
    result = bind(args.xbe, args.output, args.symbols, args.profile)
    print(json.dumps(dict(xbe_sha256=result['xbe_sha256'], bound=len(result['calls']))))
