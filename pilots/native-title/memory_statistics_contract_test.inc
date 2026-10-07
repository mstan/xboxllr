static void test_memory_snapshot(uint32_t out[9])
{
    uint32_t pointer=0x1a004;
    MEM32(pointer-4)=MEM32(pointer+36)=0xa1b2c3d4u;MEM32(pointer)=36;
    g_ebx=0x11223344;g_esi=0x22334455;g_edi=0x33445566;g_ebp=0x44556677;
    call_kernel(181,&pointer,1);CHECK(!g_eax);
    CHECK(g_ebx==0x11223344 && g_esi==0x22334455 && g_edi==0x33445566 && g_ebp==0x44556677);
    CHECK(MEM32(pointer-4)==0xa1b2c3d4u && MEM32(pointer+36)==0xa1b2c3d4u);
    memcpy(out,memory+pointer,36);CHECK(out[0]==36 && out[1]==16384);
}
static void test_memory_statistics(void)
{
    image_base=0x10000;*(uint32_t*)(image_bytes+0x10c)=0x20000;
    uint32_t before[9],after[9];test_memory_snapshot(before);
    CHECK(before[2]==16384-16 && before[3]==65536 && before[4]==0x20000 && before[8]==16);
    CHECK(!before[5] && !before[6] && !before[7]);
    uint32_t pointer=0x1a004,saved[9];MEM32(pointer)=35;memcpy(saved,memory+pointer,36);
    call_kernel(181,&pointer,1);CHECK(g_eax==VM_INVALID && !memcmp(saved,memory+pointer,36));
    pointer=0;call_kernel(181,&pointer,1);CHECK(g_eax==VM_INVALID);

    uint32_t base=0,bytes=65536;
    CHECK(!vm_allocate(&base,0,&bytes,MEM_RESERVE,PAGE_READWRITE));
    test_memory_snapshot(after);CHECK(after[2]==before[2] && after[3]==before[3] && after[4]==before[4]+65536);
    bytes=8192;CHECK(!vm_allocate(&base,0,&bytes,MEM_COMMIT,PAGE_READWRITE));
    MEM32(base)=0xabcdef12;test_memory_snapshot(after);
    CHECK(after[2]==before[2]-2 && after[3]==before[3]+8192 && after[4]==before[4]+65536);
    bytes=4096;CHECK(!vm_free(&base,&bytes,MEM_DECOMMIT));test_memory_snapshot(after);
    CHECK(after[2]==before[2]-1 && after[3]==before[3]+4096 && after[4]==before[4]+65536);
    bytes=0;CHECK(!vm_free(&base,&bytes,MEM_RELEASE));test_memory_snapshot(after);CHECK(!memcmp(before,after,36));

    uint32_t physical=physical_allocate(4097,0,0xffffffffu,4096,PAGE_READWRITE);CHECK(physical);
    test_memory_snapshot(after);CHECK(after[2]==before[2]-2);
    for(unsigned i=3;i<9;++i)CHECK(after[i]==before[i]);
    physical_free(physical);test_memory_snapshot(after);CHECK(!memcmp(before,after,36));

    CHECK(!fsc_resize(3));test_memory_snapshot(after);
    CHECK(after[2]==before[2]-3 && after[5]==3 && after[3]==before[3]);
    CHECK(!fsc_resize(1));test_memory_snapshot(after);CHECK(after[2]==before[2]-1 && after[5]==1);
    CHECK(!fsc_resize(0));test_memory_snapshot(after);CHECK(!memcmp(before,after,36));

    uint32_t small_args[]={180,0x74736554};call_kernel(15,small_args,2);uint32_t small=g_eax;
    CHECK(small>=0x80000000u && !(small&7));call_kernel(23,&small,1);CHECK(g_eax==184);
    memset(memory+small,0xa5,184);
    uint32_t size=33;call_kernel(14,&size,1);uint32_t second=g_eax;
    CHECK(second && second!=small);call_kernel(23,&second,1);CHECK(g_eax==56);
    memset(memory+second,0x5a,56);test_memory_snapshot(after);
    CHECK(after[6]==before[6]+1 && after[2]==before[2]-1 && after[3]==before[3]);
    call_kernel(17,&small,1);CHECK(MEM32(second)==0x5a5a5a5au);
    test_memory_snapshot(after);CHECK(after[6]==before[6]+1 && after[2]==before[2]-1);
    uint64_t service=vm_service_bytes;vm_service_bytes=POOL_BYTES;
    size=8192;call_kernel(14,&size,1);CHECK(!g_eax && MEM32(second)==0x5a5a5a5au);vm_service_bytes=service;
    call_kernel(17,&second,1);CHECK(test_write_faults(second));
    size=4057;call_kernel(14,&size,1);uint32_t large=g_eax;
    CHECK(large && !(large&4095));call_kernel(23,&large,1);CHECK(g_eax==4096);
    MEM32(large+4092)=0x31415926;test_memory_snapshot(after);
    CHECK(after[6]==before[6]+1 && after[2]==before[2]-1 && MEM32(large+4092)==0x31415926);
    call_kernel(17,&large,1);CHECK(test_write_faults(large));
    test_memory_snapshot(after);CHECK(!memcmp(before,after,36));

    /* Real commitment inside the reserved image (as with a deferred section). */
    commit(0x20000,4096);test_memory_snapshot(after);
    CHECK(after[8]==17 && after[2]==before[2]-1 && after[3]==before[3]+4096 && after[4]==before[4]);
    CHECK(VirtualFree(memory+0x20000,4096,MEM_DECOMMIT));
    commit(0x70000000,4096);commit(0x71000000,4096);test_memory_snapshot(after);
    CHECK(after[7]==1 && after[6]==1 && after[2]==before[2]-2 && after[3]==before[3]);
    CHECK(VirtualFree(memory+0x70000000,4096,MEM_DECOMMIT));
    CHECK(VirtualFree(memory+0x71000000,4096,MEM_DECOMMIT));
    test_memory_snapshot(after);CHECK(!memcmp(before,after,36));
    fprintf(stderr,"PASS: memory statistics ABI/guards/errors and actual reserve/commit/decommit/release, contiguous, cache, image, stack and pool deltas\n");
}
