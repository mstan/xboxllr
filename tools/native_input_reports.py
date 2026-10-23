"""Source-bound report conversion policies; SDK labels do not select behavior."""
import copy
import hashlib
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from native_binding import validate_body


def inspect(context, proof, layout):
    selected = proof.get('report_processing')
    if selected is None:
        return None  # Preserve the existing later-XDK threshold and headers.
    if (not isinstance(selected, dict) or set(selected) != {'adapter', 'class_table', 'callback', 'activity_reset'} or
            selected['adapter'] != 'xid-copy18-activity-v1'):
        raise ValueError('unreviewed input report conversion')

    def original(row, size):
        if (not isinstance(row, dict) or set(row) != {'address', 'bytes', 'sha256'} or
                type(row['address']) is not int or not 0 < row['address'] <= 0xffffffff or
                type(row['bytes']) is not int or row['bytes'] != size):
            raise ValueError('invalid original input report extent')
        validate_body(row)
        address = row['address']
        data = read(address, size)
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('original input report proof changed')
        return data

    def read(address, size):
        sections = [s for s in context.image['sections'] if s['address'] <= address and
                    address + size <= s['address'] + s['raw_size']]
        if len(sections) != 1:
            raise ValueError('input report proof outside unique stored image')
        offset = sections[0]['raw_offset'] + address - sections[0]['address']
        data = context.blob[offset:offset+size]
        if len(data) != size:
            raise ValueError('truncated input report proof')
        return data

    raw = original(selected['callback'], 65)
    table = struct.unpack('<6I', original(selected['class_table'], 24))
    if (selected['class_table']['address'] % 4 or table[:2] != (0x1001, 0x104) or
            table[5] != selected['callback'].get('address') or read(table[2], 1) != b'\x12' or
            read(table[3], 1) != b'\x04' or
            list(read(table[4], 4)) != layout['default_polling']):
        raise ValueError('original gamepad report descriptors changed')
    start = selected['callback']['address']
    code = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(raw, start))
    # Exact original copy/length/return/branch topology. The activity tail does
    # not supply input data. No canonical discovery entry is required: native
    # input replaces the USB owner and never executes this callback.
    expected = [
        ('mov', 'eax, ecx'), ('mov', 'ecx, dword ptr [eax + 0x66]'), ('cmp', 'ecx, 2'),
        ('lea', 'edx, [eax + 0x14]'), ('jb', hex(start+59)), ('add', 'ecx, -2'),
        ('push', 'esi'), ('push', 'edi'), ('lea', 'esi, [eax + 0x34]'), ('mov', 'eax, ecx'),
        ('shr', 'ecx, 2'), ('mov', 'edi, edx'),
        ('rep movsd', 'dword ptr es:[edi], dword ptr [esi]'), ('mov', 'ecx, eax'),
        ('and', 'ecx, 3'), ('rep movsb', 'byte ptr es:[edi], byte ptr [esi]'),
        ('test', 'byte ptr [edx], 0x3f'), ('pop', 'edi'), ('pop', 'esi'),
        ('jne', hex(start+60)), ('xor', 'eax, eax'), ('cmp', 'byte ptr [edx + eax + 2], 0'),
        ('jne', hex(start+60)), ('inc', 'eax'), ('cmp', 'eax, 7'), ('jle', hex(start+46)),
        ('ret', ''),
    ]
    if (len(code) != 28 or code[-1].address + code[-1].size != start+65 or
            [(i.mnemonic, i.op_str) for i in code[:-1]] != expected or
            code[-1].mnemonic != 'jmp' or not code[-1].op_str.startswith('0x') or
            start <= int(code[-1].op_str, 16) < start+65):
        raise ValueError('unreviewed original unfiltered-report callback')
    activity = selected['activity_reset']
    if (not isinstance(activity, dict) or set(activity) != {'body', 'timer', 'dpc', 'set_timer_thunk'} or
            any(type(activity[k]) is not int or activity[k] <= 0 or activity[k] % 4 or
                activity[k] > 0xffffffff for k in ('timer', 'dpc', 'set_timer_thunk'))):
        raise ValueError('unreviewed input activity tail')
    activity_raw = original(activity['body'], 27)
    tail = activity['body']['address']
    if int(code[-1].op_str, 16) != tail:
        raise ValueError('input activity tail does not match original jump')
    activity_code = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(activity_raw, tail))
    expected_activity = [('push', '-0x33'), ('pop', 'ecx'), ('push', hex(activity['dpc'])),
        ('push', 'ecx'), ('mov', 'eax, 0xb5659000'), ('push', 'eax'),
        ('push', hex(activity['timer'])), ('call', f'dword ptr [{hex(activity["set_timer_thunk"])}]'),
        ('ret', '')]
    if ([(i.mnemonic, i.op_str) for i in activity_code] != expected_activity or
            activity_code[-1].address + activity_code[-1].size != tail+27):
        raise ValueError('input tail is not the reviewed activity-timer reset')
    # Bind the sole call to the actual retail import table's KeSetTimer (149),
    # not merely a DWORD with the right ordinal somewhere in the image.
    if context.image.get('encoding') != 'retail':
        raise ValueError('unreviewed input activity import encoding')
    thunk = struct.unpack_from('<I', context.blob, 0x158)[0] ^ 0x5b6d40b6
    found = False
    for index in range(4096):
        address = thunk + index*4
        ordinal = struct.unpack('<I', read(address, 4))[0]
        if not ordinal:
            break
        if address == activity['set_timer_thunk']:
            if ordinal != 0x80000095:
                raise ValueError('input activity import is not KeSetTimer')
            found = True
    else:
        raise ValueError('unterminated input activity import table')
    if not found:
        raise ValueError('input activity target is outside the import table')
    for key, size in (('timer', 40), ('dpc', 28)):
        address = activity[key]
        if not context.image['base'] <= address < address+size <= context.image['base']+context.image['image_size']:
            raise ValueError('input activity object outside original image')
    if not (activity['timer']+40 <= activity['dpc'] or activity['dpc']+28 <= activity['timer']):
        raise ValueError('overlapping original input activity objects')
    return dict(copy.deepcopy(selected), analog_button_floor=0)
