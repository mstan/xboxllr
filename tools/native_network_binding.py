#!/usr/bin/env python3
"""Bind a closed, exact-image offline network lifecycle and guarded consumers."""
import argparse
import bisect
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
from itertools import combinations
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
from native_binding import Binding, validate_body


def original_bytes(context, address, size):
    sections = [s for s in context.image['sections'] if s['address'] <= address
                and address + size <= s['address'] + s['raw_size']]
    if len(sections) != 1:
        # Source certificate extension words can reside in stored XBE headers.
        base=context.image['base'];header_bytes=struct.unpack_from('<I',context.blob,0x108)[0]
        if not sections and base<=address<address+size<=base+header_bytes:
            return context.blob[address-base:address-base+size]
        raise ValueError('network proof outside stored original image')
    section = sections[0]
    offset = section['raw_offset'] + address - section['address']
    return context.blob[offset:offset+size]


def decode(context, address, size, begin, end, strict=True):
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.skipdata = not strict
    instructions = list(decoder.disasm(original_bytes(context, address, size), address))
    if sum(i.size for i in instructions) != size:
        raise ValueError(f'incomplete network body decoding at {address:08x}')
    refs = []
    detail = Cs(CS_ARCH_X86, CS_MODE_32); detail.detail = True; detail.skipdata = not strict
    for instruction in detail.disasm(original_bytes(context, address, size), address):
        if not instruction.id:  # Non-admitted discovery ranges can contain data.
            continue
        for operand in instruction.operands:
            target = None
            if operand.type == X86_OP_MEM and not operand.mem.base and not operand.mem.index:
                target = operand.mem.disp & 0xffffffff
            elif operand.type == X86_OP_IMM:
                target = operand.imm & 0xffffffff
            if target is not None and begin <= target < end:
                refs.append([instruction.address, target])
    return sorted({int(i.op_str or '0', 0) for i in instructions if i.mnemonic == 'ret'}), refs


def cached_update_proof(context, proof, functions, calls, begin, end, online_owner):
    if (not isinstance(proof,dict) or set(proof)!={'entry','helpers','filename','directory','default_suffix',
            'type_table','certificate_offset','partition','relative','kind','output_bytes'}
            or type(proof['kind']) is not int or proof['kind']!=8
            or type(proof['output_bytes']) is not int or proof['output_bytes']!=156
            or type(proof['partition']) is not int or not 1<=proof['partition']<=5
            or type(proof['certificate_offset']) is not int or proof['certificate_offset']%4
            or not 0<=proof['certificate_offset']<=4096):
        raise ValueError('unreviewed cached-update policy')
    selected=[c for c in calls if c['action']=='cached_update']
    if len(selected)!=1 or selected[0]['address']!=proof['entry']:
        raise ValueError('unreviewed cached-update entry')
    helpers=proof['helpers'];expected={'query':4,'enumeration':24,'open_cache':0,'constructor':4,'cache_layout':0}
    if set(helpers)!=set(expected):raise ValueError('incomplete cached-update helper proof')
    codes={}
    for name,cleanup in expected.items():
        body=helpers[name];validate_body(body);function=functions.get(body['address'])
        if (set(body)!={'address','bytes','sha256','stack_bytes'} or body['stack_bytes']!=cleanup
                or function is None or not context.matches_body(function,body)
                or decode(context,body['address'],body['bytes'],begin,end)[0]!=[cleanup]):
            raise ValueError('unreviewed cached-update helper body/RET')
        codes[name]=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(original_bytes(context,body['address'],body['bytes']),body['address']))
    wrapper=selected[0]
    code=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(original_bytes(context,wrapper['address'],wrapper['bytes']),wrapper['address']))
    if (len(code)!=2 or code[0].mnemonic!='mov' or code[0].op_str!=f'ecx, dword ptr [{hex(online_owner)}]'
            or code[-1].mnemonic!='jmp' or code[-1].op_str!=hex(helpers['query']['address'])):
        raise ValueError('unreviewed cached-update tail/ABI')
    def require(name,mnemonic,operands):
        if not any(i.mnemonic==mnemonic and i.op_str==operands for i in codes[name]):
            raise ValueError('unreviewed cached-update source role: '+name+'/'+operands)
    require('query','push',str(proof['kind']))
    require('query','push','0x27')
    require('query','call',hex(helpers['enumeration']['address']))
    require('constructor','mov',f'eax, dword ptr [eax + {hex(proof["certificate_offset"])}]')
    cert=struct.unpack_from('<I',context.blob,0x118)[0]
    if original_bytes(context,cert+proof['certificate_offset'],4)!=bytes(4):
        raise ValueError('cached-update namespace suffix is not qualified empty')
    data={}
    for name in ('filename','directory','default_suffix','type_table'):
        item=proof[name];validate_body(item)
        if set(item)!={'address','bytes','sha256'}:raise ValueError('invalid cached-update data proof')
        value=original_bytes(context,item['address'],item['bytes'])
        if hashlib.sha256(value).hexdigest()!=item['sha256']:raise ValueError('cached-update data differs')
        data[name]=value
    prefix=f'\\Device\\Harddisk0\\partition{proof["partition"]}\\'
    filename=prefix+'CACHE%.4s\\LocalCache%02d.bin'
    if (data['filename']!=(filename+'\0').encode('ascii')
            or data['directory']!=(prefix+'CACHE%.4s\\\0').encode('ascii')
            or data['default_suffix']!=b'\0'
            or data['type_table']!=struct.pack('<5H',1,0,288,8,0)
            or proof['relative']!=f'CACHE\\LocalCache{proof["kind"]:02d}.bin'):
        raise ValueError('unreviewed cached-update locator/layout')
    require('open_cache','push',hex(proof['filename']['address']))
    require('open_cache','push',hex(proof['directory']['address']))
    require('open_cache','mov',f'eax, {hex(proof["default_suffix"]["address"])}')
    table_base=proof['type_table']['address']-proof['kind']*10
    require('cache_layout','movzx',f'eax, word ptr [esi + {hex(table_base+4)}]')
    # The original private method may only be entered through its replaced
    # public wrapper. New direct/data-pointer bypasses require separate proof.
    target=helpers['query']['address'];decoder=Cs(CS_ARCH_X86,CS_MODE_32)
    decoder.detail=True;decoder.skipdata=True
    for address,function in functions.items():
        if function.get('section')=='.data':continue
        for instruction in decoder.disasm(original_bytes(context,address,function['size']),address):
            if (instruction.id and instruction.mnemonic in ('call','jmp') and instruction.operands
                    and instruction.operands[0].type==X86_OP_IMM and instruction.operands[0].imm==target
                    and address!=proof['entry']):
                raise ValueError('unprotected cached-update private method consumer')
    literal=struct.pack('<I',target)
    if any(literal in context.blob[s['raw_offset']:s['raw_offset']+s['raw_size']] for s in context.image['sections']):
        raise ValueError('unprotected cached-update method pointer/alias')
    return proof


def random_proof(context, proof, functions, calls, begin, end, network_owner):
    if (not isinstance(proof,dict) or set(proof)!={'entry','helpers','state_offset','crypt_import','output_limit'}
            or type(proof['state_offset']) is not int or not 0<proof['state_offset']<=4096
            or proof['state_offset']%4 or type(proof['output_limit']) is not int or proof['output_limit']!=64):
        raise ValueError('unreviewed random-byte policy')
    selected=[c for c in calls if c['action']=='random']
    if len(selected)!=1 or selected[0]['address']!=proof['entry']:
        raise ValueError('unreviewed random-byte entry')
    helpers=proof['helpers'];codes={}
    if not isinstance(helpers,dict) or set(helpers)!={'query','generate','crypt'}:
        raise ValueError('incomplete random-byte helper proof')
    for name,cleanup in (('query',8),('generate',8),('crypt',12)):
        body=helpers[name];validate_body(body);function=functions.get(body['address'])
        if (set(body)!={'address','bytes','sha256','stack_bytes'} or body['stack_bytes']!=cleanup
                or function is None or not context.matches_body(function,body)
                or decode(context,body['address'],body['bytes'],begin,end)[0]!=([] if name=='crypt' else [cleanup])):
            raise ValueError('unreviewed random-byte helper body/RET')
        codes[name]=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(original_bytes(context,body['address'],body['bytes']),body['address']))
    selected=selected[0]
    wrapper=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(original_bytes(context,selected['address'],selected['bytes']),selected['address']))
    if (len(wrapper)!=2 or wrapper[0].mnemonic!='mov' or wrapper[0].op_str!=f'ecx, dword ptr [{hex(network_owner)}]'
            or wrapper[-1].mnemonic!='jmp' or wrapper[-1].op_str!=hex(helpers['query']['address'])):
        raise ValueError('unreviewed random-byte tail/owner')
    def require(name,mnemonic,operands,count=1):
        if sum(i.mnemonic==mnemonic and i.op_str==operands for i in codes[name])!=count:
            raise ValueError('unreviewed random-byte source role: '+name+'/'+operands)
    require('query','test','ecx, ecx');require('query','mov','eax, 0x276d')
    require('query','push','dword ptr [esp + 8]',2)
    require('query','call',hex(helpers['generate']['address']));require('query','xor','eax, eax')
    require('generate','add',f'esi, {hex(proof["state_offset"])}')
    require('generate','call',hex(helpers['crypt']['address']))
    imported=proof['crypt_import'];validate_body(imported)
    if (set(imported)!={'address','bytes','sha256'} or imported['bytes']!=4
            or hashlib.sha256(original_bytes(context,imported['address'],4)).hexdigest()!=imported['sha256']
            or original_bytes(context,imported['address'],4)!=struct.pack('<I',0x80000153)):
        raise ValueError('unreviewed random-byte RC4 import')
    if (len(codes['crypt'])!=1 or codes['crypt'][0].mnemonic!='jmp'
            or codes['crypt'][0].op_str!=f'dword ptr [{hex(imported["address"])}]'):
        raise ValueError('unreviewed random-byte crypt import role')
    # A private entry may not bypass its replaced public owner boundary.
    target=helpers['query']['address'];decoder=Cs(CS_ARCH_X86,CS_MODE_32)
    decoder.detail=True;decoder.skipdata=True
    for address,function in functions.items():
        if function.get('section')=='.data':continue
        for instruction in decoder.disasm(original_bytes(context,address,function['size']),address):
            if (instruction.id and instruction.mnemonic in ('call','jmp') and instruction.operands
                    and instruction.operands[0].type==X86_OP_IMM and instruction.operands[0].imm==target
                    and address!=proof['entry']):
                raise ValueError('unprotected random-byte private method consumer')
    literal=struct.pack('<I',target)
    if any(literal in context.blob[s['raw_offset']:s['raw_offset']+s['raw_size']] for s in context.image['sections']):
        raise ValueError('unprotected random-byte method pointer/alias')
    return proof


def link_proof(context, proof, functions, calls, network_owner):
    fields={'entry','attachment','active_mask','thread_pointer_offset','thread_initialized_offset','value_register','imports'}
    if (not isinstance(proof,dict) or set(proof)!=fields or proof['attachment']!='unattached'
            or type(proof['active_mask']) is not int or proof['active_mask']!=1
            or type(proof['thread_pointer_offset']) is not int or not 0<=proof['thread_pointer_offset']<=4092
            or type(proof['thread_initialized_offset']) is not int or not 0<proof['thread_initialized_offset']<=4092
            or proof['thread_pointer_offset']%4 or proof['thread_initialized_offset']%4
            or proof['value_register'] not in ('ebx','esi','edi','ebp')):
        raise ValueError('unreviewed link-state proof boundary')
    selected=[c for c in calls if c['action']=='link']
    if (len(selected)!=1 or selected[0]['address']!=proof['entry'] or selected[0]['returns']!=[0]
            or not any(target==network_owner for _,target in selected[0]['references'])):
        raise ValueError('unreviewed link-state entry/ABI/owner')
    body=original_bytes(context,proof['entry'],functions[proof['entry']]['size'])
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    instructions=list(decoder.disasm(body,proof['entry']))
    imports=proof['imports'];expected={'raise':129,'initialize':253,'query':252,'lower':161}
    if not isinstance(imports,dict) or set(imports)!=set(expected):
        raise ValueError('unreviewed link-state import roles')
    addresses=[]
    for role,ordinal in expected.items():
        item=imports[role]
        if (not isinstance(item,dict) or set(item)!={'address','ordinal'} or type(item['address']) is not int
                or not 0<item['address']<=0xfffffffc or item['address']%4
                or type(item['ordinal']) is not int or item['ordinal']!=ordinal
                or original_bytes(context,item['address'],4)!=struct.pack('<I',0x80000000|ordinal)):
            raise ValueError('unreviewed link-state kernel import')
        addresses.append(item['address'])
    if len(set(addresses))!=4:raise ValueError('aliased link-state import roles')
    actual=[]
    for instruction in instructions:
        if instruction.mnemonic=='call':
            if (len(instruction.operands)!=1 or instruction.operands[0].type!=X86_OP_MEM
                    or instruction.operands[0].mem.base or instruction.operands[0].mem.index
                    or instruction.operands[0].mem.segment):
                raise ValueError('unreviewed link-state call role')
            actual.append(instruction.operands[0].mem.disp&0xffffffff)
    if actual!=addresses:raise ValueError('unreviewed link-state call order')
    pointer_registers=[]
    for instruction in instructions:
        if instruction.mnemonic=='mov' and len(instruction.operands)==2 and instruction.operands[0].type==X86_OP_REG:
            source=instruction.operands[1]
            if (source.type==X86_OP_MEM and source.size==4 and instruction.operands[0].size==4
                    and instruction.reg_name(source.mem.segment)=='fs'
                    and not source.mem.base and not source.mem.index and source.mem.disp==proof['thread_pointer_offset']):
                pointer_registers.append(instruction.operands[0].reg)
    if len(pointer_registers)!=1 or not any(
            operand.type==X86_OP_MEM and operand.mem.base==pointer_registers[0] and not operand.mem.index
            and operand.mem.disp==proof['thread_initialized_offset'] and instruction.mnemonic in ('cmp','test')
            for instruction in instructions for operand in instruction.operands):
        raise ValueError('unreviewed link-state thread context role')
    value=proof['value_register'];text=[(i.mnemonic,i.op_str) for i in instructions]
    mask=[('mov','eax, '+value),('and','al, 1'),('neg','al'),('sbb','eax, eax'),('and','eax, '+value)]
    if not any(text[i:i+len(mask)]==mask for i in range(len(text)-len(mask)+1)):
        raise ValueError('unreviewed link-state returned flag mask')
    return proof


def emit_header(binding):
    calls=binding['calls'];query=binding.get('cached_update')
    actions='PILOT_NETWORK_ACTION_GUARD, PILOT_NETWORK_ACTION_STARTUP, PILOT_NETWORK_ACTION_CLEANUP'
    if query:actions+=', PILOT_NETWORK_ACTION_CACHED_UPDATE'
    if binding.get('random'):actions+=', PILOT_NETWORK_ACTION_RANDOM'
    if binding.get('link'):actions+=', PILOT_NETWORK_ACTION_LINK'
    header=['#pragma once','#define PILOT_NETWORK_ABI_VERSION 1u','enum { '+actions+' };','enum PilotNetworkEntry {']
    header += [f'    PILOT_NETWORK_ENTRY_{c["address"]:08X},' for c in calls]
    header += ['    PILOT_NETWORK_COUNT','};',
               'static const struct { const char *name; unsigned address, action, stack_bytes; } pilot_network_calls[] = {']
    for call in calls:
        size={'startup':4,'cached_update':4,'random':8}.get(call['action'],0)
        header.append(f'    {{"{call["symbol"]}", 0x{call["address"]:08x}u, PILOT_NETWORK_ACTION_{call["action"].upper()}, {size}u}},')
    header+=['};']
    if query:
        header += ['#define PILOT_NETWORK_HAS_CACHED_UPDATE 1',
                   f'#define PILOT_NETWORK_CACHE_PARTITION {query["partition"]}u',
                   '#define PILOT_NETWORK_CACHE_RELATIVE '+json.dumps(query['relative'])]
    if binding.get('random'):header+=['#define PILOT_NETWORK_HAS_RANDOM 1']
    if binding.get('link'):header+=['#define PILOT_NETWORK_HAS_LINK 1','#define PILOT_NETWORK_LINK_ATTACHMENT 1u']
    return '\n'.join(header)+'\n'


def bind(xbe, output, symbols, profile=None):
    context = Binding(xbe, output, symbols)
    proof = context.require_profile('network', profile, adapter='xonline-offline-lifecycle-v1')
    required={'adapter','globals','consumer_range','calls','callback_table','untranslated_callbacks','set_last_error'}
    if not required<=set(proof) or set(proof)-required-{'cached_update','random','link'}:
        raise ValueError('unsupported network proof fields')
    globals_ = proof['globals']
    if (not isinstance(globals_, dict) or set(globals_) != {'online_owner', 'online_gate', 'network_owner', 'network_gate'}
            or any(type(a) is not int or not 0 < a <= 0xfffffffc or a % 4 for a in globals_.values())
            or len(set(globals_.values())) != 4):
        raise ValueError('invalid network owner globals')
    extent = proof['consumer_range']
    if (not isinstance(extent, list) or len(extent) != 2 or any(type(v) is not int for v in extent)
            or not 0 < extent[0] < extent[1] <= 0x100000000 or extent[1]-extent[0] > 4096
            or not all(extent[0] <= a and a+4 <= extent[1] for a in globals_.values())):
        raise ValueError('invalid network consumer extent')
    begin, end = extent
    functions = {int(f['start'], 16): f for f in context.functions}
    if len(functions) != len(context.functions):
        raise ValueError('duplicate generated network function entries')
    calls, seen, lifecycle = [], set(), set()
    for entry in proof['calls']:
        validate_body(entry)
        if set(entry) != {'address', 'bytes', 'sha256', 'returns', 'references', 'action'}:
            raise ValueError('invalid network entry proof fields')
        address = entry['address']; action = entry['action']
        if type(address) is not int or address in seen or address not in functions or action not in ('startup', 'cleanup', 'guard','cached_update','random','link'):
            raise ValueError('invalid network entry/action')
        function = functions[address]
        if function.get('section') == '.data' or not context.matches_body(function, entry):
            raise ValueError('unreviewed network original body')
        returns, refs = decode(context, address, entry['bytes'], begin, end)
        if returns != entry['returns'] or refs != entry['references']:
            raise ValueError('network return/global operand proof differs')
        if action in ('startup','cleanup'):
            expected = [4] if action == 'startup' else [0]
            if action in lifecycle or returns != expected or not any(target == globals_['online_owner'] for _, target in refs):
                raise ValueError('incomplete or unreviewed network lifecycle ABI')
            lifecycle.add(action)
        seen.add(address)
        calls.append(dict(entry, symbol=function['name']))
    if lifecycle != {'startup', 'cleanup'}:
        raise ValueError('missing network lifecycle entry')
    query=proof.get('cached_update')
    if query is not None:cached_update_proof(context,query,functions,calls,begin,end,globals_['online_owner'])
    elif any(c['action']=='cached_update' for c in calls):raise ValueError('missing cached-update proof')
    random=proof.get('random')
    if random is not None:random_proof(context,random,functions,calls,begin,end,globals_['network_owner'])
    elif any(c['action']=='random' for c in calls):raise ValueError('missing random-byte proof')
    link=proof.get('link')
    if link is not None:link_proof(context,link,functions,calls,globals_['network_owner'])
    elif any(c['action']=='link' for c in calls):raise ValueError('missing link-state proof')
    # A profile cannot omit another discovered original global consumer while
    # admitting startup. The exact image/body identity remains the proof basis.
    consumers = set()
    for address, function in functions.items():
        if function.get('section') == '.data':
            continue
        _, refs = decode(context, address, function['size'], begin, end, strict=False)
        if refs:
            consumers.add(address)
    if not consumers <= seen:
        raise ValueError('unprotected original network global consumer')
    # Check every byte alignment too: undecodable discovery ranges or a stored
    # literal alias must not silently bypass the decoded-consumer admission.
    starts = sorted(a for a, f in functions.items() if f.get('section') != '.data')
    for section in context.image['sections']:
        data = context.blob[section['raw_offset']:section['raw_offset']+section['raw_size']]
        for target in globals_.values():
            offset = data.find(struct.pack('<I', target))
            while offset >= 0:
                address = section['address']+offset
                index = bisect.bisect_right(starts, address)-1
                owner = starts[index] if index >= 0 else None
                if owner not in seen or address+4 > int(functions[owner]['end'], 16):
                    raise ValueError('unprotected original network global literal/alias')
                offset = data.find(struct.pack('<I', target), offset+1)
    table = proof['callback_table']
    if (not isinstance(table, dict) or set(table) != {'address', 'entries', 'publisher', 'instruction', 'owner_offset'}
            or len(table['entries']) != 4 or len(set(table['entries'])) != 4
            or any(type(a) is not int or not 0 < a <= 0xffffffff for a in table['entries'])
            or table['publisher'] not in seen or table['owner_offset'] != 0x264):
        raise ValueError('unreviewed original network callback table')
    if original_bytes(context, table['address'], 16) != struct.pack('<4I', *table['entries']):
        raise ValueError('original network callback table differs')
    publisher = functions[table['publisher']]
    detail = Cs(CS_ARCH_X86, CS_MODE_32); detail.detail = True
    publish = [i for i in detail.disasm(original_bytes(context, table['publisher'], publisher['size']), table['publisher'])
               if i.address == table['instruction']]
    if (len(publish) != 1 or publish[0].mnemonic != 'mov' or len(publish[0].operands) != 2
            or publish[0].operands[0].type != X86_OP_MEM or publish[0].operands[0].mem.disp != table['owner_offset']
            or not publish[0].operands[0].mem.base or publish[0].operands[0].mem.index
            or publish[0].operands[1].type != X86_OP_IMM or publish[0].operands[1].imm != table['address']):
        raise ValueError('original network callback publisher role differs')
    absent = set()
    for entry in proof['untranslated_callbacks']:
        validate_body(entry); address = entry['address']
        if (set(entry) != {'address', 'bytes', 'sha256', 'returns'} or address in absent or address in functions
                or hashlib.sha256(original_bytes(context, address, entry['bytes'])).hexdigest() != entry['sha256']
                or decode(context, address, entry['bytes'], begin, end)[0] != entry['returns']):
            raise ValueError('unprotected or changed original callback entry')
        # If it becomes translated, require an explicit new guard admission.
        dispatch = context.generated / 'recomp_dispatch.c'
        if dispatch.exists() and re.search(r'\b0x0*'+format(address, 'x')+r'u?\b', dispatch.read_text(), re.I):
            raise ValueError('unprotected original callback dispatch entry')
        absent.add(address)
    if set(table['entries']) != (seen & set(table['entries'])) | absent:
        raise ValueError('unprotected original callback table entry')
    error = context.symbol('SetLastError', 4)
    if (proof['set_last_error'].get('stack_bytes') != 4 or not context.matches_body(error, proof['set_last_error'])
            or decode(context, int(error['start'], 16), error['size'], begin, end)[0] != [4]):
        raise ValueError('unreviewed network thread LastError helper')
    calls.sort(key=lambda c: c['address'])
    binding = dict(xbe_sha256=context.image['sha256'], adapter=proof['adapter'], calls=calls,
                   globals=globals_, consumer_range=extent, callback_table=table,
                   untranslated_callbacks=proof['untranslated_callbacks'], set_last_error=proof['set_last_error'])
    if query:binding['cached_update']=query
    if random:binding['random']=random
    if link:binding['link']=link
    wrapper = ['#include "pilot_network_bindings.h"',
               'extern void pilot_network_dispatch(unsigned, void (*)(void));', f'extern void {error["name"]}(void);']
    replacements = {}
    for call in calls:
        symbol = call['symbol']
        if not re.fullmatch(r'sub_[0-9A-Fa-f]{8}', symbol):
            raise ValueError('invalid generated network symbol')
        original = f'pilot_original_network_{call["address"]:08x}'
        replacements[symbol] = original
        wrapper += [f'extern void {original}(void);', f'void {symbol}(void) {{', '#if defined(PILOT_NETWORK_OFFLINE) && PILOT_NETWORK_OFFLINE',
                    f'    pilot_network_dispatch(PILOT_NETWORK_ENTRY_{call["address"]:08X}, {error["name"]});',
                    '#else', f'    {original}();', '#endif', '}']
    texts = {'pilot_network_bindings.h': emit_header(binding), 'pilot_network_binding.c': '\n'.join(wrapper)+'\n'}
    record = output / 'network-binding.aot-private.json'
    if (context.generated / 'pilot_network_binding.c').exists():
        actual=json.loads(record.read_text())
        if (context.manifest.get('network_binding') != actual or
                (context.generated/'pilot_network_binding.c').read_text()!=texts['pilot_network_binding.c']):
            raise ValueError('existing network binding differs')
        header=(context.generated/'pilot_network_bindings.h').read_text()
        if actual==binding and header==texts['pilot_network_bindings.h']:return binding
        upgrades=[]
        features=[feature for feature in ('cached_update','random','link') if feature in binding]
        for removed in (group for count in range(1,len(features)+1) for group in combinations(features,count)):
            prior=copy.deepcopy(binding)
            for feature in removed:
                old=prior.pop(feature)
                next(c for c in prior['calls'] if c['address']==old['entry'])['action']='guard'
            upgrades.append(prior)
        if not any(actual==prior and header==emit_header(prior) for prior in upgrades):
            raise ValueError('existing network binding differs')
        (context.generated/'pilot_network_bindings.h').write_text(texts['pilot_network_bindings.h'],encoding='utf-8',newline='\n')
        record.write_text(json.dumps(binding,indent=2)+'\n',encoding='utf-8',newline='\n')
        context.manifest['network_binding']=binding
        context.manifest['files']['pilot_network_bindings.h']=hashlib.sha256((context.generated/'pilot_network_bindings.h').read_bytes()).hexdigest()
        context.manifest_path.write_text(json.dumps(context.manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
        return binding
    found, edits = set(), {}
    def rename(match):
        name = match[1]
        if name not in replacements:
            return match[0]
        if name in found:
            raise ValueError('duplicate original network generated body')
        found.add(name)
        return 'void '+replacements[name]+'(void)\n{'
    for path in context.generated.glob('recomp_*.c'):
        source = path.read_text(encoding='utf-8')
        updated = re.sub(r'void (sub_[0-9A-Fa-f]{8})\(void\)\n\{', rename, source)
        if source != updated:
            edits[path] = updated
    if found != set(replacements):
        raise ValueError('missing original network generated body')
    for path, source in edits.items():
        path.write_text(source, encoding='utf-8', newline='\n')
    for name, source in texts.items():
        (context.generated / name).write_text(source, encoding='utf-8', newline='\n')
    record.write_text(json.dumps(binding, indent=2)+'\n', encoding='utf-8', newline='\n')
    context.manifest['network_binding'] = binding
    context.manifest['files'] = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in sorted(context.generated.iterdir()) if p.is_file()}
    context.manifest_path.write_text(json.dumps(context.manifest, indent=2)+'\n', encoding='utf-8', newline='\n')
    return binding


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'output', 'symbols'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--profile', type=Path)
    args = parser.parse_args()
    result = bind(args.xbe, args.output, args.symbols, args.profile)
    print(json.dumps(dict(xbe_sha256=result['xbe_sha256'], bound=len(result['calls']))))
