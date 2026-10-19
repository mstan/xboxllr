"""Offline original XAPI selector fixture; never executes kernel instructions."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import *
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from aot_inventory import parse_xbe
from native_startup_binding import verify_kernel_observation


def check(xbe,kernel_ram,proof):
    image=parse_xbe(xbe)
    expected='832862490c762a723856350142f49ded92b7ad966741eeb5b89e683006b16a0d'
    if image['sha256']!=expected:raise ValueError('owned original5960 XBE required')
    facts=verify_kernel_observation(kernel_ram,proof['kernel_observation'])
    uc=Uc(UC_ARCH_X86,UC_MODE_32)
    instructions=[0]
    def only_component(engine,address,size,unused):
        if not (0x72902<=address<0x72a2b or 0x72ac4<=address<0x72ae9):
            raise AssertionError('offline fixture escaped reviewed XAPI component')
        instructions[0]+=1
    uc.hook_add(UC_HOOK_CODE,only_component)
    uc.mem_map(0x10000,0x300000)
    uc.mem_write(0x10000,xbe[:0x2000])
    for section in image['sections']:
        uc.mem_write(section['address'],xbe[section['raw_offset']:section['raw_offset']+section['raw_size']])
    uc.mem_map(0x80000000,len(kernel_ram));uc.mem_write(0x80000000,kernel_ram)
    observation=proof['kernel_observation'];gdt=observation['gdt_base']
    uc.reg_write(UC_X86_REG_GDTR,(0,gdt,55,0))
    for register,value in ((UC_X86_REG_CS,8),(UC_X86_REG_SS,16),(UC_X86_REG_DS,16),(UC_X86_REG_ES,16),(UC_X86_REG_FS,32)):
        uc.reg_write(register,value)
    uc.mem_map(0x700000,0x10000)
    stack=0x70f000;end=0x70ff00
    initial=int(observation['initial_descriptor'],16)
    # Actual ordinal addresses from the captured original kernel export table.
    base=0x10000
    u=lambda p:struct.unpack_from('<I',kernel_ram,p)[0]
    pe=base+u(base+60);exports=base+u(pe+120)
    def export(ordinal):return 0x80010000+u(base+u(exports+28)+(ordinal-u(exports+16))*4)
    uc.mem_write(proof['layout']['hardware_import'],struct.pack('<I',export(322)))
    uc.mem_write(proof['layout']['version_import'],struct.pack('<I',export(324)))
    sentinels={UC_X86_REG_EBX:0x11223344,UC_X86_REG_ESI:0x22334455,UC_X86_REG_EDI:0x33445566,UC_X86_REG_EBP:0x44556677}
    for register,value in sentinels.items():uc.reg_write(register,value)
    uc.reg_write(UC_X86_REG_EFLAGS,0x246);uc.reg_write(UC_X86_REG_ESP,stack)
    uc.mem_write(stack,struct.pack('<I',end))
    # The complete original ApplyKernelPatches body executes, including its
    # real retail/version decision and SGDT/xchg/far-jump selector reload.
    uc.emu_start(0x72966,end,count=1000)
    descriptor=struct.unpack('<Q',uc.mem_read(gdt+8,8))[0]
    pages=observation['init_start']>>12
    restricted=0x00c09b0000000000|(pages&0xffff)|((pages&0xf0000)<<32)
    assert descriptor==restricted
    assert bytes(uc.mem_read(proof['layout']['saved_low'],8))==struct.pack('<Q',initial)
    assert uc.reg_read(UC_X86_REG_EAX)==initial&0xffffffff
    assert uc.reg_read(UC_X86_REG_ECX)==initial&0xffffffff
    assert uc.reg_read(UC_X86_REG_EDX)==initial>>32
    assert uc.reg_read(UC_X86_REG_ESP)==stack+4
    assert all(uc.reg_read(register)==value for register,value in sentinels.items())
    assert uc.reg_read(UC_X86_REG_EFLAGS)&0x200
    setup_flags=uc.reg_read(UC_X86_REG_EFLAGS)
    # Isolate actual original exchange (the teardown's IDE work remains
    # deliberately unexecuted), proving previous-descriptor outputs/RET8.
    uc.reg_write(UC_X86_REG_ESP,stack);uc.mem_write(stack,struct.pack('<IQ',end,initial))
    uc.emu_start(0x72ac4,end,count=100)
    assert struct.unpack('<Q',uc.mem_read(gdt+8,8))[0]==initial
    assert uc.reg_read(UC_X86_REG_EAX)==restricted&0xffffffff
    assert uc.reg_read(UC_X86_REG_ECX)==restricted&0xffffffff
    assert uc.reg_read(UC_X86_REG_EDX)==restricted>>32
    assert uc.reg_read(UC_X86_REG_ESP)==stack+12
    assert uc.reg_read(UC_X86_REG_EFLAGS)==setup_flags
    assert all(uc.reg_read(register)==value for register,value in sentinels.items())
    return dict(original_xbe_sha256=expected,original_kernel_facts=facts,
        original_apply_and_exchange='PASS',old_descriptor=f'0x{initial:016x}',
        restricted_descriptor=f'0x{restricted:016x}',flags_preserved_by_exchange=True,
        reviewed_component_instructions=instructions[0],kernel_instructions_executed=0,host_gdt_accessed=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--kernel-ram',type=Path,required=True)
    parser.add_argument('--profile',type=Path,default=Path(__file__).resolve().parents[1]/'tools/native_profiles/dashboard-5960-startup.json')
    parser.add_argument('--receipt',type=Path)
    args=parser.parse_args()
    result=check(args.xbe.read_bytes(),args.kernel_ram.read_bytes(),json.loads(args.profile.read_text()))
    text=json.dumps(result,indent=2)+'\n'
    if args.receipt:args.receipt.write_text(text,encoding='utf-8')
    print(text,end='')
