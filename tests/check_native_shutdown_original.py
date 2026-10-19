"""Bounded original export47 oracle with authored registrations and no devices."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
from unicorn.x86_const import *


def check(blob):
    expected='827c39fd9dea2e831c7ef9091a2a4a4b13ac6ae9f69a2075c4b12e1b9ae97b3c'
    if hashlib.sha256(blob).hexdigest()!=expected:raise ValueError('qualified flat kernel source required')
    u=lambda p:struct.unpack_from('<I',blob,p)[0]
    pe=u(60);exports=u(pe+120);entry=0x80010000+u(u(exports+28)+(47-u(exports+16))*4)
    if entry!=0x800182c9:raise ValueError('original shutdown registration export differs')
    uc=Uc(UC_ARCH_X86,UC_MODE_32)
    uc.mem_map(0x80010000,0x80000);uc.mem_write(0x80010000,blob)
    uc.mem_map(0x10000,0x10000);uc.mem_map(0x700000,0x10000)
    head=0x8003d8d4
    uc.mem_write(head,struct.pack('<II',head,head))
    # Authored empty pending-interrupt state: callbacks/devices are outside this
    # list-operation oracle. Actual raise/lower-IRQL instructions still execute.
    uc.mem_write(0x8003da60,b'\0'*4);uc.mem_write(0x8003d884,b'\0'*4);uc.mem_write(0x8003dcb8,b'\0'*4)
    count=[0]
    def bounded(engine,address,size,unused):
        if not (0x800182c9<=address<0x80018350 or 0x800173a8<=address<0x800173b7 or 0x800173c8<=address<0x800173f0):
            raise AssertionError('original registration escaped list/IRQL component')
        count[0]+=1
    uc.hook_add(UC_HOOK_CODE,bounded)
    def word(address):return struct.unpack('<I',uc.mem_read(address,4))[0]
    def invoke(address,register):
        stack=0x70f000;end=0x70ff00
        uc.reg_write(UC_X86_REG_ESP,stack);uc.reg_write(UC_X86_REG_EFLAGS,0x246)
        uc.mem_write(stack,struct.pack('<III',end,address,register));uc.emu_start(entry,end,count=1000)
        assert uc.reg_read(UC_X86_REG_ESP)==stack+12
    objects=[0x12000,0x12020,0x12040,0x12060]
    priorities=[0,-2147483648,2147483647,0]
    for address,priority in zip(objects,priorities):
        uc.mem_write(address,struct.pack('<IiII',0x1f000,priority,0xaaaaaaaa,0xbbbbbbbb));invoke(address,1)
    def order(expected):
        cursor=word(head);previous=head
        for address in expected:
            assert cursor==address+8 and word(cursor+4)==previous
            previous=cursor;cursor=word(cursor)
        assert cursor==head and word(head+4)==previous
    order([objects[2],objects[0],objects[3],objects[1]])
    oldlinks=bytes(uc.mem_read(objects[0]+8,8));invoke(objects[0],0x100)
    assert bytes(uc.mem_read(objects[0]+8,8))==oldlinks
    order([objects[2],objects[3],objects[1]])
    invoke(0xdead0000,0);order([objects[2],objects[3],objects[1]])
    invoke(objects[0],1);order([objects[2],objects[3],objects[0],objects[1]])
    for address in objects:invoke(address,0)
    order([])
    return dict(source_sha256=expected,original_export=hex(entry),registration_body_sha256=hashlib.sha256(blob[entry-0x80010000:0x18350-0x10000]).hexdigest(),
        signed_priority_and_fifo='PASS',remove_preserves_own_links='PASS',absent_unregister='PASS',boolean_low_byte='PASS',
        original_component_instructions=count[0],callbacks_executed=0,physical_power_operations=0)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--kernel-flat',type=Path,required=True);parser.add_argument('--receipt',type=Path)
    args=parser.parse_args();text=json.dumps(check(args.kernel_flat.read_bytes()),indent=2)+'\n'
    if args.receipt:args.receipt.write_text(text,encoding='utf-8')
    print(text,end='')
