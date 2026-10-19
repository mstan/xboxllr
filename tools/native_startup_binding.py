#!/usr/bin/env python3
"""Bind reviewed XAPI startup effects to an explicit native kernel owner."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86_const import X86_OP_MEM, X86_OP_REG, X86_REG_EAX, X86_REG_ECX, X86_REG_EDX
from native_binding import Binding

CONTRACT = dict(kernel_build=5838,hardware_flags=0,export_only=True,io_device='OFF',usb='OFF',scheduling='NATIVE')
ROLES = {'apply':0,'setup':0,'exchange':8,'teardown_consumer':4,'startup_caller':None,'next_initializer':None}


def body_bytes(context,function):
    start=int(function['start'],16)
    section=next(s for s in context.image['sections'] if s['address']<=start and start+function['size']<=s['address']+s['raw_size'])
    offset=section['raw_offset']+start-section['address']
    return context.blob[offset:offset+function['size']]


def image_word(context,address):
    for section in context.image['sections']:
        if section['address']<=address and address+4<=section['address']+section['raw_size']:
            offset=section['raw_offset']+address-section['address']
            return struct.unpack_from('<I',context.blob,offset)[0]
    raise ValueError('startup import is outside owned image data')


def register_memory_move(instruction,address,register,store):
    if instruction.mnemonic!='mov' or len(instruction.operands)!=2:return False
    memory,reg=instruction.operands if store else reversed(instruction.operands)
    return (memory.type==X86_OP_MEM and memory.size==4 and not memory.mem.base and
            not memory.mem.index and not memory.mem.segment and memory.mem.disp==address and
            reg.type==X86_OP_REG and reg.reg==register)


def verify_kernel_observation(blob,proof):
    """Offline source verification; returns facts, never secret bytes."""
    if hashlib.sha256(blob).hexdigest()!=proof['sha256']:
        raise ValueError('kernel observation source hash differs')
    def u32(offset):return struct.unpack_from('<I',blob,offset)[0]
    base=proof['kernel_base']&0x7fffffff
    pe=base+u32(base+0x3c)
    if blob[base:base+2]!=b'MZ' or blob[pe:pe+4]!=b'PE\0\0':raise ValueError('invalid observed kernel PE')
    count=struct.unpack_from('<H',blob,pe+6)[0];optional=struct.unpack_from('<H',blob,pe+20)[0]
    last=pe+24+optional+(count-1)*40
    if not count or blob[last:last+8].rstrip(b'\0')!=b'INIT':raise ValueError('observed kernel lacks final INIT section')
    init=proof['kernel_base']+u32(last+12)
    exports=base+u32(pe+24+96)
    ordinal_base=u32(exports+16);functions=base+u32(exports+28)
    version=base+u32(functions+(324-ordinal_base)*4)
    build=struct.unpack_from('<H',blob,version+4)[0]
    descriptor=struct.unpack_from('<Q',blob,(proof['gdt_base']&0x7fffffff)+8)[0]
    pcr=proof['pcr_base']&0x7fffffff;prcb=u32(pcr+0x20)
    if (init!=proof['init_start'] or build!=proof['kernel_build'] or descriptor!=int(proof['initial_descriptor'],16) or
        prcb!=proof['prcb_base'] or u32((prcb&0x7fffffff)+0x250)!=proof['prcb_offset_250']):
        raise ValueError('observed kernel metadata differs')
    return dict(kernel_build=build,init_start=init,initial_descriptor=f'0x{descriptor:016x}',source_sha256=proof['sha256'])


def verify_kernel_metadata(blob,proof):
    if hashlib.sha256(blob).hexdigest()!=proof['sha256']:
        raise ValueError('flat kernel metadata source hash differs')
    u32=lambda offset:struct.unpack_from('<I',blob,offset)[0]
    pe=u32(60);count=struct.unpack_from('<H',blob,pe+6)[0];optional=struct.unpack_from('<H',blob,pe+20)[0]
    if blob[:2]!=b'MZ' or blob[pe:pe+4]!=b'PE\0\0' or not count:raise ValueError('invalid flat kernel PE')
    last=pe+24+optional+(count-1)*40
    exports=u32(pe+120);version=u32(u32(exports+28)+(324-u32(exports+16))*4)
    facts=dict(kernel_base=u32(pe+24+28),kernel_build=struct.unpack_from('<H',blob,version+4)[0],
               last_section=blob[last:last+8].rstrip(b'\0').decode('ascii'),init_start=u32(pe+24+28)+u32(last+12))
    if any(proof.get(key)!=value for key,value in facts.items()):raise ValueError('flat kernel metadata differs')
    return dict(**facts,source_sha256=proof['sha256'])


def validate(context,proof):
    if proof.get('native_contract')!=CONTRACT or set(proof.get('functions',{}))!=set(ROLES):
        raise ValueError('unreviewed native startup contract')
    observation=proof['kernel_observation']
    metadata=proof['kernel_metadata']
    if (observation.get('kind')!='owned-physical-ram' or observation.get('kernel_build')!=4983 or
        observation.get('code_selector')!=8 or observation.get('prcb_offset_250')!=0 or
        observation.get('last_section')!='INIT' or not re.fullmatch('[0-9a-f]{64}',observation.get('sha256',''))):
        raise ValueError('unreviewed kernel startup observation')
    if (metadata.get('kind')!='owned-flat-kernel' or metadata.get('kernel_build')!=4983 or
        metadata.get('kernel_base')!=0x80010000 or metadata.get('last_section')!='INIT' or
        metadata.get('init_start')!=observation['init_start'] or
        not re.fullmatch('[0-9a-f]{64}',metadata.get('sha256',''))):
        raise ValueError('unreviewed kernel INIT metadata')
    descriptor=int(observation['initial_descriptor'],16)
    if descriptor&~0x000f00000000ffff != 0x00c09b0000000000 or not 0x80010000<=observation['init_start']<0xc0000000:
        raise ValueError('unsupported code descriptor observation')
    rows={};instructions={}
    for role,cleanup in ROLES.items():
        item=proof['functions'][role]
        candidates=[f for f in context.functions if context.matches_body(f,item)]
        if len(candidates)!=1:raise ValueError('unreviewed startup body: '+role)
        function=candidates[0];raw=body_bytes(context,function)
        ending=b'\xc3' if cleanup==0 else b'\xc2'+struct.pack('<H',cleanup) if cleanup is not None else None
        if cleanup is not None and (item.get('stack_bytes')!=cleanup or not raw.endswith(ending)):
            raise ValueError('startup RET cleanup differs: '+role)
        decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
        decoded=list(decoder.disasm(raw,int(function['start'],16)))
        if sum(i.size for i in decoded)!=len(raw):raise ValueError('incomplete startup instruction proof')
        rows[role]=function;instructions[role]=decoded
    layout=proof['layout']
    expected={'saved_low','saved_high','hardware_import','version_import','apply_return','setup_exchange_return','restore_exchange_return'}
    if set(layout)!=expected or any(type(v) is not int or not 0<v<=0xfffffffc or v%4 for k,v in layout.items() if not k.endswith('_return')):
        raise ValueError('invalid startup layout')
    if len({layout[key] for key in ('saved_low','saved_high','hardware_import','version_import')})!=4 or layout['saved_high']!=layout['saved_low']+4:
        raise ValueError('aliased startup layout roles')
    address=lambda role:int(rows[role]['start'],16)
    calls=lambda role,target:[i for i in instructions[role] if i.mnemonic=='call' and i.op_str==hex(target)]
    setup_calls=calls('setup',address('exchange'));restore_calls=calls('teardown_consumer',address('exchange'))
    if len(setup_calls)!=1 or len(restore_calls)!=1 or setup_calls[0].address+setup_calls[0].size!=layout['setup_exchange_return'] or restore_calls[0].address+restore_calls[0].size!=layout['restore_exchange_return']:
        raise ValueError('unreviewed selector exchange consumer')
    main=instructions['startup_caller']
    if len(main)<2 or main[0].mnemonic!='call' or main[0].op_str!=hex(address('apply')) or main[1].mnemonic!='call' or main[1].op_str!=hex(address('next_initializer')) or main[1].address!=layout['apply_return']:
        raise ValueError('unreviewed startup caller')
    init=instructions['next_initializer']
    if [(i.mnemonic,i.op_str) for i in init[:3]]!=[('push','ebp'),('mov','ebp, esp'),('sub','esp, 0x34')]:
        raise ValueError('startup caller flags are not proved dead')
    if not any(i.mnemonic=='jmp' and i.op_str==hex(address('setup')) for i in instructions['apply']):
        raise ValueError('missing original selector setup tail')
    for key,register in (('saved_low',X86_REG_EAX),('saved_high',X86_REG_EDX)):
        if not any(register_memory_move(i,layout[key],register,True) for i in instructions['setup']):
            raise ValueError('saved descriptor operand role differs')
    for key,register,ordinal in (('hardware_import',X86_REG_EAX,322),('version_import',X86_REG_ECX,324)):
        if not any(register_memory_move(i,layout[key],register,False) for i in instructions['apply']):
            raise ValueError('kernel identity operand role differs')
        if image_word(context,layout[key])!=0x80000000|ordinal:
            raise ValueError('kernel identity import ordinal differs')
    return rows


def bind(xbe,output,symbols,profile=None):
    context=Binding(Path(xbe),Path(output),Path(symbols))
    proof=context.require_profile('startup',profile,adapter='native-xapi-kernel-startup-v1')
    rows=validate(context,proof)
    observation=proof['kernel_observation'];layout=proof['layout']
    binding=dict(xbe_sha256=context.image['sha256'],native_contract=CONTRACT,kernel_observation=observation,kernel_metadata=proof['kernel_metadata'],
        functions={role:dict(address=int(f['start'],16),bytes=f['size'],sha256=context.digest(f),stack_bytes=ROLES[role]) for role,f in rows.items()},layout=layout,
        flags='exchange preserves arithmetic flags; setup/apply caller overwrites flags before use',
        boundary='native code descriptor ownership; no host GDT or original kernel execution')
    header=['#pragma once','/* Reviewed owned kernel observation; native execution-domain policy only. */',
        f'#define PILOT_STARTUP_INITIAL_DESCRIPTOR {observation["initial_descriptor"]}ull',
        f'#define PILOT_STARTUP_INIT_START 0x{proof["kernel_metadata"]["init_start"]:08x}u',
        '#define PILOT_STARTUP_KERNEL_BUILD 5838u','#define PILOT_STARTUP_HARDWARE_FLAGS 0u']
    header += [f'#define PILOT_STARTUP_{key.upper()} 0x{value:08x}u' for key,value in layout.items()]
    wrapper=['#include "recomp_types.h"']
    for role in ('apply','setup','exchange'):
        name=rows[role]['name'];original='pilot_startup_original_'+name
        wrapper += [f'extern void pilot_startup_{role}(void);',f'extern void {original}(void);',
            f'void {name}(void) {{','#if defined(PILOT_STARTUP_NATIVE) && PILOT_STARTUP_NATIVE',
            f'    pilot_startup_{role}();','#else',f'    {original}();','#endif','}']
    additions={'pilot_startup_binding.c':('\n'.join(wrapper)+'\n').encode(),
               'pilot_startup_bindings.h':('\n'.join(header)+'\n').encode()}
    record=Path(output)/'startup-binding.aot-private.json'
    if record.exists():
        if json.loads(record.read_text())!=binding or any((context.generated/name).read_bytes()!=data for name,data in additions.items()):
            raise ValueError('existing startup binding differs')
        return binding
    changes={}
    for role in ('apply','setup','exchange'):
        name=rows[role]['name']
        if not re.fullmatch(r'sub_[0-9A-Fa-f]{8}',name):raise ValueError('invalid startup function identifier')
        needle=re.compile(rb'void '+name.encode()+rb'\(void\)(\r?\n)\{')
        matches=[]
        for path in context.generated.glob('recomp_*.c'):
            content=changes.get(path,path.read_bytes())
            matches.extend((path,content,match) for match in needle.finditer(content))
        if len(matches)!=1:raise ValueError('expected one generated startup original: '+role)
        path,content,match=matches[0]
        replacement=b'void pilot_startup_original_'+name.encode()+b'(void)'+match[1]+b'{'
        changes[path]=content[:match.start()]+replacement+content[match.end():]
    # All body/ABI/layout and generated-declaration preflight precedes mutation.
    for path,data in changes.items():path.write_bytes(data)
    for name,data in additions.items():(context.generated/name).write_bytes(data)
    record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8')
    context.manifest['startup_binding']=binding
    context.manifest['files']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8')
    return binding


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','output','symbols'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--profile',type=Path)
    args=parser.parse_args();bind(args.xbe,args.output,args.symbols,args.profile)
    print('Verified native startup boundary; original privileged bodies retained.')
