#!/usr/bin/env python3
"""Private original filesystem execution over the maintained IDE/PIC floor.

No replaced kernel calls or synthesized I/O completion. The HDD is mounted with
a volatile COW overlay; source image bytes are never modified. This tool does
not yet qualify a title-selected LLE file service or persistence across runs.
"""
import argparse
import ctypes
import hashlib
import json
import os
import struct
from pathlib import Path

from unicorn import UC_HOOK_BLOCK
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP,
                              UC_X86_REG_EFLAGS, UC_X86_REG_CS)
from native_io_device_reference import Reference, SNAPSHOT_SHA


class FilesystemReference(Reference):
    def __init__(self, snapshot, library, image):
        super().__init__(snapshot)
        self.enable_kernel_segments()
        self.device = ctypes.CDLL(str(library.resolve()))
        api = self.device
        api.xlle_storage_create.argtypes = [ctypes.c_char_p, ctypes.c_void_p,
                                            ctypes.c_uint32, ctypes.c_uint16, ctypes.c_uint16]
        api.xlle_storage_create.restype = ctypes.c_void_p
        api.xlle_storage_destroy.argtypes = [ctypes.c_void_p]
        api.xlle_storage_error.argtypes = [ctypes.c_void_p]
        api.xlle_storage_error.restype = ctypes.c_char_p
        api.xlle_storage_port.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                          ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_uint32)]
        api.xlle_storage_vector.argtypes = [ctypes.c_void_p, ctypes.c_int]
        for name in ('reads', 'writes', 'overlay'):
            fn = getattr(api, 'xlle_storage_'+name)
            fn.argtypes = [ctypes.c_void_p]
            fn.restype = ctypes.c_uint64
        self.instance = api.xlle_storage_create(os.fsencode(image.resolve()), self.host,
            len(self.ram), self.word(0x8003d88c) & 0xffff, self.word(0x8003d890) & 0xffff)
        if not self.instance:
            raise RuntimeError('cannot attach read-only reference HDD')
        self.pending_irq = False
        self.interrupts = 0
        self.ports = []
        self.last_blocks = []
        self.cpu.hook_add(UC_HOOK_BLOCK, self.block)
        # Isolate this synchronous qualification caller from unrelated boot
        # workers. This is an actual kernel scheduling API call, not an edited
        # ready queue or replaced wait. Scheduling under contention is separate.
        self.thread = self.word(0x8003da64)
        self.previous_priority = self.call(148, self.thread, 31)

    def close(self):
        if self.instance:
            self.device.xlle_storage_destroy(self.instance)
            self.instance = None

    def port(self, port, size, write, value=0):
        result = ctypes.c_uint32(value)
        okay = self.device.xlle_storage_port(self.instance, port, size, write, ctypes.byref(result))
        if len(self.ports) < 128:
            self.ports.append(dict(port=f'{port:04x}', size=size, write=write, value=result.value))
        if not okay:
            why = self.device.xlle_storage_error(self.instance)
            self.stop(f'{why.decode() if why else "storage failure"}: port {port:04x}')
        return result.value

    def port_in(self, cpu, port, size, user):
        return self.port(port, size, 0)

    def port_out(self, cpu, port, size, value, user):
        self.port(port, size, 1, value)

    def inject(self, vector):
        # Captured IDT, flat ring 0. Original handler performs priority changes,
        # ISR/DPC dispatch, EOI and IRET. Do not replace its completion work.
        low = self.word(0x8003dcc0 + vector*8)
        high = self.word(0x8003dcc4 + vector*8)
        if low >> 16 != 8 or high & 0xffff != 0x8e00:
            raise RuntimeError(f'unqualified interrupt gate {vector:02x}')
        flags = self.cpu.reg_read(UC_X86_REG_EFLAGS)
        stack = self.cpu.reg_read(UC_X86_REG_ESP)-12
        self.cpu.mem_write(stack, struct.pack('<III', self.cpu.reg_read(UC_X86_REG_EIP),
                                             self.cpu.reg_read(UC_X86_REG_CS), flags))
        self.cpu.reg_write(UC_X86_REG_ESP, stack)
        self.cpu.reg_write(UC_X86_REG_EFLAGS, flags & ~(0x100|0x200|0x4000|0x10000|0x20000))
        self.cpu.reg_write(UC_X86_REG_EIP, (low & 65535) | (high & 0xffff0000))
        self.interrupts += 1

    def interrupt(self, cpu, number, user):
        try:
            if not 0x20 <= number < 0x40:
                raise RuntimeError(f'unqualified CPU exception {number}')
            self.inject(number)
        except Exception as exc:
            self.stop(str(exc))

    def block(self, cpu, address, size, user):
        self.last_blocks.append(f'{address:08x}')
        self.last_blocks = self.last_blocks[-16:]
        if self.device.xlle_storage_vector(self.instance, 0) >= 0 and cpu.reg_read(UC_X86_REG_EFLAGS) & 0x200:
            self.pending_irq = True
            cpu.emu_stop()

    def call(self, ordinal, *args):
        self.last_ordinal=ordinal
        sp, end = 0x10008000, 0x10009000
        entry = self.base + self.word(self.export_table + 4*(ordinal-self.ordinal_base))
        self.cpu.mem_write(sp, struct.pack('<'+'I'*(len(args)+1), end, *args))
        self.cpu.reg_write(UC_X86_REG_ESP, sp)
        self.cpu.reg_write(UC_X86_REG_EFLAGS, 0x202)
        self.failure = None
        # A FATX free-cluster scan on the real HDD can require hundreds of
        # device transfers. Permit bounded real work, without skipping scans.
        for _ in range(8192):
            self.pending_irq = False
            self.cpu.emu_start(entry, end, count=1000000)
            if self.failure:
                raise RuntimeError(self.failure)
            entry = self.cpu.reg_read(UC_X86_REG_EIP)
            if entry == end:
                break
            if not self.pending_irq:
                raise RuntimeError(f'original call {ordinal} did not return at {entry:08x}')
            vector = self.device.xlle_storage_vector(self.instance, 1)
            if vector < 0:
                raise RuntimeError('PIC changed before INTA')
            self.inject(vector)
            entry = self.cpu.reg_read(UC_X86_REG_EIP)
        else:
            raise RuntimeError('original file call interrupt budget exhausted')
        if self.cpu.reg_read(UC_X86_REG_ESP) != sp+4*(len(args)+1):
            raise RuntimeError('original file call stack imbalance')
        # Original filesystem/cache allocation can retire or remap pool pages.
        for page, old in list(self.pages.items()):
            try:
                current = self.physical(page)
            except ValueError:
                current = None
            if current != old:
                self.cpu.mem_unmap(page, 4096)
                del self.pages[page]
        return self.cpu.reg_read(UC_X86_REG_EAX)

    def name(self, name):
        encoded = name.encode('ascii')
        if len(encoded) > 1023:
            raise ValueError('reference path too long')
        self.cpu.mem_write(0x10000100, struct.pack('<III', 0, 0x10000120, 0x40))
        self.cpu.mem_write(0x10000120, struct.pack('<HHI', len(encoded), len(encoded)+1, 0x10000400))
        self.cpu.mem_write(0x10000400, encoded+b'\0')
        self.cpu.mem_write(0x10000300, b'\xcd'*24)

    def open(self, name, access=0x100001, options=0x20):
        self.name(name)
        status = self.call(202, 0x10000300, access, 0x10000100, 0x10000310, 3, options)
        return dict(path=name, status=f'{status:08x}',
                    handle=struct.unpack('<I', self.cpu.mem_read(0x10000300,4))[0],
                    iosb=list(struct.unpack('<II', self.cpu.mem_read(0x10000310,8))))

    def create(self, name):
        self.name(name)
        status=self.call(190,0x10000300,0xc0110080,0x10000100,0x10000310,
                         0,0x80,3,2,0x60)
        handle=struct.unpack('<I',self.cpu.mem_read(0x10000300,4))[0]
        return status,handle

    def query(self, handle, kind, size):
        self.cpu.mem_write(0x10001000, b'\xcd'*(size+8))
        status = self.call(211, handle, 0x10000310, 0x10001004, size, kind)
        data = bytes(self.cpu.mem_read(0x10001000, size+8))
        assert data[:4] == data[-4:] == b'\xcd'*4, 'query bounds'
        return status, data[4:-4]

    def allocate_buffer(self, size):
        slots=0x10000200
        self.cpu.mem_write(slots, struct.pack('<II', 0, size))
        status=self.call(184, slots, 0, slots+4, 0x3000, 4)
        if status:
            raise RuntimeError(f'original buffer allocation failed: {status:08x}')
        base,actual=struct.unpack('<II', self.cpu.mem_read(slots,8))
        for page in range(base,base+actual,4096):
            if page not in self.pages:
                assert self.map_page(self.cpu,0,page,1,0,None)
        return base,actual

    def read(self, handle, buffer, size, offset):
        self.cpu.mem_write(0x10000310,b'\xcd'*8)
        if offset is not None:
            self.cpu.mem_write(0x10000330,struct.pack('<q',offset))
        status=self.call(219,handle,0,0,0,0x10000310,buffer,size,
                         0x10000330 if offset is not None else 0)
        iosb=struct.unpack('<II',self.cpu.mem_read(0x10000310,8))
        if iosb[1] > size and status == 0:
            raise RuntimeError('original read exceeds caller buffer')
        data=bytes(self.cpu.mem_read(buffer,iosb[1])) if status == 0 else b''
        return status,iosb,data

    def release(self, buffer):
        slots=0x10000200
        self.cpu.mem_write(slots,struct.pack('<II',buffer,0))
        assert self.call(199,slots,slots+4,0x8000) == 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--dependencies', type=Path)
    parser.add_argument('--hdd', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--path', default=r'\Device\Harddisk0\Partition2\xboxdash.xbe')
    parser.add_argument('--expected', type=Path, help='independently extracted file for exact byte comparison')
    args = parser.parse_args()
    dll_directory = os.add_dll_directory(str(args.dependencies.resolve())) if args.dependencies else None
    ref = FilesystemReference(args.snapshot, args.library, args.hdd)
    result = dict(snapshot_sha256=SNAPSHOT_SHA,
                  library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),
                  image=str(args.hdd.resolve()), image_bytes=args.hdd.stat().st_size,
                  image_write_policy='volatile-overlay; backing read-only', cases=[],
                  caller_priority=31, original_caller_priority=ref.previous_priority)
    error = None
    try:
        row = ref.open(args.path)
        result['cases'].append(row)
        assert row['status'] == '00000000', 'qualification file did not open'
        handle=row['handle']
        # The original FATX driver supports FileNetworkOpenInformation here;
        # desktop FileStandardInformation is not an interchangeable oracle.
        status,data=ref.query(handle,34,56)
        assert status == 0, f'network-open query: {status:08x}'
        *times,allocated,length,attributes=struct.unpack('<6QI4x',data)
        result['network_open']=dict(allocated=allocated,length=length,
                                   attributes=attributes,times=times)
        status,_=ref.query(handle,5,24)
        result['standard_query_status']=f'{status:08x}'
        assert status == 0xc000000d
        buffer,capacity=ref.allocate_buffer(65536)
        digest=hashlib.sha256()
        total=0
        while total < length:
            amount=min(capacity,length-total)
            status,iosb,data=ref.read(handle,buffer,amount,None)
            assert status == 0 and iosb == (0,amount), (status,iosb,total)
            digest.update(data)
            total+=amount
        result['read']=dict(bytes=total,sha256=digest.hexdigest())
        status,position=ref.query(handle,14,8)
        assert status == 0 and struct.unpack('<Q',position)[0] == length
        status,iosb,data=ref.read(handle,buffer,32,None)
        result['eof']=dict(status=f'{status:08x}',iosb=list(iosb))
        # This synchronous original error preserves the caller's IOSB. Do not
        # derive Xbox failure effects from the desktop NT implementation.
        assert status == 0xc0000011 and iosb == (0xcdcdcdcd,0xcdcdcdcd)
        if args.expected:
            expected=args.expected.read_bytes()
            assert len(expected) == length and hashlib.sha256(expected).hexdigest() == digest.hexdigest(), 'independent file mismatch'
            status,iosb,data=ref.read(handle,buffer,37,19)
            assert status == 0 and iosb == (0,37) and data == expected[19:56]
            result['independent_byte_match']=True
        cache=[]
        for pages in (0,1,2,16,32,2048,2049,16):
            before=ref.call(35)
            status=ref.call(37,pages)
            after=ref.call(35)
            assert (status,after) == ((0,pages) if pages<=2048 else (0xc000000d,before))
            ref.call(36)
            read_status,iosb,data=ref.read(handle,buffer,37,19)
            # Original FATX needs one cache page for this unaligned read.
            expected_status=0 if after else 0xc0000017
            assert read_status == expected_status, (pages,read_status,iosb)
            assert iosb == ((0,37) if after else (0xcdcdcdcd,0xcdcdcdcd))
            if args.expected and not read_status:
                assert data == expected[19:56]
            cache.append(dict(request=pages,status=f'{status:08x}',size=after,
                              read_status=f'{read_status:08x}',iosb=list(iosb)))
        result['cache_resize_invalidate_reads']=cache
        ref.release(buffer)
        assert ref.call(187,handle) == 0
        assert ref.call(187,handle) == 0xc0000008
        missing=ref.open(args.path+'.XLLE-does-not-exist')
        result['cases'].append(missing)
        assert missing['status'] == 'c0000034'
        # Original unbuffered async dispatch may return PENDING even when its
        # actual IDE interrupt has already completed the request. Immediate
        # rejection instead clears notification and leaves IOSB untouched.
        asynchronous=ref.open(args.path,0x80100080,0x848)
        assert asynchronous['status'] == '00000000'
        handle=asynchronous['handle']
        buffer,capacity=ref.allocate_buffer(4096)
        assert ref.call(189,0x10000380,0,0,1) == 0
        event=struct.unpack('<I',ref.cpu.mem_read(0x10000380,4))[0]
        async_cases=[]
        for offset,amount,wanted in ((0,2048,0x103),(length,2048,0xc0000011),
                                     (-1,2048,0xc000000d),(-2,2048,0xc000000d),
                                     (1,2048,0xc000000d),(0,0,0)):
            assert ref.call(225,event,0) == 0
            ref.cpu.mem_write(0x10000330,struct.pack('<q',offset))
            ref.cpu.mem_write(0x10000310,b'\xcd'*8)
            status=ref.call(219,handle,event,0,0,0x10000310,buffer,amount,0x10000330)
            iosb=struct.unpack('<II',ref.cpu.mem_read(0x10000310,8))
            assert ref.call(209,event,0x10000390) == 0
            kind,signaled=struct.unpack('<II',ref.cpu.mem_read(0x10000390,8))
            ref.cpu.mem_write(0x100003f0,bytes(8))
            file_wait=ref.call(233,handle,0,0x100003f0)
            assert status == wanted and kind == 0, (offset,status,iosb,signaled)
            rejected=bool(status&0x80000000)
            assert iosb == ((0xcdcdcdcd,0xcdcdcdcd) if rejected else (0,amount))
            assert signaled == (0 if rejected else 1), (status,signaled)
            assert file_wait == 0x102, ('explicit-event request file signal',status,file_wait)
            if amount and not rejected and args.expected:
                assert bytes(ref.cpu.mem_read(buffer,amount)) == expected[:amount]
            async_cases.append(dict(offset=offset,length=amount,status=f'{status:08x}',
                                    iosb=list(iosb),event=signaled,file_wait=f'{file_wait:08x}'))
        result['async_completion_and_rejection']=async_cases
        no_event=[]
        for offset,wanted in ((0,0x103),(length,0xc0000011)):
            ref.cpu.mem_write(0x10000330,struct.pack('<q',offset))
            ref.cpu.mem_write(0x10000310,b'\xcd'*8)
            status=ref.call(219,handle,0,0,0,0x10000310,buffer,2048,0x10000330)
            iosb=struct.unpack('<II',ref.cpu.mem_read(0x10000310,8))
            file_wait=ref.call(233,handle,0,0x100003f0)
            assert status == wanted and file_wait == (0 if status == 0x103 else 0x102)
            assert iosb == ((0,2048) if status == 0x103 else (0xcdcdcdcd,0xcdcdcdcd))
            no_event.append(dict(offset=offset,status=f'{status:08x}',iosb=list(iosb),file_wait=f'{file_wait:08x}'))
        result['async_file_notification']=no_event
        assert ref.call(187,event) == 0 and ref.call(187,handle) == 0
        ref.release(buffer)
        # Writable FATX work changes only the IDE controller's overlay.
        path=r'\Device\Harddisk0\Partition1\XLLE-LLE-contract.tmp'
        status,handle=ref.create(path)
        assert status == 0, f'create: {status:08x}'
        buffer,capacity=ref.allocate_buffer(16384)
        payload=bytes((i*73+(i>>8))&255 for i in range(9001))
        ref.cpu.mem_write(buffer,payload)
        ref.cpu.mem_write(0x10000330,struct.pack('<q',0))
        status=ref.call(236,handle,0,0,0,0x10000310,buffer,len(payload),0x10000330)
        assert status == 0, f'write: {status:08x}'
        iosb=struct.unpack('<II',ref.cpu.mem_read(0x10000310,8))
        assert iosb == (0,len(payload)), iosb
        status=ref.call(198,handle,0x10000310)
        assert status == 0, f'flush: {status:08x}'
        assert ref.call(187,handle) == 0
        reopened=ref.open(path,0xc0110080,0x60)
        assert reopened['status'] == '00000000', reopened
        handle=reopened['handle']
        status,iosb,data=ref.read(handle,buffer,len(payload)+10,0)
        assert status == 0 and iosb == (0,len(payload)) and data == payload, 'reopen bytes/short read'
        root=ref.open('\\Device\\Harddisk0\\Partition1\\',0x100001,0x21)
        assert root['status'] == '00000000', root
        mask=path.rsplit('\\',1)[1].encode('ascii')
        ref.cpu.mem_write(0x10000600,struct.pack('<HHI',len(mask),len(mask)+1,0x10000620))
        ref.cpu.mem_write(0x10000620,mask+b'\0')
        status=ref.call(207,root['handle'],0,0,0,0x10000310,buffer,256,1,0x10000600,1)
        assert status == 0, f'filtered directory: {status:08x}'
        entry=bytes(ref.cpu.mem_read(buffer,256))
        name_length=struct.unpack_from('<I',entry,60)[0]
        file_length=struct.unpack_from('<Q',entry,40)[0]
        assert entry[64:64+name_length] == mask and file_length == len(payload), 'directory name/size'
        status=ref.call(207,root['handle'],0,0,0,0x10000310,buffer,256,1,0x10000600,0)
        assert status == 0x80000006, f'directory end: {status:08x}'
        result['filtered_directory']=dict(name=mask.decode(),bytes=file_length,end_status=f'{status:08x}')
        assert ref.call(187,root['handle']) == 0
        collision,_=ref.create(path)
        assert collision == 0xc0000035, f'create collision: {collision:08x}'
        ref.cpu.mem_write(0x10000340,b'\x01')
        status=ref.call(226,handle,0x10000310,0x10000340,1,13)
        assert status == 0, f'delete disposition: {status:08x}'
        assert ref.call(187,handle) == 0
        removed=ref.open(path)
        assert removed['status'] == 'c0000034', removed
        ref.release(buffer)
        result['write_flush_reopen_delete']=dict(bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),
                                                collision=f'{collision:08x}',removed=removed['status'])
    except Exception as exc:
        error = f'{type(exc).__name__}: {exc}'
    finally:
        result.update(error=error, interrupts=ref.interrupts, ports=ref.ports,
                      last_blocks=ref.last_blocks,last_ordinal=ref.last_ordinal,
                      sectors_read=ref.device.xlle_storage_reads(ref.instance),
                      sectors_written=ref.device.xlle_storage_writes(ref.instance),
                      overlay_sectors=ref.device.xlle_storage_overlay(ref.instance))
        ref.close()
        if dll_directory:
            dll_directory.close()
        args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({key:result[key] for key in ('cases','error','interrupts','sectors_read','sectors_written')}))
    return int(error is not None)


if __name__ == '__main__':
    raise SystemExit(main())
