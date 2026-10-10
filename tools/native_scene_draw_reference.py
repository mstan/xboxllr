#!/usr/bin/env python3
"""Audit complete owned scene inputs through original XDK state/draw routines.

Preowned resources and initial render targets are explicit fixture conditions;
allocation and whole-title startup are not replaced or claimed here. Reports
retain guarded draws as missing qualification, rather than treating no-op draws
as agreement with the native after-image.

The optional noise-disabled fixture calls the original setter with zero and
records the captured value separately. It does not qualify enabled-noise scenes.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct

from native_scene_reference import read_capture, read_scene
from native_vertex_fetch_reference import Reference, OBJECT, STREAMS, FRAME, XBE_SHA
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX


def target_bytes(target,depth=False):
    width,height,fmt=target['shape'];data=target['data']
    if not width:return data
    if depth:
        assert fmt in (44,45),fmt  # D3D11 D24/S8 has the opposite word placement.
        return b''.join(struct.pack('<I',((v<<8)&0xffffffff)|(v>>24))
            for (v,) in struct.iter_unpack('<I',data))
    assert fmt in (28,87),fmt
    if fmt==87:return data  # native BGRA bytes already encode Xbox ARGB words.
    result=bytearray(data);result[0::4],result[2::4]=data[2::4],data[0::4]
    return bytes(result)


def replay(args,path,reference=None,initialize_targets=True):
    state_only=getattr(args,'observe_shader_state_only',False)
    assert not (state_only and getattr(args,'retain_targets',False)), 'Target continuity requires target observations'
    capture=read_capture(path);scene=read_scene(path.with_name(path.name.replace('draw','scene')))
    assert scene['header'][3:6]==capture['header'][2:5]
    ref=Reference(args) if reference is None else reference
    read=ref.lib.xlle_nv2a_reference_read
    read.restype=ctypes.c_uint32;read.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int]
    mmio=[];ref.scene_mmio=mmio
    def mmio_read(cpu,offset,size,data):
        value=read(ref.gpu,0xfd000000+offset,size);ref.scene_mmio.append(['read',hex(offset),size,hex(value)])
        if ref.lib.xlle_nv2a_reference_error(ref.gpu):cpu.emu_stop()
        return value
    def mmio_write(cpu,offset,size,value,data):
        ref.scene_mmio.append(['write',hex(offset),size,hex(value)])
        ref.lib.xlle_nv2a_reference_write(ref.gpu,0xfd000000+offset,size,value)
        if ref.lib.xlle_nv2a_reference_error(ref.gpu):cpu.emu_stop()
    if not getattr(ref,'scene_mmio_mapped',False):
        ref.cpu.mmio_map(0xfd000000,0x1000000,mmio_read,None,mmio_write,None)
        ref.scene_mmio_mapped=True
    # Complete original graphics/software-method service, as used by the
    # already qualified callback fixture. This is service of a real device
    # notification; transport does not acknowledge it or fabricate a return.
    ref.calls['original_graphics_service']=0x13b950
    ref.put(ref.device+0x23c0,0xfd000000)
    software_calls=[];original_call=ref.call
    def observed_call(name,values,registers=()):
        begin=ref.get(ref.device)[0]
        try:result=original_call(name,values,registers)
        except AssertionError:
            ref.check()
            raise
        end=ref.get(ref.device)[0]
        if begin<=end:
            emitted=ref.get(begin,(end-begin)//4);offset=0
            while offset<len(emitted):
                command=emitted[offset];size=(command>>18)&0x7ff
                assert size and offset+size+1<=len(emitted),(name,hex(command))
                if command&0x1fff==0x100 and emitted[offset+1]:
                    software_calls.append([name,values,hex(emitted[offset+1])])
                offset+=size+1
        return result
    ref.call=observed_call
    count_fn=ref.lib.xlle_nv2a_reference_draw_count
    count_fn.restype=ctypes.c_uint64;count_fn.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
    name_fn=ref.lib.xlle_nv2a_reference_draw_count_name
    name_fn.restype=ctypes.c_char_p;name_fn.argtypes=[ctypes.c_uint32]
    initial_counts=[count_fn(ref.gpu,i) for i in range(26)]
    initial_put=ref.get(ref.device)[0]
    try:
        if not initialize_targets:
            # Recycle the fixture's preowned input buffers through real
            # unbind calls before reinitializing their resource headers.
            # The color/depth targets remain bound and are never rewritten.
            for stream in range(16):
                if ref.get(ref.stream_table+stream*12+8)[0]:ref.set_stream(stream,0,0)
            for stage in range(4):ref.call('D3DDevice_SetTexture',[stage,0])
        ref.emit(0x184,2);ref.emit(0x188,2)  # explicit preowned texture DMA contexts
        h=capture['header'];cursor=STREAMS
        for stream,(stride,size) in enumerate(capture['metadata']):
            if not size:continue
            extent=(h[3]+h[4])*stride;assert cursor+extent<0x3000000
            ref.resource(OBJECT+0x1000+stream*16,cursor)
            ref.set_stream(stream,OBJECT+0x1000+stream*16,stride)
            ref.cpu.mem_write(cursor,bytes([0xcc])*extent)
            ref.cpu.mem_write(cursor+h[3]*stride,capture['streams'][stream])
            cursor+=(extent+0xffff)&~0xffff
        ref.put(OBJECT,1,0x10,0,0,0)
        for slot,(fmt,stream,offset) in enumerate(capture['elements']):
            ref.put(OBJECT+0x14+slot*16,stream,offset,fmt,0)
        ref.bind_program(capture['code'])
        data=OBJECT+0x3000;ref.cpu.mem_write(data,capture['constants'])
        ref.call('D3DDevice_SetVertexShaderConstantNotInline',[192*4],
            ((UC_X86_REG_ECX,0),(UC_X86_REG_EDX,data)))

        width,height,_=scene['before'][0]['shape'];assert width and height
        color=target_bytes(scene['before'][0]);depth=target_bytes(scene['before'][1],True)
        if initialize_targets:
            ref.cpu.mem_write(FRAME,color)
            if depth:ref.cpu.mem_write(FRAME+0x200000,depth)
        color_header=OBJECT+0x1c00;depth_header=OBJECT+0x1c20
        ref.put(color_header,*scene['color_header']);ref.put(color_header+4,FRAME)
        ref.put(depth_header,*scene['depth_header']);ref.put(depth_header+4,FRAME+0x200000)
        # Honest preinitialized target metadata for the original viewport/state
        # callees; no function body or command emitter is intercepted.
        for offset,value in ((0x21b4,color_header),(0x21b8,depth_header if depth else 0),
                (0x21c0,color_header),(0x21c4,color_header),(0x510,scene['header'][9])):
            ref.put(ref.device+offset,value)
        # SetViewport consumes the preinitialized device's screen offset when
        # it writes reserved constants 58/59. Zeroing this field silently
        # changes the captured program inputs by the SDK's half-pixel bias.
        transform=ref.binding['transform_binding']
        ref.put(ref.device+transform['fields']['screen_offset'],
            transform['screen_bias'],transform['screen_bias'])
        for method,value in ((0x208,0x128),(0x20c,(width*4)*0x10001),
                (0x210,FRAME),(0x214,FRAME+0x200000),(0x200,width<<16),
                (0x204,height<<16),(0x2c0,(width-1)<<16),(0x2e0,(height-1)<<16)):
            ref.emit(method,value)
        ref.cpu.mem_write(0x141d88,struct.pack('<166I',*capture['render']))
        ref.cpu.mem_write(0x141b88,struct.pack('<128I',*capture['texture']))
        ref.cpu.mem_write(ref.device+0xb58,struct.pack('<16I',*scene['packed']))
        cursor=0x3000000
        for stage,texture in enumerate(scene['textures']):
            if texture is None:
                ref.call('D3DDevice_SetTexture',[stage,0]);continue
            # Readback verifies owned compressed CPU bytes are the live GPU
            # input. Other layouts/aliases require explicit reconstruction.
            assert texture['cpu'] and texture['cpu']==b''.join(m['data'] for m in texture['mips']),path
            header=OBJECT+0x2000+stage*32;resource=list(texture['resource'])
            resource[0]=(resource[0]&~0x00f80000)|1
            resource[1]=cursor;resource[2]=0
            assert cursor+len(texture['cpu'])<=len(ref.ram)
            ref.put(header,*resource);ref.cpu.mem_write(cursor,texture['cpu'])
            cursor+=(len(texture['cpu'])+0xffff)&~0xffff
            ref.call('D3DDevice_SetTexture',[stage,header])
        if scene['header'][8]:
            pixel=OBJECT+0x2800;ref.put(pixel,1,1,pixel+12,*scene['pixel'])
            ref.call('D3DDevice_SetPixelShader',[pixel])
        else:ref.call('D3DDevice_SetPixelShader',[0])
        # Real scalar setters and their deferred dirty paths consume the input
        # state. No handwritten reproduction of their packet layout is used.
        for state in range(57,166):
            if 84<=state<=91:continue  # repeated clear-value aliases do not affect a draw
            if state in (154,163,164,165):
                # These disabled auxiliary modes are initial fixture state.
                # Their setters emit separate software-control callbacks and
                # are not part of replaying this already initialized draw.
                assert capture['render'][state]==0,(path,state)
                continue
            value=0 if args.noise_disabled_fixture and state==159 else capture['render'][state]
            ref.call('D3DDevice_SetRenderStateNotInline',[state,value])
        if scene['header'][8]:
            for state in range(57):
                ref.call('D3DDevice_SetRenderStateNotInline',[state,capture['render'][state]])
        for stage in range(4):
            for state in range(30):
                ref.call('D3DDevice_SetTextureStageStateNotInline',[stage,state,capture['texture'][stage*32+state]])
        ref.put(data,*capture['viewport']);ref.call('D3DDevice_SetViewport',[data])
        before=None if state_only else [bytes(ref.cpu.mem_read(FRAME,len(color))),bytes(ref.cpu.mem_read(FRAME+0x200000,len(depth)))]
        guard=None;methods={};graph_services=0
        try:
            _,methods=ref.draw(h[3],h[2],h[4],clear=False,state_dirty=None);ref.check()
        except AssertionError:
            raw=ref.lib.xlle_nv2a_reference_error(ref.gpu)
            if not raw:raise
            guard=raw.decode()
        while not guard and read(ref.gpu,0xfd400100,4):
            assert graph_services<32,'Original graphics service made no bounded progress'
            try:ref.call('original_graphics_service',[],((UC_X86_REG_ECX,ref.device+0x23c0),))
            except AssertionError:
                raw=ref.lib.xlle_nv2a_reference_error(ref.gpu)
                if not raw:raise
                guard=raw.decode();break
            graph_services+=1
        census={name_fn(i).decode():count_fn(ref.gpu,i)-initial_counts[i] for i in range(26)}
        get=None if guard else read(ref.gpu,0xfd800044,4);put=ref.get(ref.device)[0]
        pending_words=ref.get(get,min(20,(put-get)//4)) if get is not None and get<=put else []
        if not guard:ref.check()
        method_value=ref.lib.xlle_nv2a_reference_method_value
        method_value.restype=ctypes.c_uint32;method_value.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
        dispatched={hex(i):hex(method_value(ref.gpu,i)) for i in (0x200,0x204,0x208,0x20c,0x210,0x214,0x290,0x294,0x2c0,0x2e0,0x3b8,0x1e60,0x1e70)}
        vertices=ref.lib.xlle_nv2a_reference_vertices
        vertices.restype=ctypes.c_uint32;vertices.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_float),ctypes.c_uint32]
        vertex_count=None if guard else vertices(ref.gpu,None,0)
        diagnostics=None
        if args.diagnostics and not guard:
            values=(ctypes.c_float*(vertex_count*28))()
            assert vertices(ref.gpu,values,vertex_count)==vertex_count
            fog=ref.lib.xlle_nv2a_reference_fog
            fog.restype=ctypes.c_uint32
            fog.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_float),ctypes.c_uint32]
            fog_values=(ctypes.c_float*vertex_count)()
            assert fog(ref.gpu,fog_values,vertex_count)==vertex_count
            # Device observations are diagnostic inputs, never an expected
            # shader/raster result. Keep them separate from qualification.
            diagnostics=dict(vertices=[list(values[i*28:(i+1)*28]) for i in range(vertex_count)],
                fog=list(fog_values),
                vertex_capture_sha256=capture['sha256'],scene_capture_sha256=scene['sha256'],
                library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),
                noise_disabled_fixture=args.noise_disabled_fixture,
                pixel_targets_observed=not state_only,
                retained_targets=getattr(args,'retain_targets',False),
                complete_draw_executed=bool(get==put and vertex_count==h[4] and census['executions'] and not any(list(census.values())[3:])),
                methods={hex(i):method_value(ref.gpu,i) for i in
                    list(range(0x200,0x400,4))+list(range(0x1b00,0x1c00,4))+
                    list(range(0xa60,0xae0,4))+list(range(0x1e40,0x1e80,4))+[0x9c0,0x9c4,0x9c8]})
        packets=ref.get(initial_put,(put-initial_put)//4);bindings=[];software_methods=[];offset=0
        while offset<len(packets):
            command=packets[offset];size=(command>>18)&0x7ff
            assert size and offset+size+1<=len(packets)
            if not command&0x1fff:bindings.append([hex(command),hex(packets[offset+1])])
            if command&0x1fff==0x100:software_methods.append([hex(command),hex(packets[offset+1])])
            offset+=size+1
        after=[] if state_only else [bytes(ref.cpu.mem_read(FRAME,len(color))),bytes(ref.cpu.mem_read(FRAME+0x200000,len(depth)))]
        if diagnostics is not None:
            args.diagnostics.mkdir(parents=True,exist_ok=True)
            for name,data in zip(('color','depth'),after):
                (args.diagnostics/f'{path.stem}-{name}.bin').write_bytes(data)
            if getattr(args,'retain_targets',False):
                for name,data in zip(('color','depth'),before):
                    (args.diagnostics/f'{path.stem}-before-{name}.bin').write_bytes(data)
            (args.diagnostics/f'{path.stem}.aot-private.json').write_text(json.dumps(diagnostics,indent=2)+'\n')
        differences=None if state_only else []
        if not state_only:
            for i in range(2):
                expected=target_bytes(scene['after'][i],bool(i))
                assert len(after[i])==len(expected)
                differences.append(sum(a!=b for a,b in zip(struct.iter_unpack('<I',after[i]),struct.iter_unpack('<I',expected))))
        return dict(capture=path.name,vertex_sha256=capture['sha256'],scene_sha256=scene['sha256'],
            fixture_render_overrides=[dict(state=159,captured_value=capture['render'][159],fixture_value=0,
                reason='noise-disabled offline scene fixture; enabled-noise pixels remain deferred')]
                if args.noise_disabled_fixture else [],
            census=census,color_depth_differing_words=differences,
            emitted_draw_methods={hex(k):hex(v) for k,v in methods.items()},
            fifo_get=hex(get) if get is not None else None,fifo_put=hex(put),pending_words=[hex(v) for v in pending_words],
            last_dispatched_parameters=dispatched,transformed_vertices=vertex_count,
            object_bindings=bindings,
            software_methods=software_methods,pgraph_irq=None if guard else [hex(read(ref.gpu,0xfd000000+a,4)) for a in (0x400100,0x400704,0x400708)],
            software_calls=software_calls,
            graph_services=graph_services,guard=guard,original_service_mmio=mmio,
            disabled_auxiliary_setters_not_replayed=[154,163,164,165],
            complete_draw_executed=bool(not guard and get==put and vertex_count==h[4] and census['executions'] and not any(list(census.values())[3:])),
            pixel_targets_observed=not state_only,
            pixel_qualified=False)  # independent pixel tolerance/oracles are still required
    finally:
        ref.call=original_call
        if reference is None:ref.close()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output','captures'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--limit',type=int,default=32)
    parser.add_argument('--draws',type=int,nargs='+',help='Replay only these retained draw numbers')
    parser.add_argument('--diagnostics',type=Path,help='Save actual target bytes and device observations; does not supply expected pixels')
    parser.add_argument('--noise-disabled-fixture',action='store_true',help='Explicitly set render state 159 to zero via the original setter; does not qualify enabled-noise scenes')
    parser.add_argument('--retain-targets',action='store_true',help='Execute the captured draw sequence on one device with targets initialized once')
    parser.add_argument('--observe-shader-state-only',action='store_true',
        help='Execute the complete draw but omit target files and native-after comparisons; shader/state observations do not qualify pixels')
    parser.add_argument('--require-qualified',action='store_true',help='Fail if any draw remains guarded or lacks independent pixel qualification')
    args=parser.parse_args();dependency_handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    rows=[]
    paths=sorted(args.captures.glob('draw-*.bin'),key=lambda p:int(p.stem.split('-')[1]))
    if args.draws:
        requested=set(args.draws)
        paths=[p for p in paths if int(p.stem.split('-')[1]) in requested]
        assert {int(p.stem.split('-')[1]) for p in paths}==requested,'Missing requested draw'
    shared=Reference(args) if args.retain_targets else None
    try:
        for index,path in enumerate(paths[:args.limit]):
            row=replay(args,path,shared,initialize_targets=not args.retain_targets or index==0)
            rows.append(row);print(json.dumps(row),flush=True)
    finally:
        if shared is not None:shared.close()
    assert rows
    args.output.write_text(json.dumps(dict(xbe_sha256=XBE_SHA,draws=rows,
        noise_disabled_fixture=args.noise_disabled_fixture,
        retained_targets=args.retain_targets,
        scope=('noise-disabled scene fixture: original setter 159 receives zero, captured enabled value is retained; original enabled scenes and pixel agreement remain unqualified'
            if args.noise_disabled_fixture else 'draw-state audit with captured initial targets and preowned resources; guards and mismatches remain unqualified')),indent=2)+'\n')
    qualified=sum(row['pixel_qualified'] for row in rows)
    print(f'AUDIT: {len(rows)} captured draws, {qualified} independently pixel-qualified',flush=True)
    if args.require_qualified and qualified!=len(rows):raise SystemExit(1)
