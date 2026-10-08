#!/usr/bin/env python3
"""Check native declaration captures against the unhooked original XDK setter.

Requires private original XBE and revision binding; imports no game bytes.
This qualifies binding/query-visible state, not rendering with an empty FVF.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from aot_inventory import parse_xbe
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import UC_X86_REG_ESP,UC_X86_REG_EIP

def run(xbe,binding,captures):
    blob=xbe.read_bytes();digest=hashlib.sha256(blob).hexdigest()
    assert digest=='e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
    image=parse_xbe(blob);b=json.loads(binding.read_text());device=b['device_object']
    obj=b['fvf_binding']['object'];tf=b['transform_binding']['fields']
    setter=next(int(c['address'],16) for c in b['calls'] if c['name']=='D3DDevice_SetVertexShader')
    rows=[]
    for path in sorted(captures.glob('fvf-*.bin')):
        raw=path.read_bytes();fvf,size=struct.unpack_from('<2I',raw)
        assert size==0x114 and len(raw)==8+size*2
        u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0,0x4000000);u.mem_map(0x70000000,0x10000)
        for s in image['sections']:
            if s['preload']:u.mem_write(s['address'],blob[s['raw_offset']:s['raw_offset']+s['raw_size']])
        def put(a,*v):u.mem_write(a,struct.pack('<'+'I'*len(v),*v))
        def floats(a,*v):u.mem_write(a,struct.pack('<'+'f'*len(v),*v))
        def get(a):return struct.unpack('<I',u.mem_read(a,4))[0]
        put(b['device_global'],device);put(device,0x2000000,0x2100000)
        fields=b['stateblock']['fields']
        put(device+fields['vertex_object'],0x70003000);put(0x70003000,1,0x10,2)
        u.mem_write(obj,raw[8:8+size]);put(device+0xa90,4,4,56,56)
        floats(device+0xaa0,.25,.75);floats(device+tf['depth_scale'],16777215)
        floats(device+tf['pixel_scale'],1,1);floats(device+tf['screen_offset'],.53125,.53125)
        put(b['render_states']+92*4,0);put(b['render_states']+143*4,1)
        constants=device+fields['vertex_constants'];cache=device+b['vertex_binding']['program_cache']
        u.mem_write(constants,b'\x57'*3072);u.mem_write(cache,b'\x68'*2176)
        end=0x70000000;sp=0x70008000;put(sp,end,fvf);u.reg_write(UC_X86_REG_ESP,sp)
        u.emu_start(setter,end,count=100000)
        assert u.reg_read(UC_X86_REG_EIP)==end and u.reg_read(UC_X86_REG_ESP)==sp+8
        assert bytes(u.mem_read(obj,size))==raw[8+size:],path
        assert get(device+fields['vertex_object'])==obj
        assert get(device+fields['vertex_shader'])==fvf and get(device+b['vertex_binding']['start'])==0
        assert bytes(u.mem_read(constants,3072))==b'\x57'*3072
        assert bytes(u.mem_read(cache,2176))==b'\x68'*2176
        words=list(struct.unpack('<'+'I'*((get(device)-0x2000000)//4),u.mem_read(0x2000000,get(device)-0x2000000)))
        if not fvf:
            assert get(obj+4)==0 and get(obj+0x10)==0
            assert all(get(obj+0x1c+i*16)==2 for i in range(16))
            assert any(words[i]==0x41e94 and words[i+1]==4 for i in range(len(words)-1))
        rows.append(dict(fvf=fvf,capture=path.name,descriptor_equal=size,packets=words))
    assert rows and any(r['fvf']==0 for r in rows)
    return dict(xbe_sha256=digest,cases=rows,scope='complete original binding; empty-format drawing remains unqualified')

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for name in ('xbe','binding','captures','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.output.write_text(json.dumps(run(a.xbe,a.binding,a.captures),indent=2)+'\n')
    print('PASS: original FVF bindings including zero, full descriptors, cached handle, preserved constants and ABI')
