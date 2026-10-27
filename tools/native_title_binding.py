"""Bind source-hashed static title function hooks; retain every original body.

Profiles contain IDs and proofs, never source code, paths to plugins or names
of arbitrary host symbols. The trusted title registry owns hook semantics.
"""
import hashlib
import json
import struct
from native_binding import Binding


def bind(xbe, output, symbols, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('title', profile, adapter='static-title-functions-v1')
    entries = proof.get('functions')
    if not isinstance(entries, list) or not 1 <= len(entries) <= 64:
        raise ValueError('invalid static title function list')
    verified = []
    for row in entries:
        if (not isinstance(row, dict) or set(row)-{'callers'} != {'id', 'address', 'bytes', 'sha256', 'stack_bytes'} or
                type(row['id']) is not int or not 1 <= row['id'] <= 255 or
                type(row['address']) is not int or type(row['stack_bytes']) is not int or
                not 0 <= row['stack_bytes'] <= 64 or row['stack_bytes'] % 4):
            raise ValueError('invalid static title function contract')
        found = [f for f in context.functions if int(f['start'], 16) == row['address']]
        if len(found) != 1 or not context.matches_body(found[0], row):
            raise ValueError('unreviewed static title function body')
        if 'callers' in row:
            callers=row['callers']
            if (not isinstance(callers,list) or not 1<=len(callers)<=16 or
                any(type(c) is not int or not 5<=c<=0xffffffff for c in callers) or len(set(callers))!=len(callers)):
                raise ValueError('invalid static title caller list')
            for caller in callers:
                call=None
                for section in context.image['sections']:
                    if section['address']<=caller-5 and caller<=section['address']+section['raw_size']:
                        at=section['raw_offset']+caller-5-section['address'];call=context.blob[at:at+5];break
                if (call is None or len(call)!=5 or call[0]!=0xe8 or
                    (caller+struct.unpack_from('<i',call,1)[0])&0xffffffff!=row['address']):
                    raise ValueError('unreviewed static title direct caller')
        verified.append((row, found[0]))
    if len({r['id'] for r, _ in verified}) != len(entries) or len({r['address'] for r, _ in verified}) != len(entries):
        raise ValueError('duplicate static title function')
    for row, function in verified:
        original = f"pilot_title_original_{row['id']}"
        guard=''
        if 'callers' in row:
            allowed=' && '.join(f'MEM32(g_esp)!=0x{c:08x}u' for c in row['callers'])
            guard=f'if({allowed}){{{original}();return;}}'
        code = ('#include "recomp_types.h"\n'
                'extern void pilot_title_function(unsigned,recomp_func_t,unsigned);\n'
                f'extern void {original}(void);\n'
                f'void {function["name"]}(void) {{{guard}pilot_title_function({row["id"]},{original},{row["stack_bytes"]});}}\n')
        context.emit(f'title_hook_{row["id"]}', function, original, row, code)
    registry = '#include "recomp_types.h"\n'
    for row, _ in verified:
        registry += f'extern void pilot_title_original_{row["id"]}(void);\n'
    registry += 'recomp_func_t pilot_title_original_lookup(unsigned id,unsigned *cleanup) {switch(id) {\n'
    for row, _ in verified:
        registry += f'case {row["id"]}:*cleanup={row["stack_bytes"]};return pilot_title_original_{row["id"]};\n'
    registry += 'default:return 0;}}\n'
    path = context.generated / 'pilot_title_binding.c'
    if not path.exists() or path.read_text() != registry:
        path.write_text(registry, encoding='utf-8', newline='\n')
    context.manifest['title_binding'] = dict(adapter=proof['adapter'], functions=entries)
    context.manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest, indent=2)+'\n', newline='\n')
    return context.manifest['title_binding']
