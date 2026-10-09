#!/usr/bin/env python3
"""Replay retained Blinx scene declarations/programs through original submission.

The v1 captures contain real native raw shader output, not full texture/frame
inputs. This checks composition of independently qualified shader/fetch pieces;
it does not label the raw-output comparison as a hardware golden or full-scene
pixel qualification. Original allocation/startup remain outside the preowned
resource fixture.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import struct

from native_vertex_fetch_reference import Reference, XBE_SHA, OBJECT, STREAMS
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX


def read_capture(path):
    blob=path.read_bytes();cursor=0
    def words(count):
        nonlocal cursor
        result=list(struct.unpack_from('<'+'I'*count,blob,cursor));cursor+=count*4
        return result
    header=words(8)
    assert header[:2]==[0x44534c58,1] and 0<header[4]<=65536 and 0<header[5]<=136
    viewport=words(6);render=words(166);texture=words(128)
    elements=[words(3) for _ in range(16)]
    constants=blob[cursor:cursor+192*16];cursor+=192*16
    code=words(header[5]*4)
    metadata=[words(2) for _ in range(16)]
    streams=[]
    for stride,size in metadata:
        assert (not size) or (0<stride<=2048 and size==header[4]*stride)
        streams.append(blob[cursor:cursor+size]);cursor+=size
    outputs=list(struct.unpack_from('<'+'f'*(header[4]*52),blob,cursor));cursor+=header[4]*52*4
    assert cursor==len(blob)
    return dict(header=header,viewport=viewport,render=render,texture=texture,
        elements=elements,constants=constants,code=code,metadata=metadata,
        streams=streams,outputs=outputs,sha256=hashlib.sha256(blob).hexdigest())


def written_masks(code):
    # Decode just published destination fields, independently of the shared
    # decoder. Unwritten lanes/defaults cannot establish matching correctness.
    masks=[0]*13
    for first in range(0,len(code),4):
        word1,word3=code[first+1],code[first+3]
        if word3&(1<<11):
            reg=(word3>>3)&255
            if reg<13: masks[reg]|=(word3>>12)&15
        if ((word3>>20)&15)==12:
            if (word1>>21)&15: masks[0]|=(word3>>24)&15
            if (word1>>25)&7 and not ((word1>>21)&15): masks[0]|=(word3>>16)&15
    return masks


def read_scene(path):
    """Read the actual native GPU companion without guessing absent inputs."""
    blob=path.read_bytes();cursor=0
    def take(size):
        nonlocal cursor
        assert 0<=size<=len(blob)-cursor,(path,cursor,size,len(blob))
        result=blob[cursor:cursor+size];cursor+=size
        return result
    def words(count):return list(struct.unpack('<'+'I'*count,take(count*4)))
    def target():
        shape=words(3)
        assert shape[:2]==[0,0] or (0<shape[0]<=4096 and 0<shape[1]<=4096)
        return dict(shape=shape,data=take(shape[0]*shape[1]*4))
    header=words(10);assert header[:2]==[0x534c4c58,1]
    color_header=words(5);depth_header=words(5);pixel=words(60);packed=words(16)
    before=[target(),target()];textures=[]
    for stage in range(4):
        info=words(10);assert info[0]==stage
        if not info[1]:textures.append(None);continue
        assert 0<info[4]<=13 and info[8]<=32*1024*1024
        resource=words(5);cpu=take(info[8]);mips=[]
        for level in range(info[4]):
            shape=words(6);assert shape[4]==shape[2]*shape[3]
            mips.append(dict(shape=shape,data=take(shape[4])))
        textures.append(dict(info=info,resource=resource,cpu=cpu,mips=mips))
    after=[target(),target()];assert cursor==len(blob),(path,cursor,len(blob))
    return dict(header=header,color_header=color_header,depth_header=depth_header,
        pixel=pixel,packed=packed,before=before,textures=textures,after=after,
        sha256=hashlib.sha256(blob).hexdigest())


def run(args):
    ref=Reference(args);rows=[]
    observe=ref.lib.xlle_nv2a_reference_vertices
    observe.restype=ctypes.c_uint32
    observe.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_float),ctypes.c_uint32]
    observe_fog=ref.lib.xlle_nv2a_reference_fog
    observe_fog.restype=ctypes.c_uint32
    observe_fog.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_float),ctypes.c_uint32]
    captures=[(path,read_capture(path)) for path in sorted(args.captures.glob('draw-*.bin'),key=lambda p:int(p.stem.split('-')[1]))]
    addresses=[];cursor=STREAMS
    for stream in range(16):
        addresses.append(cursor)
        extent=max([(c['header'][3]+c['header'][4])*c['metadata'][stream][0] for _,c in captures]+[16])
        cursor+=(extent+0xffff)&~0xffff
    assert cursor<=0x3000000,'Captured stream ranges exceed the bounded scene arena'
    try:
        for stream in range(16): ref.resource(OBJECT+0x1000+stream*16,addresses[stream])
        for path,capture in captures:
            h=capture['header']
            ref.put(ref.device+0xa90,*capture['viewport'][:4])
            ref.cpu.mem_write(ref.device+0xaa0,struct.pack('<2I',*capture['viewport'][4:]))
            for stream,(stride,size) in enumerate(capture['metadata']):
                if not size: continue
                address=addresses[stream]
                ref.set_stream(stream,OBJECT+0x1000+stream*16,stride)
                ref.cpu.mem_write(address,bytes([0xcc])*((h[3]+h[4])*stride))
                ref.cpu.mem_write(address+h[3]*stride,capture['streams'][stream])
            ref.put(OBJECT,1,0x10,0,0,0)
            for slot,(fmt,stream,offset) in enumerate(capture['elements']):
                ref.put(OBJECT+0x14+slot*16,stream,offset,fmt,0)
            ref.bind_program(capture['code'])
            data=OBJECT+0x3000;ref.cpu.mem_write(data,capture['constants'])
            ref.call('D3DDevice_SetVertexShaderConstantNotInline',[192*4],
                ((UC_X86_REG_ECX,0),(UC_X86_REG_EDX,data)))
            _,methods=ref.draw(h[3],h[2],h[4])
            ref.check()
            count=observe(ref.gpu,None,0);assert count==h[4],(path,count,h[4])
            actual=(ctypes.c_float*(count*28))()
            assert observe(ref.gpu,actual,count)==count;ref.check()
            fog_values=(ctypes.c_float*count)()
            assert observe_fog(ref.gpu,fog_values,count)==count;ref.check()
            masks=written_masks(capture['code']);compared=0;maximum=0
            for vertex in range(count):
                for group,reg in enumerate((0,3,4,9,10,11,12)):
                    for lane in range(4):
                        if not masks[reg]&(8>>lane):continue
                        got=actual[vertex*28+group*4+lane]
                        want=capture['outputs'][vertex*52+reg*4+lane]
                        if reg==0 and lane<2:
                            # Observation is after raster preparation. Compare
                            # to the independently expressed four-bit truncation,
                            # not a guessed half-unit raw-output allowance.
                            want=math.trunc(want*16)/16
                        assert math.isfinite(got) and math.isfinite(want),(path,vertex,reg,lane,got,want)
                        tolerance=2e-4*max(1,abs(want))
                        error=abs(got-want)
                        assert error<=tolerance,(path,vertex,reg,lane,got,want,error,tolerance)
                        maximum=max(maximum,error/max(1,abs(want)));compared+=1
                if masks[5]:
                    got=fog_values[vertex];want=capture['outputs'][vertex*52+20]
                    assert math.isfinite(got) and math.isfinite(want),(path,vertex,'fog',got,want)
                    error=abs(got-want);assert error<=2e-4*max(1,abs(want)),(path,vertex,'fog',got,want)
                    maximum=max(maximum,error/max(1,abs(want)));compared+=1
            for slot,(fmt,stream,offset) in enumerate(capture['elements']):
                if fmt==2:continue
                assert methods[0x1760+slot*4]==(capture['metadata'][stream][0]<<8)|fmt
                assert methods[0x1720+slot*4]==addresses[stream]+offset
            assert methods[0x1810]==((h[4]-1)<<24)|h[3]
            rows.append(dict(capture=path.name,sha256=capture['sha256'],primitive=h[2],first=h[3],
                vertices=count,instructions=h[5],written_masks=masks,compared_lanes=compared,
                maximum_scaled_error=maximum))
        for stream in range(16):
            if ref.get(ref.stream_table+stream*12+8)[0]:ref.set_stream(stream,0,0)
        assert all(ref.get(header)[0]==0x01000001 for header in ref.resources)
    finally:ref.close()
    assert rows
    return dict(xbe_sha256=XBE_SHA,captures=str(args.captures),draws=rows,
        scope='retained written shader/declaration/stream composition through original submission; raw vertex capture alone does not qualify complete pixels')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output','captures'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    dependency_handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    result=run(args)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(f"PASS: {len(result['draws'])} complete original-submission/LLE scene shader draws, "
          f"{sum(row['compared_lanes'] for row in result['draws'])} observed written lanes")
