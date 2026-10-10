#!/usr/bin/env python3
"""Captured inputs to independent noise-disabled Blinx scene-fixture pixels.

Finite vertex fetch/shader math and raster-state expectations are generated
from retained input bytes, API numeric facts and declared fixture setup. Device
vertices/methods and after-images are observations only. This closes a bounded
software composition gate; enabled noise, native HLE pixels, exact silicon and
whole-title resource/startup lifetimes retain separate qualification gates.
"""
import argparse
from fractions import Fraction
import json
import math
from pathlib import Path
import struct

from native_scene_reference import read_capture,read_scene
from native_scene_shader_reference import audit as shader_audit
from native_scene_pixel_reference import audit as pixel_audit,binary32


STATE_SOURCE='https://github.com/XboxDev/nxdk/blob/master/lib/pbkit/nv_regs.h'
COMBINER_SOURCE='https://registry.khronos.org/OpenGL/extensions/NV/NV_register_combiners.txt'
PACKED_PROFILES={
    'firstworld':[0,0,0xff000000,0x05000000]+[0]*12,
    'frame-firstworld':[0,0xff000000,0,0x39000000]+[0]*12,
}


def number(word):return Fraction(struct.unpack('<f',struct.pack('<I',word))[0])


def expected_state(capture,scene,packed_profile='firstworld'):
    """Closed API profile to methods, without any observed method input.

    Render indices follow the reviewed Blinx build (its noise slot is 159).
    nxdk publishes NV097 numeric fields; NVIDIA publishes combiner equations.
    Preowned linear BGRA/D24S8 targets, DMA contexts, texture address and initial
    disabled modes are declared by native_scene_draw_reference, not inferred
    from its pixels. Only the independently checked consumed profile is admitted.
    """
    r,t=capture['render'],capture['texture']
    assert capture['header'][2]==6 and scene['header'][3:6]==capture['header'][2:5]
    assert not scene['header'][8] and scene['textures'][0] and not any(scene['textures'][1:])
    width,height,fmt=scene['before'][0]['shape']
    assert fmt==87 and scene['before'][1]['shape'][:2]==[width,height]
    assert scene['before'][1]['shape'][2] in (44,45)
    assert capture['viewport']==[0,0,width,height,0,0x3f800000]
    assert scene['header'][9]==0x4b7fffff
    # The original viewport setter owns constants 58/59. Check its published
    # coordinate mapping and previously qualified XDK screen bias (17/32).
    constants=list(struct.iter_unpack('<4f',capture['constants']))
    assert list(constants[58])==[width/2,-height/2,16777215,0]
    assert list(constants[59])==[width/2+17/32,height/2+17/32,0,0]
    # Reject unmodelled API effects rather than treating them as disabled.
    required={57:0x201,59:1,60:0,62:0x302,63:0x303,64:1,65:0,66:0x1d01,
        67:0x10101,74:0x8006,77:0,78:0,79:0,80:0,81:0,82:1,83:0,
        92:1,93:1,96:0,97:0,102:0,103:1,139:0x1b02,140:0x1b02,
        141:0,146:0x900,143:1,144:0,149:0,150:0,151:0,154:0,
        163:0,164:0,165:0}
    assert all(r[k]==v for k,v in required.items()),'Unsupported captured render profile'
    assert r[147] in (0x900,0x901) and r[159] in (0,1)
    assert scene['packed']==PACKED_PROFILES[packed_profile],'Unsupported packed cache profile'
    # All scalar methods below carry their native API numeric value unchanged.
    direct={0x300:60,0x304:59,0x30c:143,0x310:65,0x314:102,0x32c:144,
        0x330:79,0x334:80,0x338:81,0x33c:58,0x340:61,0x344:62,0x348:63,
        0x34c:75,0x350:74,0x354:57,0x358:67,0x35c:64,0x360:73,0x364:70,
        0x368:71,0x36c:72,0x370:145,0x374:68,0x378:69,0x37c:66,
        0x38c:139,0x390:140,0x3a0:146,0x3b8:103,0x2a4:92}
    m={method:r[state] for method,state in direct.items()}
    # Original CullMode compares the requested winding with FrontFace, then
    # selects FRONT (equal) or BACK (different); no observed cull result is used.
    m[0x308]=1;m[0x39c]=0x404+int(r[147]!=r[146])
    m.update({0x200:width<<16,0x204:height<<16,0x208:0x128,
        0x20c:width*4*0x10001,0x210:0x600000,0x214:0x800000,
        0x2c0:(width-1)<<16,0x2e0:(height-1)<<16,0x394:0,0x398:scene['header'][9],
        0x318:0,0x31c:0,0x320:0,0x324:0,0x328:0,0x380:0})
    # Previously qualified EXP/planar setter contract, specialized to density0.
    # exp(-density*coordinate)==1, regardless of the independently computed fog.
    fog=r[138];m[0x2a8]=((fog&255)<<16)|(fog&0xff00)|((fog>>16)&255)
    m.update({0x29c:0x800,0x2a0:2,0x9c0:0x3fc00000,0x9c4:0x80000000,0x9c8:0})
    for stage in range(4):
        ts=t[stage*32:(stage+1)*32];base=0x1b00+stage*64
        assert ts[:3]==[1,1,1] and ts[3:5]==[2,2] and ts[5] in (0,2)
        assert ts[7:12]==[0,1,0,0,0] and ts[21]==0 and ts[28]==0
        if stage==0:
            # MODULATE(TEXTURE,DIFFUSE) for RGB and alpha; CURRENT result.
            assert [ts[i] for i in (12,14,15,16,18,19,20)]==[4,2,0,4,2,0,1]
        else:assert ts[12]==1,'Additional fixed-function stages are outside the fixture'
        m[base+8]=ts[0]|ts[1]<<8|ts[2]<<16
        m[base+12]=int(stage==0)<<30|4095<<6
        # Reviewed original sampler helper 0x13cbc0: (bias+device_bias)*256
        # +0.5, binary32 store, CVTTSS2SI, signed clamp, 13-bit mask.
        # Device bias is zero in this fixture. This intentionally differs from
        # rounding negatives to nearest; expected packing is instruction-derived.
        bias=number(ts[6]);assert abs(bias)<16
        packed=max(-4096,min(4095,math.trunc(binary32(bias*256+Fraction(1,2)))))&8191
        m[base+20]=(ts[3]<<24)|((6 if ts[5]==2 else ts[4])<<16)|(1<<13)|packed
    texture=scene['textures'][0];resource=texture['resource'];fmt=resource[3]
    assert (fmt>>8)&255==12 and not fmt&4 and (fmt>>4)&15==2
    assert fmt&3==1 and resource[4]==0
    assert len(texture['mips'])==(fmt>>16)&15
    for level,mip in enumerate(texture['mips']):
        assert mip['shape'][:2]==[max(1,(1<<((fmt>>20)&15))>>level),max(1,(1<<((fmt>>24)&15))>>level)]
    m[0x1b00]=0x3000000;m[0x1b04]=fmt
    # Construct the one MODULATE stage from register IDs and channel selection.
    # AB writes R0; final RGB=fog*(specular+R0)+(1-fog)*fog_color.
    # Independent shader assertions require specular0 and saturated source alpha.
    texture_reg,diffuse_reg,r0,fog_reg,sum_reg=8,4,12,3,14
    m[0xac0]=texture_reg<<24|diffuse_reg<<16
    m[0x260]=(texture_reg|16)<<24|(diffuse_reg|16)<<16
    m[0xaa0]=m[0x1e40]=r0<<8
    m[0x288]=(fog_reg|16)<<24|sum_reg<<16|fog_reg<<8
    m[0x28c]=(r0|16)<<8|128
    m[0x1e60]=1;m[0x1e70]=1  # one combiner, stage0 PROJECT2D
    return m


def audit(captures,diagnostics,number,arithmetic,allow_empty=False,chain=False,previous=None):
    capture=read_capture(captures/f'draw-{number}.bin');scene=read_scene(captures/f'scene-{number}.bin')
    m=expected_state(capture,scene)
    shader=shader_audit(captures,diagnostics,number)
    actual=json.loads((diagnostics/f'draw-{number}.aot-private.json').read_text())
    if chain:
        assert actual['retained_targets'],'Expected a retained-target observation'
        before={kind:(diagnostics/f'draw-{number}-before-{kind}.bin').read_bytes() for kind in ('color','depth')}
        expected_before=previous if previous is not None else {'color':scene['before'][0]['data'],
            'depth':b''.join(struct.pack('<I',((v&0xffffff)<<8)|(v>>24)) for (v,) in struct.iter_unpack('<I',scene['before'][1]['data']))}
        assert all(before[k]==expected_before[k] for k in before),'Broken LLE retained-target sequence'
        scene['before'][0]['data']=before['color']
        scene['before'][1]['data']=b''.join(struct.pack('<I',((v>>8)&0xffffff)|((v&255)<<24)) for (v,) in struct.iter_unpack('<I',before['depth']))
    observed={int(k,16):v for k,v in actual['methods'].items()}
    failures=[[hex(k),observed.get(k),v] for k,v in m.items() if observed.get(k)!=v]
    inputs={**actual,'methods':{hex(k):v for k,v in m.items()},
        'vertices':shader['expected_vertices'],'fog':shader['expected_fog']}
    pixels=pixel_audit(captures,diagnostics,number,arithmetic,expected_inputs=inputs,allow_empty=allow_empty,scene_inputs=scene)
    return dict(**pixels,shader_compared_lanes=shader['compared_lanes'],
        shader_failure_count=shader['failure_count'],shader_first_failures=shader['first_failures'],
        state_methods_compared=len(m),state_failure_count=len(failures),state_first_failures=failures[:12],
        captured_input_to_fixture_pixel_qualified=bool(not failures and shader['shader_input_to_output_qualified'] and pixels['post_transform_pixel_qualified']),
        original_noise_enabled_scene_qualified=False,native_hle_scene_qualified=False,
        captured_noise_value=capture['render'][159],fixture_noise_value=0,
        state_numeric_source=STATE_SOURCE,combiner_equation_source=COMBINER_SOURCE)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',default=[2,3,4,5,6])
    parser.add_argument('--depth-arithmetic',required=True,choices=('separate','fma-b-first'))
    parser.add_argument('--include-noop',action='store_true',help='Also admit target-preservation draws; aggregate must still contain exercised writes')
    parser.add_argument('--retain-targets',action='store_true',help='Qualify the uninterrupted sequence, requiring each actual before-target to equal its qualified predecessor')
    args=parser.parse_args()
    rows=[];previous=None
    for n in args.draws:
        row=audit(args.captures,args.diagnostics,n,args.depth_arithmetic,allow_empty=args.include_noop,
            chain=args.retain_targets,previous=previous);rows.append(row)
        if args.retain_targets:
            assert row['captured_input_to_fixture_pixel_qualified'],'A failed prefix cannot qualify a later chained draw'
            previous={kind:(args.diagnostics/f'draw-{n}-{kind}.bin').read_bytes() for kind in ('color','depth')}
    assert sum(r['expected_written_pixels'] for r in rows),'All-noop inputs cannot qualify an exercised scene corpus'
    args.output.write_text(json.dumps(dict(draws=rows,retained_targets=args.retain_targets,
        scope='Independent captured-input to noise-disabled software fixture pixels; original enabled-noise, native HLE and silicon gates remain separate'),indent=2)+'\n')
    for row in rows:print(json.dumps(row),flush=True)
    raise SystemExit(0 if rows and all(r['captured_input_to_fixture_pixel_qualified'] for r in rows) else 1)
