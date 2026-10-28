/* XBE section residency over the immutable, identity-checked input image.
 * nxdk supplies the API contract; Cxbx is an external behavioral reference for
 * boundary counters and preserving shared pages. No donor loader body is used.
 * Each page has an ownership count, independent of boundary-counter aliases. */
struct PilotSection { uint32_t address,size,raw,bytes,head,tail,refs,flags; };
static struct PilotSection *sections;
static uint16_t *section_pages;
static uint32_t section_page_base,section_page_count;
static SRWLOCK section_lock=SRWLOCK_INIT;
static int section_preloads_finalized;

static void section_counter(uint32_t va,int delta)
{
    if ((delta>0 && MEM16(va)==0xffff) || (delta<0 && !MEM16(va))) stop("section boundary counter overflow/underflow",va);
    MEM16(va)=(uint16_t)(MEM16(va)+delta);
}
static void section_span(const struct PilotSection *s,uint32_t *first,uint32_t *end)
{
    *first=(s->address-section_page_base)/4096;
    *end=(uint32_t)(((uint64_t)s->address+s->size-section_page_base+4095)/4096);
    if (*end>section_page_count) stop("section outside owned image pages",s->address);
}
static void section_readonly(const struct PilotSection *s)
{
    /* The original 8003285C helper includes the head/tail page only when
     * flags 10/20 are set. An aligned head still skips one page when 10 is
     * clear. Xbox has no NX: original EXECUTE_READ becomes READONLY in 217. */
    if(s->flags&1)return;
    uint32_t start=s->flags&0x10?s->address:(s->address+4096)&~4095u;
    uint32_t end=s->flags&0x20?s->address+s->size:(s->address+s->size)&~4095u;
    if(end<=start)return;
    uint32_t low=start&~4095u,bytes=((end+4095)&~4095u)-low,status;
#ifdef PILOT_MEMORY_LLE
    uint32_t inputs[]={start,end-start,0,PAGE_EXECUTE_READ,0},outputs[7]={0};
    const char *error=pilot_io_device_vm(204,memory,inputs,outputs,&status);
    if(error)stop(error,204);
    if(!status && (outputs[0]!=low || outputs[1]!=bytes))
        stop("original section protection changed owned range",start);
#else
    PVOID host=memory+start;SIZE_T span=end-start;ULONG previous;
    status=(uint32_t)vm_native_protect(GetCurrentProcess(),&host,&span,PAGE_READONLY,&previous);
    if(!status && (host!=memory+low || span!=bytes))
        stop("native section protection changed owned range",start);
#endif
    if(status)stop("owned section readonly protection failed",status);
}
static void sections_finalize_preloads(void)
{
    /* Imports and all preload bytes must already be copied, and MEMORY_LLE
     * residency published. Keep this separate from bookkeeping initialization
     * so authored CPU fixtures can prepare their own mutable scratch first. */
    if(!sections || section_preloads_finalized)stop("invalid section preload finalization",0);
    vm_ensure();AcquireSRWLockExclusive(&section_lock);AcquireSRWLockExclusive(&vm_lock);
    for(unsigned i=0;i<section_count;++i)if(sections[i].flags&2)section_readonly(&sections[i]);
    section_preloads_finalized=1;
    ReleaseSRWLockExclusive(&vm_lock);ReleaseSRWLockExclusive(&section_lock);
}
static uint32_t section_commit(uint32_t base,uint32_t bytes)
{
    PVOID host=memory+base;SIZE_T span=bytes;
    uint32_t status=(uint32_t)vm_owner_allocate(GetCurrentProcess(),&host,0,&span,MEM_COMMIT,PAGE_READWRITE);
    if(status)return status;
    if(host!=memory+base || span!=bytes)stop("section commitment changed owned range",base);
    /* Original cold COMMIT/EXECUTE_READWRITE also changes already committed
     * pages to canonical READWRITE. Let its real 184 return/publication own
     * that transition before copying; restoring old readonly boundary policy
     * here would be incorrect when the new section excludes that boundary. */
#ifdef PILOT_MEMORY_LLE
    uint32_t inputs[]={base,bytes,MEM_COMMIT,PAGE_EXECUTE_READWRITE,0},outputs[7]={0};
    const char *error=pilot_io_device_vm(184,memory,inputs,outputs,&status);
    if(error)stop(error,184);
    if(status) {
        /* The host commitment is preparation, not a successful guest load.
         * Retain the terminal session without copying/publishing section refs
         * if the original owner rejects it; never run over unowned new pages. */
        fprintf(stderr,"[SECTION ORIGINAL COMMIT GUARD] base=%08x bytes=%u status=%08x\n",base,bytes,status);
        stop("original section commit failed after host admission",status);
    }
    if(outputs[0]!=base || outputs[1]!=bytes)stop("original section commit changed owned range",base);
#else
    ULONG previous;
    status=(uint32_t)vm_native_protect(GetCurrentProcess(),&host,&span,PAGE_READWRITE,&previous);
    if(status)stop("native section recommit protection failed",status);
    if(host!=memory+base || span!=bytes)stop("native section recommit changed owned range",base);
#endif
    return 0;
}
static void sections_initialize(void)
{
    if (sections || !section_count || section_count>4096) stop("invalid section residency initialization",section_count);
    section_page_base=image_base&~4095u;
    uint64_t image_end=(uint64_t)image_base+file32(0x10c);
    if (image_end>0x60000000u || image_end<=image_base) stop("invalid image residency range",image_base);
    section_page_count=(uint32_t)((image_end-section_page_base+4095)/4096);
    sections=calloc(section_count,sizeof(*sections));
    section_pages=calloc(section_page_count,sizeof(*section_pages));
    if (!sections || !section_pages) stop("section residency allocation failed",0);
    for (uint32_t page=0;page<(image_base-section_page_base+header_bytes+4095)/4096;++page) section_pages[page]=1;
    for (unsigned i=0;i<section_count;++i) {
        size_t off=section_table-image_base+i*56;
        struct PilotSection *s=&sections[i];
        *s=(struct PilotSection){file32(off+4),file32(off+8),file32(off+12),file32(off+16),file32(off+28),file32(off+32),0,file32(off)};
        if (!s->size || s->address<image_base+header_bytes || (uint64_t)s->address+s->size>image_end ||
            s->bytes>s->size || (uint64_t)s->raw+s->bytes>image_load_size()) stop("invalid section payload/range",i);
        for (unsigned j=0;j<i;++j) if ((uint64_t)s->address+s->size>sections[j].address &&
            (uint64_t)sections[j].address+sections[j].size>s->address) stop("overlapping section byte ranges",i);
        uint32_t counters[]={s->head,s->tail};
        for (unsigned c=0;c<2;++c) {
            if (counters[c]<image_base || (uint64_t)counters[c]+2>(uint64_t)image_base+header_bytes || (counters[c]&1))
                stop("section boundary counter outside image headers",counters[c]);
            MEM16(counters[c])=0;
        }
        MEM32(section_table+i*56+24)=0;
    }
    for (unsigned i=0;i<section_count;++i) if (file32(section_table-image_base+i*56)&2) {
        struct PilotSection *s=&sections[i]; uint32_t first,end; section_span(s,&first,&end);
        for (uint32_t page=first;page<end;++page) ++section_pages[page];
        s->refs=1; MEM32(section_table+i*56+24)=1;
        section_counter(s->head,1); section_counter(s->tail,1);
    }
}
static uint32_t section_change(uint32_t descriptor,int load)
{
    if (!sections || descriptor<section_table || (descriptor-section_table)%56 ||
        (descriptor-section_table)/56>=section_count) return 0xc0000008u;
    uint32_t index=(descriptor-section_table)/56,status=0;
    AcquireSRWLockExclusive(&section_lock);
    struct PilotSection *s=&sections[index];
    if (MEM32(descriptor+24)!=s->refs) stop("external section reference mutation unsupported",descriptor);
    uint32_t first,end; section_span(s,&first,&end);
    if (load && s->refs) {
        if (s->refs==0xffffffffu) status=0xc000009au;
        else MEM32(descriptor+24)=++s->refs;
    } else if (!load && !s->refs) status=0xc000000du;
    else if (!load && s->refs>1) MEM32(descriptor+24)=--s->refs;
    else {
        vm_ensure(); AcquireSRWLockExclusive(&vm_lock);
        uint32_t base=section_page_base+first*4096,bytes=(end-first)*4096;
        if (load) {
            uint64_t additional=bytes-vm_committed(base,bytes);
            if (vm_committed(0,0x100000000ull)+vm_service_bytes+additional>64ull*1024*1024) status=VM_NOMEM;
            else {
                status=section_commit(base,bytes);
                if (!status) {
                    memcpy(memory+s->address,image_load_bytes()+s->raw,s->bytes);
                    memset(memory+s->address+s->bytes,0,s->size-s->bytes);
                    section_readonly(s);
                    for (uint32_t page=first;page<end;++page) ++section_pages[page];
                    section_counter(s->head,1); section_counter(s->tail,1);
                    s->refs=1; MEM32(descriptor+24)=1;
                }
            }
        } else {
            /* Release runs owned only by this section. Shared bytes, including
             * the unloaded section's fragment of a shared page, stay untouched. */
            for (uint32_t page=first;page<end;) {
                if (!section_pages[page]) stop("missing resident page owner",page);
                if (section_pages[page]!=1) { ++page; continue; }
                uint32_t start=page++;
                while (page<end && section_pages[page]==1) ++page;
                PVOID host=memory+section_page_base+start*4096; SIZE_T span=(page-start)*4096;
#ifdef PILOT_USB_LLE
                const char *error=pilot_io_device_image_release(section_page_base+start*4096,(uint32_t)span);
                if(error)stop(error,section_page_base+start*4096);
#endif
                status=(uint32_t)vm_owner_free(GetCurrentProcess(),&host,&span,MEM_DECOMMIT);
                if (status) stop("host failed section decommit",status);
            }
            for (uint32_t page=first;page<end;++page) --section_pages[page];
            section_counter(s->head,-1); section_counter(s->tail,-1);
            s->refs=0; MEM32(descriptor+24)=0;
        }
        ReleaseSRWLockExclusive(&vm_lock);
    }
    fprintf(stderr,"[SECTION %s] index=%u address=%08x size=%u refs=%u status=%08x\n",
            load?"LOAD":"UNLOAD",index,s->address,s->size,s->refs,status);
    ReleaseSRWLockExclusive(&section_lock);
    return status;
}
