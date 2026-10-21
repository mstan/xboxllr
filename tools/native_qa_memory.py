"""Windows development QA transport for a receipt-identified native title.

This opens only the exact private child/image in the launch receipt. Reads and
briefly frozen writes stay in committed guest RAM; execution-context slots and
MMIO are excluded. It exposes no host-call, hardware or storage-write service.
Title object layouts and code hashes are supplied separately as revision data.
"""
from pathlib import Path
import contextlib,ctypes as c,hashlib,json,struct,sys,time
from ctypes import wintypes as w

class NativeQaMemory:
    # The 64 MiB limit is physical commitment, not the user virtual-address
    # bound. Exclude runtime execution-context reservations and all MMIO.
    ranges=((0x10000,0x60000000),(0x80000000,0x84000000),(0xd0000000,0xd4000000))
    def valid(self,at,size):return 0<size<=0x1000000 and any(lo<=at and at+size<=hi for lo,hi in self.ranges)
    def __init__(self,run,facts,write=False):
        self.run=Path(run)
        if self.run.resolve().drive.upper()=='C:':raise ValueError('keep QA outputs off C:')
        self.receipt=json.loads((self.run/'launch.aot-private.json').read_text())
        if sys.platform!='win32':raise RuntimeError('native process QA currently requires Windows')
        self.facts=facts
        self.pid=self.receipt['pid'];command=self.receipt['command']
        if self.receipt.get('owner_state_modified') is not False:raise ValueError('requires private QA state')
        if hashlib.sha256(Path(command[1]).read_bytes()).hexdigest()!=self.facts['xbe_sha256']:raise ValueError('wrong original image')
        self.k=c.WinDLL('kernel32',use_last_error=True);self.n=c.WinDLL('ntdll')
        self.k.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];self.k.OpenProcess.restype=w.HANDLE
        self.k.CloseHandle.argtypes=[w.HANDLE]
        for name in ('ReadProcessMemory','WriteProcessMemory'):
            fn=getattr(self.k,name);fn.argtypes=[w.HANDLE,c.c_void_p,c.c_void_p,c.c_size_t,c.POINTER(c.c_size_t)];fn.restype=w.BOOL
        self.k.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,c.POINTER(w.DWORD)]
        self.k.QueryFullProcessImageNameW.restype=w.BOOL
        self.k.GetProcessTimes.argtypes=[w.HANDLE]+[c.POINTER(w.FILETIME)]*4
        self.k.GetProcessTimes.restype=w.BOOL
        self.k.GetExitCodeProcess.argtypes=[w.HANDLE,c.POINTER(w.DWORD)]
        self.k.GetExitCodeProcess.restype=w.BOOL
        self.n.NtSuspendProcess.argtypes=self.n.NtResumeProcess.argtypes=[w.HANDLE]
        self.n.NtSuspendProcess.restype=self.n.NtResumeProcess.restype=c.c_long
        self.handle=self.k.OpenProcess(0x410|(0x828 if write else 0),False,self.pid)
        if not self.handle:raise OSError(c.get_last_error(),'OpenProcess')
        try:
            size=w.DWORD(32768);exe=c.create_unicode_buffer(size.value)
            if not self.k.QueryFullProcessImageNameW(self.handle,0,exe,c.byref(size)):raise OSError('process identity unavailable')
            if Path(exe.value).resolve()!=Path(command[0]).resolve():raise ValueError('PID no longer identifies this QA child')
            created,exited,kernel,user=(w.FILETIME() for _ in range(4))
            if not self.k.GetProcessTimes(self.handle,c.byref(created),c.byref(exited),c.byref(kernel),c.byref(user)):raise OSError('process creation time unavailable')
            timestamp=((created.dwHighDateTime<<32)|created.dwLowDateTime)/10000000-11644473600
            if not 0<=(self.run/'launch.aot-private.json').stat().st_mtime-timestamp<15:raise ValueError('launch receipt does not identify this process lifetime')
            class MBI(c.Structure):
                _fields_=[('BaseAddress',c.c_void_p),('AllocationBase',c.c_void_p),('AllocationProtect',w.DWORD),('RegionSize',c.c_size_t),('State',w.DWORD),('Protect',w.DWORD),('Type',w.DWORD)]
            self.MBI=MBI;assert c.sizeof(MBI)==48
            self.k.VirtualQueryEx.argtypes=[w.HANDLE,c.c_void_p,c.POINTER(MBI),c.c_size_t];self.k.VirtualQueryEx.restype=c.c_size_t
            signature=Path(command[1]).read_bytes()[:64]
            cache=self.run/'guest-qa-base.aot-private.json'
            saved=json.loads(cache.read_text()) if cache.exists() else {}
            if saved.get('pid')==self.pid and self._raw(saved['base']+0x10000,64,optional=True)==signature:self.base=saved['base']
            else:
                at=0;bases=set()
                while at<0x7fffffffffff:
                    m=MBI()
                    if not self.k.VirtualQueryEx(self.handle,c.c_void_p(at),c.byref(m),c.sizeof(m)):break
                    if m.Type==0x20000 and m.AllocationBase and m.RegionSize>=0x4000000:bases.add(m.AllocationBase)
                    nxt=(m.BaseAddress or 0)+m.RegionSize
                    if nxt<=at:break
                    at=nxt
                matches=[a for a in bases if self._raw(a+0x10000,64,optional=True)==signature]
                if len(matches)!=1:raise ValueError('ambiguous guest mapping')
                self.base=matches[0];cache.write_text(json.dumps({'pid':self.pid,'base':self.base}))
            for f in self.facts['functions']:
                if hashlib.sha256(self.read(f['address'],f['bytes'])).hexdigest()!=f['sha256']:raise ValueError('QA layout source changed')
            self.writable=write;self.is_frozen=False
        except BaseException:self.close();raise
    def close(self):
        if self.handle:self.k.CloseHandle(self.handle);self.handle=None
    def ensure_alive(self):
        code=w.DWORD()
        if not self.k.GetExitCodeProcess(self.handle,c.byref(code)):raise OSError('process state unavailable')
        if code.value!=259:raise ChildProcessError('native QA child exited with status '+str(code.value))
    def _raw(self,at,size,optional=False):
        buf=c.create_string_buffer(size);got=c.c_size_t()
        if not self.k.ReadProcessMemory(self.handle,c.c_void_p(at),buf,size,c.byref(got)) or got.value!=size:
            if optional:return None
            raise OSError(c.get_last_error(),'guest read')
        return buf.raw
    def read(self,at,size):
        if not self.valid(at,size):raise ValueError('guest RAM range')
        return self._raw(self.base+at,size)
    def u32(self,at):return struct.unpack('<I',self.read(at,4))[0]
    @contextlib.contextmanager
    def frozen(self):
        if not self.writable:raise ValueError('read-only connection')
        self.ensure_alive()
        status=self.n.NtSuspendProcess(self.handle)
        if status:raise OSError(status,'NtSuspendProcess')
        try:self.is_frozen=True;yield
        finally:
            self.is_frozen=False
            status=self.n.NtResumeProcess(self.handle)
            if status:raise OSError(status,'NtResumeProcess')
    def write(self,at,data):
        if not self.writable or not self.is_frozen or not self.valid(at,len(data)) or len(data)>64:raise ValueError('guest write extent/state')
        m=self.MBI()
        if (not self.k.VirtualQueryEx(self.handle,c.c_void_p(self.base+at),c.byref(m),c.sizeof(m)) or
            m.State!=0x1000 or m.Protect&0x101 or not m.Protect&0xcc or
            self.base+at+len(data)>(m.BaseAddress or 0)+m.RegionSize):raise ValueError('guest target is not writable RAM')
        copied=c.c_size_t();buf=c.create_string_buffer(data)
        if not self.k.WriteProcessMemory(self.handle,c.c_void_p(self.base+at),buf,len(data),c.byref(copied)) or copied.value!=len(data):raise OSError(c.get_last_error(),'guest write')
    def find_values(self,values):
        self.ensure_alive()
        if not values or any(not isinstance(v,int) or not 0<v<=0xffffffff for v in values):raise ValueError('invalid scan values')
        found={name:[] for name in values.values()}
        for begin,limit in self.ranges:
          at=begin
          while at<limit:
            m=self.MBI()
            if not self.k.VirtualQueryEx(self.handle,c.c_void_p(self.base+at),c.byref(m),c.sizeof(m)):break
            end=min(limit,(m.BaseAddress or 0)+m.RegionSize-self.base)
            if end<=at:break
            if m.State==0x1000 and not m.Protect&0x101:
                block=at
                while block<end:
                    count=min(0x100000,end-block);data=self._raw(self.base+block,count,optional=True)
                    if data:
                        for value,name in values.items():
                            needle=struct.pack('<I',value);offset=0
                            while True:
                                offset=data.find(needle,offset)
                                if offset<0:break
                                if (block+offset)%4==0:found[name].append(block+offset)
                                offset+=4
                    block+=count
            at=end
        return found
    def record(self,event):
        event.update(time=time.time(),pid=self.pid)
        with (self.run/'qa-actions.aot-private.jsonl').open('a') as log:log.write(json.dumps(event)+'\n')
