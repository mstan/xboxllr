"""Resolve PDB locations using explicitly supplied guest context and reads."""
from native_guest_symbols import _matches_image
from native_guest_types import TypeDecoder

GPRS = ('eax','ecx','edx','ebx','esp','ebp','esi','edi')


def frame_program_base(frame, target, context, function, read=None):
    """Evaluate the bounded DIA program prefix establishing a virtual base.

    Return-address search is a guest stack-walk input, never a heuristic scan
    over host/guest words that merely look like code pointers. Stop once the
    requested base is established; do not unwind or alter the guest's registers.
    """
    tokens = frame.get('program','').split()
    if not tokens or len(tokens)>256:
        return None
    values = {'$'+name:value for name,value in context.get('registers',{}).items() if type(value) is int}
    constants = {'.cbLocals':frame.get('locals_length'),'.cbSavedRegs':frame.get('saved_registers_length'),
                 '.cbParams':frame.get('params_length'),
                 '.raSearchStart':context.get('frame_search_start',{}).get(str(function))}
    stack = []
    def value(item):
        result = values.get(item) if isinstance(item,str) else item
        if type(result) is not int or not 0<=result<0x100000000:
            raise ValueError('frame program needs unavailable guest context')
        return result
    try:
        for token in tokens:
            if token=='=':
                right,left=stack.pop(),stack.pop()
                if not isinstance(left,str) or not left.startswith('$'):
                    return None
                values[left]=value(right)
                if left==target:
                    return values[left]
            elif token in ('+','-'):
                right,left=value(stack.pop()),value(stack.pop())
                stack.append(left+right if token=='+' else left-right)
            elif token=='^':
                address=value(stack.pop())
                if read is None or address>0xfffffffc:
                    return None
                raw=read(address,4)
                if not isinstance(raw,bytes) or len(raw)!=4:
                    return None
                stack.append(int.from_bytes(raw,'little'))
            elif token.startswith('$'):
                stack.append(token)
            elif token.startswith('.'):
                stack.append(value(constants.get(token)))
            else:
                stack.append(int(token,0))
            if len(stack)>64:
                return None
    except (IndexError,ValueError,OSError):
        return None
    return None


def register_bytes(identifier, registers):
    """CodeView x86 register IDs; never read the host process's registers."""
    shift = 0
    if 1 <= identifier <= 8:
        name = GPRS[(identifier-1)%4];shift = 8 if identifier>=5 else 0;bits = 8
    elif 9 <= identifier <= 16:
        name = GPRS[identifier-9];bits = 16
    elif 17 <= identifier <= 24:
        name = GPRS[identifier-17];bits = 32
    elif 25 <= identifier <= 30:
        name = ('es','cs','ss','ds','fs','gs')[identifier-25];bits = 16
    elif identifier in (31,32,33,34):
        name = 'eip' if identifier in (31,33) else 'eflags';bits = 16 if identifier<=32 else 32
    elif 154 <= identifier <= 161:
        name = 'xmm'+str(identifier-154);bits = 128
    elif 162 <= identifier <= 193:
        name = 'xmm'+str((identifier-162)//4);bits = 32;shift = ((identifier-162)%4)*32
    elif 194 <= identifier <= 209:
        name = 'xmm'+str((identifier-194)%8);bits = 64;shift = 64 if identifier>=202 else 0
    elif identifier == 212:
        low,high = register_bytes(17,registers),register_bytes(19,registers)
        return low+high if low is not None and high is not None else None
    else:
        return None
    value = registers.get(name)
    if isinstance(value, bytes):
        value = int.from_bytes(value,'little')
    storage_bits = 128 if name.startswith('xmm') else 32
    if type(value) is not int or not 0 <= value < (1<<storage_bits):
        return None
    return ((value>>shift)&((1<<bits)-1)).to_bytes(bits//8,'little')


class Variables:
    def __init__(self, document, expected_xbe_sha256):
        self.decoder = TypeDecoder(document,expected_xbe_sha256)
        self.image = expected_xbe_sha256.lower()
        self.variables = document.get('variables',{})
        self.scopes = document.get('scopes',{})
        self.tls_template = document.get('tls_template')
        self.frames = document.get('frames',[])

    def _scope(self, variable, pc):
        parent = variable.get('parent');seen = set();function = None
        while str(parent) in self.scopes:
            if parent in seen or len(seen)>=128:
                return None,'invalid lexical scope chain'
            seen.add(parent);scope = self.scopes[str(parent)]
            if type(pc) is not int or 'address' not in scope or 'end' not in scope:
                return None,'guest PC or scope bounds unavailable'
            if not scope['address'] <= pc < scope['end']:
                return None,'out-of-scope'
            if scope['tag']==5:
                function = scope['id']
                if pc < scope.get('debug_start',scope['address']) or pc >= scope.get('debug_end',scope['end']):
                    return None,'outside function debug body'
            parent = scope.get('parent')
        if function is None and variable.get('data_kind') in (1,2,3,4):
            return None,'owning function scope unavailable'
        return function,None

    def locate(self, variable_id, context, read=None):
        if not _matches_image(context,self.image):
            raise ValueError('variable context belongs to another guest image')
        variable = self.variables.get(str(variable_id))
        if variable is None:
            return dict(status='unavailable',reason='unknown variable')
        function,error = self._scope(variable,context.get('pc'))
        if error:
            return dict(status='out-of-scope' if error=='out-of-scope' else 'unavailable',reason=error)
        if variable.get('optimized_away'):
            return dict(status='optimized-away',reason='PDB marks this variable optimized away')
        if 'live_start' in variable:
            if not variable['live_start'] <= context.get('pc',-1) < variable['live_end']:
                return dict(status='unavailable' if variable.get('live_range_count',1)>1 else 'out-of-scope',
                            reason='outside supplied live range')
        elif variable.get('live_range_count',0)>0 or variable.get('control_flow_dependent'):
            return dict(status='unavailable',reason='location needs unavailable PC/liveness metadata')
        location = variable.get('location')
        common = dict(status='available',variable_id=variable_id,type_id=variable.get('type'),name=variable.get('name'))
        if location==10:
            constant = variable.get('constant',{})
            if constant.get('kind')=='integer':
                return dict(common,kind='constant',value=constant['value'])
            return dict(status='unavailable',reason='constant value unavailable')
        if location==5:
            identifier = variable.get('register_id')
            data = register_bytes(identifier,context.get('registers',{})) if type(identifier) is int else None
            if data is not None:
                return dict(common,kind='register',register_id=identifier,raw=data.hex())
            return dict(status='unavailable',reason='required guest register unavailable')
        address = None
        if location==1:
            address = variable.get('address')
        elif location in (3,4):
            offset = variable.get('location_offset')
            if location==4:
                base = context.get('this_address')
            else:
                identifier = variable.get('register_id')
                if identifier in (30006,30008,30009):
                    base = context.get('frame_bases',{}).get(str(function),{}).get(str(identifier))
                    if base is None:
                        pc=context.get('pc')
                        frames=[f for f in self.frames if type(pc) is int and
                                f.get('address',0x100000000)<=pc<f.get('end',0)]
                        if frames:
                            frame=max(frames,key=lambda f:(f['address'],-(f['end']-f['address']),bool(f.get('program'))))
                            target={30006:'$T0',30008:'$P',30009:'$L'}[identifier]
                            if pc>=frame['address']+frame.get('prolog_length',0):
                                base=frame_program_base(frame,target,context,function,read)
                else:
                    raw = register_bytes(identifier,context.get('registers',{})) if type(identifier) is int else None
                    base = int.from_bytes(raw,'little') if raw is not None and len(raw)==4 else None
            if type(base) is int and type(offset) is int:
                address = base+offset
        elif location==2:
            tls = context.get('tls',{});template = self.tls_template
            if template and type(variable.get('address')) is int and type(tls.get('base')) is int:
                offset = variable['address']-template['start']
                if 0<=offset<template['size']:
                    address = tls['base']+offset
        if type(address) is int and 0<=address<0x100000000:
            return dict(common,kind='memory',address=address)
        return dict(status='unavailable',reason='guest location context or source metadata unavailable')

    def value(self, variable_id, context, read, max_bytes=4096):
        result = self.locate(variable_id,context,read)
        if result['status']!='available' or result['kind']=='constant':
            return result
        row = self.decoder.types.get(str(result['type_id']))
        seen = set()
        while row and row.get('tag')==17 and row['id'] not in seen:
            seen.add(row['id']);row = self.decoder.types.get(str(row.get('type')))
        size = row.get('length') if row else None
        if type(size) is not int or not 0<size<=max_bytes:
            return dict(status='unavailable',reason='variable type size unavailable or exceeds read limit')
        if result['kind']=='register':
            data = bytes.fromhex(result['raw'])
            if len(data)<size:
                return dict(status='unavailable',reason='guest register has insufficient value bits')
            data = data[:size]
        else:
            if result['address']+size>0x100000000:
                return dict(status='unavailable',reason='guest variable extent wraps address space')
            try:
                data = read(result['address'],size)
            except (ValueError,OSError):
                return dict(status='unavailable',reason='guest memory is not readable')
            if not isinstance(data,bytes) or len(data)!=size:
                return dict(status='unavailable',reason='incomplete guest memory read')
        return dict(result,raw=data.hex(),decoded=self.decoder.scalar(result['type_id'],data))
