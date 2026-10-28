/* Guest reservation ownership over the pilot's shifted 4 GiB VA range.
 * Host NT services supply actual commitment/protection/decommit behavior.
 * Xbox signatures and 32-bit MEMORY_BASIC_INFORMATION are documented by nxdk;
 * range/alignment constants were cross-checked against Cxbx. No donor service
 * bodies are incorporated. Physical allocation is bounded, not a full MMU. */
#define VM_END 0x7ffe0000u /* original captured retail kernel's query/allocation bound */
#define VM_INVALID 0xc000000du
#define VM_NOMEM 0xc0000017u
#define VM_CONFLICT 0xc0000018u
#define VM_NOT_ALLOCATED 0xc00000a0u
struct PilotRegion { uint32_t base,size,protect; int fixed; };
/* Host ownership records are not Xbox RAM. Grow independently of the real
 * 64 MiB commitment budget: hundreds of small GPU objects must not turn a
 * bookkeeping limit into a guest out-of-memory failure. Pointers into this
 * table are used only while vm_lock is held (or during one-time setup). */
static struct PilotRegion vm_initial_regions[512];
static struct PilotRegion *vm_regions=vm_initial_regions;
static unsigned vm_region_capacity=512;
static struct PilotRegion *vm_grow_regions(void)
{
    unsigned old=vm_region_capacity;
    /* Every reservation starts at a distinct 64 KiB boundary. */
    unsigned maximum=(VM_END+0xffffu)/0x10000u;
    if(old>=maximum)return NULL;
    unsigned capacity=old*2;if(capacity>maximum)capacity=maximum;
    struct PilotRegion *next=calloc(capacity,sizeof(*next));
    if(!next)return NULL;
    memcpy(next,vm_regions,old*sizeof(*next));
    if(vm_regions!=vm_initial_regions)free(vm_regions);
    vm_regions=next;vm_region_capacity=capacity;
    return &vm_regions[old];
}
static uint64_t vm_service_bytes; /* real native service buffers; guarded by vm_lock */
static INIT_ONCE vm_once=INIT_ONCE_STATIC_INIT;
typedef LONG (NTAPI *PilotNtAllocate)(HANDLE,PVOID*,ULONG_PTR,PSIZE_T,ULONG,ULONG);
typedef LONG (NTAPI *PilotNtFree)(HANDLE,PVOID*,PSIZE_T,ULONG);
typedef LONG (NTAPI *PilotNtProtect)(HANDLE,PVOID*,PSIZE_T,ULONG,PULONG);
static PilotNtAllocate vm_native_allocate;
static PilotNtFree vm_native_free;
static PilotNtProtect vm_native_protect;
/* Direct NT and Win32 operations publish to the same mapping authority. */
static LONG NTAPI vm_owner_allocate(HANDLE process,PVOID *address,ULONG_PTR zero_bits,
                                    PSIZE_T bytes,ULONG type,ULONG protect)
{
    LONG status=vm_native_allocate(process,address,zero_bits,bytes,type,protect);
    if(status>=0 && (type&MEM_COMMIT))pilot_commit_owner_publish(*address,*bytes,1);
    return status;
}
static LONG NTAPI vm_owner_free(HANDLE process,PVOID *address,PSIZE_T bytes,ULONG type)
{
    void *before=*address;SIZE_T requested=*bytes;
    LONG status=vm_native_free(process,address,bytes,type);
    if(status>=0)pilot_commit_owner_release(*address?*address:before,*bytes?*bytes:requested,(type&MEM_RELEASE)!=0);
    return status;
}

static void vm_fixed_region(uint32_t base,uint32_t size)
{
    if (!size || (uint64_t)base+size>VM_END) stop("invalid fixed VM reservation",base);
    for (unsigned i=0;i<vm_region_capacity;++i) if (!vm_regions[i].size) {
        vm_regions[i]=(struct PilotRegion){base,size,PAGE_READWRITE,1}; return;
    }
    struct PilotRegion *region=vm_grow_regions();
    if(!region)stop("fixed VM ownership allocation failed",base);
    *region=(struct PilotRegion){base,size,PAGE_READWRITE,1};
}
static BOOL CALLBACK vm_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once; (void)parameter; (void)context;
    HMODULE nt=GetModuleHandleW(L"ntdll.dll");
    vm_native_allocate=(PilotNtAllocate)(void*)GetProcAddress(nt,"NtAllocateVirtualMemory");
    vm_native_free=(PilotNtFree)(void*)GetProcAddress(nt,"NtFreeVirtualMemory");
    vm_native_protect=(PilotNtProtect)(void*)GetProcAddress(nt,"NtProtectVirtualMemory");
    if (!vm_native_allocate || !vm_native_free || !vm_native_protect) stop("native VM services unavailable",0);
    uint32_t size=file32(0x10c);
    if (size) {
        uint64_t end=(uint64_t)image_base+size+4095;
        if (end>0x60000000) stop("XBE overlaps pilot execution contexts",image_base);
        vm_fixed_region(image_base,(uint32_t)(end&~4095ull)-image_base);
    } else vm_fixed_region(0x10000,0x10000); /* contract-test input mapping */
    vm_fixed_region(0x60000000,0x4000000); /* title-thread address slots */
    vm_fixed_region(0x70000000,0x10000);
    vm_fixed_region(0x71000000,0x10000);
    vm_fixed_region(0x72000000,0x20000);
    return TRUE;
}
static void vm_ensure(void)
{
    if (!InitOnceExecuteOnce(&vm_once,vm_start,NULL,NULL)) stop("VM initialization failed",0);
}
static struct PilotRegion *vm_find(uint32_t base,uint32_t size)
{
    for (unsigned i=0;i<vm_region_capacity;++i) {
        struct PilotRegion *r=&vm_regions[i];
        if (r->size && base>=r->base && (uint64_t)base+size<=(uint64_t)r->base+r->size) return r;
    }
    return NULL;
}
/* Native service arenas have exclusive owners with page-accurate ledgers.
 * Device/original-kernel backends can publish other mappings in these ranges
 * and must keep the full host query. All callers serialize with vm_lock. */
#if !defined(PILOT_IO_DEVICE_LLE) && !defined(PILOT_MEMORY_LLE) && !defined(PILOT_USB_LLE)
#define PILOT_NATIVE_SERVICE_ACCOUNTING 1
static uint64_t vm_native_services_committed(void);
#endif
static uint64_t vm_committed_host(uint32_t base,uint64_t size)
{
    uint64_t count=0,end=(uint64_t)base+size;
    for (uint64_t at=base;at<end;) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(memory+at,&info,sizeof(info))) stop("host VM query failed",(uint32_t)at);
        uint64_t next=(unsigned char*)info.BaseAddress-memory+info.RegionSize;
        if (next>end) next=end;
        if (next<=at) stop("invalid host VM query range",(uint32_t)at);
        if (info.State==MEM_COMMIT) {
            count+=next-at;
#ifdef PILOT_IO_DEVICE_LLE
            /* Like the private interpreter's RAM, shared ABI/clock backing
             * is host implementation overhead, not a guest allocation. */
            const uint64_t ranges[][2]={{PILOT_IO_BRIDGE_BASE,PILOT_IO_BRIDGE_BYTES},
                {PILOT_IO_CLOCK_BASE,PILOT_IO_CLOCK_BYTES}};
            for(unsigned i=0;i<2;++i) {
                uint64_t lo=at>ranges[i][0]?at:ranges[i][0];
                uint64_t hi=next<ranges[i][0]+ranges[i][1]?next:ranges[i][0]+ranges[i][1];
                if(lo<hi)count-=hi-lo;
            }
#endif
        }
        at=next;
    }
    return count;
}
static uint64_t vm_committed(uint32_t base,uint64_t size)
{
    uint64_t owned;
    if(pilot_commit_owner_query(base,size,&owned))return owned;
#ifdef PILOT_NATIVE_SERVICE_ACCOUNTING
    if(!base && size==UINT64_C(0x100000000)) {
        /* Pool D0000000-D3FFFFFF and contiguous 80000000-83FFFFFF are
         * inaccessible to guest NtAllocateVirtualMemory (VM_END<80000000).
         * Avoid a syscall for each fragmented service page on every new GPU
         * resource. Query every other address exactly as before. */
        return vm_native_services_committed()+vm_committed_host(0,UINT64_C(0x80000000))+
            vm_committed_host(0x84000000u,UINT64_C(0x4c000000))+
            vm_committed_host(0xd4000000u,UINT64_C(0x2c000000));
    }
#endif
    return vm_committed_host(base,size);
}
static int vm_range(uint32_t base,uint32_t size,uint32_t alignment,uint32_t *low,uint32_t *bytes)
{
    if (!size || base>=VM_END || (uint64_t)base+size>VM_END) return 0;
    *low=base&~(alignment-1);
    *bytes=(uint32_t)(((uint64_t)base+size+4095)&~4095ull)-*low;
    return 1;
}
static uint32_t vm_choose(uint32_t size,uint32_t limit,int topdown)
{
    if (limit<0x10000 || size>limit-0x10000) return 0;
    uint32_t at=topdown ? (limit-size)&~0xffffu : 0x10000;
    for (;;) {
        int conflict=0;
        for (unsigned i=0;i<vm_region_capacity;++i) {
            struct PilotRegion *r=&vm_regions[i];
            if (!r->size || (uint64_t)at+size<=r->base || at>=(uint64_t)r->base+r->size) continue;
            if (topdown) {
                if (r->base<size+0x10000ull) return 0;
                at=(r->base-size)&~0xffffu;
            } else {
                uint64_t next=((uint64_t)r->base+r->size+0xffff)&~0xffffull;
                if (next+size>limit) return 0;
                at=(uint32_t)next;
            }
            conflict=1; break;
        }
        if (!conflict) return at;
    }
}
static uint32_t vm_allocate(uint32_t *base,uint32_t zero_bits,uint32_t *size,uint32_t type,uint32_t protect)
{
    /* Xbox MEM_NOZERO permits unspecified initial bytes; it does not require
     * stale data. Host commitment may supply zeros. Existing committed pages
     * retain their contents. Never forward this bit as Windows MEM_ROTATE. */
#ifndef PILOT_MEMORY_LLE
    type&=~0x00800000u;
#endif
    if (type&(0x00080000u|0x20000000u|0x80000000u))
        stop("guest reset/large-page allocation outside VM scope",type);
    if (!*size || zero_bits>21 || (type&~(MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN|0x00800000u)) || !(type&(MEM_RESERVE|MEM_COMMIT))) return VM_INVALID;
    /* Guard exceptions and executable/modified code require the CPU exception
     * and invalidation contracts; do not pretend they are qualified here. */
    if (protect&(PAGE_GUARD|PAGE_NOCACHE|PAGE_WRITECOMBINE|0x800)) stop("guest guard/cache protection outside VM scope",protect);
    if (protect!=PAGE_NOACCESS && protect!=PAGE_READONLY && protect!=PAGE_READWRITE &&
        protect!=PAGE_EXECUTE && protect!=PAGE_EXECUTE_READ && protect!=PAGE_EXECUTE_READWRITE)
        return 0xc0000045u;
    vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
    uint32_t result=0,low,bytes;
    int reserve=(type&MEM_RESERVE)!=0 || !*base;
    if (!vm_range(*base,*size,reserve && *base ? 0x10000 : 0x1000,&low,&bytes)) { result=VM_INVALID; goto done; }
    struct PilotRegion *region=NULL;
#ifdef PILOT_MEMORY_LLE
    if(zero_bits)stop("original VM constrained address search unqualified",zero_bits);
    if(reserve) {
        for(unsigned i=0;i<vm_region_capacity;++i)if(!vm_regions[i].size){region=vm_regions+i;break;}
        if(!region && !(region=vm_grow_regions())){result=VM_NOMEM;goto done;}
    } else if(!(region=vm_find(low,bytes)) || region->fixed){result=VM_CONFLICT;goto done;}
    if((type&MEM_COMMIT) && vm_committed(0,0x100000000ull)+vm_service_bytes+
       bytes-(*base?vm_committed(low,bytes):0)>64ull*1024*1024){result=VM_NOMEM;goto done;}
    uint32_t inputs[]={*base,*size,type,protect,zero_bits},outputs[7]={0};
    const char *error=pilot_io_device_vm(184,memory,inputs,outputs,&result);
    if(error)stop(error,184);
    if(!result) {
        if(reserve)*region=(struct PilotRegion){outputs[0],outputs[1],protect,0};
        *base=outputs[0];*size=outputs[1];
    }
#else
    if (reserve) {
        if (!*base) {
            uint64_t bound=zero_bits ? (0x100000000ull>>zero_bits) : VM_END;
            if (bound>VM_END) bound=VM_END;
            low=vm_choose(bytes,(uint32_t)bound,(type&MEM_TOP_DOWN)!=0);
            if (!low) { result=VM_NOMEM; goto done; }
        }
        if (low<0x10000) { result=VM_CONFLICT; goto done; }
        for (unsigned i=0;i<vm_region_capacity;++i) {
            struct PilotRegion *r=&vm_regions[i];
            if (!r->size) region=r;
            else if ((uint64_t)low+bytes>r->base && low<(uint64_t)r->base+r->size) { result=VM_CONFLICT; goto done; }
        }
        if (!region && !(region=vm_grow_regions())) { fprintf(stderr,"[VM FAILURE] host reservation ledger allocation failed: records=%u\n",vm_region_capacity); result=VM_NOMEM; goto done; }
    } else {
        region=vm_find(low,bytes);
        if (!region || region->fixed) { result=VM_CONFLICT; goto done; }
    }
    if (type&MEM_COMMIT) {
        uint64_t additional=bytes-vm_committed(low,bytes);
        if (vm_committed(0,0x100000000ull)+vm_service_bytes+additional>64ull*1024*1024) { fprintf(stderr,"[VM FAILURE] guest memory budget: requested=%llu resident=%llu service=%llu\n",(unsigned long long)additional,(unsigned long long)vm_committed(0,0x100000000ull),(unsigned long long)vm_service_bytes); result=VM_NOMEM; goto done; }
        PVOID host=memory+low; SIZE_T span=bytes;
#ifdef PILOT_MEMORY_WRITE_WATCH
        if(!pilot_write_watch_native_begin(host,bytes))stop("cannot relinquish write watch for commit",low);
#endif
        result=(uint32_t)vm_owner_allocate(GetCurrentProcess(),&host,0,&span,MEM_COMMIT,protect);
#ifdef PILOT_MEMORY_WRITE_WATCH
        pilot_write_watch_native_end(memory+low,bytes);
#endif
        if (result) goto done;
    }
    if (reserve) *region=(struct PilotRegion){low,bytes,protect,0};
    *base=low; *size=bytes;
#endif
done:
    ReleaseSRWLockExclusive(&vm_lock); return result;
}
static uint32_t vm_free(uint32_t *base,uint32_t *size,uint32_t type)
{
    if (type!=MEM_RELEASE && type!=MEM_DECOMMIT) return VM_INVALID;
    vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
    uint32_t result=0,low,bytes;
    struct PilotRegion *r=vm_find(*base,1);
    if (!r || r->fixed) { result=VM_NOT_ALLOCATED; goto done; }
    if (!*size) {
        if ((*base&~4095u)!=r->base) { result=0xc000009fu; goto done; }
        low=r->base; bytes=r->size;
    } else if (!vm_range(*base,*size,4096,&low,&bytes) || vm_find(low,bytes)!=r) { result=0xc000001au; goto done; }
    /* Original Xbox NtFreeVirtualMemory accepts sized MEM_RELEASE, including
     * a middle-page hole. Each surviving interval becomes its own reservation.
     * Secure bookkeeping before releasing real storage; a growing table can
     * relocate r. These effects are observed in native_memory_reference.py. */
    unsigned index=(unsigned)(r-vm_regions),split=vm_region_capacity;
    struct PilotRegion old=*r;
    uint32_t end=low+bytes,old_end=old.base+old.size;
    if(type==MEM_RELEASE && low>old.base && end<old_end) {
        for(split=0;split<vm_region_capacity && vm_regions[split].size;++split){}
        if(split==vm_region_capacity && !vm_grow_regions()){result=VM_NOMEM;goto done;}
        r=vm_regions+index;
    }
#ifdef PILOT_MEMORY_LLE
    const char *error=pilot_io_device_release_check(low,bytes);
    if(error)stop(error,low);
    /* The original code receives the equivalent resolved page range, already
     * checked against the original zero-length and rounded-base contract. */
    uint32_t inputs[]={low,bytes,type,0,0},outputs[7]={0};
    error=pilot_io_device_vm(199,memory,inputs,outputs,&result);
    if(error)stop(error,199);
    if(!result && (outputs[0]!=low || outputs[1]!=bytes))stop("original release range differs",low);
#else
    PVOID host=memory+low; SIZE_T span=bytes;
#ifdef PILOT_MEMORY_WRITE_WATCH
    if(!pilot_write_watch_native_begin(host,bytes))stop("cannot relinquish write watch for decommit",low);
#endif
    result=(uint32_t)vm_owner_free(GetCurrentProcess(),&host,&span,MEM_DECOMMIT);
#ifdef PILOT_MEMORY_WRITE_WATCH
    pilot_write_watch_native_end(memory+low,bytes);
#endif
#endif
    if (!result) {
        if(type==MEM_RELEASE) {
            if(low==old.base && end==old_end)memset(r,0,sizeof(*r));
            else if(low==old.base){r->base=end;r->size=old_end-end;}
            else {
                r->size=low-old.base;
                if(end<old_end)vm_regions[split]=(struct PilotRegion){end,old_end-end,old.protect,0};
            }
        }
        *base=low; *size=bytes;
    }
done:
    ReleaseSRWLockExclusive(&vm_lock); return result;
}
static uint32_t vm_protect(uint32_t *base,uint32_t *size,uint32_t protect,uint32_t *old)
{
    if (protect&(PAGE_GUARD|PAGE_NOCACHE|PAGE_WRITECOMBINE|0x800)) stop("guest guard/cache protection outside VM scope",protect);
    vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
    uint32_t low,bytes,result;
    struct PilotRegion *r;
    if (!vm_range(*base,*size,4096,&low,&bytes)) result=VM_INVALID;
    else if (!(r=vm_find(low,bytes)) || r->fixed) result=VM_CONFLICT;
    else {
#ifdef PILOT_MEMORY_LLE
        uint32_t inputs[]={*base,*size,0,protect,0},outputs[7]={0};
        const char *error=pilot_io_device_vm(204,memory,inputs,outputs,&result);
        if(error)stop(error,204);
        if(!result){*base=outputs[0];*size=outputs[1];*old=outputs[2];}
#else
        PVOID host=memory+low; SIZE_T span=bytes; ULONG previous;
#ifdef PILOT_MEMORY_WRITE_WATCH
        if(!pilot_write_watch_native_begin(host,bytes))stop("cannot relinquish write watch for protection",low);
#endif
        result=(uint32_t)vm_native_protect(GetCurrentProcess(),&host,&span,protect,&previous);
#ifdef PILOT_MEMORY_WRITE_WATCH
        pilot_write_watch_native_end(memory+low,bytes);
#endif
        if (!result) { *base=low; *size=bytes; *old=previous; }
#endif
    }
    ReleaseSRWLockExclusive(&vm_lock); return result;
}
static uint32_t vm_query(uint32_t base,uint32_t out[7])
{
    if (base>=VM_END) return VM_INVALID;
    vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
#ifdef PILOT_MEMORY_LLE
    uint32_t inputs[]={base,0,0,0,0},result=0;
    const char *error=pilot_io_device_vm(217,memory,inputs,out,&result);
    if(error)stop(error,217);
    ReleaseSRWLockExclusive(&vm_lock);return result;
#else
    uint32_t low=base&~4095u;
    struct PilotRegion *r=vm_find(low,1);
    memset(out,0,28); out[0]=low;
    if (!r) {
        uint32_t end=VM_END;
        for (unsigned i=0;i<vm_region_capacity;++i) if (vm_regions[i].size && vm_regions[i].base>low && vm_regions[i].base<end) end=vm_regions[i].base;
        out[3]=end-low; out[4]=MEM_FREE; out[5]=PAGE_NOACCESS;
    } else {
        MEMORY_BASIC_INFORMATION host;
#ifdef PILOT_MEMORY_WRITE_WATCH
        /* Original query extents must not expose internal RO page splits. */
        if(!pilot_write_watch_invalidate(memory+r->base,r->size))stop("cannot relinquish write watch for query",low);
#endif
        if (!VirtualQuery(memory+low,&host,sizeof(host))) stop("host VM query failed",low);
        uint64_t end=(unsigned char*)host.BaseAddress-memory+host.RegionSize;
        if (end>(uint64_t)r->base+r->size) end=(uint64_t)r->base+r->size;
        out[1]=r->base; out[2]=r->protect; out[3]=(uint32_t)(end-low);
        out[4]=host.State; out[5]=host.State==MEM_COMMIT ? host.Protect : 0; out[6]=MEM_PRIVATE;
    }
    ReleaseSRWLockExclusive(&vm_lock); return 0;
#endif
}
static void vm_kernel(unsigned ordinal)
{
    uint32_t p=MEM32(g_esp+4),q=MEM32(g_esp+8),base,size,result;
    if (ordinal==217) {
        uint32_t info[7];
        if (!q) result=VM_INVALID;
        else { result=vm_query(p,info); if (!result) memcpy(memory+q,info,sizeof(info)); }
        g_eax=result; g_esp+=12; return;
    }
    if (ordinal==184) q=MEM32(g_esp+12);
    if (!p || !q) result=VM_INVALID;
    else {
        base=MEM32(p); size=MEM32(q);
        if (ordinal==184) result=vm_allocate(&base,MEM32(g_esp+8),&size,MEM32(g_esp+16),MEM32(g_esp+20));
        else if (ordinal==199) result=vm_free(&base,&size,MEM32(g_esp+12));
        else {
            uint32_t previous,oldptr=MEM32(g_esp+16);
            if (!oldptr) result=VM_INVALID;
            else { result=vm_protect(&base,&size,MEM32(g_esp+12),&previous); if (!result) MEM32(oldptr)=previous; }
        }
        if (!result) { MEM32(p)=base; MEM32(q)=size; }
    }
    g_eax=result; g_esp+=ordinal==184 ? 24 : ordinal==199 ? 16 : 20;
}
