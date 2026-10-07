/* Native read-through cache: FscGet/Set sizes actual 4 KiB buffers, and
 * invalidation discards actual cached bytes. nxdk declares the three ABIs;
 * Cxbx supplies default/maximum page-count facts (16/2048). Donor bridges that
 * only remember a size were inspected, but no donor service body is imported.
 * No dirty cache: writes go to native IO and invalidate the affected handle.
 * Cache only when write sharing is denied, so a host writer cannot stale it. */
static SRWLOCK fsc_lock=SRWLOCK_INIT;
static INIT_ONCE fsc_once=INIT_ONCE_STATIC_INIT;
static unsigned char *fsc_bytes;
static unsigned fsc_pages;
static struct { uint32_t id,low,high; uint64_t page; } fsc_entries[2048];
static uint64_t fsc_hit_bytes,fsc_host_reads;

static uint32_t fsc_resize(unsigned pages)
{
    if (pages>2048) return VM_INVALID;
    vm_ensure(); AcquireSRWLockExclusive(&fsc_lock);
    uint32_t status=0;
    if (pages!=fsc_pages) {
        AcquireSRWLockExclusive(&vm_lock);
        uint64_t previous=(uint64_t)fsc_pages*4096,requested=(uint64_t)pages*4096;
        if (requested>previous && vm_committed(0,0x100000000ull)+vm_service_bytes-previous+requested>64ull*1024*1024) status=VM_NOMEM;
        unsigned char *replacement=NULL;
        if (!status && pages) {
            replacement=VirtualAlloc(NULL,(SIZE_T)requested,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
            if (!replacement) status=VM_NOMEM;
        }
        if (!status) {
            if (fsc_bytes && !VirtualFree(fsc_bytes,0,MEM_RELEASE)) stop("file-cache release failed",0);
            fsc_bytes=replacement; fsc_pages=pages;
            vm_service_bytes=vm_service_bytes-previous+requested;
            memset(fsc_entries,0,sizeof(fsc_entries));
        }
        ReleaseSRWLockExclusive(&vm_lock);
    }
    ReleaseSRWLockExclusive(&fsc_lock); return status;
}
static BOOL CALLBACK fsc_start(PINIT_ONCE once,void *parameter,void **context)
{
    (void)once; (void)parameter; (void)context;
    uint32_t status=fsc_resize(16);
    if (status) stop("initial filesystem cache allocation failed",status);
    return TRUE;
}
static void fsc_ensure(void) { InitOnceExecuteOnce(&fsc_once,fsc_start,NULL,NULL); }
static void fsc_forget(uint32_t id)
{
    AcquireSRWLockExclusive(&fsc_lock);
    for (unsigned i=0;i<fsc_pages;++i) if (!id || fsc_entries[i].id==id) fsc_entries[i].id=0;
    ReleaseSRWLockExclusive(&fsc_lock);
}
static unsigned fsc_slot(uint32_t id,uint64_t page)
{
    return (unsigned)((page^(page>>32)^((uint64_t)id*2654435761u))%fsc_pages);
}
/* All entries are idle once a reader releases fsc_lock; no borrowed pointers
 * escape. A caller owns its file's IO lock across cache/position/host IO. */
static int fsc_copy(uint32_t id,uint64_t start,unsigned length,void *buffer)
{
    if (!fsc_pages || (uint64_t)length>(uint64_t)fsc_pages*4096) return 0;
    for (unsigned pass=0;pass<2;++pass) for (unsigned done=0;done<length;) {
        uint64_t position=start+done,page=position>>12;
        unsigned low=(unsigned)(position&4095),count=4096-low;
        if (count>length-done) count=length-done;
        unsigned slot=fsc_slot(id,page);
        if (fsc_entries[slot].id!=id || fsc_entries[slot].page!=page ||
            fsc_entries[slot].low>low || fsc_entries[slot].high<low+count) return 0;
        if (pass) memcpy((unsigned char*)buffer+done,fsc_bytes+slot*4096+low,count);
        done+=count;
    }
    return 1;
}
static void fsc_store(uint32_t id,uint64_t start,unsigned length,const void *buffer)
{
    if (!fsc_pages) return;
    /* One native IO for large reads; retain only a cache-sized trailing range. */
    unsigned done=length>fsc_pages*4096 ? length-fsc_pages*4096 : 0;
    while (done<length) {
        uint64_t position=start+done,page=position>>12;
        unsigned low=(unsigned)(position&4095),count=4096-low;
        if (count>length-done) count=length-done;
        unsigned slot=fsc_slot(id,page),high=low+count;
        memcpy(fsc_bytes+slot*4096+low,(const unsigned char*)buffer+done,count);
        if (fsc_entries[slot].id==id && fsc_entries[slot].page==page &&
            low<=fsc_entries[slot].high && high>=fsc_entries[slot].low) {
            if (low>fsc_entries[slot].low) low=fsc_entries[slot].low;
            if (high<fsc_entries[slot].high) high=fsc_entries[slot].high;
        }
        fsc_entries[slot].id=id; fsc_entries[slot].page=page;
        fsc_entries[slot].low=low; fsc_entries[slot].high=high;
        done+=count;
    }
}
static uint32_t fsc_read(struct MediaFile *file,IO_STATUS_BLOCK *io,void *buffer,unsigned length,LARGE_INTEGER *offset)
{
    int eligible=file->cacheable && length;
    LARGE_INTEGER start;
    if (eligible) {
        if (offset && offset->QuadPart!=-2) start=*offset;
        else {
            IO_STATUS_BLOCK position_io={0};
            uint32_t status=(uint32_t)media_nt_query(file->host,&position_io,&start,sizeof(start),14);
            if (status) return status;
        }
        if (start.QuadPart<0 || (uint64_t)start.QuadPart+length>INT64_MAX) eligible=0;
    }
    AcquireSRWLockExclusive(&fsc_lock);
    if (eligible && fsc_copy(file->id,(uint64_t)start.QuadPart,length,buffer)) {
        LARGE_INTEGER next; next.QuadPart=start.QuadPart+length; IO_STATUS_BLOCK position_io={0};
        uint32_t status=(uint32_t)media_nt_set(file->host,&position_io,&next,sizeof(next),14);
        if (!status) { io->Status=0; io->Information=length; fsc_hit_bytes+=length; }
        ReleaseSRWLockExclusive(&fsc_lock); return status;
    }
    ++fsc_host_reads; ReleaseSRWLockExclusive(&fsc_lock);
    /* No global cache lock while the host performs storage IO. */
    uint32_t status=(uint32_t)media_nt_read(file->host,NULL,NULL,NULL,io,buffer,length,offset,NULL);
    if (!status && eligible && io->Information) {
        AcquireSRWLockExclusive(&fsc_lock);
        fsc_store(file->id,(uint64_t)start.QuadPart,(unsigned)io->Information,buffer);
        ReleaseSRWLockExclusive(&fsc_lock);
    }
    return status;
}
static void fsc_kernel(unsigned ordinal)
{
    fsc_ensure();
    if (ordinal==35) {
        AcquireSRWLockShared(&fsc_lock); g_eax=fsc_pages; ReleaseSRWLockShared(&fsc_lock);
    } else if (ordinal==36) fsc_forget(0);
    else {
        unsigned pages=MEM32(g_esp+4); g_eax=fsc_resize(pages);
        fprintf(stderr,"[FILE CACHE] requested=%u pages status=%08x\n",pages,g_eax);
    }
    g_esp+=ordinal==37 ? 8 : 4;
}
