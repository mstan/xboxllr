#!/usr/bin/env python3
"""Bind a reviewed whole XAPI input family; preserve original USB driver bodies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from native_binding import Binding

def absent_classes(context, proof, variables):
    """Admit exact original objects only under the explicit absent-host policy."""
    entries = proof.get('known_absent_classes', [])
    if not isinstance(entries, list) or len(entries) > 16:
        raise ValueError('invalid absent input class table')
    result, occupied = [], list(variables.values())
    names = set(variables)
    for entry in entries:
        if (not isinstance(entry, dict) or set(entry) != {'symbol', 'address', 'bytes', 'sha256', 'policy'}
                or entry['policy'] != 'native-host-absent-v1'
                or not isinstance(entry['symbol'], str) or not re.fullmatch(r'g_DeviceType_[A-Za-z0-9_]+', entry['symbol'])
                or entry['symbol'] in names or type(entry['address']) is not int
                or not 0 < entry['address'] <= 0xfffffff4 or entry['address'] % 4
                or type(entry['bytes']) is not int or entry['bytes'] != 12):
            raise ValueError('unreviewed absent input class policy/object')
        address = entry['address']
        addresses = {s['address'] for s in context.symbols if s['name'] == entry['symbol']
                     and s['library'] == 'XAPILIB' and s['kind'] == 'variable'}
        if addresses != {address} or any(abs(address-other) < 12 for other in occupied):
            raise ValueError('ambiguous/overlapping absent input object')
        sections = [s for s in context.image['sections'] if s['address'] <= address
                    and address+12 <= s['address']+s['raw_size']]
        if len(sections) != 1:
            raise ValueError('absent input object outside original stored image')
        section = sections[0]
        offset = section['raw_offset'] + address-section['address']
        body = context.blob[offset:offset+12]
        if body != bytes(12) or hashlib.sha256(body).hexdigest() != entry['sha256']:
            raise ValueError('absent input object differs from reviewed initial state')
        names.add(entry['symbol']); occupied.append(address); result.append(dict(entry))
    return result

def bind(xbe, output, symbols, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('input', profile, adapter='xapi-gamepad-v1')
    required = {'XInitDevices': 8, 'XGetDevices': 4, 'XGetDeviceChanges': 12,
                'XInputOpen': 16, 'XInputClose': 4, 'XInputGetCapabilities': 8,
                'XInputGetState': 8, 'XInputSetState': 8,
                'XGetDeviceEnumerationStatus': 0, 'XInputGetDeviceDescription': 4}
    # Explicit optional admission keeps existing generated headers byte stable.
    if 'XInputPoll' in proof['calls']:
        required['XInputPoll']=4
    core = {'XInitDevices', 'XGetDevices', 'XGetDeviceChanges', 'XInputOpen', 'XInputClose', 'XInputGetState'}
    if not core <= set(proof['calls']) <= set(required):
        raise ValueError('incomplete or unsupported input family proof')
    layout = proof['layout']
    layout_fields = {'state_bytes', 'default_polling'}
    if 'XInputGetCapabilities' in proof['calls']:
        layout_fields.add('capabilities_bytes')
    if 'XInputSetState' in proof['calls']:
        layout_fields.add('feedback_motor_offset')
    if set(layout) != layout_fields:
        raise ValueError('unsupported input layout fields')
    for key in layout_fields - {'default_polling'}:
        if type(layout.get(key)) is not int or not 1 <= layout[key] <= 4096:
            raise ValueError('invalid input layout: ' + key)
    if (layout['state_bytes'] != 22 or ('capabilities_bytes' in layout and layout['capabilities_bytes'] != 25)
            or ('feedback_motor_offset' in layout and layout['feedback_motor_offset'] != 66)
            or len(layout.get('default_polling', [])) != 4
            or any(type(n) is not int or not 0 <= n <= 255 for n in layout['default_polling'])):
        raise ValueError('unsupported gamepad ABI layout')
    calls = []
    for name, stack_bytes in required.items():
        if name not in proof['calls']:
            continue
        body = proof['calls'][name]
        if body.get('stack_bytes') != stack_bytes:
            raise ValueError('unsupported input cleanup: ' + name)
        digest = body['sha256']
        # Some database labels have an incorrect prototype. An explicit profile
        # records both that label and the actual decoded RET cleanup separately.
        f = context.symbol(name, body.get('symbol_stack_bytes', stack_bytes))
        if not context.matches_body(f, body):
            raise ValueError('unreviewed input body: ' + name)
        start = int(f['start'], 16)
        section = next(s for s in context.image['sections'] if s['address'] <= start and start+f['size'] <= s['address']+s['raw_size'])
        offset = section['raw_offset'] + start - section['address']
        code = Cs(CS_ARCH_X86, CS_MODE_32).disasm(context.blob[offset:offset+f['size']], start)
        if {int(i.op_str or '0', 0) for i in code if i.mnemonic == 'ret'} != {stack_bytes}:
            raise ValueError('input return disagrees with reviewed cleanup: ' + name)
        calls.append(dict(name=name, symbol=f['name'], address=int(f['start'], 16),
                          stack_bytes=stack_bytes, sha256=digest))
    last_error = context.symbol('SetLastError', 4)
    if proof['set_last_error'].get('stack_bytes') != 4 or not context.matches_body(last_error, proof['set_last_error']):
        raise ValueError('unreviewed thread error helper')
    variables = {}
    variable_names = {'g_DeviceType_Gamepad', 'g_DeviceType_MU', 'g_DeviceType_IRDongle'}
    if set(proof['variables']) != variable_names or any(type(v) is not int or not 0 < v <= 0xffffffff for v in proof['variables'].values()):
        raise ValueError('invalid input device variable proof')
    for name in ('g_DeviceType_Gamepad', 'g_DeviceType_MU', 'g_DeviceType_IRDongle'):
        addresses = {s['address'] for s in context.symbols
                     if s['name'] == name and s['library'] == 'XAPILIB' and s['kind'] == 'variable'}
        if len(addresses) != 1:
            raise ValueError('ambiguous input variable: ' + name)
        variables[name] = addresses.pop()
        if variables[name] != proof['variables'][name]:
            raise ValueError('input variable differs from reviewed profile: ' + name)
    absent_types = absent_classes(context, proof, variables)

    binding = dict(xbe_sha256=context.image['sha256'], calls=calls, variables=variables,
                   set_last_error=dict(symbol=last_error['name'], sha256=proof['set_last_error']['sha256']),
                   **layout)
    if absent_types:
        binding['known_absent_classes'] = absent_types
    header = ['#pragma once', 'enum PilotInputEntry {']
    header += [f'    PILOT_INPUT_{c["name"]},' for c in calls]
    header += ['    PILOT_INPUT_COUNT', '};',
               'static const struct { const char *name; unsigned stack_bytes; } pilot_input_calls[] = {']
    header += [f'    {{"{c["name"]}", {c["stack_bytes"]}}},' for c in calls]
    header += ['};']
    for name, address in variables.items():
        header.append(f'#define PILOT_INPUT_{name} 0x{address:08x}u')
    old_header = '\n'.join(header) + '\n'
    absent = [name for name in required if name not in proof['calls']]
    header += ['enum PilotInputAbsentEntry {'] + [f'    PILOT_INPUT_{name} = PILOT_INPUT_COUNT + {i+1},' for i, name in enumerate(absent)] + ['};'] if absent else []
    header += [f'#define PILOT_INPUT_HAS_ENUMERATION_STATUS {int("XGetDeviceEnumerationStatus" in proof["calls"])}',
               f'#define PILOT_INPUT_HAS_DEVICE_DESCRIPTION {int("XInputGetDeviceDescription" in proof["calls"])}',
               f'#define PILOT_INPUT_HAS_CAPABILITIES {int("XInputGetCapabilities" in proof["calls"])}',
               f'#define PILOT_INPUT_HAS_FEEDBACK {int("XInputSetState" in proof["calls"])}',
               '#define PILOT_INPUT_ABI_VERSION 1u',
               f'#define PILOT_INPUT_PROFILE_SHA256 "{context.image["sha256"]}"']
    for key in ('state_bytes', 'capabilities_bytes', 'feedback_motor_offset'):
        if key in layout:
            header.append(f'#define PILOT_INPUT_{key.upper()} {layout[key]}u')
    for i, value in enumerate(layout['default_polling']):
        header.append(f'#define PILOT_INPUT_DEFAULT_POLLING_{i} {value}u')
    previous_header = '\n'.join(header) + '\n'
    empty_class_header = previous_header + '#define PILOT_INPUT_ABSENT_CLASS_ABI_VERSION 1u\n#define PILOT_INPUT_ABSENT_CLASS_COUNT 0u\n'
    header += ['#define PILOT_INPUT_ABSENT_CLASS_ABI_VERSION 1u',
               f'#define PILOT_INPUT_ABSENT_CLASS_COUNT {len(absent_types)}u']
    if absent_types:
        header += ['static const struct { unsigned address; const char *name; } pilot_input_absent_classes[] = {']
        header += [f'    {{0x{entry["address"]:08x}u, "{entry["symbol"]}"}},' for entry in absent_types]
        header += ['};']
    if 'XInputPoll' in proof['calls']:
        header += ['#define PILOT_INPUT_HAS_POLL 1']
    wrapper = ['#include "pilot_input_bindings.h"',
               'extern void pilot_input_dispatch(unsigned, void (*)(void));',
               f'extern void {last_error["name"]}(void);']
    symbols_by_name = {c['name']: c['symbol'] for c in calls}
    qualify = {'XInputGetCapabilities', 'XInputSetState'} <= set(symbols_by_name)
    probe_declarations = ['#ifdef PILOT_INPUT_QUALIFICATION',
        'extern void pilot_input_qualify_open(void (*)(void), void (*)(void));',
        'extern void pilot_input_qualify_state(void);',
        f'extern void {symbols_by_name.get("XInputGetCapabilities", "pilot_absent_input_capabilities")}(void);',
        f'extern void {symbols_by_name.get("XInputSetState", "pilot_absent_input_feedback")}(void);', '#endif'] if qualify else []
    wrapper += probe_declarations
    probe_call = ['#ifdef PILOT_INPUT_QUALIFICATION',
        f'    pilot_input_qualify_open({symbols_by_name.get("XInputGetCapabilities", "pilot_absent_input_capabilities")}, {symbols_by_name.get("XInputSetState", "pilot_absent_input_feedback")});', '#endif'] if qualify else []
    state_probe = ['#ifdef PILOT_INPUT_QUALIFICATION', '    pilot_input_qualify_state();', '#endif']
    call_blocks={}
    for c in calls:
        block = [f'extern void pilot_original_input_{c["name"]}(void);',
                    f'void {c["symbol"]}(void) {{', '#ifdef PILOT_INPUT_NATIVE',
                    f'    pilot_input_dispatch(PILOT_INPUT_{c["name"]}, {last_error["name"]});',
                    '#else', f'    pilot_original_input_{c["name"]}();', '#endif']
        if c['name'] == 'XInputOpen':
            block += probe_call
        if c['name'] == 'XInputGetState':
            block += state_probe
        block += ['}'];call_blocks[c['name']]='\n'.join(block)+'\n';wrapper += block
    texts = {'pilot_input_bindings.h': '\n'.join(header) + '\n',
             'pilot_input_binding.c': '\n'.join(wrapper) + '\n'}
    record = output / 'input-binding.aot-private.json'
    if (context.generated / 'pilot_input_binding.c').exists():
        prior=json.loads(record.read_text())
        without_poll=dict(binding,calls=[c for c in calls if c['name']!='XInputPoll'])
        if 'XInputPoll' in call_blocks and prior==without_poll and context.manifest.get('input_binding')==prior:
            prior_header='\n'.join(line for line in texts['pilot_input_bindings.h'].splitlines() if
                'PILOT_INPUT_XInputPoll' not in line and '"XInputPoll"' not in line and
                not line.startswith('#define PILOT_INPUT_HAS_POLL '))+'\n'
            prior_wrapper=texts['pilot_input_binding.c'].replace(call_blocks['XInputPoll'],'')
            if ((context.generated/'pilot_input_bindings.h').read_text()!=prior_header or
                    (context.generated/'pilot_input_binding.c').read_text()!=prior_wrapper):
                raise ValueError('existing input source differs before optional poll admission')
            poll=next(c for c in calls if c['name']=='XInputPoll');needle=f'void {poll["symbol"]}(void)\n{{'
            matches=[]
            for path in context.generated.glob('recomp_*.c'):
                source=path.read_text(encoding='utf-8')
                if needle in source:matches.append((path,source))
            if len(matches)!=1 or matches[0][1].count(needle)!=1:
                raise ValueError('missing original poll body before optional admission')
            path,source=matches[0]
            path.write_text(source.replace(needle,'void pilot_original_input_XInputPoll(void)\n{',1),encoding='utf-8',newline='\n')
            for name,text in texts.items():(context.generated/name).write_text(text,encoding='utf-8',newline='\n')
            record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
            context.manifest['input_binding']=binding
            for path in (path,*(context.generated/name for name in texts)):
                context.manifest['files'][path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
            context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
            return binding
        without_state = texts['pilot_input_binding.c'].replace('extern void pilot_input_qualify_state(void);\n', '').replace('\n'.join(state_probe)+'\n', '')
        old_wrapper = texts['pilot_input_binding.c'].replace('\n'.join(probe_declarations)+'\n', '').replace('\n'.join(probe_call)+'\n', '').replace('\n'.join(state_probe)+'\n', '')
        previous_binding = {k: v for k, v in binding.items() if k != 'known_absent_classes'}
        prior = json.loads(record.read_text())
        # Explicit bind may add this newly proved table to an otherwise exact
        # prior admission. Existing bodies/layout/objects must remain identical.
        if context.manifest.get('input_binding') != prior or prior not in (binding, previous_binding) or any(
                (context.generated / n).read_text() not in
                ([s, without_state, old_wrapper] if n == 'pilot_input_binding.c' else [s, previous_header, empty_class_header, old_header]) for n, s in texts.items()):
            raise ValueError('existing input binding differs')
        changed = False
        if prior != binding:
            record.write_text(json.dumps(binding, indent=2)+'\n', newline='\n')
            context.manifest['input_binding'] = binding
            changed = True
        for name, text in texts.items():
            path = context.generated / name
            if path.read_text() != text:
                path.write_text(text, encoding='utf-8', newline='\n')
                context.manifest['files'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
                changed = True
        if changed:
            context.manifest_path.write_text(json.dumps(context.manifest, indent=2)+'\n', newline='\n')
        return binding
    replacements = {c['symbol']: 'pilot_original_input_' + c['name'] for c in calls}
    found = set()
    def rename(match):
        name = match[1]
        if name not in replacements:
            return match[0]
        if name in found:
            raise ValueError('duplicate input body')
        found.add(name)
        return 'void ' + replacements[name] + '(void)\n{'
    edits = {}
    for path in context.generated.glob('recomp_*.c'):
        source = path.read_text(encoding='utf-8')
        updated = re.sub(r'void (sub_[0-9A-Fa-f]{8})\(void\)\n\{', rename, source)
        if source != updated:
            edits[path] = updated
    if found != set(replacements):
        raise ValueError('missing original input bodies')
    for path, source in edits.items():
        path.write_text(source, encoding='utf-8', newline='\n')
    for name, source in texts.items():
        (context.generated / name).write_text(source, encoding='utf-8', newline='\n')
    record.write_text(json.dumps(binding, indent=2) + '\n', newline='\n')
    context.manifest['input_binding'] = binding
    context.manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest, indent=2) + '\n', newline='\n')
    return binding


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'output', 'symbols'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument("--profile", type=Path)
    args = parser.parse_args()
    result = bind(args.xbe, args.output, args.symbols, args.profile)
    print(json.dumps(dict(xbe_sha256=result['xbe_sha256'], bound=len(result['calls']))))
