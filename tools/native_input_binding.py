#!/usr/bin/env python3
"""Bind a reviewed whole XAPI input family; preserve original USB driver bodies."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from native_binding import Binding

IMAGE = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
REVIEWED = {
    'XInitDevices': (8, '4a88597b418d0b7805b55033a499f5ba42c1b3d4618ab137822f941edc8572a6'),
    'XGetDevices': (4, 'a66f3c95c357eaa27eaf85e9a2a563a7fa1dda3ba4841df773b5b95d54b8c29f'),
    'XGetDeviceChanges': (12, '00843c1d6b7e7094fcc024e51e477d888d546f829d0d43c2853a0c3baf32d9e4'),
    'XInputOpen': (16, '8447e7cbf75ebf05a111e7d9066b20b302dfc62a2444cc52b1cc06377c191e09'),
    'XInputClose': (4, 'e764297b82caa4ba15f4e5165fba6f2c8fdf6624777ae940f3dc89c34cd73643'),
    'XInputGetCapabilities': (8, 'e3fe8b687db930a2198e51927df8a4a030082ddcc13341f63f1c6dbde36fd521'),
    'XInputGetState': (8, '5706c20aae6e7e5b8f80116260bc1cff23701c3fb6e9134644983dac955e9cb5'),
    'XInputSetState': (8, '98c3978b7d7467a26d99174fa9951b30101dbcd8a39e21c69a2c548485ac7152'),
}
LAST_ERROR = '1f297a4f8228adc6af4f404c12430e9738a6fbadf4e7185745f123b6216d38e7'


def bind(xbe, output, symbols):
    context = Binding(xbe, output, symbols)
    if context.image['sha256'] != IMAGE:
        raise ValueError('unreviewed XAPI input image')
    calls = []
    for name, (stack_bytes, digest) in REVIEWED.items():
        f = context.symbol(name, stack_bytes)
        if context.digest(f) != digest:
            raise ValueError('unreviewed input body: ' + name)
        calls.append(dict(name=name, symbol=f['name'], address=int(f['start'], 16),
                          stack_bytes=stack_bytes, sha256=digest))
    last_error = context.symbol('SetLastError', 4)
    if context.digest(last_error) != LAST_ERROR:
        raise ValueError('unreviewed thread error helper')
    variables = {}
    for name in ('g_DeviceType_Gamepad', 'g_DeviceType_MU', 'g_DeviceType_IRDongle'):
        addresses = {s['address'] for s in context.symbols
                     if s['name'] == name and s['library'] == 'XAPILIB' and s['kind'] == 'variable'}
        if len(addresses) != 1:
            raise ValueError('ambiguous input variable: ' + name)
        variables[name] = addresses.pop()

    # XDK4831: packed subtype + reserved WORD + 18-byte gamepad + 4-byte rumble.
    # Later donor layouts containing Type/SubType are not this ABI.
    binding = dict(xbe_sha256=IMAGE, calls=calls, variables=variables,
                   set_last_error=dict(symbol=last_error['name'], sha256=LAST_ERROR),
                   state_bytes=22, capabilities_bytes=25,
                   feedback_motor_offset=66, default_polling=[1, 8, 0, 0])
    header = ['#pragma once', 'enum PilotInputEntry {']
    header += [f'    PILOT_INPUT_{c["name"]},' for c in calls]
    header += ['    PILOT_INPUT_COUNT', '};',
               'static const struct { const char *name; unsigned stack_bytes; } pilot_input_calls[] = {']
    header += [f'    {{"{c["name"]}", {c["stack_bytes"]}}},' for c in calls]
    header += ['};']
    for name, address in variables.items():
        header.append(f'#define PILOT_INPUT_{name} 0x{address:08x}u')
    wrapper = ['#include "pilot_input_bindings.h"',
               'extern void pilot_input_dispatch(unsigned, void (*)(void));',
               f'extern void {last_error["name"]}(void);']
    symbols_by_name = {c['name']: c['symbol'] for c in calls}
    probe_declarations = ['#ifdef PILOT_INPUT_QUALIFICATION',
        'extern void pilot_input_qualify_open(void (*)(void), void (*)(void));',
        'extern void pilot_input_qualify_state(void);',
        f'extern void {symbols_by_name["XInputGetCapabilities"]}(void);',
        f'extern void {symbols_by_name["XInputSetState"]}(void);', '#endif']
    wrapper += probe_declarations
    probe_call = ['#ifdef PILOT_INPUT_QUALIFICATION',
        f'    pilot_input_qualify_open({symbols_by_name["XInputGetCapabilities"]}, {symbols_by_name["XInputSetState"]});', '#endif']
    state_probe = ['#ifdef PILOT_INPUT_QUALIFICATION', '    pilot_input_qualify_state();', '#endif']
    for c in calls:
        wrapper += [f'extern void pilot_original_input_{c["name"]}(void);',
                    f'void {c["symbol"]}(void) {{', '#ifdef PILOT_INPUT_NATIVE',
                    f'    pilot_input_dispatch(PILOT_INPUT_{c["name"]}, {last_error["name"]});',
                    '#else', f'    pilot_original_input_{c["name"]}();', '#endif']
        if c['name'] == 'XInputOpen':
            wrapper += probe_call
        if c['name'] == 'XInputGetState':
            wrapper += state_probe
        wrapper += ['}']
    texts = {'pilot_input_bindings.h': '\n'.join(header) + '\n',
             'pilot_input_binding.c': '\n'.join(wrapper) + '\n'}
    record = output / 'input-binding.aot-private.json'
    if (context.generated / 'pilot_input_binding.c').exists():
        without_state = texts['pilot_input_binding.c'].replace('extern void pilot_input_qualify_state(void);\n', '').replace('\n'.join(state_probe)+'\n', '')
        old_wrapper = texts['pilot_input_binding.c'].replace('\n'.join(probe_declarations)+'\n', '').replace('\n'.join(probe_call)+'\n', '').replace('\n'.join(state_probe)+'\n', '')
        if json.loads(record.read_text()) != binding or any(
                (context.generated / n).read_text() not in
                ([s, without_state, old_wrapper] if n == 'pilot_input_binding.c' else [s]) for n, s in texts.items()):
            raise ValueError('existing input binding differs')
        path = context.generated / 'pilot_input_binding.c'
        if path.read_text() != texts[path.name]:
            path.write_text(texts[path.name], encoding='utf-8', newline='\n')
            context.manifest['files'][path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
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
    args = parser.parse_args()
    result = bind(args.xbe, args.output, args.symbols)
    print(json.dumps(dict(xbe_sha256=result['xbe_sha256'], bound=len(result['calls']))))
