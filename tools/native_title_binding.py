"""Bind source-hashed static title function hooks; retain every original body.

Profiles contain IDs and proofs, never source code, paths to plugins or names
of arbitrary host symbols. The trusted title registry owns hook semantics.
"""
import hashlib
import json
from native_binding import Binding


def bind(xbe, output, symbols, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('title', profile, adapter='static-title-functions-v1')
    entries = proof.get('functions')
    if not isinstance(entries, list) or not 1 <= len(entries) <= 64:
        raise ValueError('invalid static title function list')
    verified = []
    for row in entries:
        if (not isinstance(row, dict) or set(row) != {'id', 'address', 'bytes', 'sha256', 'stack_bytes'} or
                type(row['id']) is not int or not 1 <= row['id'] <= 255 or
                type(row['address']) is not int or type(row['stack_bytes']) is not int or
                not 0 <= row['stack_bytes'] <= 64 or row['stack_bytes'] % 4):
            raise ValueError('invalid static title function contract')
        found = [f for f in context.functions if int(f['start'], 16) == row['address']]
        if len(found) != 1 or not context.matches_body(found[0], row):
            raise ValueError('unreviewed static title function body')
        verified.append((row, found[0]))
    if len({r['id'] for r, _ in verified}) != len(entries) or len({r['address'] for r, _ in verified}) != len(entries):
        raise ValueError('duplicate static title function')
    for row, function in verified:
        original = f"pilot_title_original_{row['id']}"
        code = ('#include "recomp_types.h"\n'
                'extern void pilot_title_function(unsigned,recomp_func_t,unsigned);\n'
                f'extern void {original}(void);\n'
                f'void {function["name"]}(void) {{pilot_title_function({row["id"]},{original},{row["stack_bytes"]});}}\n')
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
