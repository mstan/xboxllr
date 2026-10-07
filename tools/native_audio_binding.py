#!/usr/bin/env python3
"""Image-bound whole DirectSound API selection; unknown native services stop."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from native_binding import Binding

IMAGE='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
CONSTRUCTORS={
    'DEVICE':'c5d1fa21da86b5c90669fb8216650c2c081b61518125b7302a89812ac9f3a98e',
    'BUFFER':'745bf616d305cc7d2d37072af4cf998c7268047dda66a0f6571a7b3deb968d7d',
}
ADDREF='e683ffe809612832ed3454d2c9b13c8f2172e58e9c2a076c640b08c7caa8ff70'

def bind(xbe,output,symbols):
    context=Binding(xbe,output,symbols)
    if context.image['sha256']!=IMAGE:raise ValueError('unreviewed audio image')
    functions={int(f['start'],16):f for f in context.functions}
    def data(address,size):
        s=next(s for s in context.image['sections'] if s['address']<=address and address+size<=s['address']+s['raw_size'])
        offset=s['raw_offset']+address-s['address'];return context.blob[offset:offset+size]
    selected=[s for s in context.symbols if s['library']=='DSOUND' and s.get('kind')=='function'
              and (s['name']=='DirectSoundCreate' or s['name'].startswith('IDirectSound'))]
    matches=[f for f in context.functions if f['size']==71 and context.digest(f)==ADDREF]
    if len(matches)!=1:raise ValueError('missing reviewed audio object AddRef')
    selected.append(dict(name='ObjectAddRef',address=int(matches[0]['start'],16),call='stdcall',stack_bytes=4))
    calls=[]
    for s in selected:
        f=functions[s['address']]
        if s['call']!='stdcall' or not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*',s['name']):
            raise ValueError('unqualified audio symbol ABI')
        code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(s['address'],f['size']),s['address']))
        returns={int(i.op_str or '0',0) for i in code if i.mnemonic=='ret'}
        if returns!={s['stack_bytes']}:raise ValueError('audio return disagrees with symbol: '+s['name'])
        calls.append(dict(name=s['name'],symbol=f['name'],address=s['address'],
                          stack_bytes=s['stack_bytes'],sha256=context.digest(f)))
    tables={}
    for kind,digest in CONSTRUCTORS.items():
        matches=[f for f in context.functions if f['size'] in (24,29) and context.digest(f)==digest]
        if len(matches)!=1:raise ValueError('missing reviewed audio constructor')
        f=matches[0];start=int(f['start'],16)
        code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(start,f['size']),start))
        addresses=[int(m[1],16) for i in code if i.mnemonic=='mov' and
                   (m:=re.fullmatch(r'dword ptr \[(?:eax|esi)\], (0x[0-9a-f]+)',i.op_str))]
        if len(addresses)!=1:raise ValueError('ambiguous audio object vtable')
        tables[kind]=dict(address=addresses[0],entries=list(struct.unpack('<III',data(addresses[0],12))),constructor_sha256=digest)
    binding=dict(xbe_sha256=IMAGE,calls=calls,tables=tables,device_interface_offset=8,buffer_interface_offset=28,buffer_descriptor_bytes=24)
    header=['#pragma once','enum PilotAudioEntry {']+[f'    PILOT_AUDIO_{c["name"]},' for c in calls]
    header+=['    PILOT_AUDIO_COUNT','};','static const struct { const char *name; unsigned stack_bytes; } pilot_audio_calls[] = {']
    header+=[f'    {{"{c["name"]}",{c["stack_bytes"]}}},' for c in calls]+['};']
    header+=[f'#define PILOT_AUDIO_{kind}_VTABLE 0x{v["address"]:08x}u' for kind,v in tables.items()]
    wrapper=['#include "pilot_audio_bindings.h"','extern void pilot_audio_dispatch(unsigned);']
    for c in calls:
        wrapper += [f'extern void pilot_original_audio_{c["name"]}(void);',f'void {c["symbol"]}(void) {{',
                    '#ifdef PILOT_AUDIO_NATIVE',f'    pilot_audio_dispatch(PILOT_AUDIO_{c["name"]});',
                    '#else',f'    pilot_original_audio_{c["name"]}();','#endif','}']
    texts={'pilot_audio_bindings.h':'\n'.join(header)+'\n','pilot_audio_binding.c':'\n'.join(wrapper)+'\n'}
    record=output/'audio-binding.aot-private.json'
    if (context.generated/'pilot_audio_binding.c').exists():
        if json.loads(record.read_text())!=binding or any((context.generated/n).read_text()!=s for n,s in texts.items()):
            raise ValueError('existing audio binding differs')
        return binding
    replacements={c['symbol']:'pilot_original_audio_'+c['name'] for c in calls};found=set();edits={}
    def rename(m):
        if m[1] not in replacements:return m[0]
        if m[1] in found:raise ValueError('duplicate audio body')
        found.add(m[1]);return 'void '+replacements[m[1]]+'(void)\n{'
    for p in context.generated.glob('recomp_*.c'):
        original=p.read_text(encoding='utf-8');updated=re.sub(r'void (sub_[0-9A-Fa-f]{8})\(void\)\n\{',rename,original)
        if updated!=original:edits[p]=updated
    if found!=set(replacements):raise ValueError('missing audio bodies')
    for p,s in edits.items():p.write_text(s,encoding='utf-8',newline='\n')
    for n,s in texts.items():(context.generated/n).write_text(s,encoding='utf-8',newline='\n')
    record.write_text(json.dumps(binding,indent=2)+'\n',newline='\n')
    context.manifest['audio_binding']=binding
    context.manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',newline='\n')
    return binding

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','output','symbols'):parser.add_argument('--'+name,type=Path,required=True)
    a=parser.parse_args();result=bind(a.xbe,a.output,a.symbols)
    print(json.dumps(dict(xbe_sha256=result['xbe_sha256'],bound=len(result['calls']))))
