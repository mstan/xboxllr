#!/usr/bin/env python3
"""Execute original Blinx XDK vertex submission over maintained NV2A fetch.

Preowned shader, streams and pushbuffer bound the original fast-path fixture.
Unmodified Load/Select/Draw functions and their original callees execute; no
instruction hooks, synthetic returns or replacement command emitters are used.
This is component qualification, not original whole-title GPU startup.
"""
import argparse
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import struct

from aot_inventory import parse_xbe
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_PROT_ALL
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
    UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EIP)

XBE_SHA = 'e6b7cd8a5527dd001e05bb3e881d9b24ffc58824ff67def394215ddbc8009249'
PUSH, OBJECT, STREAMS, FRAME = 0x2100000, 0x2200000, 0x2300000, 0x600000
STACK, STOP = 0x70008000, 0x70009000


class Reference:
    def __init__(self, args):
        self.blob = args.xbe.read_bytes()
        assert hashlib.sha256(self.blob).hexdigest() == XBE_SHA
        self.binding = json.loads(args.binding.read_text())
        assert self.binding['xbe_sha256'] == XBE_SHA
        self.device = self.binding['device_object']
        self.calls = {c['name']: int(c['address'], 16) for c in self.binding['calls']}
        self.ram = ctypes.create_string_buffer(64*1024*1024)
        self.cpu = Uc(UC_ARCH_X86, UC_MODE_32)
        self.cpu.mem_map_ptr(0, len(self.ram), UC_PROT_ALL, ctypes.addressof(self.ram))
        self.cpu.mem_map(0x70000000, 0x10000)
        for section in parse_xbe(self.blob)['sections']:
            if section['preload']:
                self.cpu.mem_write(section['address'], self.blob[section['raw_offset']:
                    section['raw_offset']+section['raw_size']])
        self.lib = ctypes.CDLL(str(args.library.resolve()))
        for name, result, params in (
            ('create', ctypes.c_void_p, []), ('destroy', None, [ctypes.c_void_p]),
            ('memory', None, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32]),
            ('write', None, [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]),
            ('error', ctypes.c_char_p, [ctypes.c_void_p]),
            ('vertex', None, [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float)])):
            fn = getattr(self.lib, 'xlle_nv2a_reference_'+name)
            fn.restype, fn.argtypes = result, params
        self.gpu = self.lib.xlle_nv2a_reference_create()
        assert self.gpu
        self.lib.xlle_nv2a_reference_memory(self.gpu, ctypes.addressof(self.ram), len(self.ram))
        self.put(self.binding['device_global'], self.device)
        self.put(self.device, PUSH, PUSH+0x100000, 0)
        self.put(self.device+0xa90, 0, 0, 64, 64)
        self.floats(self.device+0xaa0, 0, 1, 0, 0)
        self.floats(self.device+0x510, 16777215, 0, 1, 1)
        self.stream_table = self.binding['stateblock']['streams']
        self.cpu.mem_write(self.stream_table, bytes(16*12))
        self.fence = 0x12345678
        self.put(self.device+self.binding['resource_binding']['fence'], self.fence)
        self.resources = {}
        self.transitions = []
        self.configure()

    def close(self):
        if self.gpu:
            self.lib.xlle_nv2a_reference_destroy(self.gpu)
            self.gpu = None

    def put(self, address, *values):
        self.cpu.mem_write(address, struct.pack('<'+'I'*len(values), *values))

    def floats(self, address, *values):
        self.cpu.mem_write(address, struct.pack('<'+'f'*len(values), *values))

    def get(self, address, count=1):
        return list(struct.unpack('<'+'I'*count, self.cpu.mem_read(address, count*4)))

    def check(self):
        error = self.lib.xlle_nv2a_reference_error(self.gpu)
        assert not error, error.decode() if error else ''

    def write(self, address, value):
        self.lib.xlle_nv2a_reference_write(self.gpu, 0xfd000000+address, 4, value)
        self.check()

    def emit(self, method, *values):
        cursor = self.get(self.device)[0]
        assert cursor+4+len(values)*4 < PUSH+0x100000
        self.put(cursor, (len(values)<<18)|method, *values)
        self.put(self.device, cursor+4+len(values)*4)

    def kick(self):
        self.write(0x800040, self.get(self.device)[0])

    def call(self, name, values, registers=()):
        canaries = ((UC_X86_REG_EBX, 123), (UC_X86_REG_ESI, 456),
                    (UC_X86_REG_EDI, 789), (UC_X86_REG_EBP, 876))
        self.put(STACK, STOP, *values)
        self.cpu.reg_write(UC_X86_REG_ESP, STACK)
        self.cpu.reg_write(UC_X86_REG_ECX, self.device)
        for reg, value in canaries:
            self.cpu.reg_write(reg, value)
        for reg, value in registers:
            self.cpu.reg_write(reg, value)
        self.cpu.emu_start(self.calls[name], STOP, count=1000000)
        assert self.cpu.reg_read(UC_X86_REG_EIP) == STOP, (name,'Original call did not return',hex(self.cpu.reg_read(UC_X86_REG_EIP)))
        assert self.cpu.reg_read(UC_X86_REG_ESP) == STACK+4+4*len(values), name
        assert all(self.cpu.reg_read(reg) == value for reg, value in canaries), name
        return self.cpu.reg_read(UC_X86_REG_EAX)

    def configure(self):
        for address, value in (
            (0x700008,1),(0x70000c,0x80010020),(0x700200,0x97),
            (0x700010,2),(0x700014,0x80010030),(0x700300,0x3d),
            (0x700304,len(self.ram)-1),(0x700308,0),
            (0x700180,0x3d),(0x700184,len(self.ram)-1),(0x700188,0),
            (0x002504,1),(0x003204,0x100),(0x00322c,0x18),
            (0x003240,PUSH),(0x003244,PUSH),(0x003220,1),(0x003250,1),
            (0x003200,1),(0x400720,1),(0x400880,0x28c3ff)):
            self.write(address, value)
        for method, value in (
            (0,1),(0x194,2),(0x198,2),(0x19c,2),(0x1a0,2),
            (0x208,0x128),(0x20c,0x01000100),(0x210,FRAME),(0x214,FRAME+0x4000),
            (0x200,64<<16),(0x204,64<<16),(0x290,0x00100001),
            (0x2c0,63<<16),(0x2e0,63<<16),(0x33c,0x207),
            (0x350,0x8006),(0x354,0x201),(0x358,0x01010101),(0x35c,0),(0x30c,0),
            (0x37c,0x1d01),(0x38c,0x1b02),(0x390,0x1b02),(0x394,0),
            (0x398,0x4b7fffff),(0x39c,0x405),(0x3a0,0x900),(0x1d78,1),
            (0x1e78,0x00210000),(0x1e60,1),(0xac0,0x04200000),(0x260,0x14200000),
            (0x1e40,0xc00),(0xaa0,0xc00),(0x288,0xc),(0x28c,0x1c80)):
            self.emit(method,value)
        self.kick()

    def bind(self, slot, fmt, stream, offset):
        # Original programmable descriptors use the identity register map.
        mapping = bytes(self.cpu.mem_read(0x37aa8c,16))
        assert mapping == bytes(range(16)), mapping.hex()
        self.put(OBJECT,1,0x10,2,9,0)
        for i in range(16):
            self.put(OBJECT+0x14+i*16,0,0,2,0)
        self.put(OBJECT+0x14,0,0,0x42)
        self.put(OBJECT+0x14+slot*16,stream,offset,fmt)
        code = [0,0x0020001b,0x08000000,0x0000f800,
                0,0x0020001b|(slot<<9),0x08000000,0x0000f819]
        self.bind_program(code)

    def bind_program(self, code):
        assert len(code)%4==0 and 0<len(code)//4<=136
        # The hardware upload-method array holds eight instructions. The
        # preowned driver descriptor repeats that array for longer programs.
        packets=[]
        for first in range(0,len(code),32):
            chunk=code[first:first+32]
            packets.extend([(len(chunk)<<18)|0xb00,*chunk])
        self.put(OBJECT+8,len(code)//4,len(packets))
        self.put(OBJECT+0x114,*packets)
        self.call('D3DDevice_LoadVertexShader',[OBJECT|1,0])
        self.call('D3DDevice_SelectVertexShader',[OBJECT|1,0])
        self.kick()

    def resource(self, header, data):
        self.put(header, 0x01000001, data, 0)
        self.resources[header] = data

    def set_stream(self, stream, header, stride):
        old = self.get(self.stream_table+stream*12+8)[0]
        self.call('D3DDevice_SetStreamSource', [stream, header, stride])
        assert self.get(self.stream_table+stream*12)[0] == stride
        assert self.get(self.stream_table+stream*12+8)[0] == header
        assert self.get(0x141b84)[0] & 0x40
        if old:
            assert self.get(old+8)[0] == self.fence
        bound = [self.get(self.stream_table+i*12+8)[0] for i in range(16)]
        for resource, data in self.resources.items():
            assert self.get(resource)[0] == 0x01000001+bound.count(resource)*0x80000
            assert self.get(resource+4)[0] == data
        self.transitions.append(dict(stream=stream, old_header=old, header=header, stride=stride))

    def draw(self, first, primitive=5, count=3, *, clear=True, state_dirty=0x50,up_data=None,up_stride=0):
        if clear:
            self.emit(0x1d90,0xff102030)
            self.emit(0x1d98,63<<16); self.emit(0x1d9c,63<<16); self.emit(0x1d94,0xf0)
        # The default bounds the original submission to declaration/streams.
        # Scene replay passes None to retain all dirty bits set by real setters.
        self.put(0x141b84,(0x70 if up_data is not None else 0x50)|self.get(0x141b84)[0]
                 if state_dirty is None else state_dirty)
        begin=self.get(self.device)[0]
        if up_data is None:self.call('D3DDevice_DrawVertices',[primitive,first,count])
        else:
            assert first==0 and up_data and up_stride
            self.call('D3DDevice_DrawVerticesUP',[primitive,count,up_data,up_stride])
        packets=self.get(begin,(self.get(self.device)[0]-begin)//4)
        methods={}
        cursor=0
        while cursor<len(packets):
            header=packets[cursor];cursor+=1
            count=(header>>18)&0x7ff
            assert count and cursor+count<=len(packets)
            for i,value in enumerate(packets[cursor:cursor+count]):
                method=(header&0x1fff)+(0 if header&0x40000000 else i*4)
                methods[method]=value
            cursor+=count
        self.kick()
        output=(ctypes.c_float*12)()
        self.lib.xlle_nv2a_reference_vertex(self.gpu,output)
        return list(output), methods


def formats():
    """Independent mathematical expectations; never decode the LLE output."""
    result=[]
    for lanes in (1,2,3,4):
        fmt=(lanes<<4)|2
        for values in ((.25,-.5,.75,2),(-1,1,0,-2),(0,0,0,0),(65536,-32768,.125,1)):
            expected=list(values[:lanes])+[0]*(3-lanes)+([1] if lanes<4 else [])
            result.append((fmt,struct.pack('<'+'f'*lanes,*values[:lanes]),expected))
    for color in (0xffff0000,0xff0000ff,0x7f123456,0x019be327):
        result.append((0x40,struct.pack('<I',color),
            [(color>>16&255)/255,(color>>8&255)/255,(color&255)/255,(color>>24)/255]))
    for xyz in ((-1024,-1024,-512),(1023,1023,511),(-1,1,0),(511,-513,255)):
        word=(xyz[0]&2047)|((xyz[1]&2047)<<11)|((xyz[2]&1023)<<22)
        result.append((0x16,struct.pack('<I',word),
            [max(-1,xyz[0]/1023),max(-1,xyz[1]/1023),max(-1,xyz[2]/511),1]))
    for normalized in (False,True):
        for lanes in (1,2,4):
            fmt=(lanes<<4)|(1 if normalized else 5)
            for values in ((-32768,32767,-1,1),(0,0,0,0),(1234,-2345,32766,-32767),(-1,1,-2,2)):
                expected=[max(-1,v/32767) if normalized else v for v in values[:lanes]]
                expected += [0]*(3-lanes)+([1] if lanes<4 else [])
                result.append((fmt,struct.pack('<'+'h'*lanes,*values[:lanes]),expected))
    return result


def run(args):
    rows=[]
    ref=Reference(args)
    try:
        # Real original stream binding owns every resource. Each format group
        # replaces A with B and mutates both persistent payloads without a bind.
        position_header=OBJECT+0x1000
        ref.resource(position_header,STREAMS)
        ref.set_stream(0,position_header,32)
        for stream in (1,7,15):
            headers=[OBJECT+0x1000+stream*32,OBJECT+0x1010+stream*32]
            addresses=[STREAMS+stream*0x1000,STREAMS+stream*0x1000+0x800]
            for header,address in zip(headers,addresses): ref.resource(header,address)
            for slot in (1,2,3,4,15):
                previous=None;offset=8;sample=0
                for fmt,payload,expected in formats():
                    if fmt!=previous:
                        ref.bind(slot,fmt,stream,offset);previous=fmt
                        sample=0
                    buffer=sample//2;stride=(32,48)[buffer];first=1+sample%2
                    if sample%2==0:
                        ref.set_stream(stream,headers[buffer],stride)
                    for i in range(6): ref.floats(STREAMS+i*32,-200,-200,10,1)
                    for i,xy in enumerate(((8,8),(56,8),(8,56)),first):
                        ref.floats(STREAMS+i*32,*xy,10,1)
                    address=addresses[buffer]
                    # Poison both inactive storage and the unsubmitted vertex.
                    ref.cpu.mem_write(addresses[1-buffer],bytes([0xcc])*0x200)
                    ref.cpu.mem_write(address,bytes([0xcc])*0x200)
                    for i in range(first,first+3):
                        ref.cpu.mem_write(address+i*stride+offset,payload)
                    output,methods=ref.draw(first)
                    assert methods[0x1760]==0x2042
                    assert methods[0x1760+slot*4]==(stride<<8)|fmt
                    assert methods[0x1720]==STREAMS
                    assert methods[0x1720+slot*4]==address+offset
                    assert methods[0x1810]==0x02000000|first
                    for got,want in zip(output[4:8],expected):
                        assert math.isfinite(got) and abs(got-want)<=2e-6*max(1,abs(want)),(stream,slot,hex(fmt),expected,output)
                    # Observe the fetch through real pixels as well as raw MOV
                    # output. Allow one framebuffer LSB after interpolation.
                    actual_color=ref.get(FRAME+(16*64+16)*4)[0]
                    for lane,shift in enumerate((16,8,0,24)):
                        byte=round(max(0,min(1,expected[lane]))*255)
                        assert abs((actual_color>>shift&255)-byte)<=1,(hex(fmt),hex(actual_color),expected)
                    rows.append(dict(stream=stream,slot=slot,format=hex(fmt),payload=payload.hex(),
                        offset=offset,stride=stride,first=first,buffer=buffer,header=headers[buffer],
                        persistent_write_without_bind=bool(sample%2),
                        expected=expected,actual=output[4:8],pixel=hex(actual_color)))
                    sample+=1
                    if sample==4:
                        # Same-header rebind must preserve the ownership count.
                        ref.set_stream(stream,headers[buffer],stride)
            ref.set_stream(stream,0,0)
        ref.set_stream(0,0,0)
        assert all(ref.get(header)[0]==0x01000001 for header in ref.resources)
        transitions=ref.transitions
    finally:
        ref.close()
    names=('D3DDevice_SetStreamSource','D3DDevice_LoadVertexShader',
           'D3DDevice_SelectVertexShader','D3DDevice_DrawVertices')
    calls=[dict(name=c['name'],address=c['address'],function_sha256=c['function_sha256'])
           for c in ref.binding['calls'] if c['name'] in names]
    return dict(xbe_sha256=XBE_SHA,original_calls=calls,cases=rows,stream_transitions=transitions,
        scope='original binding/ownership, declared packed/short/float fetch and shader output; preowned resource fast paths')


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
        with args.native_cases.open('wb') as f:
            f.write(struct.pack('<4I',0x46564c58,2,len(result['cases']),0))
            for case in result['cases']:
                f.write(struct.pack('<8I',int(case['format'],16),case['slot'],case['stream'],case['offset'],
                    case['stride'],case['first'],case['buffer'],case['persistent_write_without_bind']))
                f.write(bytes.fromhex(case['payload']).ljust(16,b'\xcc'))
                # These are the independent expectations, not device output.
                f.write(struct.pack('<4f',*case['expected']))
    print(f"PASS: {len(result['cases'])} original XDK declaration/fetch/shader cases")
