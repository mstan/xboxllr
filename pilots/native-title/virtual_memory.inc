/* Guest reservation ownership over the pilot's shifted 4 GiB VA range.
 * Host NT services supply actual commitment/protection/decommit behavior.
 * Xbox signatures and 32-bit MEMORY_BASIC_INFORMATION are documented by nxdk;
 * range/alignment constants were cross-checked against Cxbx. No donor service
 * bodies are incorporated. Physical allocation is bounded, not a full MMU. */
#define VM_END 0x7fff0000u
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
static uint64_t vm_committed(uint32_t base,uint64_t size)
{
    uint64_t count=0,end=(uint64_t)base+size;
    for (uint64_t at=base;at<end;) {
        MEMORY_BASIC_INFORMATION info;
        if (!VirtualQuery(memory+at,&info,sizeof(info))) stop("host VM query failed",(uint32_t)at);
        uint64_t next=(unsigned char*)info.BaseAddress-memory+info.RegionSize;
        if (next>end) next=end;
        if (next<=at) stop("invalid host VM query range",(uint32_t)at);
        if (info.State==MEM_COMMIT) count+=next-at;
        at=next;
    }
    return count;
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
    type&=~0x00800000u;
    if (type&(0x00080000u|0x20000000u|0x80000000u))
        stop("guest reset/large-page allocation outside VM scope",type);
    if (!*size || zero_bits>21 || (type&~(MEM_RESERVE|MEM_COMMIT|MEM_TOP_DOWN)) || !(type&(MEM_RESERVE|MEM_COMMIT))) return VM_INVALID;
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
        result=(uint32_t)vm_native_allocate(GetCurrentProcess(),&host,0,&span,MEM_COMMIT,protect);
        if (result) goto done;
    }
    if (reserve) *region=(struct PilotRegion){low,bytes,protect,0};
    *base=low; *size=bytes;
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
    if (type==MEM_RELEASE) {
        if (*size) { result=0xc000009fu; goto done; }
        if (*base!=r->base) { result=0xc00000a1u; goto done; }
        low=r->base; bytes=r->size;
    } else if (!*size) {
        if (*base!=r->base) { result=VM_INVALID; goto done; }
        low=r->base; bytes=r->size;
    } else if (!vm_range(*base,*size,4096,&low,&bytes) || vm_find(low,bytes)!=r) { result=VM_CONFLICT; goto done; }
    PVOID host=memory+low; SIZE_T span=bytes;
    result=(uint32_t)vm_native_free(GetCurrentProcess(),&host,&span,MEM_DECOMMIT);
    if (!result) { if (type==MEM_RELEASE) memset(r,0,sizeof(*r)); *base=low; *size=bytes; }
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
        PVOID host=memory+low; SIZE_T span=bytes; ULONG previous;
        result=(uint32_t)vm_native_protect(GetCurrentProcess(),&host,&span,protect,&previous);
        if (!result) { *base=low; *size=bytes; *old=previous; }
    }
    ReleaseSRWLockExclusive(&vm_lock); return result;
}
static uint32_t vm_query(uint32_t base,uint32_t out[7])
{
    if (base>=VM_END) return VM_INVALID;
    vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
    uint32_t low=base&~4095u;
    struct PilotRegion *r=vm_find(low,1);
    memset(out,0,28); out[0]=low;
    if (!r) {
        uint32_t end=VM_END;
        for (unsigned i=0;i<vm_region_capacity;++i) if (vm_regions[i].size && vm_regions[i].base>low && vm_regions[i].base<end) end=vm_regions[i].base;
        out[3]=end-low; out[4]=MEM_FREE;
    } else {
        MEMORY_BASIC_INFORMATION host;
        if (!VirtualQuery(memory+low,&host,sizeof(host))) stop("host VM query failed",low);
        uint64_t end=(unsigned char*)host.BaseAddress-memory+host.RegionSize;
        if (end>(uint64_t)r->base+r->size) end=(uint64_t)r->base+r->size;
        out[1]=r->base; out[2]=r->protect; out[3]=(uint32_t)(end-low);
        out[4]=host.State; out[5]=host.State==MEM_COMMIT ? host.Protect : 0; out[6]=MEM_PRIVATE;
    }
    ReleaseSRWLockExclusive(&vm_lock); return 0;
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
