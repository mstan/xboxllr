#!/usr/bin/env python3
"""Original programmable fog setters/submission and independent finite pixels.

Authored ISA/masks and D3D formulas produce expectations; neither shader
implementation generates expected values. Resources, shader objects and the
target start preowned. No original callee/return/packet is replaced. Exact
silicon rounding, nonfinite values and fixed-function fog remain separate.
"""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import struct

from native_vertex_fetch_reference import Reference, OBJECT, STREAMS, FRAME, XBE_SHA
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX


def bits(value):
    return struct.unpack('<I',struct.pack('<f',value))[0]


def f32(value):
    return struct.unpack('<f',struct.pack('<f',value))[0]


def program(mask,ilu):
    code=[0,0x0020001b,0x08000000,0x0000f800,
          0,0x0020001b|(96<<13),0x0c000000,0x0000f818]
    # MAC A=v1 or ILU C=v1; scalar output chooses first masked result lane.
    code+=([0,0x02000200,0x0000006c,0x2000082d|(mask<<12)] if ilu else
           [0,0x0020021b,0x08000000,0x00000829|(mask<<12)])
    return code


def pixel_state(blend):
    state=[0]*60
    state[0]=0x14200000 if blend else 0x13200000
    state[34]=0x04200000 if blend else 0x03200000
    state[26]=state[45]=0xc00;state[53]=1
    state[8]=0x130c0300 if blend else 0xc
    state[9]=0x1c00
    return state


def factor(mode,enabled,distance,density):
    if not enabled:return 1.0
    if mode==3:return (3-distance)/2
    if mode==1:return math.exp(-density*distance)
    return math.exp(-(density*distance)**2)


def pixel_expectations(row,raw):
    factors=[factor(row['mode'],row['enabled'],d,row['density']) for d in raw]
    result=[]
    for y in range(8,56):
        for x in range(8,56):
            b=(x+.5-8)/48;c=(y+.5-8)/48;a=1-b-c
            if x+y+1>=64:continue  # exact diagonal coverage is a separate gate
            q=[a/row['w'][0],b/row['w'][1],c/row['w'][2]]
            f=max(0,min(1,sum(weight*value for weight,value in zip(q,factors))/sum(q)))
            rgb=[f*d+(1-f)*color if row['blend'] else color for d,color in zip((.8,.2,.1),(18/255,52/255,86/255))]
            rgba=rgb+[.6 if row['blend'] else f]
            result.append(((y*64+x),[round(v*255) for v in rgba]))
    assert len(result)==1128
    return result


def check_pixels(ref,expected):
    actual=struct.unpack('<4096I',ref.cpu.mem_read(FRAME,64*64*4))
    for index,rgba in expected:
        word=actual[index]
        assert all(abs((word>>shift&255)-value)<=1 for shift,value in zip((16,8,0,24),rgba)),(index,hex(word),rgba)


def cases():
    for ilu in (False,True):
        for blend in (False,True):
            for mode in (1,2,3):
                for mask in range(1,16):
                    yield dict(mask=mask,ilu=ilu,blend=blend,mode=mode,enabled=1,density=.75,
                        inputs=[[.2,.4,.6,.8]]*3,w=[1,1,1])
    for mode in (1,2,3):
        for blend in (False,True):
            for enabled in (0,1):
                yield dict(mask=1,ilu=False,blend=blend,mode=mode,enabled=enabled,density=.75,
                    inputs=[[0,0,0,d] for d in (-1,3,0)],w=[1,2,4])
    for mode in (1,2):
        for blend in (False,True):
            yield dict(mask=8,ilu=True,blend=blend,mode=mode,enabled=1,density=0,
                inputs=[[d,0,0,0] for d in (-2,1,4)],w=[1,2,4])


def run(args):
    ref=Reference(args);rows=[]
    count=ref.lib.xlle_nv2a_reference_draw_count
    count.argtypes=[ctypes.c_void_p,ctypes.c_uint32];count.restype=ctypes.c_uint64
    fog=ref.lib.xlle_nv2a_reference_fog
    fog.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_float),ctypes.c_uint32];fog.restype=ctypes.c_uint32
    method_value=ref.lib.xlle_nv2a_reference_method_value
    method_value.argtypes=[ctypes.c_void_p,ctypes.c_uint32];method_value.restype=ctypes.c_uint32
    try:
        ref.resource(OBJECT+0x1000,STREAMS);ref.set_stream(0,OBJECT+0x1000,32)
        ref.put(OBJECT,1,0x10,0,0,0)
        for i in range(16):ref.put(OBJECT+0x14+i*16,0,0,2,0)
        ref.put(OBJECT+0x14,0,0,0x42);ref.put(OBJECT+0x24,0,16,0x42)
        # Honest initial target header for original format/viewport decisions.
        surface=OBJECT+0x1c00
        ref.put(surface,0x01050001,FRAME,0,0x00010c29,0x0303f03f)
        for offset in (0x21b4,0x21c0,0x21c4):ref.put(ref.device+offset,surface)
        ref.cpu.mem_write(0x141d88,bytes(166*4))
        constants=OBJECT+0x3000;ref.floats(constants,.8,.2,.1,.6)
        ref.call('D3DDevice_SetVertexShaderConstantNotInline',[4],
            ((UC_X86_REG_ECX,96),(UC_X86_REG_EDX,constants)))
        previous=None
        for row in cases():
            code=program(row['mask'],row['ilu'])
            if code!=previous:ref.bind_program(code);previous=code
            for i,(xy,input_values,w) in enumerate(zip(((8,8),(56,8),(8,56)),row['inputs'],row['w']),1):
                ref.floats(STREAMS+i*32,*xy,10,w,*input_values)
            pixel=OBJECT+0x2800;ref.put(pixel,1,1,pixel+12,*pixel_state(row['blend']))
            ref.call('D3DDevice_SetPixelShader',[pixel])
            states={92:row['enabled'],93:row['mode'],94:bits(1),95:bits(3),
                96:bits(row['density']),97:0,138:0xa7123456}
            for state,value in states.items():
                ref.call('D3DDevice_SetRenderStateNotInline',[state,value])
            before=count(ref.gpu,1)
            _,methods=ref.draw(1,state_dirty=None)
            ref.check();assert count(ref.gpu,1)==before+1 and count(ref.gpu,2)>0
            lane=next(i for i in range(4) if row['mask']&(8>>i))
            raw=[f32(v[lane]) for v in row['inputs']]
            observed=(ctypes.c_float*3)();assert fog(ref.gpu,observed,3)==3
            assert all(abs(a-b)<1e-6 for a,b in zip(observed,raw)),(row,list(observed),raw)
            # Perspective weights at the covered sample center (16.5,16.5).
            b=8.5/48;weights=[(1-2*b)/row['w'][0],b/row['w'][1],b/row['w'][2]]
            expected_factor=sum(q*factor(row['mode'],row['enabled'],d,row['density']) for q,d in zip(weights,raw))/sum(weights)
            expected_factor=max(0,min(1,expected_factor))
            colors=[18/255,52/255,86/255];diffuse=[.8,.2,.1]
            expected=[expected_factor*d+(1-expected_factor)*c if row['blend'] else c for d,c in zip(diffuse,colors)]
            expected.append(.6 if row['blend'] else expected_factor)
            actual=ref.get(FRAME+(16*64+16)*4)[0]
            assert all(abs((actual>>shift&255)-round(v*255))<=1 for v,shift in zip(expected,(16,8,0,24))),(row,hex(actual),expected,methods)
            assert ref.get(FRAME)[0]==0xff102030
            all_pixels=pixel_expectations(row,raw);check_pixels(ref,all_pixels)
            if row['enabled']:
                assert methods.get(0x29c)=={1:0x800,2:0x801,3:0x2601}[row['mode']],methods
                params=[struct.unpack('<f',struct.pack('<I',methods[m]))[0] for m in (0x9c0,0x9c4,0x9c8)]
                multiplier=-.5 if row['mode']==3 else -row['density']/(2*(math.log(256) if row['mode']==1 else math.sqrt(math.log(256))))
                # Original XDK's logarithm constants are approximations.
                # This independent coefficient bound accompanies real 1-LSB
                # pixel assertions; exact constants are retained in the report.
                assert abs(params[0]-(2.5 if row['mode']==3 else 1.5))<1e-6 and abs(params[1]-multiplier)<2e-6 and params[2]==0,(row,params)
            else:params=None
            assert method_value(ref.gpu,0x2a8)==0xa7563412,hex(method_value(ref.gpu,0x2a8))
            # The second original draw reuses committed state and resources.
            _,_=ref.draw(1,state_dirty=None)
            assert ref.get(FRAME+(16*64+16)*4)[0]==actual
            check_pixels(ref,all_pixels)
            rows.append(dict(**row,code=code,raw=raw,expected=expected,coefficient_values=params,pixel=hex(actual)))
        ref.set_stream(0,0,0)
    finally:ref.close()
    return dict(xbe_sha256=XBE_SHA,cases=rows,original_draws=2*len(rows),interior_pixels=2*len(rows)*1128,
        scope='finite programmable fog, original preowned state/pixel/shader/stream submission; no hardware precision or fixed-function fog claim')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--native-cases',type=Path)
    args=parser.parse_args();handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    result=run(args);args.output.write_text(json.dumps(result,indent=2)+'\n')
    if args.native_cases:
        with args.native_cases.open('wb') as file:
            file.write(struct.pack('<4I',0x46474c58,1,len(result['cases']),0))
            for row in result['cases']:
                file.write(struct.pack('<4I',len(row['code'])//4,row['blend'],row['mode'],row['enabled']))
                file.write(struct.pack('<'+'I'*len(row['code']),*row['code']))
                file.write(struct.pack('<3f',1,3,row['density']))
                file.write(struct.pack('<12f',*(v for vector in row['inputs'] for v in vector)))
                file.write(struct.pack('<3f',*row['w']));file.write(struct.pack('<3f',*row['raw']))
                file.write(struct.pack('<4f',*row['expected']))
    print(f"PASS: {len(result['cases'])} independent fog cases / {result['original_draws']} original state/pixel/shader/stream draws")
