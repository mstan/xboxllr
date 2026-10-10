#!/usr/bin/env python3
"""Independent scalar/sampler, fixed-function and custom-pixel state words.

Expectations use API values and declared preowned fixture storage before any
device observation is inspected. Original target binding/startup, vertex state
and scene pixels remain separate gates.
"""
import argparse
import copy
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct

from native_frame_inputs import Reader,audit as input_audit,require
from native_frame_state_reference import Inputs,selected
from native_scene_pixel_reference import binary32
from native_vertex_fetch_reference import XBE_SHA
from native_frame_combiner_reference import expected_fixed_function

STATE_SOURCE='https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/nv_regs.h'
DIRECT={0x300:60,0x304:59,0x30c:143,0x310:65,0x314:102,0x32c:144,
    0x330:79,0x334:80,0x338:81,0x33c:58,0x340:61,0x344:62,0x348:63,
    0x34c:75,0x350:74,0x354:57,0x358:67,0x35c:64,0x360:73,0x364:70,
    0x368:71,0x36c:72,0x370:145,0x374:68,0x378:69,0x37c:66,
    0x38c:139,0x390:140,0x3a0:146,0x3b8:103,0x2a4:92}


def default_final(render):
    # Reviewed original helpers1365e0/13cdb0: SetPixelShader records whether
    # definition[8]|definition[9] specifies a final combiner. If absent, the
    # deferred fog helper builds the final RGB sum from R0 and optional specular,
    # then applies fog if enabled. NVIDIA's register IDs define these equations;
    # the alpha source is R0 and the color-sum clamp bit is set.
    result=12+2*bool(render[103])
    rgb=((3|16)<<24)|(result<<16)|(3<<8) if render[92] else result
    return rgb,((12|16)<<8)|128


def expected_delivery(capture,scene):
    r,t=capture['render'],capture['texture']
    require(len(r)==166 and len(t)==128,'Incomplete API state')
    require(scene['input_only'] and scene['after'] is None,'Expected input-only target roots')
    require(scene['header'][3:6]==capture['header'][2:5],'State belongs to another draw')
    required={77:0,78:0,82:1,83:0,93:1,96:0,97:0,102:0,103:1,
        139:0x1b02,140:0x1b02,141:0,149:0,151:0,154:0,163:0,164:0,165:0}
    require(all(r[k]==v for k,v in required.items()),'Unmodelled auxiliary/fog/polygon profile')
    require(r[146] in (0x900,0x901) and r[147] in (0,0x900,0x901),'Unmodelled face winding')
    require(r[92] in (0,1),'Unmodelled fog enable')
    require(r[150]==0 or 0x1500<=r[150]<=0x150f,'Unmodelled logic operation')
    require(r[159] in (0,1),'Unmodelled captured noise mode')
    width,height,fmt=scene['before'][0]['shape'];dw,dh,df=scene['before'][1]['shape']
    require(fmt in (28,87) and df in (44,45) and min(width,height,dw,dh)>0,'Unmodelled target roles')
    require(capture['viewport']==[0,0,width,height,0,0x3f800000] and
            scene['header'][9]==0x4b7fffff,'Unmodelled viewport/depth mapping')
    methods={m:r[state] for m,state in DIRECT.items()}
    methods.update({0x308:int(bool(r[147])),
        0x318:0,0x31c:0,0x320:0,0x324:0,0x328:0,0x380:0,
        0x394:0,0x398:scene['header'][9],0x17bc:int(bool(r[150]))})
    if r[150]:methods[0x17c0]=r[150]
    if r[147]:methods[0x39c]=0x404+int(r[147]!=r[146])
    fog=r[138]
    methods[0x2a8]=((fog&255)<<16)|(fog&0xff00)|((fog>>16)&255)
    # Original deferred fog setup does not consume mode/coefficient registers
    # when disabled. Inactive words cannot be advertised as delivered state.
    if r[92]:methods.update({0x29c:0x800,0x2a0:2,0x9c0:0x3fc00000,0x9c4:0x80000000,0x9c8:0})
    methods[0x288],methods[0x28c]=default_final(r)
    # These target words are explicit fixture setup, not original SetRenderTarget
    # output. Verify each role independently; smaller color does not shrink zeta.
    fixture={0x200:width<<16,0x204:height<<16,0x208:0x128,
        0x20c:(width*4)|((dw*4)<<16),0x210:0x600000,0x214:0x800000,
        0x2c0:(width-1)<<16,0x2e0:(height-1)<<16}
    plan=scene.get('frame_resource_plan')
    if plan is not None:
        from native_frame_storage import validate,target_layouts
        validate(plan,scene)
        fixture[0x210],fixture[0x214]=plan['target_addresses']
        layouts=target_layouts(scene);fixture[0x20c]=layouts[0][2]|(layouts[1][2]<<16)
    # Original LineWidth consumes the fixture's minimum multisample scale at
    # device+0x520. The older fresh draw fixture leaves it zero; original target
    # binding on unit-scale retained roots establishes one. This field is not
    # consumed by the admitted filled triangles/quads, but its delivered value
    # still follows the declared fixture and the active API float.
    line=struct.unpack('<f',struct.pack('<I',r[157]))[0]
    require(math.isfinite(line) and 0<=line<=64,'Unmodelled line width')
    methods[0x380]=min(511,math.trunc(binary32(Fraction(line)*(8 if plan is not None else 0)+Fraction(1,2))))
    cursor=0x3000000
    for stage,texture in enumerate(scene['textures']):
        ts=t[stage*32:(stage+1)*32];base=0x1b00+stage*64
        require(all(v in (1,3) for v in ts[:3]) and ts[3] in (1,2) and
            ts[4] in (1,2) and ts[5] in (0,1,2) and ts[7:12]==[0,1,0,0,0] and
            ts[21]==0 and ts[28:]==[0]*4,'Unmodelled sampler/coordinate profile')
        bias=struct.unpack('<f',struct.pack('<I',ts[6]))[0]
        require(math.isfinite(bias) and abs(bias)<16,'Unmodelled LOD bias')
        # Reviewed original helper: binary32((bias+zero_device_bias)*256+.5),
        # truncate toward zero, clamp signed twelve-bit magnitude, mask13bits.
        packed=max(-4096,min(4095,math.trunc(binary32(Fraction(bias)*256+Fraction(1,2)))))&8191
        min_filter=ts[4] if not ts[5] else ts[4]+2*ts[5]
        methods.update({base+8:ts[0]|ts[1]<<8|ts[2]<<16,
            base+12:(int(texture is not None)<<30)|(4095<<6),
            base+20:(ts[3]<<24)|(min_filter<<16)|(1<<13)|packed})
        if texture is None:continue  # Disabled resource headers are unconsumed.
        resource=texture['resource'];fmt=resource[3];code=(fmt>>8)&255
        require(fmt&3==1 and not fmt&4 and (fmt>>4)&15==2 and texture['cpu'],
                'Unmodelled texture DMA/dimension/storage')
        require(code in (4,6,7,12,14,15,0x12,0x1e),'Unmodelled texture format')
        methods[base]=cursor if plan is None else plan['texture_addresses'][stage];methods[base+4]=fmt
        if plan is None:cursor+=(len(texture['cpu'])+0xffff)&~0xffff
        if code in (0x12,0x1e):
            size=resource[4];w=(size&0xfff)+1;h=((size>>12)&0xfff)+1
            pitch=((size>>24)+1)*64
            require(pitch>=w*4 and (h-1)*pitch+w*4<=len(texture['cpu']),
                    'Incomplete pitch-linear texture storage')
            methods[base+16]=pitch<<16;methods[base+28]=(w<<16)|h
        else:
            require(resource[4]==0,'Unmodelled normalized texture size')
            # Original normalized bind does not clear the previous linear
            # pitch/rectangle words, and normalized sampling does not consume
            # them. The old fresh fixture explicitly has their reset zeros.
            if plan is None:fixture[base+16]=0;fixture[base+28]=0
    pixel={}
    if scene['header'][8]:
        require(scene['pixel'] is not None and len(scene['pixel'])==60,'Missing custom pixel definition')
        # The actual API render-state cache carries the mapped constant colors.
        # Replaying states0..56 explicitly submits those inputs after binding;
        # the definition's original constants cannot replace the current cache.
        for first,count,method in ((0,8,0x260),(10,8,0xa60),(18,8,0xa80),
                (26,8,0xaa0),(34,8,0xac0),(45,8,0x1e40)):
            pixel.update({method+4*i:r[first+i] for i in range(count)})
        pixel.update({0x288:r[8],0x28c:r[9],0x1e20:r[43],0x1e24:r[44],
                      0x1e60:r[53],0x1e70:r[54],0x1e78:r[56]})
        if not (scene['pixel'][8]|scene['pixel'][9]):
            pixel[0x288],pixel[0x28c]=default_final(r)
        require(r[42]==0 and r[55]==0,'Unmodelled clip/dot mapping')
        pixel[0x17f8]=0;pixel[0x1e74]=0
        require(all(((r[54]>>(stage*5))&31) in (0,1) for stage in range(4)) and
                r[54]&~0xfffff==0,'Unmodelled custom texture program')
    else:pixel=expected_fixed_function(capture,scene)
    return methods,fixture,pixel


def qualify(capture,scene,actual,library_sha=None):
    methods,fixture,pixel=expected_delivery(capture,scene)
    require(actual['vertex_capture_sha256']==capture['sha256'] and
            actual['scene_capture_sha256']==scene['sha256'],'Input provenance mismatch')
    require(actual.get('frame_resource_plan')==scene.get('frame_resource_plan'),'Frame placement provenance mismatch')
    require(actual['input_only_capture'] is True and actual['input_sequence']==capture['input_sequence'] and
            type(actual['input_sequence']) is int,'Wrong input observation')
    require(actual['xbe_sha256']==XBE_SHA,'Another original XBE')
    if library_sha:require(actual['library_sha256']==library_sha,'Another LLE build')
    require(actual['noise_disabled_fixture'] is True and actual['complete_draw_executed'] is True and
            actual['original_draw_returned'] is True and actual['pixel_targets_observed'] is False,
            'Not a complete noise-disabled input-only replay')
    require(type(actual['fifo_get']) is int and type(actual['fifo_put']) is int and
            actual['fifo_get']==actual['fifo_put'] and len(actual['vertices'])==capture['header'][4],
            'Incomplete original submission')
    route='original DrawVerticesUP' if capture['submission_kind']&1 else 'original DrawVertices'
    require(actual['submission']==route,'Wrong original submission route')
    counts=actual['draw_counters']
    require(len(counts)==26 and all(type(v) is int and v>=0 for v in counts) and
        not any(counts[3:]) and (counts[1]>0 or capture['header'][2]==8),'Guarded or missing draw')
    expected={**methods,**fixture,**pixel};observed=actual['methods']
    failures=[[hex(m),observed.get(hex(m)),v] for m,v in expected.items()
              if type(observed.get(hex(m))) is not int or observed.get(hex(m))!=v]
    custom=bool(scene['header'][8])
    return dict(input=capture['input_sequence'],api_words=len(methods),fixture_words=len(fixture),
        custom_pixel_words=len(pixel) if custom else 0,fixed_function_words=0 if custom else len(pixel),
        compared_words=len(expected),failure_count=len(failures),
        first_failures=failures[:12],state_word_components_qualified=not failures,
        fixed_function_combiner_delivery_qualified=not custom and not failures,
        custom_pixel_delivery_qualified=custom and not failures,captured_input_to_full_state_qualified=False,
        complete_scene_pixel_qualified=False,resource_lifetime_integration_qualified=False)


def controls(capture,scene,actual,library_sha):
    rows=[]
    names=['baseline','changed-render','changed-sampler','wrong-method',
                 'missing-method','bool-method','pending-fifo','guarded','wrong-route',
                 'wrong-build','wrong-xbe','ignored-native-output']
    bound=next((i for i,t in enumerate(scene['textures']) if t),None)
    if bound is not None:names.extend(('changed-format','ignored-sampled-gpu-bytes'))
    if scene['header'][8]:names.append('changed-pixel-constant')
    else:names.extend(('changed-fixed-color-arg','wrong-fixed-slot','wrong-inactive-slot',
                       'ignored-unused-arg0','ignored-inactive-stage'))
    for name in names:
        cap,sc,obs=copy.deepcopy((capture,scene,actual))
        if name=='changed-render':cap['render'][57]^=1
        elif name=='changed-sampler':cap['texture'][6]=0
        elif name=='changed-format':sc['textures'][bound]['resource'][3]^=0x100
        elif name=='changed-pixel-constant':cap['render'][10]^=1
        elif name=='changed-fixed-color-arg':cap['texture'][14]=0
        elif name=='wrong-fixed-slot':obs['methods']['0xac0']^=1
        elif name=='wrong-inactive-slot':obs['methods']['0xadc']=1
        elif name=='ignored-unused-arg0':cap['texture'][13]=3
        elif name=='ignored-inactive-stage':cap['texture'][3*32+12]=24
        elif name=='wrong-method':obs['methods']['0x354']^=1
        elif name=='missing-method':obs['methods'].pop('0x354')
        elif name=='bool-method':obs['methods']['0x354']=True
        elif name=='pending-fifo':obs['fifo_get']+=4
        elif name=='guarded':obs['draw_counters'][3]=1
        elif name=='wrong-route':obs['submission']='substituted draw'
        elif name=='wrong-build':obs['library_sha256']='changed'
        elif name=='wrong-xbe':obs['xbe_sha256']='changed'
        elif name=='ignored-native-output':cap['outputs']=[['changed observation']]
        elif name=='ignored-sampled-gpu-bytes':sc['textures'][bound]['mips'][0]['data']=b'changed GPU observation'
        # Refresh both identities even for changed input bytes: a stale hash
        # cannot be the only reason a semantic rejection control succeeds.
        cap['sha256']=obs['vertex_capture_sha256']='refreshed-control'
        sc['sha256']=obs['scene_capture_sha256']='refreshed-control'
        want=name in ('baseline','ignored-native-output','ignored-sampled-gpu-bytes',
                      'ignored-unused-arg0','ignored-inactive-stage')
        try:got=qualify(cap,sc,obs,library_sha)['state_word_components_qualified']
        except (ValueError,AssertionError):got=False
        rows.append(dict(input=cap['input_sequence'],control=name,expected_qualified=want,
                         qualified=got,control_passed=got==want))
    return rows


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output','library'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+');parser.add_argument('--all-draws',action='store_true')
    parser.add_argument('--controls',action='store_true');args=parser.parse_args()
    require(not(args.draws and args.all_draws),'Choose numbers or complete frame')
    coverage=input_audit(args.captures/'frame.jsonl');reader=Reader(args.captures/'frame-inputs.bin').read()
    inputs=Inputs(reader);numbers=selected(reader,args);rows=[]
    require(numbers and len(set(numbers))==len(numbers) and all(type(n) is int and 0<n<=len(reader.draws) for n in numbers),'Bad selection')
    library_sha=hashlib.sha256(args.library.read_bytes()).hexdigest()
    for n in numbers:
        cap,scene=inputs.draw(n)
        try:
            # Construct the independent expected words before opening observations.
            expected_delivery(cap,scene)
            actual=json.loads((args.diagnostics/f'input-{n}.aot-private.json').read_text())
            if args.controls:rows.extend(controls(cap,scene,actual,library_sha))
            else:rows.append(qualify(cap,scene,actual,library_sha))
        except (ValueError,AssertionError,KeyError,IndexError) as error:
            rows.append(dict(input=n,state_word_components_qualified=False,guard=str(error)))
    key='control_passed' if args.controls else 'state_word_components_qualified'
    passed=bool(rows and all(r.get(key,False) for r in rows))
    report=dict(inputs_sha256=coverage['inputs_sha256'],library_sha256=library_sha,xbe_sha256=XBE_SHA,
        rows=rows,selected_draws=len(numbers),passed=passed,
        compared_words=sum(r.get('compared_words',0) for r in rows),
        captured_input_to_full_state_qualified=False,complete_scene_pixel_qualified=False,
        original_target_binding_qualified=False,resource_lifetime_integration_qualified=False,
        numeric_source=STATE_SOURCE,scope=__doc__)
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(f"{'PASS' if passed else 'FAIL'}: {len(rows)} {'controls' if args.controls else 'state component draws'}, {report['compared_words']} words")
    for row in rows:
        if not row.get(key,False):print(json.dumps(row))
    raise SystemExit(0 if passed else 1)
