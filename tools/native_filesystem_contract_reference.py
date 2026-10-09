#!/usr/bin/env python3
"""Execute original metadata/directory/error/namespace contracts over COW HDD.

Reuses the original-kernel/IDE/PIC floor. Observations include untouched output
bytes, not merely returned status. No donor filesystem implementation is copied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import traceback

from native_filesystem_reference import FilesystemReference, SNAPSHOT_SHA

IOSB = 0x10000310
OUT = 0x10001004
SENTINEL = 0xcdcdcdcd


def expect(row,status,information=None,unchanged=None):
    assert row['status']==f'{status:08x}', row
    expected=[SENTINEL,SENTINEL] if information is None else [status,information]
    assert row['iosb']==expected, row
    if unchanged is not None:
        assert row['unchanged']==unchanged, row


def words(ref, address, count=2):
    return list(struct.unpack('<'+'I'*count, ref.cpu.mem_read(address,count*4)))


def string(ref, slot, value):
    data=value.encode('ascii')
    ref.cpu.mem_write(slot,struct.pack('<HHI',len(data),len(data)+1,slot+16))
    ref.cpu.mem_write(slot+16,data+b'\0')
    return slot


def observe(ref, label, ordinal, args, size=64, output=OUT):
    ref.cpu.mem_write(IOSB,b'\xcd'*8)
    before=bytes(ref.cpu.mem_read(output,size))
    status=ref.call(ordinal,*args)
    after=bytes(ref.cpu.mem_read(output,size))
    return dict(case=label,status=f'{status:08x}',iosb=words(ref,IOSB),
                unchanged=before==after,output=after.hex())


def qualify(ref,rows):
    root=r'\Device\Harddisk0\Partition1\XLLE-contract-family'
    ref.name(root)
    status=ref.call(190,0x10000300,0x100001,0x10000100,IOSB,0,0x10,3,2,0x21)
    assert status==0, f'isolated directory create {status:08x}'
    directory=words(ref,0x10000300,1)[0]
    assert ref.call(187,directory)==0
    buffer,capacity=ref.allocate_buffer(16384)
    payload=bytes((i*29+(i>>5))&255 for i in range(9001))
    for name in ('Alpha.bin','Beta.dat','Plain'):
        status,handle=ref.create(root+'\\'+name)
        assert status==0
        ref.cpu.mem_write(buffer,payload)
        assert ref.call(236,handle,0,0,0,IOSB,buffer,len(payload),0)==0
        assert ref.call(187,handle)==0
    opened=ref.open(root+r'\Alpha.bin',0xc0110080,0x60)
    assert opened['status']=='00000000'
    handle=opened['handle']
    for kind,size in ((34,56),(34,64),(34,55),(34,52),(14,8),(14,16),(14,7),(5,23),(5,24),(5,64),
                      (4,39),(4,40),(4,64),(0,64),(37,64),(999,64)):
        ref.cpu.mem_write(OUT-4,b'\xcd'*136)
        row=observe(ref,f'query-{kind}-{size}',211,[handle,IOSB,OUT,size,kind],128)
        assert bytes(ref.cpu.mem_read(OUT-4,4))==b'\xcd'*4
        assert bytes(ref.cpu.mem_read(OUT+size,4))==b'\xcd'*4
        rows.append(row)
        data=bytes.fromhex(row['output'])
        minimum={34:56,14:8,5:24,4:40}.get(kind)
        if minimum is None:
            expect(row,0xc0000003,unchanged=True)
        elif size<minimum:
            expect(row,0xc0000004,unchanged=True)
        elif kind in (4,5):
            expect(row,0xc000000d)
            assert data[:size]==bytes(size) and data[size:]==b'\xcd'*(128-size),row
        else:
            expect(row,0,minimum)
            if kind==34:
                assert struct.unpack_from('<QQ',data,32)==(9001,9001),row
                assert data[52:size]==bytes(size-52),row
            else:
                assert data[:8]==bytes(8),row
            written=size if kind==34 else 8
            assert data[written:]==b'\xcd'*(128-written),row
    for size in (23,24,64):
        ref.cpu.mem_write(OUT,b'\xcd'*128)
        row=observe(ref,f'volume-3-{size}',218,[handle,IOSB,OUT,size,3],128)
        rows.append(row)
        if size<24:
            expect(row,0xc0000004,unchanged=True)
        else:
            expect(row,0,24)
            data=bytes.fromhex(row['output']);total,available,sectors,sector_size=struct.unpack_from('<QQII',data)
            assert total>0 and available<=total and (sectors,sector_size)==(32,512),row
            assert data[24:size]==bytes(size-24) and data[size:]==b'\xcd'*(128-size),row
    for kind,size,value,completion,length in ((14,8,7,0,9001),(14,7,3,None,9001),
        (14,8,-1,None,9001),(20,8,17,512,17),(20,7,18,None,17),
        (19,8,32768,512,17),(19,8,32768,0,17),(20,8,17,0,17)):
        ref.cpu.mem_write(OUT,struct.pack('<q',value)+b'\xcd'*56)
        rows.append(observe(ref,f'set-{kind}-{size}-{value}',226,[handle,IOSB,OUT,size,kind]))
        status,data=ref.query(handle,34,56)
        assert status==0
        rows[-1]['length']=struct.unpack_from('<Q',data,40)[0]
        rows[-1]['allocation']=struct.unpack_from('<Q',data,32)[0]
        expect(rows[-1],0xc0000004 if size<8 else 0xc000000d if value<0 else 0,
               completion,unchanged=True)
        assert rows[-1]['length']==rows[-1]['allocation']==length,rows[-1]
    for offset in (-1,-2,0,17,32768):
        ref.cpu.mem_write(0x10000330,struct.pack('<q',offset))
        ref.cpu.mem_write(buffer,b'\xcd'*128)
        rows.append(observe(ref,f'read-offset-{offset}',219,
            [handle,0,0,0,IOSB,buffer,32,0x10000330],128,buffer))
        row=rows[-1]
        if offset==-1 or offset>=17:
            expect(row,0xc000000d if offset==-1 else 0xc0000011,unchanged=True)
        else:
            start=7 if offset==-2 else 0;count=17-start
            expect(row,0,count)
            assert bytes.fromhex(row['output'])==payload[start:17]+b'\xcd'*(128-count),row
    assert ref.call(187,handle)==0
    for ordinal,args in ((219,[handle,0,0,0,IOSB,buffer,32,0]),
                         (211,[handle,IOSB,OUT,56,34]),(198,[handle,IOSB]),
                         (207,[handle,0,0,0,IOSB,OUT,256,1,0,0])):
        output=buffer if ordinal==219 else OUT
        ref.cpu.mem_write(output,b'\xcd'*512)
        rows.append(observe(ref,f'closed-{ordinal}',ordinal,args,output=output))
        expect(rows[-1],0xc0000008,unchanged=True)
    write_only=ref.open(root+r'\Alpha.bin',0x40100000,0x60)
    assert write_only['status']=='00000000'
    handle=write_only['handle']
    rows.append(observe(ref,'read-write-only',219,[handle,0,0,0,IOSB,buffer,32,0],64,buffer))
    expect(rows[-1],0xc0000022,unchanged=True)
    assert ref.call(187,handle)==0
    for mask,amount,kind in (('*.*',256,1),('*.bin',256,1),('ALPHA.BIN',256,1),
                             ('absent',256,1),('*',63,1),('*',64,1),('*',68,1),('*',256,2)):
        opened=ref.open(root,0x100001,0x21);assert opened['status']=='00000000'
        directory=opened['handle'];pattern=string(ref,0x10000600,mask)
        for step,restart in enumerate((0,0,0,0,1)):
            ref.cpu.mem_write(OUT-4,b'\xcd'*520)
            row=observe(ref,f'dir-{mask}-{amount}-{kind}-{step}',207,
                [directory,0,0,0,IOSB,OUT,amount,kind,pattern,restart],512)
            row['prefix_unchanged']=bytes(ref.cpu.mem_read(OUT-4,4))==b'\xcd'*4
            row['suffix_unchanged']=bytes(ref.cpu.mem_read(OUT+amount,4))==b'\xcd'*4
            data=bytes.fromhex(row['output'])
            if data[:4]!=b'\xcd'*4:
                n=struct.unpack_from('<I',data,60)[0]
                row['name_length']=n
                row['name']=data[64:64+min(n,max(0,amount-64))].decode('ascii')
                row['next_entry']=struct.unpack_from('<I',data,0)[0]
                row['file_index']=struct.unpack_from('<I',data,4)[0]
            rows.append(row)
            assert row['prefix_unchanged'],row
            entries=['Alpha.bin','Beta.dat','Plain'] if mask in ('*','*.*') else [] if mask=='absent' else ['Alpha.bin']
            index=0 if restart else step
            if kind!=1:
                expect(row,0xc0000003,unchanged=True)
            elif not entries and step==0:
                expect(row,0xc000000f,unchanged=True)
            elif not entries or index>=len(entries):
                expect(row,0x80000006,0,unchanged=True)
            else:
                name_bytes=entries[index].encode('ascii')
                count=len(name_bytes) if amount<64 else min(len(name_bytes),amount-64)
                expect(row,0 if count==len(name_bytes) else 0x80000005,64+count)
                assert row['next_entry']==row['file_index']==0,row
                assert row['name_length']==len(name_bytes) and data[64:64+count]==name_bytes[:count],row
                length=17 if index==0 else 9001
                assert struct.unpack_from('<QQ',data,40)==(length,length),row
                assert data[64+count:]==b'\xcd'*(512-64-count),row
            # Preserve evidence of the original unsigned-short-buffer overflow,
            # not an expectation that a native replacement reproduce it.
            assert row['suffix_unchanged']==(amount!=63 or index>=len(entries)),row
        assert ref.call(187,directory)==0
    # Actual object-manager names and retained link object after unlink.
    name=string(ref,0x10000600,r'\??\XLLE-contract:')
    target=string(ref,0x10000a00,root)
    assert ref.call(67,name,target)==0
    rows.append(dict(case='link-collision',status=f'{ref.call(67,name,target):08x}'))
    assert rows[-1]['status']=='c0000035'
    ref.cpu.mem_write(0x10000100,struct.pack('<III',0,name,0x40))
    assert ref.call(203,0x10000300,0x10000100)==0
    link=words(ref,0x10000300,1)[0]
    for size in (0,2,len(root)-1,len(root),len(root)+1):
        ref.cpu.mem_write(OUT,b'\xcd'*512)
        ref.cpu.mem_write(0x10000e00,struct.pack('<HHI',0xabcd,size,OUT))
        ref.cpu.mem_write(0x10000e10,b'\xcd'*4)
        row=observe(ref,f'link-query-{size}',215,[link,0x10000e00,0x10000e10])
        row.update(length=struct.unpack('<H',ref.cpu.mem_read(0x10000e00,2))[0],
                   required=words(ref,0x10000e10,1)[0])
        rows.append(row)
        copied=min(size,len(root))
        expect(row,0xc0000023 if copied<len(root) else 0)
        assert row['length']==copied and row['required']==len(root),row
        assert bytes.fromhex(row['output'])==root.encode('ascii')[:copied]+b'\xcd'*(64-copied),row
    assert ref.call(69,name)==0
    rows.append(observe(ref,'unlinked-object-query',215,[link,0x10000e00,0x10000e10]))
    expect(rows[-1],0,unchanged=True)
    assert ref.call(187,link)==0
    # Resource reuse with actual original handles and namespace each cycle.
    for cycle in range(64):
        assert ref.call(67,name,target)==0
        opened=ref.open(r'\??\XLLE-contract:\Alpha.bin')
        assert opened['status']=='00000000'
        status,iosb,data=ref.read(opened['handle'],buffer,32,0)
        assert status==0 and iosb==(0,17) and data==payload[:17]
        assert ref.call(187,opened['handle'])==0
        assert ref.call(69,name)==0
    ref.release(buffer)
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('snapshot','library','dependencies','hdd','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    with os.add_dll_directory(str(args.dependencies.resolve())):
        ref=FilesystemReference(args.snapshot,args.library,args.hdd)
        result=dict(snapshot_sha256=SNAPSHOT_SHA,library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),
                    image=str(args.hdd.resolve()),write_policy='read-only backing; COW overlay',error=None,cases=[])
        try:
            qualify(ref,result['cases'])
            result['repeated_mount_open_read_close_unlink']=64
        except Exception as exc:
            result['error']=f'{type(exc).__name__}: {exc}'
            result['traceback']=traceback.format_exc()
        finally:
            result.update(last_ordinal=ref.last_ordinal,last_blocks=ref.last_blocks,
                interrupts=ref.interrupts,sectors_read=ref.device.xlle_storage_reads(ref.instance),
                sectors_written=ref.device.xlle_storage_writes(ref.instance))
            ref.close()
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({key:result[key] for key in ('error','last_ordinal','interrupts','sectors_read','sectors_written')}))
    return int(result['error'] is not None)


if __name__=='__main__':
    raise SystemExit(main())
