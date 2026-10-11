#!/usr/bin/env python3
"""Execute original Blinx FE helper over the observed idle device context.

Actual guest kernel allocation, original control/critical callees and RAM DMA
remain in the route. Instruction boundaries supply explicit nominal device
service; they do not establish silicon latency or active-DPC ordering. No Xbox
connection, audio playback, original-function replacement or CPU token store.
"""
import argparse
import ctypes as c
import gc
import hashlib
import json
from pathlib import Path
import traceback

from native_audio_configuration_reference import IMAGE
from native_audio_interrupt_reference import AudioReference, DEVICE
from native_io_device_reference import SNAPSHOT_SHA
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE


def run_case(args, period, sequence):
    ref=AudioReference(args)
    try:
        # These new capabilities belong to this fixture. Existing IRQ fixtures
        # remain usable with their retained earlier transport generations.
        for name,arguments in (
                ('capture_fe_idle',[c.c_void_p]+[c.c_uint32]*4),
                ('service_fe_fence',[c.c_void_p])):
            function=getattr(ref.hardware.dll,'xlle_apu_reference_'+name)
            function.restype,function.argtypes=c.c_int,arguments
            ref.hardware.functions[name]=function
        baseline=ref.statistics()
        ref.hardware.call('capture_fe_idle',0x71f8f,7,1,0)
        scratch,physical=ref.allocate(4096)
        assert not physical&4095 and physical+4096<=len(ref.ram)
        ref.put(0x17df18,scratch);ref.put(0x17df1c,physical)
        ref.put(0x17ddbc,sequence)
        cpu_writes=[];services=[];instructions=0;running=False

        def scratch_write(cpu,access,address,size,value,data):
            if running:cpu_writes.append([address-scratch,size,value])

        def service(cpu,address,size,data):
            nonlocal instructions
            if not running:return
            instructions+=1
            if not period or instructions%period:return
            try:
                before=ref.get(scratch)
                # A real owner boundary, independent of any poll/read address.
                ref.hardware.call('service_fe_fence')
                after=ref.get(scratch)
                if before!=after:
                    services.append(dict(instruction=instructions,before=before,after=after,
                                         control=ref.read(0x1510)))
            except Exception as error:ref.stop(str(error))

        ref.cpu.hook_add(UC_HOOK_MEM_WRITE,scratch_write,None,scratch,scratch+4095)
        ref.cpu.hook_add(UC_HOOK_CODE,service)
        token=(sequence+1)&0xffffffff
        running=True
        ref.original('fence',context=DEVICE)
        running=False
        assert ref.executed['fence']==1 and ref.executed['fe_control']==2
        assert ref.get(0x17ddbc)==token
        assert cpu_writes==[[0,4,(~token)&0xffffffff]]
        assert ref.read(0x1510)==0
        assert [event for event in ref.accesses if event[0]=='write']==[
            ['write',0x1510,1],['write',0x1324,physical],
            ['write',0x1334,token],['write',0x1510,0]]
        assert ref.hardware.call('irq')==0
        if period:
            assert ref.get(scratch)==token and len(services)==1
            if period==1:assert services[0]['control']==1
            outcome='device_token_observed'
        else:
            # The real helper has a bounded loop but no success-status return.
            # Its return alone cannot prove that the device published anything.
            assert ref.get(scratch)==((~token)&0xffffffff) and not services
            outcome='original_return_without_token'
        assert ref.cpu.mem_read(scratch+4,4092)==bytes(4092)
        # Reproduce actual helper restoration, without a transport shortcut.
        ref.original('fe_control',1,context=DEVICE+8)
        assert ref.executed['fe_control']==3 and ref.read(0x1510)==1
        if not period:
            # Page remains owned until a subsequent explicit device service;
            # this also shows that return/control restoration did not retire it.
            assert ref.get(scratch)==((~token)&0xffffffff)
            ref.hardware.call('service_fe_fence')
            assert ref.get(scratch)==token
        ref.call(171,scratch)
        assert ref.statistics()==baseline
        return dict(period_instructions=period,initial_sequence=sequence,token=token,
                    physical=physical,cpu_scratch_writes=cpu_writes,
                    publication=services,outcome=outcome,instructions=instructions,
                    original_helper_calls=1,original_control_calls=3,
                    allocator_restored=True,irq=False)
    finally:ref.hardware.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','library','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    result=dict(schema='xboxlle.original-idle-fe-fence.v1',xbe_sha256=IMAGE,
                snapshot_sha256=SNAPSHOT_SHA,
                library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),
                observed_context=dict(fectl=0x71f8f,sectl=7,control=1,gp_reset=0),
                cases=[],error=None,
                scope='Original helper/control callees and real owned-RAM DMA in captured idle context; no cold reset, active DPC, cadence, full audio or title qualification')
    try:
        for period in (1,64,256,0):
            for sequence in (0,0xfffffffe,0xffffffff):
                result['cases'].append(run_case(args,period,sequence));gc.collect()
    except Exception as error:
        result.update(error=f'{type(error).__name__}: {error}',traceback=traceback.format_exc())
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(error=result['error'],cases=len(result['cases']))))
    if result['error']:print(result['traceback'])
    return int(result['error'] is not None)


if __name__=='__main__':raise SystemExit(main())
