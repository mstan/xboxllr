/* XBE section residency over the immutable, identity-checked input image.
 * nxdk supplies the API contract; Cxbx is an external behavioral reference for
 * boundary counters and preserving shared pages. No donor loader body is used.
 * Each page has an ownership count, independent of boundary-counter aliases. */
struct PilotSection { uint32_t address,size,raw,bytes,head,tail,refs; };
static struct PilotSection *sections;
static uint16_t *section_pages;
static uint32_t section_page_base,section_page_count;
static SRWLOCK section_lock=SRWLOCK_INIT;

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
        *s=(struct PilotSection){file32(off+4),file32(off+8),file32(off+12),file32(off+16),file32(off+28),file32(off+32),0};
        if (!s->size || s->address<image_base+header_bytes || (uint64_t)s->address+s->size>image_end ||
            s->bytes>s->size || (uint64_t)s->raw+s->bytes>image_size) stop("invalid section payload/range",i);
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
                PVOID host=memory+base; SIZE_T span=bytes;
                status=(uint32_t)vm_native_allocate(GetCurrentProcess(),&host,0,&span,MEM_COMMIT,PAGE_READWRITE);
                if (!status) {
                    memcpy(memory+s->address,image_bytes+s->raw,s->bytes);
                    memset(memory+s->address+s->bytes,0,s->size-s->bytes);
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
                status=(uint32_t)vm_native_free(GetCurrentProcess(),&host,&span,MEM_DECOMMIT);
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
