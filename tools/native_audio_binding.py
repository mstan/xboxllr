#!/usr/bin/env python3
"""Image-bound whole DirectSound API selection; unknown native services stop."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP, CS_OP_IMM
from native_binding import Binding, validate_body
from native_audio_i3dl2 import bind_i3dl2
from native_audio_spatial import bind_spatial
from native_audio_spatial_math import bind_spatial_math, spatial_math_header

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
    'IDirectSoundBuffer_SetNotificationPositions',
    'IDirectSoundBuffer_Pause',
)

LISTENER_CALLS = {'SetAllParameters':12, 'SetDistanceFactor':12,
    'SetDopplerFactor':12, 'SetOrientation':32, 'SetPosition':20,
    'SetRolloffFactor':12, 'SetVelocity':20, 'CommitDeferredSettings':4}

def _audio_body_code(context, function, proof, data):
    """Decode one complete canonical extent from its original stored bytes."""
    validate_body(proof)
    try:
        start, end = int(function['start'], 16), int(function['end'], 16)
        size = function['size']
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError('invalid canonical audio extent') from error
    if (type(size) is not int or size <= 0 or start < 0 or end > 0x100000000
            or end-start != size):
        raise ValueError('invalid canonical audio extent')
    sections = [s for s in context.image['sections'] if s['address'] <= start
                and end <= s['address']+s['raw_size']]
    if len(sections) != 1 or not context.matches_body(function, proof):
        raise ValueError('unreviewed original audio body')
    raw = data(start, size)
    if len(raw) != size or hashlib.sha256(raw).hexdigest() != proof['sha256']:
        raise ValueError('changed original audio bytes')
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    code = list(decoder.disasm(raw, start))
    if not code or sum(i.size for i in code) != size or code[-1].address+code[-1].size != end:
        raise ValueError('incomplete original audio decode')
    return code


def public_audio_abi(context, symbol, proof, functions, data):
    """Inspect a public ABI; optional JMP proof does not admit its semantics.

    Unsupported public setters still use their original name and the existing
    native dispatch stop. No target body is executed or replaced here.
    """
    name, address, stack = symbol['name'], symbol['address'], symbol['stack_bytes']
    if symbol['call'] != 'stdcall' or not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*', name):
        raise ValueError('unqualified audio symbol ABI')
    validate_body(proof)
    if (type(stack) is not int or stack < 0 or stack > 256 or stack % 4
            or proof.get('stack_bytes') != stack):
        raise ValueError('unreviewed audio cleanup: '+name)
    function = functions.get(address)
    if function is None:
        raise ValueError('missing canonical audio body: '+name)
    code = _audio_body_code(context, function, proof, data)
    if code[0].address != address:
        raise ValueError('canonical audio entry disagrees: '+name)
    tail = None
    if 'tail_target' not in proof:
        returns = {int(i.op_str or '0', 0) for i in code if i.mnemonic == 'ret'}
        if returns != {stack}:
            raise ValueError('audio return disagrees with symbol: '+name)
    else:
        selected = proof['tail_target']
        if (not isinstance(selected, dict) or set(selected) != {'address', 'body'}
                or type(selected['address']) is not int or not 0 < selected['address'] <= 0xffffffff
                or not isinstance(selected['body'], dict)
                or set(selected['body']) != {'sha256', 'bytes', 'stack_bytes'}
                or type(proof.get('bytes')) is not int or proof['bytes'] != function['size']):
            raise ValueError('invalid audio tail-target proof: '+name)
        target, body = selected['address'], selected['body']
        validate_body(body)
        if type(body['bytes']) is not int or type(body['stack_bytes']) is not int or body['stack_bytes'] != stack:
            raise ValueError('audio tail-target cleanup disagrees: '+name)
        if (len(code) != 1 or code[0].mnemonic != 'jmp' or len(code[0].operands) != 1
                or code[0].operands[0].type != CS_OP_IMM or code[0].operands[0].imm != target):
            raise ValueError('audio tail-target requires one exact direct JMP: '+name)
        matches = [f for f in context.functions if int(f['start'], 16) == target]
        if len(matches) != 1 or functions.get(target) != matches[0]:
            raise ValueError('missing or ambiguous canonical audio tail target: '+name)
        target_function = matches[0]
        target_code = _audio_body_code(context, target_function, body, data)
        public_end, target_end = int(function['end'], 16), int(target_function['end'], 16)
        if not (target_end <= address or target >= public_end):
            raise ValueError('overlapping audio tail target: '+name)
        returns = {int(i.op_str or '0', 0) for i in target_code if i.mnemonic == 'ret'}
        if returns != {stack} or target_code[-1].mnemonic != 'ret':
            raise ValueError('audio tail-target return disagrees: '+name)
        starts = {i.address for i in target_code}
        for instruction in target_code:
            if instruction.group(CS_GRP_JUMP) and (len(instruction.operands) != 1
                    or instruction.operands[0].type != CS_OP_IMM
                    or instruction.operands[0].imm not in starts):
                raise ValueError('unproved audio tail-target branch: '+name)
        tail = dict(address=target, bytes=target_function['size'],
                    stack_bytes=stack, sha256=body['sha256'])
    call = dict(name=name, symbol=function['name'], address=address,
                stack_bytes=stack, sha256=proof['sha256'])
    if tail is not None:
        call['tail_target'] = tail
    return call


def listener_binding(context, proof, calls, functions, data):
    """Optional whole listener family; no implicit admission for older profiles."""
    listener=proof.get('listener')
    if listener is None:return None
    if (not isinstance(listener,dict) or set(listener)!={'adapter','helpers'} or
            listener['adapter']!='listener-parameters-2d-v1' or
            set(listener['helpers'])!=set(LISTENER_CALLS)):
        raise ValueError('incomplete native listener proof')
    admitted={c['name']:c for c in calls};helpers={}
    decoder=Cs(CS_ARCH_X86,CS_MODE_32)
    for name,stack in LISTENER_CALLS.items():
        public=admitted.get('IDirectSound_'+name);body=listener['helpers'][name]
        if not public or public['stack_bytes']!=stack or body.get('stack_bytes')!=stack:
            raise ValueError('listener ABI differs: '+name)
        validate_body(body)
        matches=[f for f in functions.values() if context.matches_body(f,body)]
        if len(matches)!=1:raise ValueError('missing reviewed listener helper: '+name)
        helper=matches[0];address=int(helper['start'],16)
        code=list(decoder.disasm(data(address,helper['size']),address))
        if {int(i.op_str or '0',0) for i in code if i.mnemonic=='ret'}!={stack}:
            raise ValueError('listener helper cleanup differs: '+name)
        wrapper=functions[public['address']]
        targets={int(i.op_str,0) for i in decoder.disasm(data(public['address'],wrapper['size']),public['address'])
                 if i.mnemonic=='call' and re.fullmatch('0x[0-9a-f]+',i.op_str)}
        if address not in targets:raise ValueError('listener wrapper does not call its helper: '+name)
        helpers[name]=dict(address=address,**body)
    return dict(adapter=listener['adapter'],helpers=helpers)

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
        calls.append(public_audio_abi(context, s, proof['calls'][s['name']], functions, data))
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
    listener=listener_binding(context,proof,calls,functions,data)
    i3dl2=bind_i3dl2(context,proof,functions,data)
    spatial=bind_spatial(context,proof,calls,functions,data)
    spatial_math=bind_spatial_math(context,proof,functions,data)
    binding=dict(xbe_sha256=context.image['sha256'],calls=calls,tables=tables,**layout,dsp_helpers=dsp)
    if listener:binding['listener']=listener
    if i3dl2:binding['i3dl2']=i3dl2
    if spatial:binding['spatial']=spatial
    if spatial_math:binding['spatial_math']=spatial_math
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
    header+=[f'#define PILOT_AUDIO_HAS_NOTIFICATIONS {int("IDirectSoundBuffer_SetNotificationPositions" in proof["calls"])}']
    header+=[f'#define PILOT_AUDIO_HAS_PAUSE {int("IDirectSoundBuffer_Pause" in proof["calls"])}']
    if listener:header+=['#define PILOT_AUDIO_LISTENER_VERSION 1u']
    if spatial:header+=['#define PILOT_AUDIO_SPATIAL_CACHE_VERSION 1u']
    if spatial_math:header+=spatial_math_header(spatial_math,data)
    if i3dl2:header+=['#define PILOT_AUDIO_I3DL2_VERSION 1u',
        f'#define PILOT_AUDIO_I3DL2_INITIALIZE 0x{i3dl2["initialize"]:x}u',
        f'#define PILOT_AUDIO_I3DL2_CALCULATE 0x{i3dl2["calculate"]:x}u']
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
        from native_audio_spatial_math import retains_spatial_math
        def retained(k,v):
            if k not in binding:return False
            if k=='dsp_helpers':return all(binding[k].get(n)==d for n,d in v.items())
            if k=='spatial_math':return retains_spatial_math(v,binding[k])
            return binding[k]==v
        if not all(retained(k,v) for k,v in old.items()) or (context.generated/'pilot_audio_binding.c').read_text()!=texts['pilot_audio_binding.c']:
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
