"""Admit a reviewed original native, closed I3DL2 coefficient calculation."""
import re
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from native_binding import validate_body

def bind_i3dl2(context,proof,functions,data):
    selected=proof.get('i3dl2')
    if selected is None:return None
    keys={'adapter','initialize','calculate','helpers','set_listener','driver_commit'}
    if (not isinstance(selected,dict) or set(selected)!=keys or
            selected['adapter']!='original-native-i3dl2-v1' or 'listener' not in proof or
            not isinstance(selected['helpers'],list) or not 1<=len(selected['helpers'])<=64):
        raise ValueError('incomplete original native I3DL2 proof')
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);helpers={};edges={}
    def verify(entry):
        if not isinstance(entry,dict) or set(entry)!={'address','body'} or type(entry['address']) is not int:
            raise ValueError('invalid original I3DL2 helper proof')
        address=entry['address'];body=entry['body'];validate_body(body)
        function=functions.get(address)
        if not function or not context.matches_body(function,body):raise ValueError('I3DL2 helper differs')
        code=list(decoder.disasm(data(address,function['size']),address))
        if {int(i.op_str or '0',0) for i in code if i.mnemonic=='ret'}!={body['stack_bytes']}:
            raise ValueError('I3DL2 helper cleanup differs')
        return address,function,code
    for entry in selected['helpers']:
        address,function,code=verify(entry)
        if address in helpers:raise ValueError('duplicate I3DL2 helper')
        helpers[address]=entry;targets=set()
        for i in code:
            if i.mnemonic in ('call','jmp'):
                if not re.fullmatch('0x[0-9a-f]+',i.op_str):raise ValueError('indirect I3DL2 dependency')
                target=int(i.op_str,0)
                if i.mnemonic=='call' or not address<=target<address+function['size']:targets.add(target)
            if i.mnemonic in ('in','out','int','int3','syscall','sysenter','cli','sti','hlt') or 'fs:' in i.op_str or 'gs:' in i.op_str:
                raise ValueError('I3DL2 arithmetic helper has external machine effects')
        edges[address]=targets
    roots=[selected[k] for k in ('initialize','calculate')]
    if any(type(at) is not int or at not in helpers or helpers[at]['body']['stack_bytes'] for at in roots):
        raise ValueError('I3DL2 native roots require RET0')
    pending=list(roots);seen=set()
    while pending:
        at=pending.pop()
        if at in seen:continue
        if at not in helpers:raise ValueError('unclosed native I3DL2 dependency')
        seen.add(at);pending.extend(edges[at])
    if seen!=set(helpers):raise ValueError('unreachable native I3DL2 helper')
    for key,stack in (('set_listener',12),('driver_commit',0)):
        _,_,code=verify(selected[key])
        if selected[key]['body']['stack_bytes']!=stack:raise ValueError('I3DL2 owner cleanup differs')
    setter=selected['set_listener']['address']
    public=next((s for s in context.symbols if s['name']=='IDirectSound_SetI3DL2Listener' and s['library']=='DSOUND'),None)
    if not public:raise ValueError('missing original I3DL2 public owner')
    wrapper=functions[public['address']]
    if not any(i.mnemonic=='call' and i.op_str==hex(setter) for i in decoder.disasm(data(public['address'],wrapper['size']),public['address'])):
        raise ValueError('I3DL2 public wrapper/helper mismatch')
    return selected
