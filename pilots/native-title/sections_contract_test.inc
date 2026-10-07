static DWORD WINAPI test_section_worker(void *argument)
{
    uint32_t descriptor=(uint32_t)(uintptr_t)argument;
    for (unsigned i=0;i<100;++i) {
        CHECK(!section_change(descriptor,1));
        CHECK(MEM8(0x21800)==0x5a);
        CHECK(!section_change(descriptor,0));
    }
    return 0;
}
static void test_sections(void)
{
    image_size=0x8000; free(image_bytes); image_bytes=calloc(1,image_size); CHECK(image_bytes);
    image_base=0x10000; header_bytes=0x1000; section_table=0x10180; section_count=3;
    *(uint32_t*)(image_bytes+0x10c)=0x16000;
    uint32_t descriptors[3][14]={
        {3,0x20000,0x1800,0x2000,0x1800,0,0,0x10700,0x10702},
        {1,0x21800,0x1400,0x4000,0x1100,0,0,0x10702,0x10704},
        {1,0x22c00,0x0400,0x6000,0x0200,0,0,0x10704,0x10704}
    };
    memcpy(image_bytes+section_table-image_base,descriptors,sizeof(descriptors));
    memset(image_bytes+0x2000,0x11,0x1800); memset(image_bytes+0x4000,0x5a,0x1100); memset(image_bytes+0x6000,0xe7,0x200);
    memcpy(memory+image_base,image_bytes,header_bytes);
    commit(0x20000,0x1800); memcpy(memory+0x20000,image_bytes+0x2000,0x1800);
    sections_initialize();
    const uint32_t a=section_table,b=section_table+56,c=section_table+112;
    CHECK(MEM32(a+24)==1 && MEM16(0x10700)==1 && MEM16(0x10702)==1 && !MEM16(0x10704));
    CHECK(test_write_faults(0x22000));
    call_kernel(327,&c,1); CHECK(!g_eax && MEM16(0x10704)==2);
    CHECK(!memcmp(memory+0x22c00,image_bytes+0x6000,0x200) && !MEM8(0x22e00));
    call_kernel(327,&b,1); CHECK(!g_eax && MEM16(0x10702)==2 && MEM16(0x10704)==3);
    CHECK(!memcmp(memory+0x21800,image_bytes+0x4000,0x1100));
    CHECK(!MEM8(0x22a00) && MEM8(0x217ff)==0x11 && MEM8(0x22c00)==0xe7);
    MEM8(0x22a00)=0x7b; MEM8(0x21800)=0x91;
    uint64_t committed=vm_committed(0,0x100000000ull);
    call_kernel(327,&b,1); CHECK(!g_eax && MEM32(b+24)==2 && MEM8(0x21800)==0x91 && MEM8(0x22a00)==0x7b);
    CHECK(vm_committed(0,0x100000000ull)==committed && MEM16(0x10702)==2 && MEM16(0x10704)==3);
    call_kernel(328,&b,1); CHECK(!g_eax && MEM32(b+24)==1);
    call_kernel(328,&b,1); CHECK(!g_eax && !MEM32(b+24) && MEM16(0x10702)==1 && MEM16(0x10704)==2);
    CHECK(MEM8(0x21800)==0x91 && MEM8(0x22a00)==0x7b && vm_committed(0,0x100000000ull)==committed);
    call_kernel(328,&b,1); CHECK(g_eax==0xc000000du);
    call_kernel(328,&c,1); CHECK(!g_eax && !MEM16(0x10704) && test_write_faults(0x22000));
    CHECK(MEM8(0x217ff)==0x11 && MEM8(0x21800)==0x91); /* shared head retained, never erased on unload */
    call_kernel(327,&b,1); CHECK(!g_eax && MEM8(0x21800)==0x5a && !MEM8(0x22a00));
    CHECK(MEM8(0x217ff)==0x11 && !MEM8(0x22c00)); /* reload restores its bytes only */
    call_kernel(328,&b,1); CHECK(!g_eax && test_write_faults(0x22000));
    uint32_t invalid=section_table+1; call_kernel(327,&invalid,1); CHECK(g_eax==0xc0000008u);
    sections[0].refs=MEM32(a+24)=0xffffffffu;
    call_kernel(327,&a,1); CHECK(g_eax==0xc000009au && MEM32(a+24)==0xffffffffu);
    sections[0].refs=MEM32(a+24)=1;

    HANDLE workers[]={CreateThread(NULL,0,test_section_worker,(void*)(uintptr_t)b,0,NULL),
                      CreateThread(NULL,0,test_section_worker,(void*)(uintptr_t)b,0,NULL)};
    CHECK(workers[0] && workers[1] && WaitForMultipleObjects(2,workers,TRUE,10000)==WAIT_OBJECT_0);
    CloseHandle(workers[0]); CloseHandle(workers[1]);
    CHECK(!MEM32(b+24) && MEM16(0x10702)==1 && !MEM16(0x10704) && test_write_faults(0x22000));
    call_kernel(328,&a,1); CHECK(!g_eax && test_write_faults(0x20000) && test_write_faults(0x21000));
    CHECK(!MEM16(0x10700) && !MEM16(0x10702));
    commit(0x30000000,64*1024*1024);
    committed=vm_committed(0,0x100000000ull);
    call_kernel(327,&c,1); CHECK(g_eax==VM_NOMEM && !MEM32(c+24) && !MEM16(0x10704) && test_write_faults(0x22000));
    CHECK(vm_committed(0,0x100000000ull)==committed);
    CHECK(VirtualFree(memory+0x30000000,64*1024*1024,MEM_DECOMMIT));
    fprintf(stderr,"PASS: actual section bytes/zero-fill/reload, shared-head/tail ownership, aliased counters, unload faults, concurrent lifetime and budget-failure preservation\n");
}
