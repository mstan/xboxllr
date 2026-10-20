"""Image-bound original spatial parameter setters, before active output admission."""
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from native_binding import validate_body

SPATIAL_CALLS={'SetAllParameters':12,'SetConeAngles':16,'SetConeOrientation':20,
    'SetConeOutsideVolume':12,'SetMaxDistance':12,'SetMinDistance':12,'SetMode':12,
    'SetPosition':20,'SetVelocity':20,'SetDistanceFactor':12,'SetDopplerFactor':12,
    'SetRolloffFactor':12,'SetI3DL2Source':12}

def bind_spatial(context,proof,calls,functions,data):
    selected=proof.get('spatial')
    if selected is None:return None
    if (not isinstance(selected,dict) or set(selected)!={'adapter','setters'} or
        selected['adapter']!='spatial-cache-v1' or 'listener' not in proof or
        not isinstance(selected['setters'],dict) or set(selected['setters'])!=set(SPATIAL_CALLS)):
        raise ValueError('incomplete native spatial cache proof')
    admitted={c['name']:c for c in calls};decoder=Cs(CS_ARCH_X86,CS_MODE_32)
    def verify(entry,stack):
        if not isinstance(entry,dict) or set(entry)!={'address','body'} or type(entry['address']) is not int:
            raise ValueError('invalid spatial setter helper')
        address=entry['address'];body=entry['body'];validate_body(body)
        function=functions.get(address)
        if (not function or body['stack_bytes']!=stack or not context.matches_body(function,body)):
            raise ValueError('spatial helper body/ABI differs')
        code=list(decoder.disasm(data(address,function['size']),address))
        if {int(i.op_str or '0',0) for i in code if i.mnemonic=='ret'}!={stack}:
            raise ValueError('spatial helper cleanup differs')
        return code
    def calls_address(code,address):
        return any(i.mnemonic=='call' and i.op_str==hex(address) for i in code)
    for name,stack in SPATIAL_CALLS.items():
        public=admitted.get('IDirectSoundBuffer_'+name);chain=selected['setters'][name]
        if not public or public['stack_bytes']!=stack or not isinstance(chain,dict) or set(chain)!={'buffer','voice'}:
            raise ValueError('spatial public owner/ABI differs: '+name)
        buffer=verify(chain['buffer'],stack);verify(chain['voice'],stack)
        wrapper=functions[public['address']]
        code=list(decoder.disasm(data(public['address'],wrapper['size']),public['address']))
        if not calls_address(code,chain['buffer']['address']) or not calls_address(buffer,chain['voice']['address']):
            raise ValueError('spatial public/buffer/voice chain differs: '+name)
    return selected
