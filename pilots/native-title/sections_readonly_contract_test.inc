/* Actual Blinx preload boundary flags and DOLBY/XTIMAGE residency. This calls
 * the maintained HLE loader; MEMORY_LLE protection/query still execute the
 * unchanged original 204/217 over the same physical image owner. */
static void section_readonly_test_policy(uint32_t address,DWORD protection)
{
    uint32_t info[7];MEMORY_BASIC_INFORMATION host;
    CHECK(!vm_query(address,info) && info[4]==MEM_COMMIT && info[5]==protection);
    CHECK(VirtualQuery(memory+address,&host,sizeof(host))==sizeof(host));
    CHECK(host.State==MEM_COMMIT && host.Protect==info[5]);
    unsigned char value=MEM8(address);
    CHECK(test_write_faults(address)==(protection==PAGE_READONLY));
    if(protection==PAGE_READWRITE)MEM8(address)=value;
}
static void section_readonly_test_statistics(uint32_t address,uint32_t values[9])
{
    MEM32(address)=36;CHECK(!memory_statistics(address));
    memcpy(values,memory+address,36);
}
static void test_sections_readonly(const char *comparison)
{
    FILE *file=fopen(comparison,"rb");CHECK(file && !fseek(file,0,SEEK_END));
    long length=ftell(file);CHECK(length>0x2000);rewind(file);
    free(image_bytes);image_bytes=malloc((size_t)length);image_size=(size_t)length;
    CHECK(image_bytes && fread(image_bytes,1,image_size,file)==image_size);fclose(file);
    unsigned char hash[32];char hex[65];
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,image_bytes,(ULONG)image_size,hash,32)>=0);
    for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",hash[i]);
    CHECK(!strcmp(hex,PILOT_XBE_SHA256));
    CHECK(file32(0x104)==0x10000);
    image_base=file32(0x104);header_bytes=file32(0x108);
    section_count=file32(0x11c);section_table=file32(0x120);CHECK(section_count==56);
    CHECK((uint64_t)section_table-image_base+(uint64_t)section_count*56<=header_bytes);
    commit(image_base,header_bytes);memcpy(memory+image_base,image_bytes,header_bytes);
    for(unsigned i=0;i<section_count;++i) {
        size_t off=section_table-image_base+i*56;
        if(!(file32(off)&2))continue;
        uint32_t address=file32(off+4),size=file32(off+8),raw=file32(off+12),bytes=file32(off+16);
        CHECK((uint64_t)raw+bytes<=image_size && bytes<=size);
        commit(address,size);memcpy(memory+address,image_bytes+raw,bytes);
        memset(memory+address+bytes,0,size-bytes);
    }
    sections_initialize();
#ifdef PILOT_MEMORY_LLE
    CHECK(!pilot_io_device_resident(image_base,(file32(0x10c)+4095)&~4095u));
#endif
    uint64_t commitment=vm_committed(0,0x100000000ull);
    sections_finalize_preloads();
    CHECK(vm_committed(0,0x100000000ull)==commitment);
    /* Literal expectations from actual descriptors and original8003285C,
     * not a second invocation of the production range calculation. */
    const struct {unsigned index;uint32_t flags,address,end;} expected[]={
        {0,0x16,0x12000,0x131e60},   /* include head, exclude tail */
        {10,0x06,0x189e40,0x1d0648},/* exclude both */
        {54,0x26,0x31be0e0,0x31c5260},/* exclude head, include tail */
        {5,0x36,0x17e000,0x181310} /* include both, aligned head */
    };
    for(unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);++i) {
        struct PilotSection *s=&sections[expected[i].index];
        CHECK(s->flags==expected[i].flags && s->address==expected[i].address &&
              s->address+s->size==expected[i].end && s->refs==1);
    }
    const struct {uint32_t address;DWORD protection;} pages[]={
        {0x12000,PAGE_READONLY},{0x130000,PAGE_READONLY},{0x131000,PAGE_READWRITE},
        {0x189000,PAGE_READWRITE},{0x18a000,PAGE_READONLY},{0x1cf000,PAGE_READONLY},{0x1d0000,PAGE_READWRITE},
        {0x31be000,PAGE_READWRITE},{0x31bf000,PAGE_READONLY},{0x31c5000,PAGE_READONLY},
        {0x17e000,PAGE_READONLY},{0x181000,PAGE_READONLY},{0x182000,PAGE_READWRITE}
    };
    for(unsigned i=0;i<sizeof(pages)/sizeof(pages[0]);++i)
        section_readonly_test_policy(pages[i].address,pages[i].protection);

    /* Statistics uses a separately owned original/native allocation, never
     * low .text scratch which is now correctly readonly. No authored threads
     * or handles are created by this loader fixture. */
    uint32_t scratch=0,scratch_bytes=4096;
    CHECK(!vm_allocate(&scratch,0,&scratch_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    uint32_t baseline[9],after[9];section_readonly_test_statistics(scratch,baseline);
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t threads_before=thread_count;
    struct PilotSection *dolby=&sections[54],*xt=&sections[55];
    const uint32_t descriptor=section_table+55*56;
    CHECK(dolby->flags==0x26 && xt->flags==0x38 && xt->address==0x31c5260 &&
          xt->address+xt->size==0x31c7a60 && dolby->tail==xt->head && !xt->refs);
    uint16_t head=MEM16(xt->head),tail=MEM16(xt->tail);CHECK(head && !tail);
    CHECK(test_write_faults(0x31c6000) && test_write_faults(0x31c7000));
    CHECK(!memcmp(memory+dolby->address,image_bytes+dolby->raw,dolby->bytes));
    for(uint32_t i=dolby->bytes;i<dolby->size;++i)CHECK(!MEM8(dolby->address+i));
    for(unsigned life=0;life<3;++life) {
        CHECK(!section_change(descriptor,1));
        CHECK(xt->refs==1 && MEM32(descriptor+24)==1 && MEM16(xt->head)==head+1 && MEM16(xt->tail)==tail+1);
        CHECK(vm_committed(0,0x100000000ull)==resident+8192);
        CHECK(!memcmp(memory+xt->address,image_bytes+xt->raw,xt->bytes));
        for(uint32_t i=xt->bytes;i<xt->size;++i)CHECK(!MEM8(xt->address+i));
        CHECK(!memcmp(memory+dolby->address,image_bytes+dolby->raw,dolby->bytes));
        for(uint32_t i=dolby->bytes;i<dolby->size;++i)CHECK(!MEM8(dolby->address+i));
        for(uint32_t page=0x31c5000;page<=0x31c7000;page+=4096)
            section_readonly_test_policy(page,PAGE_READONLY);

        /* Privileged fixture write into its loaded XTIMAGE fragment provides
         * an independent warm-reference oracle. Restore policy before any
         * loader call; shared DOLBY bytes and original PTE remain unchanged. */
        DWORD previous,unused;
        CHECK(VirtualProtect(memory+0x31c5000,4096,PAGE_READWRITE,&previous) && previous==PAGE_READONLY);
        unsigned char changed=(unsigned char)(image_bytes[xt->raw]^0x5a);MEM8(xt->address)=changed;
        CHECK(VirtualProtect(memory+0x31c5000,4096,previous,&unused));
        CHECK(!section_change(descriptor,1));
        CHECK(xt->refs==2 && MEM32(descriptor+24)==2 && MEM8(xt->address)==changed);
        CHECK(MEM16(xt->head)==head+1 && MEM16(xt->tail)==tail+1);
        CHECK(vm_committed(0,0x100000000ull)==resident+8192);
        section_readonly_test_policy(0x31c5000,PAGE_READONLY);
        CHECK(!section_change(descriptor,0) && xt->refs==1 && MEM8(xt->address)==changed);
        CHECK(!section_change(descriptor,0) && !xt->refs && !MEM32(descriptor+24));
        CHECK(MEM16(xt->head)==head && MEM16(xt->tail)==tail);
        CHECK(MEM8(xt->address)==changed && !memcmp(memory+dolby->address,image_bytes+dolby->raw,dolby->bytes));
        section_readonly_test_policy(0x31c5000,PAGE_READONLY);
        CHECK(test_write_faults(0x31c6000) && test_write_faults(0x31c7000));
        CHECK(vm_committed(0,0x100000000ull)==resident && thread_count==threads_before);
        section_readonly_test_statistics(scratch,after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
        fprintf(stderr,"[SECTION READONLY LIFE] life=%u shared_head_restore=1 cold_bytes=1 warm_no_recopy=1 refs/counters=1 exact9memory=1\n",life);
    }
    /* Rejected cold load cannot unprotect the retained readonly head. */
    uint64_t services=vm_service_bytes;vm_service_bytes=64ull*1024*1024;
    CHECK(section_change(descriptor,1)==VM_NOMEM);
    vm_service_bytes=services;
    CHECK(!xt->refs && !MEM32(descriptor+24) && MEM16(xt->head)==head && MEM16(xt->tail)==tail);
    section_readonly_test_policy(0x31c5000,PAGE_READONLY);
    CHECK(test_write_faults(0x31c6000) && test_write_faults(0x31c7000));
    CHECK(vm_committed(0,0x100000000ull)==resident);
    section_readonly_test_statistics(scratch,after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
    uint32_t release=0;CHECK(!vm_free(&scratch,&release,MEM_RELEASE));
    CHECK(vm_committed(0,0x100000000ull)==commitment);
    fprintf(stderr,"PASS: actual preload four-boundary readonly policy, original217/host parity, protected shared DOLBY/XTIMAGE copy/ref/unload/reload and failure preservation\n");
}

/* A bounded authored image covers the cold boundary which actual Blinx's
 * sole readonly adjacency (DOLBY/XTIMAGE, both include the shared page) cannot
 * distinguish: original recommit makes an excluded/writable boundary RW. */
static void test_sections_readonly_recommit(void)
{
    image_size=0x5000;free(image_bytes);image_bytes=calloc(1,image_size);CHECK(image_bytes);
    image_base=0x10000;header_bytes=0x1000;section_table=0x10180;section_count=3;
    *(uint32_t*)(image_bytes+0x10c)=0x10000;
    uint32_t descriptors[3][14]={
        {0x32,0x14800,0x1000,0x2000,0x1000,0,0,0x10700,0x10702},
        {0x20,0x15800,0x1800,0x3000,0x1400,0,0,0x10702,0x10704},
        {0x01,0x14000,0x0800,0x4800,0x0700,0,0,0x10700,0x10700}
    };
    memcpy(image_bytes+section_table-image_base,descriptors,sizeof(descriptors));
    memset(image_bytes+0x2000,0xc3,0x1000);memset(image_bytes+0x3000,0x6d,0x1400);
    memset(image_bytes+0x4800,0x5c,0x700);
    memcpy(memory+image_base,image_bytes,header_bytes);
    memcpy(memory+0x14800,image_bytes+0x2000,0x1000);
    /* The contract runner's mutable bootstrap initially commits64KiB. This
     * authored cold page has no section owner and must start uncommitted. */
    CHECK(VirtualFree(memory+0x16000,4096,MEM_DECOMMIT));
    sections_initialize();
#ifdef PILOT_MEMORY_LLE
    CHECK(!pilot_io_device_resident(image_base,0x10000));
#endif
    sections_finalize_preloads();
    section_readonly_test_policy(0x14000,PAGE_READONLY);
    section_readonly_test_policy(0x15000,PAGE_READONLY);
    CHECK(test_write_faults(0x16000));
    uint64_t commitment=vm_committed(0,0x100000000ull);
    uint32_t scratch=0,scratch_bytes=4096;
    CHECK(!vm_allocate(&scratch,0,&scratch_bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    uint64_t resident=vm_committed(0,0x100000000ull);
    uint32_t baseline[9],after[9];section_readonly_test_statistics(scratch,baseline);
    for(unsigned life=0;life<3;++life) {
        /* Re-establish the authored neighbour's initial policy via the real
         * policy owner; production unload deliberately does not undo the
         * genuine cold recommit's policy on a still-owned shared page. */
        AcquireSRWLockExclusive(&section_lock);AcquireSRWLockExclusive(&vm_lock);
        section_readonly(&sections[0]);
        ReleaseSRWLockExclusive(&vm_lock);ReleaseSRWLockExclusive(&section_lock);
        section_readonly_test_policy(0x14000,PAGE_READONLY);
        section_readonly_test_policy(0x15000,PAGE_READONLY);
        CHECK(MEM16(0x10700)==1 && MEM16(0x10702)==1 && !MEM16(0x10704));
        uint32_t excluded=section_table+56,writable=section_table+112;
        CHECK(!section_change(excluded,1));
        CHECK(!memcmp(memory+0x14800,image_bytes+0x2000,0x1000));
        CHECK(!memcmp(memory+0x15800,image_bytes+0x3000,0x1400));
        for(uint32_t at=0x16c00;at<0x17000;++at)CHECK(!MEM8(at));
        section_readonly_test_policy(0x15000,PAGE_READWRITE);
        section_readonly_test_policy(0x16000,PAGE_READONLY);
        CHECK(MEM16(0x10702)==2 && MEM16(0x10704)==1);
        CHECK(vm_committed(0,0x100000000ull)==resident+4096);
        CHECK(!section_change(excluded,0));
        section_readonly_test_policy(0x15000,PAGE_READWRITE);
        CHECK(test_write_faults(0x16000) && MEM16(0x10702)==1 && !MEM16(0x10704));
        CHECK(!MEM32(excluded+24));

        CHECK(!section_change(writable,1));
        CHECK(!memcmp(memory+0x14000,image_bytes+0x4800,0x700));
        for(uint32_t at=0x14700;at<0x14800;++at)CHECK(!MEM8(at));
        CHECK(!memcmp(memory+0x14800,image_bytes+0x2000,0x1000));
        section_readonly_test_policy(0x14000,PAGE_READWRITE);
        CHECK(MEM16(0x10700)==3 && MEM32(writable+24)==1);
        CHECK(!section_change(writable,0));
        section_readonly_test_policy(0x14000,PAGE_READWRITE);
        CHECK(MEM16(0x10700)==1 && !MEM32(writable+24));
        CHECK(!memcmp(memory+0x14800,image_bytes+0x2000,0x1000));
        CHECK(vm_committed(0,0x100000000ull)==resident);
        section_readonly_test_statistics(scratch,after);CHECK(!memcmp(baseline,after,sizeof(baseline)));
        fprintf(stderr,"[SECTION RECOMMIT LIFE] life=%u excluded_head_RW=1 writable_shared_RW=1 original217/host=1 preserved_neighbor=1 exact9memory=1\n",life);
    }
    uint32_t released=0;CHECK(!vm_free(&scratch,&released,MEM_RELEASE));
    CHECK(vm_committed(0,0x100000000ull)==commitment);
    fprintf(stderr,"PASS: authored cold excluded-head and writable-neighbour real recommit policy, original184/217 ownership and exact reusable bytes/counters/memory\n");
}
