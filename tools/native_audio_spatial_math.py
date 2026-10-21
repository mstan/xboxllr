"""Revision-bound original spatial arithmetic, independent of output admission.

The emitted roots are native CPU helpers. This proof does not enable Play or
claim that a backend consumes their filters, HRTF data or send gains.
"""
import hashlib
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from native_binding import validate_body

ROOT_ABIS = {
    'table_initialize': 0, 'hrtf_lookup': 16,
    'calculator_initialize': 8, 'listener_initialize': 4, 'listener_update': 4,
    'relative': 4, 'hrtf': 4, 'distance': 4, 'cone_angle': 4,
    'cone_gain': 4, 'weights': 4, 'doppler': 4,
    'source_i3dl2_initialize': 8, 'source_i3dl2_calculate': 4,
    'gain': 4, 'pitch': 4, 'frequency_pitch': 4,
}
DEFAULT_SIZES = {'listener': 64, 'source': 76, 'source_i3dl2': 36,
                 'listener_i3dl2': 48, 'calculator': 76,
                 'mix_descriptor': 8, 'mix_pairs': 40}


def retains_spatial_math(old, new):
    """Allow additional proved roots/defaults without replacing prior proof."""
    if not isinstance(old, dict) or not isinstance(new, dict):
        return False
    if set(old) != set(new):
        return False
    for key in old:
        if key in ('roots', 'defaults'):
            if any(new[key].get(name) != value for name, value in old[key].items()):
                return False
        elif key == 'helpers':
            if any(entry not in new[key] for entry in old[key]):
                return False
        elif old[key] != new[key]:
            return False
    return True


def bind_spatial_math(context, proof, functions, data):
    selected = proof.get('spatial_math')
    if selected is None:
        return None
    if (not isinstance(selected, dict) or
            set(selected) != {'adapter', 'roots', 'helpers', 'defaults', 'indirect'} or
            selected['adapter'] != 'original-native-spatial-math-v1' or
            'spatial' not in proof or 'listener' not in proof or
            not isinstance(selected['roots'], dict) or
            set(selected['roots']) != set(ROOT_ABIS) or
            not isinstance(selected['helpers'], list) or
            not 1 <= len(selected['helpers']) <= 128 or
            not isinstance(selected['defaults'], dict) or
            set(selected['defaults']) != set(DEFAULT_SIZES) or
            not isinstance(selected['indirect'], list) or len(selected['indirect']) > 16):
        raise ValueError('incomplete original spatial arithmetic proof')
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    helpers, edges, decoded = {}, {}, {}
    indirect, used = {}, set()
    for entry in selected['indirect']:
        if (not isinstance(entry, dict) or set(entry) != {'site', 'slot', 'target'} or
                any(type(v) is not int or not 0 < v <= 0xffffffff for v in entry.values()) or
                entry['site'] in indirect):
            raise ValueError('invalid spatial arithmetic indirect proof')
        indirect[entry['site']] = entry
    for entry in selected['helpers']:
        if (not isinstance(entry, dict) or set(entry) != {'address', 'body'} or
                type(entry['address']) is not int or not 0 < entry['address'] <= 0xffffffff):
            raise ValueError('invalid spatial arithmetic helper')
        address, body = entry['address'], entry['body']
        validate_body(body)
        function = functions.get(address)
        if (address in helpers or not function or
                not context.matches_body(function, body)):
            raise ValueError('duplicate or changed spatial arithmetic helper')
        blob = data(address, function['size'])
        code = list(decoder.disasm(blob, address))
        if not code or sum(i.size for i in code) != len(blob):
            raise ValueError('incomplete spatial arithmetic decoding')
        returns = {int(i.op_str or '0', 0) for i in code if i.mnemonic == 'ret'}
        if returns != {body['stack_bytes']}:
            raise ValueError('spatial arithmetic helper cleanup differs')
        targets = set()
        for instruction in code:
            if (instruction.mnemonic == 'call' or instruction.mnemonic.startswith('j') or
                    instruction.mnemonic in ('loop', 'loope', 'loopne')):
                if not re.fullmatch('0x[0-9a-f]+', instruction.op_str):
                    # Original SDK lookup selected by its own initializer.
                    # Register calls and unreviewed mutable slots stay closed.
                    callsite = indirect.get(instruction.address)
                    if (instruction.mnemonic != 'call' or callsite is None or
                            instruction.op_str != f'dword ptr [{hex(callsite["slot"])}]'):
                        raise ValueError('indirect spatial arithmetic dependency')
                    target = callsite['target']
                    used.add(instruction.address)
                else:
                    if instruction.address in indirect:
                        raise ValueError('spatial arithmetic indirect site changed')
                    target = int(instruction.op_str, 0)
                if instruction.mnemonic == 'call' or not address <= target < address + function['size']:
                    targets.add(target)
            if (instruction.mnemonic in
                    ('in', 'out', 'int', 'int3', 'syscall', 'sysenter', 'sysret',
                     'cli', 'sti', 'hlt', 'cpuid', 'rdtsc', 'rdtscp', 'rdmsr', 'wrmsr') or
                    'fs:' in instruction.op_str or 'gs:' in instruction.op_str):
                raise ValueError('spatial arithmetic helper has external machine effects')
        helpers[address], edges[address], decoded[address] = entry, targets, code
    if used != set(indirect):
        raise ValueError('unreachable spatial arithmetic indirect proof')
    roots = selected['roots']
    if any(type(address) is not int for address in roots.values()):
        raise ValueError('invalid spatial arithmetic root address')
    if len(set(roots.values())) != len(roots):
        raise ValueError('aliased spatial arithmetic roots')
    for name, stack in ROOT_ABIS.items():
        address = roots[name]
        if (type(address) is not int or address not in helpers or
                helpers[address]['body']['stack_bytes'] != stack):
            raise ValueError('spatial arithmetic root ABI differs: ' + name)
    for entry in indirect.values():
        expected = f'dword ptr [{hex(entry["slot"])}], {hex(entry["target"])}'
        stores = [i for i in decoded[roots['table_initialize']]
                  if i.mnemonic == 'mov' and i.op_str == expected]
        if len(stores) != 1 or entry['target'] != roots['hrtf_lookup']:
            raise ValueError('unproven original spatial lookup initialization')
    pending, seen = list(roots.values()), set()
    while pending:
        address = pending.pop()
        if address in seen:
            continue
        if address not in helpers:
            raise ValueError('unclosed spatial arithmetic dependency')
        seen.add(address)
        pending.extend(edges[address])
    if seen != set(helpers):
        raise ValueError('unreachable spatial arithmetic helper')
    for name, size in DEFAULT_SIZES.items():
        entry = selected['defaults'][name]
        if (not isinstance(entry, dict) or set(entry) != {'address', 'bytes', 'sha256'} or
                type(entry['address']) is not int or not 0 < entry['address'] <= 0xffffffff or
                type(entry['bytes']) is not int or
                entry['bytes'] != size or not isinstance(entry['sha256'], str) or
                not re.fullmatch('[0-9a-f]{64}', entry['sha256'])):
            raise ValueError('invalid spatial arithmetic defaults: ' + name)
        if hashlib.sha256(data(entry['address'], size)).hexdigest() != entry['sha256']:
            raise ValueError('changed spatial arithmetic defaults: ' + name)
    return selected


def spatial_math_header(selected, data):
    result = ['#define PILOT_AUDIO_SPATIAL_MATH_VERSION 1u']
    for name, address in selected['roots'].items():
        result.append(f'#define PILOT_AUDIO_SPATIAL_{name.upper()} 0x{address:08x}u')
    for name, entry in selected['defaults'].items():
        words = struct.unpack('<' + 'I' * (entry['bytes'] // 4), data(entry['address'], entry['bytes']))
        values = ','.join(f'0x{word:08x}u' for word in words)
        result.append(f'static const uint32_t pilot_audio_spatial_default_{name}[] = {{{values}}};')
    result.append(f'#define PILOT_AUDIO_SPATIAL_INDIRECT_COUNT {len(selected["indirect"])}u')
    for i, entry in enumerate(selected['indirect']):
        result.append(f'#define PILOT_AUDIO_SPATIAL_INDIRECT_{i}_SLOT 0x{entry["slot"]:08x}u')
        result.append(f'#define PILOT_AUDIO_SPATIAL_INDIRECT_{i}_TARGET 0x{entry["target"]:08x}u')
    return result
