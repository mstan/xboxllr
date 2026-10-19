#!/usr/bin/env python3
"""Keep the original CRT's unwind work; bridge only its final native transfer.

Revision-specific code stays in private generated output. Unknown CRT bodies
and unqualified donor intercepts remain explicit unsupported operations.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
from native_binding import Binding

HEADER = '''#pragma once
#include <stdint.h>
#include <setjmp.h>
extern jmp_buf *pilot_setjmp_slot(uint32_t,uint32_t,uint32_t);
extern void pilot_jump_frame_leave(uint32_t);
extern void pilot_longjmp_begin(uint32_t);
'''


def bind(xbe: Path, output: Path, symbols: Path, profile=None):
    context=Binding(xbe,output,symbols)
    proof=context.require_profile('jump',profile,adapter='crt-native-transfer-v1')
    if set(proof['bodies'])!={'setjmp','longjmp'} or set(proof['lowering_counts'])!={'setjmp','longjmp','tail'}:
        raise ValueError('incomplete CRT transfer proof')
    if any(type(n) is not int or n<1 for n in proof['lowering_counts'].values()) or proof['lowering_counts']['tail']!=1:
        raise ValueError('invalid CRT lowering counts')
    selected={}
    for kind,body in proof['bodies'].items():
        matches=[f for f in context.functions if context.matches_body(f,body)]
        if len(matches)!=1: raise ValueError(f'expected one reviewed {kind} body')
        selected[kind]=matches[0]
    binding=dict(xbe_sha256=context.image['sha256'],functions=selected,version=1)
    record=output/'jump-binding.aot-private.json'
    header=context.generated/'pilot_jump_bindings.h'
    if header.exists():
        if json.loads(record.read_text())!=binding or header.read_text()!=HEADER:
            raise ValueError('existing jump binding differs')
        return binding
    edits={}; counts=dict(setjmp=0,longjmp=0,tail=0)
    function=re.compile(r'(void (\w+)\(void\)\n\{\n)(.*?)(\n\})',re.S)
    setjmp=re.compile(r'\{ int _sjv = setjmp\(\*recomp_setjmp_slot\(MEM32\(esp\)\)\); (.*?)else \{ PUSH32\(esp, (0x[0-9A-F]+u)\); ('+selected['setjmp']['name']+r')\(\); \} \}',re.S)
    longjmp=re.compile(r'if \(!recomp_guest_longjmp\(MEM32\(esp\), MEM32\(esp \+ 4\)\)\) \{ (PUSH32\(esp, 0x[0-9A-F]+u\); '+selected['longjmp']['name']+r'\(\);) \}')

    def rewrite(match):
        prefix,name,body,end=match.groups()
        body,n=longjmp.subn(r'\1',body); counts['longjmp']+=n
        if name==selected['longjmp']['name']:
            body='    pilot_longjmp_begin(MEM32(esp+4));\n'+body
            # The donor does not publish EBP before calls in this frameless CRT
            # routine. Its helpers use the restored frame, including _NLG_Notify.
            body=re.sub(r'(^    .*PUSH32\(esp, 0x[0-9A-F]+u\);)',
                        r'    g_ebp=ebp; g_seh_ebp=ebp;\n\1',body,flags=re.M)
            tail='g_seh_ebp = ebp; RECOMP_ITAIL(MEM32(edx + 0x14)); return; /* indirect tail jmp */'
            if body.count(tail)!=1: raise ValueError('longjmp transfer shape changed')
            body=body.replace(tail,'g_seh_ebp = ebp; recomp_guest_longjmp(edx, eax); return; /* native stack transfer after guest CRT */')
            counts['tail']+=1
        def arm(s):
            return ('{ int _sjv = setjmp(*pilot_setjmp_slot(MEM32(esp), '+s[2]+', _pilot_jump_frame)); '+s[1]+
                    'else { PUSH32(esp, '+s[2]+'); '+s[3]+'(); } }')
        body,n=setjmp.subn(arm,body); counts['setjmp']+=n
        if n:
            body='    const uint32_t _pilot_jump_frame=g_esp;\n'+body
            body=body.replace('return;', 'do { pilot_jump_frame_leave(_pilot_jump_frame); return; } while (0);')
        return prefix+body+end

    for path in context.generated.glob('recomp_*.c'):
        original=path.read_text(); updated=function.sub(rewrite,original)
        if original!=updated: edits[path]='#include "pilot_jump_bindings.h"\n'+updated
    if counts!=proof['lowering_counts']: raise ValueError(f'unreviewed jump lowering: {counts}')
    for path,code in edits.items(): path.write_text(code,newline='\n')
    header.write_text(HEADER,newline='\n')
    record.write_text(json.dumps(binding,indent=2)+'\n',newline='\n')
    context.manifest['jump_binding']=binding
    context.manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',newline='\n')
    return binding


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True)
    parser.add_argument('--profile',type=Path)
    args=parser.parse_args(); print(json.dumps(bind(args.xbe,args.output,args.symbols,args.profile)))
