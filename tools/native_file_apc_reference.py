#!/usr/bin/env python3
"""Observe actual original232 callback arguments; no original callee replaced."""
import argparse
import json
import struct
from pathlib import Path
from native_io_device_reference import Reference, SNAPSHOT_SHA


def run(snapshot):
    reference=Reference(snapshot)
    reference.enable_kernel_segments()
    # Authored caller-owned stdcall observer, not a replacement for a kernel
    # function: record the callback's three arguments and return RET12.
    callback=bytearray()
    for index in range(3):
        callback.extend(bytes((0x8b,0x44,0x24,4+index*4,0xa3)))
        callback.extend(struct.pack('<I',0x10000400+index*4))
    callback.extend(b'\xc2\x0c\x00')
    reference.cpu.mem_write(0x10000300,bytes(callback))
    rows=[]
    for status,count,error in ((0,123,0),(0x40000000,97,0),(0x80000005,41,0),
                               (0xc0000011,0xfeed,38),(0xc000000d,0xfeed,87)):
        reference.cpu.mem_write(0x10000200,struct.pack('<II',status,count))
        reference.cpu.mem_write(0x10000400,b'\xcc'*16)
        reference.call(232,0x10000300,0x10000200,0x12345678)
        got=struct.unpack('<IIII',reference.cpu.mem_read(0x10000400,16))
        expected=(error,0 if error else count,0x10000200,0xcccccccc)
        if got!=expected:raise AssertionError((hex(status),got,expected))
        rows.append(dict(status=f'{status:08x}',count=count,arguments=list(got[:3]),canary=True))
    return dict(snapshot_sha256=SNAPSHOT_SHA,cases=rows,
                scope='original232 RET12, severity and callback arguments only; thread/APC queue timing is separate')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();result=run(args.snapshot)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(f'PASS: {len(result["cases"])} original232 severity/argument/RET12 cases')
