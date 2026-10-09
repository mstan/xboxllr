#!/usr/bin/env python3
"""Qualify original XDK constant uploads, recorder policy and actual NV2A output.

Reuses the preowned-resource original submission fixture. Expected values and
cache effects come from authored inputs and original software contracts, never
from measured native or LLE output. No original instructions are replaced.
"""
import argparse
import json
import math
import os
from pathlib import Path
import struct

from native_vertex_fetch_reference import Reference, XBE_SHA, OBJECT, STREAMS, FRAME
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_EDX

DATA, QUERY = OBJECT+0x3000, OBJECT+0x6000


def layouts():
    yield from (('D3DDevice_SetVertexShaderConstant1', i, 1) for i in range(192))
    yield from (('D3DDevice_SetVertexShaderConstant4', i, 4)
                for i in (0, 1, 56, 58, 96, 100, 188))
    for name in ('D3DDevice_SetVertexShaderConstantNotInline',
                 'D3DDevice_SetVertexShaderConstantNotInlineFast'):
        for count in (1, 2, 3, 4, 5, 8, 17, 32):
            for first in dict.fromkeys((0, 1, 58, 96, 192-count)):
                yield name, first, count


def values(first, count, revision):
    return [[((r+revision)%7-3)*.25, ((r+2*revision)%5)*.125,
             ((r+revision)%3-1)*8, .25+((r+revision)%3)*.25]
            for r in range(first, first+count)]


def run(args):
    ref=Reference(args)
    rows=[]
    try:
        header=OBJECT+0x1000
        ref.resource(header, STREAMS)
        ref.set_stream(0, header, 32)
        for i,xy in enumerate(((-200,-200),(8,8),(56,8),(8,56))):
            ref.floats(STREAMS+i*32, *xy, 10, 1)
        cache=ref.device+ref.binding['stateblock']['fields']['vertex_constants']
        record=ref.device+ref.binding['vertex_binding']['record_flags']
        disable=ref.binding['vertex_binding']['record_disable']
        previous=None
        for name,first,count in layouts():
            observed=first+count-1
            if observed!=previous:
                # MOV oPos,v0; MOV oD0,c[observed]. No CPU shader reference.
                ref.put(OBJECT,1,0x10,2,9,0)
                for i in range(16): ref.put(OBJECT+0x14+i*16,0,0,2,0)
                ref.put(OBJECT+0x14,0,0,0x42)
                ref.bind_program([0,0x0020001b,0x08000000,0x0000f800,
                    0,0x0020001b|(observed<<13),0x0c000000,0x0000f819])
                previous=observed
            for disabled in (False,True):
                ref.put(record,disable if disabled else 0)
                records=(name.endswith('Constant1') or name.endswith('Constant4') or
                         (name.endswith('NotInline') and not disabled))
                for revision in (0,1):
                    # Three queued writes: overwritten data, final data and an
                    # identical repeat. Native may coalesce these until draw.
                    final=values(first,count,revision)
                    before=bytes(ref.cpu.mem_read(cache,192*16))
                    expected_cache=bytearray(before)
                    for payload in (values(first,count,revision+3),final,final):
                        raw=struct.pack('<'+'f'*(count*4),*(v for row in payload for v in row))
                        ref.put(DATA-4,0x76543210);ref.put(DATA+len(raw),0x89abcdef)
                        ref.cpu.mem_write(DATA,raw)
                        stack=[] if name.endswith(('Constant1','Constant4')) else [count*4]
                        ref.call(name,stack,((UC_X86_REG_ECX,first),(UC_X86_REG_EDX,DATA)))
                        if records: expected_cache[first*16:(first+count)*16]=raw
                        assert bytes(ref.cpu.mem_read(cache,192*16))==expected_cache,(name,first,count,disabled)
                        assert ref.get(DATA-4)[0]==0x76543210 and ref.get(DATA+len(raw))[0]==0x89abcdef
                        assert bytes(ref.cpu.mem_read(DATA,len(raw)))==raw
                    # Original query uses the signed [-96,95] namespace; -96
                    # addresses the first slot of the cache written above.
                    ref.put(QUERY-4,0x12345678);ref.put(QUERY+192*16,0xabcdef01)
                    ref.call('D3DDevice_GetVertexShaderConstant',[(-96)&0xffffffff,QUERY,192])
                    assert bytes(ref.cpu.mem_read(QUERY,192*16))==expected_cache
                    assert ref.get(QUERY-4)[0]==0x12345678 and ref.get(QUERY+192*16)[0]==0xabcdef01
                    single=QUERY+0x2000
                    ref.put(single-4,0x13579bdf);ref.put(single+16,0x2468ace0)
                    ref.call('D3DDevice_GetVertexShaderConstant',[(observed-96)&0xffffffff,single,1])
                    assert bytes(ref.cpu.mem_read(single,16))==expected_cache[observed*16:(observed+1)*16]
                    assert ref.get(single-4)[0]==0x13579bdf and ref.get(single+16)[0]==0x2468ace0
                    output,_=ref.draw(1)
                    expected=final[-1]
                    assert all(math.isfinite(got) and abs(got-want)<=2e-6*max(1,abs(want))
                               for got,want in zip(output[4:8],expected)),(name,first,count,observed,expected,output)
                    color=ref.get(FRAME+(16*64+16)*4)[0]
                    for lane,shift in enumerate((16,8,0,24)):
                        want=round(max(0,min(1,expected[lane]))*255)
                        assert abs((color>>shift&255)-want)<=1,(name,first,count,hex(color),expected)
                    rows.append(dict(setter=name,first=first,count=count,observed=observed,
                        recorder_disabled=disabled,records_cache=records,revision=revision,
                        preliminary=values(first,count,revision+3),values=final,
                        expected=expected,actual=output[4:8],pixel=hex(color)))
        ref.set_stream(0,0,0)
    finally:
        ref.close()
    names={row['setter'] for row in rows}|{'D3DDevice_GetVertexShaderConstant'}
    return dict(xbe_sha256=XBE_SHA,original_calls=[c for c in ref.binding['calls'] if c['name'] in names],
        cases=rows,scope='original 192-slot constant uploads/cache/query/recorder and real shader output; preowned fast paths')


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
            file.write(struct.pack('<4I',0x43564c58,1,len(result['cases']),0))
            for row in result['cases']:
                file.write(struct.pack('<4I',row['first'],row['count'],row['observed'],0))
                for key in ('preliminary','values'):
                    file.write(struct.pack('<'+'f'*(row['count']*4),*(v for vector in row[key] for v in vector)))
                file.write(struct.pack('<4f',*row['expected']))
    print(f"PASS: {len(result['cases'])} original constant/cache/query and LLE shader draw cases")
