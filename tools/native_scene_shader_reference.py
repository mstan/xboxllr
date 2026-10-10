#!/usr/bin/env python3
"""Independent retained-scene vertex fetch and finite shader math.

Kelvin fields are read from the published 92-bit instruction layout as one
integer, not through the maintained disassembler. Captured stream bytes and
constant inputs supply all values. Observed native/LLE vertices are compared
only AFTER evaluating expectations. This fixture does not qualify unimplemented
opcodes, indexed constants, nonfinite math or exact silicon approximations.
"""
import argparse
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import struct

from native_scene_reference import read_capture
from native_scene_pixel_reference import binary32


ISA_SOURCE='https://envytools.readthedocs.io/en/latest/hw/graph/xf/isa.html'


def field(instruction, start, count):
    return (instruction>>start)&((1<<count)-1)


def fetch(capture, vertex):
    inputs=[[Fraction(0),Fraction(0),Fraction(0),Fraction(1)] for _ in range(16)]
    for slot,(fmt,stream,offset) in enumerate(capture['elements']):
        if fmt==2:continue
        stride,size=capture['metadata'][stream];data=capture['streams'][stream]
        start=vertex*stride+offset
        size=4 if fmt in (0x16,0x40,0x25) else 12 if fmt==0x32 else 0
        assert size and offset+size<=stride and start+size<=len(data),(slot,fmt)
        if fmt==0x32:
            values=list(struct.unpack_from('<3f',data,start))+[1]
        elif fmt==0x25:
            values=list(struct.unpack_from('<2h',data,start))+[0,1]
        else:
            word=struct.unpack_from('<I',data,start)[0]
            if fmt==0x40:values=[Fraction((word>>shift)&255,255) for shift in (16,8,0,24)]
            else:
                values=[]
                for shift,bits in ((0,11),(11,11),(22,10)):
                    value=(word>>shift)&((1<<bits)-1)
                    if value&(1<<(bits-1)):value-=1<<bits
                    values.append(max(Fraction(-1),Fraction(value,(1<<(bits-1))-1)))
                values.append(1)
        assert all(math.isfinite(v) for v in values)
        inputs[slot]=[binary32(v) for v in values]
    return inputs


def sqrt32(value):
    """Prove nearest/even sqrt rounding with exact midpoint squares.

    Host sqrt supplies a candidate only; exact rational comparisons establish
    that it owns the rounding interval. No maintained reciprocal/root routine.
    """
    assert value>0
    candidate=struct.unpack('<I',struct.pack('<f',math.sqrt(float(value))))[0]
    def number(bits):return Fraction(struct.unpack('<f',struct.pack('<I',bits))[0])
    for _ in range(4):
        root=number(candidate)
        lo=(number(candidate-1)+root)/2;hi=(root+number(candidate+1))/2
        if value<lo*lo or (value==lo*lo and candidate%2):candidate-=1
        elif value>hi*hi or (value==hi*hi and candidate%2):candidate+=1
        else:return root
    raise AssertionError('No proven binary32 sqrt interval')


def vector_op(op, a, b, c):
    if not op:return None
    if op==1:return a
    if op==3:return [binary32(x+y) for x,y in zip(a,c)]
    if op in (9,10):return [(min if op==9 else max)(x,y) for x,y in zip(a,b)]
    assert op in (2,4,5,6,7),op
    products=[binary32(x*y) for x,y in zip(a,b)]
    if op==2:return products
    if op==4:return [binary32(p+z) for p,z in zip(products,c)]
    terms=products[:3] if op in (5,6) else products
    if op==6:terms.append(b[3])
    result=terms[0]
    for term in terms[1:]:result=binary32(result+term)
    return [result]*4


def scalar_op(op, c):
    if not op:return None
    if op==1:return c
    assert op in (2,3,4) and c[0],(op,c)
    value=binary32(1/sqrt32(abs(c[0]))) if op==4 else binary32(1/c[0])
    if op==3:
        low=Fraction(1,2**64);high=Fraction(2**64)
        value=max(low,min(high,value)) if value>0 else max(-high,min(-low,value))
    return [value]*4


def evaluate(capture, vertex):
    inputs=fetch(capture,vertex)
    constants=[list(map(Fraction,v)) for v in struct.iter_unpack('<4f',capture['constants'])]
    assert all(math.isfinite(v) for vector in constants for v in vector)
    temps=[[Fraction(0)]*4 for _ in range(12)]
    outputs=[[Fraction(0),Fraction(0),Fraction(0),Fraction(1)] for _ in range(13)]
    masks=[0]*13
    def store(index,mask,value):
        assert value is not None and 0<=index<13
        masks[index]|=mask
        if index==5 and mask:
            outputs[5][0]=value[next(l for l in range(4) if mask&(8>>l))]
            return
        for lane in range(4):
            if mask&(8>>lane):outputs[index][lane]=value[lane]
    def temporary(index,mask,value):
        if not mask:return
        assert value is not None and index<=12
        if index==12:store(0,mask,value);return
        for lane in range(4):
            if mask&(8>>lane):temps[index][lane]=value[lane]
    code=capture['code']
    for pc in range(0,len(code),4):
        w0,w1,w2,w3=code[pc:pc+4]
        instruction=(w1<<64)|(w2<<32)|w3
        assert not w0 and instruction<1<<92 and not field(instruction,1,1)
        mac=field(instruction,85,4);ilu=field(instruction,89,3)
        assert mac in (0,1,2,3,4,5,6,7,9,10) and ilu in (0,1,2,3,4)
        def source(slot):
            packed=field(instruction,(58,43,28)[slot],15)
            mux=packed&3;index=field(packed,2,4)
            if mux==1:base=outputs[0] if index==12 else temps[index]
            elif mux==2:base=inputs[field(instruction,73,4)]
            elif mux==3:base=constants[field(instruction,77,8)]
            else:raise AssertionError(('Unsupported source mux',pc,slot,mux))
            negate=-1 if packed&(1<<14) else 1
            return [negate*base[field(packed,shift,2)] for shift in (12,10,8,6)]
        # Snapshot both units' operands before either writes its destination.
        a=source(0) if mac else None
        b=source(1) if mac in (2,4,5,6,7,9,10) else None
        c=source(2) if ilu or mac in (3,4) else None
        vector=vector_op(mac,a,b,c);scalar=scalar_op(ilu,c)
        destination=field(instruction,20,4)
        if mac and not (ilu and destination==1):temporary(destination,field(instruction,24,4),vector)
        if ilu:temporary(1 if mac else destination,field(instruction,16,4),scalar)
        mask=field(instruction,12,4)
        if mask:
            assert field(instruction,11,1),'Context writes are outside the fixture'
            store(field(instruction,3,8),mask,scalar if field(instruction,2,1) else vector)
        if field(instruction,0,1):
            assert pc+4==len(code),'Instructions after END are outside the fixture'
            break
    else:raise AssertionError('Missing END')
    return outputs,masks


def expectation(capture):
    """Generate every expected vertex from inputs, before reading observations."""
    expected_vertices=[];expected_fog=[];raw_outputs=[]
    for vertex in range(capture['header'][4]):
        outputs,masks=evaluate(capture,vertex)
        expected=[]
        for reg in (0,3,4,9,10,11,12):
            for lane in range(4):
                want=float(outputs[reg][lane])
                if reg==0 and lane<2:want=math.trunc(want*16)/16
                expected.append(want)
        expected_vertices.append(expected);expected_fog.append(float(outputs[5][0]))
        raw_outputs.append(outputs)
    assert expected_vertices,'Empty draws cannot qualify this exercised fixture'
    return expected_vertices,expected_fog,raw_outputs


def audit(captures,diagnostics,number):
    capture=read_capture(captures/f'draw-{number}.bin')
    expected_vertices,expected_fog,raw_outputs=expectation(capture)
    observation=json.loads((diagnostics/f'draw-{number}.aot-private.json').read_text())
    assert observation['vertex_capture_sha256']==capture['sha256'] and observation['complete_draw_executed']
    assert len(observation['vertices'])==len(expected_vertices),'Missing or extra observed vertices'
    assert all(len(v)==28 for v in observation['vertices']),'Incomplete vertex observation'
    assert len(observation['fog'])==len(expected_fog),'Incomplete fog observation'
    compared=0;failures=[];native_maximum=0
    for vertex,(actual,expected,outputs) in enumerate(zip(observation['vertices'],expected_vertices,raw_outputs)):
        for group,reg in enumerate((0,3,4,9,10,11,12)):
            for lane in range(4):
                want=expected[group*4+lane];got=actual[group*4+lane]
                # This is an exact software binary32 contract. Native outputs
                # remain diagnostics and cannot supply or widen the expectation.
                if got!=want:failures.append([vertex,reg,lane,got,want])
                compared+=1
                raw=capture['outputs'][vertex*52+reg*4+lane]
                native_maximum=max(native_maximum,abs(raw-float(outputs[reg][lane])))
        want_fog=expected_fog[vertex]
        if observation['fog'][vertex]!=want_fog:failures.append([vertex,'fog',observation['fog'][vertex],want_fog])
        compared+=1
    return dict(draw=number,vertex_capture_sha256=capture['sha256'],
        program_sha256=hashlib.sha256(struct.pack('<'+'I'*len(capture['code']),*capture['code'])).hexdigest(),
        isa_source=ISA_SOURCE,arithmetic='separate nearest/even binary32; rounded sqrt then reciprocal; finite RCC clamp',
        compared_lanes=compared,failure_count=len(failures),first_failures=failures[:16],
        native_maximum_absolute_difference_diagnostic_only=native_maximum,
        expected_vertices=expected_vertices,expected_fog=expected_fog,
        shader_input_to_output_qualified=not failures,complete_scene_pixel_qualified=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('captures','diagnostics','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--draws',type=int,nargs='+',default=[2,3,4,5,6])
    args=parser.parse_args()
    rows=[audit(args.captures,args.diagnostics,n) for n in args.draws]
    args.output.write_text(json.dumps(dict(draws=rows,scope='Independent captured stream/constant/program to software transform output; state/pixel composition and exact silicon precision remain separate'),indent=2)+'\n')
    for row in rows:print(json.dumps({k:v for k,v in row.items() if k not in ('expected_vertices','expected_fog')}),flush=True)
    raise SystemExit(0 if rows and all(r['shader_input_to_output_qualified'] for r in rows) else 1)
