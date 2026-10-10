#!/usr/bin/env python3
"""Original target/clear components on retained, preowned linear storage.

Only the root bind, three captured binds and two captured clears run; intervening
draws are omitted. Clear viewport is an explicit full-target fixture because
the compact clear record does not carry the live viewport. This is not whole
frame continuity, pixels, original allocation/startup or lifetime qualification.
Expected bytes/fields use input roots and public NV097 fields, never native
after-images, emitted packets, maintained renderer or target-binding compiler.
"""
import argparse
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct

from native_frame_inputs import Reader,audit as input_audit,require
from native_vertex_fetch_reference import Reference,OBJECT,FRAME,XBE_SHA

FIELD_SOURCE='https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h'
CLEAR_SOURCE='https://learn.microsoft.com/en-us/windows/win32/direct3d9/viewports-and-clipping'


def layout(target):
    r=target['resource'];w=target['width'];h=target['height'];role=target['role']
    require(role in (0,1) and len(r)==5 and 0<w<=4096 and 0<h<=4096,'Incomplete target')
    require(target['format'] in ((44,45) if role else (28,87)),'Unmodelled input target encoding')
    require(((r[3]>>8)&255)==(0x2e if role else 0x12) and r[4]!=0,
            'Only explicit linear ARGB/D24S8 target headers admitted')
    require((r[4]&4095)+1==w and ((r[4]>>12)&4095)+1==h,'Header and target dimensions differ')
    pitch=((r[4]>>24)+1)*64
    require(pitch>=w*4,'Insufficient target pitch')
    return w,h,pitch


def initial_storage(target,data):
    w,h,pitch=layout(target);require(len(data)==w*h*4,'Incomplete supplied target bytes')
    result=bytearray(b'\xa5'*(pitch*h))
    for y in range(h):
        row=bytearray(data[y*w*4:(y+1)*w*4])
        if target['role']:
            # Native D24S8 readback has depth low24/stencil high8; NV097 stores
            # depth high24/stencil low8. This is a declared input conversion.
            row=bytearray(b''.join(struct.pack('<I',((v&0xffffff)<<8)|(v>>24))
                                  for (v,) in struct.iter_unpack('<I',row)))
        elif target['format']==28:row[0::4],row[2::4]=row[2::4],row[0::4]
        result[y*pitch:y*pitch+w*4]=row
    return bytes(result)


def target_words(targets,pair,storage):
    color,depth=(targets[n] for n in pair)
    require(color['role']==0 and depth['role']==1,'Wrong bound target roles')
    w,h,cp=layout(color);dw,dh,dp=layout(depth)
    require(dw>=w and dh>=h,'Depth storage smaller than bound color')
    return {0x200:w<<16,0x204:h<<16,0x208:0x128,0x20c:cp|(dp<<16),
            0x210:storage[pair[0]],0x214:storage[pair[1]],0x290:0x100001,
            0x30c:0,0x32c:0}


def clear_storage(targets,pair,args,before):
    require(len(args)==6 and all(type(a) is int and 0<=a<=0xffffffff for a in args),'Invalid clear arguments')
    count,rects,flags,color,z,stencil=args
    require(count==rects==0 and not flags&~0xf3,'Unmodelled rectangle or clear flags')
    w,h,_=layout(targets[pair[0]])
    require(layout(targets[pair[1]])[0]>=w and layout(targets[pair[1]])[1]>=h,'Clear exceeds depth storage')
    # Only exact endpoint depths are admitted; no silicon rounding assumption.
    require(not flags&1 or z in (0,0x3f800000),'Unmodelled nonendpoint clear depth')
    require(not flags&2 or stencil<=255,'Unmodelled stencil value')
    result=dict(before);methods={}
    if not flags:return result,methods
    depth_word=((0xffffff if z else 0)<<8) if flags&1 else 0
    methods={0x1d98:(w-1)<<16,0x1d9c:(h-1)<<16,0x1d8c:depth_word|(stencil if flags&2 else 0),
             0x1d90:color,0x1d94:flags}
    # Source flags name RGBA, while these linear bytes are BGRA.
    for n in pair:
        t=targets[n];_,_,pitch=layout(t);data=bytearray(before[n])
        if t['role']:
            mask=(0xffffff00 if flags&1 else 0)|(0xff if flags&2 else 0)
            if mask:
                value=methods[0x1d8c]&mask
                for y in range(h):
                    for x in range(w):
                        offset=y*pitch+x*4;old=struct.unpack_from('<I',data,offset)[0]
                        struct.pack_into('<I',data,offset,(old&~mask)|value)
        else:
            values=struct.pack('<I',color)
            for lane,bit in ((0,6),(1,5),(2,4),(3,7)):
                if flags&(1<<bit):
                    for y in range(h):data[y*pitch+lane:y*pitch+w*4:4]=bytes([values[lane]])*w
        result[n]=bytes(data)
    return result,methods


class Prepared:
    def __init__(self,targets,headers,storage,pair,args,before):
        self.targets=targets;self.pair=pair;self.headers=headers
        self.words=target_words(targets,pair,storage)
        self.viewport=[0,0,*layout(targets[pair[0]])[:2],0,0x3f800000]
        self.bytes=dict(before)
        self.submission='original D3DDevice_Clear' if args is not None else 'original D3DDevice_SetRenderTarget'
        if args is not None:
            self.bytes,clear=clear_storage(targets,pair,args,before);self.words.update(clear)
        require(set(self.bytes)==set(targets),'Missing retained target inputs')

    def qualify(self,actual,observed_bytes,library_sha=None,inputs_sha=None):
        require(actual['original_returned'] is True and actual['fifo_get']==actual['fifo_put'] and
                type(actual['fifo_get']) is int and type(actual['fifo_put']) is int and
                actual['guard'] is None,'Incomplete original target/clear execution')
        require(actual['full_target_viewport_fixture'] is True and actual['draws_omitted'] is True,
                'Wrong target component fixture')
        require(actual['xbe_sha256']==XBE_SHA,'Wrong original XBE')
        require(actual['submission']==self.submission,'Wrong original route')
        if library_sha:require(actual['library_sha256']==library_sha,'Wrong LLE build')
        if inputs_sha:require(actual['inputs_sha256']==inputs_sha,'Wrong captured inputs')
        failures=[]
        for m,w in self.words.items():
            got=actual['methods'].get(hex(m))
            if type(got) is not int or got!=w:failures.append(['method',hex(m),got,w])
        if actual['bound_headers']!=[self.headers[n] for n in self.pair]:failures.append(['bound_headers'])
        if actual['viewport']!=self.viewport:failures.append(['viewport'])
        require(set(observed_bytes)==set(self.bytes),'Missing retained target observations')
        for n,want in self.bytes.items():
            got=observed_bytes[n]
            if got!=want:failures.append(['target_bytes',n,len(got),len(want)])
        return dict(component_qualified=not failures,compared_words=len(self.words)+8,
                    compared_bytes=sum(len(b) for b in self.bytes.values()),failures=failures)


def placement(targets):
    require(0<len(targets)<=14,'Too many retained targets for the owned fixture')
    return ({n:OBJECT+0x4000+i*32 for i,n in enumerate(targets)},
            {n:FRAME+i*0x200000 for i,n in enumerate(targets)})


def controls(prepared,actual,images,library_sha,inputs_sha):
    rows=[]
    names=('baseline','wrong-method','missing-method','bool-method','wrong-bound','wrong-viewport',
           'missing-target','corrupt-target','pending-fifo','guarded','nonreturned','wrong-route',
           'wrong-build','wrong-inputs','wrong-fixture','wrong-xbe')
    for name in names:
        obs,data=copy.deepcopy((actual,images));method=next(iter(obs['methods']))
        if name=='wrong-method':obs['methods'][method]^=1
        elif name=='missing-method':obs['methods'].pop(method)
        elif name=='bool-method':obs['methods'][method]=True
        elif name=='wrong-bound':obs['bound_headers'][0]^=32
        elif name=='wrong-viewport':obs['viewport'][2]^=1
        elif name=='missing-target':data.pop(next(iter(data)))
        elif name=='corrupt-target':
            n=next(iter(data));data[n]=bytes([data[n][0]^1])+data[n][1:]
        elif name=='pending-fifo':obs['fifo_get']+=4
        elif name=='guarded':obs['guard']='deliberate guard'
        elif name=='nonreturned':obs['original_returned']=False
        elif name=='wrong-route':obs['submission']='substituted'
        elif name=='wrong-build':obs['library_sha256']='changed'
        elif name=='wrong-inputs':obs['inputs_sha256']='changed'
        elif name=='wrong-fixture':obs['draws_omitted']=False
        elif name=='wrong-xbe':obs['xbe_sha256']='changed'
        try:got=prepared.qualify(obs,data,library_sha,inputs_sha)['component_qualified']
        except (AssertionError,ValueError):got=False
        rows.append(dict(control=name,qualified=got,control_passed=got==(name=='baseline')))
    return rows


class TargetReference(Reference):
    def __init__(self,args,targets,initial,pair):
        super().__init__(args)
        read=self.lib.xlle_nv2a_reference_read
        read.restype=ctypes.c_uint32;read.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_int]
        self.read=read
        def rd(cpu,offset,size,data):return read(self.gpu,0xfd000000+offset,size)
        def wr(cpu,offset,size,value,data):self.lib.xlle_nv2a_reference_write(self.gpu,0xfd000000+offset,size,value)
        self.cpu.mmio_map(0xfd000000,0x1000000,rd,None,wr,None)
        require(self.calls['D3DDevice_SetRenderTarget']==0x131e90 and self.calls['D3DDevice_Clear']==0x135a60,
                'Binding does not select the reviewed original entry points')
        self.targets=targets;self.headers,self.storage=placement(targets);self.sizes={}
        for n,t in targets.items():
            header=self.headers[n];address=self.storage[n]
            require(len(initial[n])<=0x200000 and address+len(initial[n])<OBJECT,'Target storage overlaps another fixture')
            self.sizes[n]=len(initial[n])
            words=list(t['resource']);words[0]=(words[0]&~0xf80000)|2;words[1]=address;words[2]=0
            # Retained owner references avoid allocation/deletion; their
            # counts and original resource lifetime are not qualified here.
            self.put(header,*words);self.cpu.mem_write(address,initial[n])
        for offset,n in ((0x21b4,pair[0]),(0x21b8,pair[1]),(0x21c0,pair[0]),(0x21c4,pair[0])):
            self.put(self.device+offset,self.headers[n])
        self.floats(self.device+0x518,1,1,1);self.floats(self.device+0x528,1,1)
        self.put(self.device+0xa90,0,0,*layout(targets[pair[0]])[:2])
        self.put(self.device+0xaa0,0,0x3f800000)

    def observe(self,words,submission):
        method=self.lib.xlle_nv2a_reference_method_value
        method.restype=ctypes.c_uint32;method.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
        raw=self.lib.xlle_nv2a_reference_error(self.gpu)
        return dict(original_returned=True,fifo_get=self.read(self.gpu,0xfd800044,4),fifo_put=self.get(self.device)[0],
                    guard=raw.decode() if raw else None,methods={hex(m):method(self.gpu,m) for m in words},
                    bound_headers=self.get(self.device+0x21b4,2),viewport=self.get(self.device+0xa90,6),
                    xbe_sha256=XBE_SHA,submission=submission,full_target_viewport_fixture=True,draws_omitted=True)

    def images(self):return {n:bytes(self.cpu.mem_read(a,self.sizes[n])) for n,a in self.storage.items()}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output','xbe','binding','library','dependencies'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--audit-existing',action='store_true');parser.add_argument('--controls',action='store_true')
    args=parser.parse_args();handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    require(not args.controls or args.audit_existing,'Controls use retained actual observations')
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    initial={n:initial_storage(t,reader.asset_bytes(t['initial_asset'])) for n,t in reader.targets.items()}
    library_sha=hashlib.sha256(args.library.read_bytes()).hexdigest()
    ref=None if args.audit_existing else TargetReference(args,reader.targets,initial,reader.root)
    headers,storage=placement(reader.targets);expected=dict(initial);rows=[]
    args.diagnostics.mkdir(parents=True,exist_ok=True)
    try:
        for event in reader.events:
            if event['type'] not in ('root','bind','clear'):continue
            pair=event['targets'];clear=event['args'] if event['type']=='clear' else None
            prepared=Prepared(reader.targets,headers,storage,pair,clear,expected);stem=f"call-{event['call']}"
            if args.audit_existing:
                actual=json.loads((args.diagnostics/f'{stem}.aot-private.json').read_text())
                observed={n:(args.diagnostics/f'{stem}-target-{n}.aot-private.bin').read_bytes() for n in reader.targets}
            else:
                if clear is None:ref.call('D3DDevice_SetRenderTarget',[ref.headers[n] for n in pair])
                else:ref.call('D3DDevice_Clear',clear)
                ref.kick();actual=ref.observe(prepared.words,prepared.submission);observed=ref.images()
                actual.update(library_sha256=library_sha,inputs_sha256=coverage['inputs_sha256'])
                (args.diagnostics/f'{stem}.aot-private.json').write_text(json.dumps(actual,indent=2)+'\n')
                for n,data in observed.items():(args.diagnostics/f'{stem}-target-{n}.aot-private.bin').write_bytes(data)
            row=dict(call=event['call'],event=event['type'],**prepared.qualify(actual,observed,library_sha,coverage['inputs_sha256']))
            if args.controls:
                rows.extend(dict(call=event['call'],**r) for r in controls(prepared,actual,observed,library_sha,coverage['inputs_sha256']))
            else:rows.append(row)
            expected=prepared.bytes
            print(json.dumps(row),flush=True)
            require(row['component_qualified'],'Original target/clear component mismatch')
    finally:
        if ref:ref.close()
    passed=bool(rows and all(r.get('control_passed' if args.controls else 'component_qualified',False) for r in rows))
    report=dict(passed=passed,rows=rows,
        inputs_sha256=coverage['inputs_sha256'],library_sha256=library_sha,
        original_target_clear_components_qualified=passed and not args.controls,full_target_viewport_fixture=True,draws_omitted=True,
        compared_words=sum(r.get('compared_words',0) for r in rows),compared_bytes=sum(r.get('compared_bytes',0) for r in rows),
        complete_frame_target_continuity_qualified=False,complete_scene_pixel_qualified=False,
        resource_lifetime_integration_qualified=False,scope=__doc__)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{'PASS' if passed else 'FAIL'}: {len(rows)} target/clear {'controls' if args.controls else 'components'}")
    raise SystemExit(0 if passed else 1)
