#!/usr/bin/env python3
"""Image-bound whole DirectSound API selection; unknown native services stop."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from native_binding import Binding, validate_body

# Semantic enum names used by the shared runtime. Absent title APIs get unique
# out-of-range values; they do not acquire a wrapper or a callable table row.
SEMANTIC_ENTRIES = (
    'DirectSoundCreate', 'IDirectSound_CreateSoundBuffer', 'IDirectSound_DownloadEffectsImage',
    'IDirectSound_SetMixBinHeadroom',
    'IDirectSound_Release', 'ObjectAddRef', 'IDirectSoundBuffer_Release',
    'IDirectSoundBuffer_SetBufferData', 'IDirectSoundBuffer_SetVolume',
    'IDirectSoundBuffer_SetHeadroom', 'IDirectSoundBuffer_SetPitch',
    'IDirectSoundBuffer_SetFrequency', 'IDirectSoundBuffer_SetMixBins',
    'IDirectSoundBuffer_SetMixBinVolumes_8', 'IDirectSoundBuffer_SetLoopRegion',
    'IDirectSoundBuffer_Play', 'IDirectSoundBuffer_Stop', 'IDirectSoundBuffer_GetStatus',
    'IDirectSoundBuffer_GetCurrentPosition', 'IDirectSoundBuffer_SetCurrentPosition',
    'IDirectSoundBuffer_Lock', 'IDirectSoundBuffer_Unlock',
)

def bind(xbe,output,symbols,profile=None):
    context=Binding(xbe,output,symbols)
    proof=context.require_profile('audio',profile,adapter='dsound-objects-v1')
    layout=proof['layout']
    if set(layout)!={'device_interface_offset','buffer_interface_offset','buffer_descriptor_bytes'}:
        raise ValueError('incomplete audio object layout')
    if (any(type(v) is not int for v in layout.values()) or
            (layout['device_interface_offset'], layout['buffer_interface_offset'], layout['buffer_descriptor_bytes']) != (8,28,24)):
        raise ValueError('unsupported audio object layout')
    if set(proof['constructors'])!={'DEVICE','BUFFER'} or set(proof['dsp_helpers']) not in (set(),{'step','seed','decode','location'}):
        raise ValueError('incomplete audio body proofs')
    if not proof['dsp_helpers'] and 'IDirectSound_DownloadEffectsImage' in proof['calls']:
        raise ValueError('effects API requires complete DSP helper proof')
    for body in proof['calls'].values():validate_body(body)
    functions={int(f['start'],16):f for f in context.functions}
    def data(address,size):
        s=next(s for s in context.image['sections'] if s['address']<=address and address+size<=s['address']+s['raw_size'])
        offset=s['raw_offset']+address-s['address'];return context.blob[offset:offset+size]
    selected=[s for s in context.symbols if s['library']=='DSOUND' and s.get('kind')=='function'
              and (s['name']=='DirectSoundCreate' or s['name'].startswith('IDirectSound'))]
    matches=[f for f in context.functions if context.matches_body(f,proof['addref'])]
    if len(matches)!=1:raise ValueError('missing reviewed audio object AddRef')
    selected.append(dict(name='ObjectAddRef',address=int(matches[0]['start'],16),call='stdcall',stack_bytes=4))
    if len({s['name'] for s in selected})!=len(selected) or {s['name'] for s in selected}!=set(proof['calls']):
        raise ValueError('audio symbol family differs from reviewed profile')
    calls=[]
    for s in selected:
        f=functions[s['address']]
        if s['call']!='stdcall' or not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*',s['name']):
            raise ValueError('unqualified audio symbol ABI')
        body=proof['calls'][s['name']]
        if body.get('stack_bytes')!=s['stack_bytes'] or not context.matches_body(f,body):
            raise ValueError('unreviewed audio body: '+s['name'])
        code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(s['address'],f['size']),s['address']))
        returns={int(i.op_str or '0',0) for i in code if i.mnemonic=='ret'}
        if returns!={s['stack_bytes']}:raise ValueError('audio return disagrees with symbol: '+s['name'])
        calls.append(dict(name=s['name'],symbol=f['name'],address=s['address'],
                          stack_bytes=s['stack_bytes'],sha256=context.digest(f)))
    tables={}
    for kind,body in proof['constructors'].items():
        digest=body['sha256']
        matches=[f for f in context.functions if context.matches_body(f,body)]
        if len(matches)!=1:raise ValueError('missing reviewed audio constructor')
        f=matches[0];start=int(f['start'],16)
        code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(start,f['size']),start))
        addresses=[int(m[1],16) for i in code if i.mnemonic=='mov' and
                   (m:=re.fullmatch(r'dword ptr \[(?:eax|esi)\], (0x[0-9a-f]+)',i.op_str))]
        if len(addresses)!=1:raise ValueError('ambiguous audio object vtable')
        tables[kind]=dict(address=addresses[0],entries=list(struct.unpack('<III',data(addresses[0],12))),constructor_sha256=digest)
    dsp={}
    for kind,body in proof['dsp_helpers'].items():
        size,digest=body['bytes'],body['sha256']
        matches=[f for f in context.functions if context.matches_body(f,body)]
        if len(matches)!=1:raise ValueError('missing reviewed DSP helper: '+kind)
        f=matches[0];dsp[kind]=dict(sha256=digest)
        if kind=='seed':
            code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(int(f['start'],16),size),int(f['start'],16)))
            pushes=[int(i.op_str,0) for i in code if i.mnemonic=='push' and re.fullmatch('0x[0-9a-f]+|[0-9]+',i.op_str)]
            if len(pushes)!=8 or pushes[:2]!=proof['seed_push_prefix'] or pushes[4:]!=proof['seed_push_suffix']:raise ValueError('unreviewed DSP seed ABI')
            dsp['seed']['value']=(pushes[2]<<32)|pushes[3]
    binding=dict(xbe_sha256=context.image['sha256'],calls=calls,tables=tables,**layout,dsp_helpers=dsp)
    header=['#pragma once','#include <stdint.h>','enum PilotAudioEntry {']+[f'    PILOT_AUDIO_{c["name"]},' for c in calls]
    header+=['    PILOT_AUDIO_COUNT','};','static const struct { const char *name; uint32_t address, stack_bytes, bytes; const char *sha256; } pilot_audio_calls[] = {']
    header+=[f'    {{"{c["name"]}", {c["address"]}u, {c["stack_bytes"]}u, {functions[c["address"]]["size"]}u, "{c["sha256"]}"}},' for c in calls]+['};']
    absent=[name for name in SEMANTIC_ENTRIES if name not in proof['calls']]
    if absent:
        header+=['enum PilotAudioAbsentEntry {']+[f'    PILOT_AUDIO_{name} = PILOT_AUDIO_COUNT + {i+1},' for i,name in enumerate(absent)]+['};']
    # These vtable entries destroy the original, larger object layout. A whole
    # public Release owns native teardown; an original CPU must stop before
    # running an inner Release or ECX scalar destructor on a native object.
    header+=['static const struct { const char *name; uint32_t address, bytes; const char *sha256; } pilot_audio_guards[] = {']
    for kind,table in tables.items():
        for slot,name in ((0,'Destructor'),(2,'InnerRelease')):
            address=table['entries'][slot];function=functions[address]
            code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(data(address,function['size']),address))
            if {int(i.op_str or '0',0) for i in code if i.mnemonic=='ret'}!={4}:
                raise ValueError('unreviewed audio object guard cleanup')
            header.append(f'    {{"{kind}_{name}", {address}u, {function["size"]}u, "{context.digest(function)}"}},')
    header+=['};']
    header+=[f'#define PILOT_AUDIO_{kind}_VTABLE 0x{v["address"]:08x}u' for kind,v in tables.items()]
    header+=[f'#define PILOT_AUDIO_HAS_DSP {int(bool(dsp))}']
    header+=[f'#define PILOT_AUDIO_HAS_MIXBIN_HEADROOM {int("IDirectSound_SetMixBinHeadroom" in proof["calls"])}']
    if dsp:
        header+=[f'#define PILOT_AUDIO_DSP_SEED UINT64_C(0x{dsp["seed"]["value"]:016x})']
    header+=[f'#define PILOT_AUDIO_{key.upper()} {value}u' for key,value in layout.items()]
    wrapper=['#include "pilot_audio_bindings.h"','extern void pilot_audio_dispatch(unsigned);']
    for c in calls:
        wrapper += [f'extern void pilot_original_audio_{c["name"]}(void);',f'void {c["symbol"]}(void) {{',
                    '#ifdef PILOT_AUDIO_NATIVE',f'    pilot_audio_dispatch(PILOT_AUDIO_{c["name"]});',
                    '#else',f'    pilot_original_audio_{c["name"]}();','#endif','}']
    texts={'pilot_audio_bindings.h':'\n'.join(header)+'\n','pilot_audio_binding.c':'\n'.join(wrapper)+'\n'}
    record=output/'audio-binding.aot-private.json'
    if (context.generated/'pilot_audio_binding.c').exists():
        old=json.loads(record.read_text())
        if any(k not in binding or (binding[k]!=v if k!='dsp_helpers' else any(binding[k].get(n)!=d for n,d in v.items())) for k,v in old.items()) or (context.generated/'pilot_audio_binding.c').read_text()!=texts['pilot_audio_binding.c']:
            raise ValueError('existing audio binding differs')
        if old!=binding or (context.generated/'pilot_audio_bindings.h').read_text()!=texts['pilot_audio_bindings.h']:
            (context.generated/'pilot_audio_bindings.h').write_text(texts['pilot_audio_bindings.h'],newline='\n')
            record.write_text(json.dumps(binding,indent=2)+'\n',newline='\n')
            context.manifest['audio_binding']=binding
            context.manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(context.generated.iterdir()) if p.is_file()}
            context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',newline='\n')
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
    parser.add_argument('--profile',type=Path)
    a=parser.parse_args();result=bind(a.xbe,a.output,a.symbols,a.profile)
    print(json.dumps(dict(xbe_sha256=result['xbe_sha256'],bound=len(result['calls']))))
