#!/usr/bin/env python3
"""Original shader upload/submission with independently authored arithmetic.

Instruction fields follow XboxDevWiki; ordinary finite-value math follows
NVIDIA NV_vertex_program/1_1. This does not qualify silicon rounding, exceptional
floats, or ARL values close to integer boundaries. No shared shader decoder or
emulator is used to produce the expected values.
"""
import argparse
import json
import math
import os
from pathlib import Path
import struct

from native_vertex_fetch_reference import Reference, XBE_SHA, OBJECT, STREAMS, FRAME
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

INPUTS=(
    ((.25,-.5,.75,2.5),(.5,.25,-.5,1.25),(4,.25,-4,.5)),
    ((-1.5,.25,.5,-.75),(-.5,1,2,.25),(.25,1,4,-4)),
    ((2.5,-2.5,1.5,-1.5),(.125,-.25,.5,2),(1,4,.25,.0625)),
    ((-.25,.5,-.75,1.25),(-2,.5,.25,-1),(-4,-.25,1,.0625)))
MODIFIERS=(
    ((0,1,2,3),(0,1,2,3),(0,1,2,3),(False,False,False)),
    ((3,2,1,0),(1,0,3,2),(2,3,0,1),(True,False,True)),
    ((0,0,0,0),(2,2,2,2),(1,1,1,1),(False,True,False)),
    ((2,0,3,1),(3,2,0,1),(3,3,0,0),(True,True,True)))
SEED=(.125,.25,.5,.75)


def swizzle_bits(swizzle):
    return sum(component<<(6-2*lane) for lane,component in enumerate(swizzle))


def instruction(mac=0,ilu=0,modifier=MODIFIERS[0],mask=15,paired=False,end=True):
    a,b,c,neg=modifier
    word1=(ilu<<25)|(mac<<21)|(96<<13)|(1<<9)|(neg[0]<<8)|swizzle_bits(a)
    word2=(2<<26)|(neg[1]<<25)|(swizzle_bits(b)<<17)|(3<<11)|(neg[2]<<10)|(swizzle_bits(c)<<2)
    # C is R2; the overall output is oD0. Paired MAC writes R2 while ILU
    # writes oD0 from the old R2; a later instruction observes the new R2.
    word3=(2<<30)|(1<<28)|(mask<<12)|(1<<11)|(3<<3)|int(end)
    if ilu: word3|=1<<2
    if paired: word3|=(15<<24)|(2<<20)
    return [0,word1,word2,word3]


def modified(values,swizzle,negated):
    return [(-1 if negated else 1)*values[i] for i in swizzle]


def mac_value(op,a,b,c):
    if op==1: return a
    if op==2: return [x*y for x,y in zip(a,b)]
    if op==3: return [x+y for x,y in zip(a,c)]  # ADD uses source A/C.
    if op==4: return [x*y+z for x,y,z in zip(a,b,c)]
    if op==5: return [sum(a[i]*b[i] for i in range(3))]*4
    if op==6: return [sum(a[i]*b[i] for i in range(3))+b[3]]*4
    if op==7: return [sum(x*y for x,y in zip(a,b))]*4
    if op==9: return [min(x,y) for x,y in zip(a,b)]
    if op==10: return [max(x,y) for x,y in zip(a,b)]
    raise ValueError(op)


def ilu_value(op,c):
    if op==1: return c
    if op in (2,3): return [1/c[0]]*4  # finite values inside RCC clamps
    if op==4: return [1/math.sqrt(abs(c[0]))]*4
    raise ValueError(op)


def cases(group):
    operations={'mac':[(op,0) for op in (1,2,3,4,5,6,7,9,10)],
        'ilu':[(0,op) for op in (1,2,3,4)],
        'paired':[(2,1),(5,1),(2,4),(2,3),(5,4),(2,2)],'arl':[(13,0)]}[group]
    masks=(15,8,4,2,1) if group in ('mac','ilu') else ((15,8,2) if group=='paired' else (15,))
    for mac,ilu in operations:
        for modifier in MODIFIERS:
            for mask in masks:
                for inputs in INPUTS:
                    a,b,c=(modified(vector,swizzle,neg) for vector,swizzle,neg in zip(inputs,modifier[:3],modifier[3]))
                    constants=[[((r-88)%13)/16,((r-88)%7)/8,((r-88)%5)/4,1] for r in range(88,120)]
                    constants[96-88]=list(inputs[1]);constants[97-88]=list(inputs[2]);constants[99-88]=list(SEED)
                    code=[0,0x0020001b,0x08000000,0x0000f800]  # oPos,v0
                    # Explicitly initialize both observed outputs. Unwritten
                    # hardware register defaults are outside this contract.
                    code+=[0,0x0020001b|(99<<13),0x0c000000,0x0000f820]
                    if group=='arl':
                        code+=instruction(mac=13,modifier=modifier,mask=0,end=False)
                        code+=[0,0x0020001b|(100<<13),0x0c000000,0x0000f81b]
                        index=100+math.floor(a[0]);assert 88<=index<120
                        expected=constants[index-88]
                        specular=list(SEED)
                    else:
                        code+=[0,0x0020001b|(97<<13),0x0c000000,0x0f200000]  # R2,c97
                        code+=[0,0x0020001b|(99<<13),0x0c000000,0x0000f818]  # seed oD0
                        paired=group=='paired'
                        code+=instruction(mac,ilu,modifier,mask,paired,end=not paired)
                        value=ilu_value(ilu,c) if ilu else mac_value(mac,a,b,c)
                        expected=[value[lane] if mask&(8>>lane) else SEED[lane] for lane in range(4)]
                        if paired:
                            code+=[0,0x0020001b,0x24000000,0x0000f821]  # oD1,R2
                            specular=mac_value(mac,a,b,c)
                        else: specular=list(SEED)
                    yield dict(group=group,mac=mac,ilu=ilu,mask=mask,modifier=modifier,
                        input=list(inputs[0]),constants=constants,code=code,
                        expected=list(expected)+specular)


def run(args):
    rows=[]
    for group in ('mac','ilu','paired','arl'):
        ref=Reference(args)
        try:
            ref.resource(OBJECT+0x1000,STREAMS);ref.resource(OBJECT+0x1020,STREAMS+0x1000)
            ref.set_stream(0,OBJECT+0x1000,32);ref.set_stream(1,OBJECT+0x1020,32)
            for i,xy in enumerate(((-200,-200),(8,8),(56,8),(8,56))): ref.floats(STREAMS+i*32,*xy,10,1)
            previous=None
            for row in cases(group):
                if row['code']!=previous:
                    ref.put(OBJECT,1,0x10,0,0,0)
                    for i in range(16): ref.put(OBJECT+0x14+i*16,0,0,2,0)
                    ref.put(OBJECT+0x14,0,0,0x42);ref.put(OBJECT+0x24,1,8,0x42)
                    ref.bind_program(row['code']);previous=row['code']
                for i in range(1,4): ref.floats(STREAMS+0x1000+i*32+8,*row['input'])
                data=OBJECT+0x3000
                ref.floats(data,*(v for vector in row['constants'] for v in vector))
                ref.call('D3DDevice_SetVertexShaderConstantNotInline',[32*4],
                         ((UC_X86_REG_ECX,88),(UC_X86_REG_EDX,data)))
                output,_=ref.draw(1)
                for got,want in zip(output[4:12],row['expected']):
                    assert math.isfinite(got) and abs(got-want)<=2e-6*max(1,abs(want)),(group,row['mac'],row['ilu'],row['mask'],row['modifier'],row['input'],row['expected'],output)
                color=ref.get(FRAME+(16*64+16)*4)[0]
                for lane,shift in enumerate((16,8,0,24)):
                    want=round(max(0,min(1,row['expected'][lane]))*255)
                    assert abs((color>>shift&255)-want)<=1,(group,hex(color),row['expected'])
                rows.append(dict(**row,actual=output[4:12],pixel=hex(color)))
            ref.set_stream(1,0,0);ref.set_stream(0,0,0)
        finally:
            ref.close()
    return dict(xbe_sha256=XBE_SHA,device_lifetimes=4,cases=rows,
        scope='ordinary finite shader arithmetic, masks/swizzles/negation/paired issue/relative addressing; original submission fast paths')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','binding','library','dependencies','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--native-cases',type=Path)
    args=parser.parse_args()
    dependency_handles=[os.add_dll_directory(str(args.dependencies.resolve()))]
    result=run(args)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    if args.native_cases:
        with args.native_cases.open('wb') as file:
            file.write(struct.pack('<4I',0x41564c58,1,len(result['cases']),0))
            for row in result['cases']:
                file.write(struct.pack('<4I',len(row['code'])//4,0,0,0))
                file.write(struct.pack('<'+'I'*len(row['code']),*row['code']))
                file.write(struct.pack('<4f',*row['input']))
                file.write(struct.pack('<128f',*(v for vector in row['constants'] for v in vector)))
                file.write(struct.pack('<8f',*row['expected']))
    print(f"PASS: {len(result['cases'])} original-submission/LLE shader arithmetic draws across four device lifetimes")
