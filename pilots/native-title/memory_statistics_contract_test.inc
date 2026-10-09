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
#ifdef PILOT_MEMORY_LLE
    /* Original memory statistics report the selected kernel's real counters,
     * including kernel/PTE costs absent from a native host allocator. Do not
     * require its absolute free-page count to equal the native configuration. */
    uint32_t before[9],reserved[9],committed[9],after[9];test_memory_snapshot(before);
    uint32_t pointer=0x1a004,saved[9];MEM32(pointer)=35;memcpy(saved,memory+pointer,36);
    call_kernel(181,&pointer,1);CHECK(g_eax==VM_INVALID && !memcmp(saved,memory+pointer,36));
    uint32_t base=0,bytes=65536;
    CHECK(!vm_allocate(&base,0,&bytes,MEM_RESERVE,PAGE_READWRITE));test_memory_snapshot(reserved);
    CHECK(reserved[2]==before[2] && reserved[3]==before[3] && reserved[4]==before[4]+65536);
    bytes=8192;CHECK(!vm_allocate(&base,0,&bytes,MEM_COMMIT,PAGE_READWRITE));
    MEM32(base)=0xabcdef12;test_memory_snapshot(committed);
    CHECK(committed[2]<=before[2]-2 && committed[3]==before[3]+8192 && committed[4]==before[4]+65536);
    bytes=4096;CHECK(!vm_free(&base,&bytes,MEM_DECOMMIT));test_memory_snapshot(after);
    CHECK(after[2]==committed[2]+1 && after[3]==before[3]+4096 && after[4]==before[4]+65536);
    bytes=0;CHECK(!vm_free(&base,&bytes,MEM_RELEASE));test_memory_snapshot(after);CHECK(!memcmp(before,after,36));
    uint32_t physical=physical_allocate(4097,0,0xffffffffu,4096,PAGE_READWRITE);CHECK(physical);
    test_memory_snapshot(after);CHECK(after[2]==before[2]-2);
    for(unsigned i=3;i<9;++i)CHECK(after[i]==before[i]);
    physical_free(physical);test_memory_snapshot(after);CHECK(!memcmp(before,after,36));
    for(unsigned i=0;i<32;++i) {
        uint32_t pool=pool_allocate(180,0x74736554);CHECK(pool);
        memset(memory+pool,0x5a,180);pool_free(pool);
    }
    test_memory_snapshot(after);CHECK(!memcmp(before,after,36));
    /* Original REP zeroing of a large commit must finish, rather than trip
     * the bounded interpreter budget previously sized for small I/O calls. */
    base=0;bytes=4*1024*1024;
    CHECK(!vm_allocate(&base,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    CHECK(bytes==4*1024*1024);
    for(uint32_t offset=0;offset<bytes;offset+=4096)CHECK(!MEM32(base+offset));
    bytes=0;CHECK(!vm_free(&base,&bytes,MEM_RELEASE));test_memory_snapshot(after);CHECK(!memcmp(before,after,36));
    /* Loader bytes acquire real original PFNs without first requiring a USB
     * request. Reserved but unloaded pages must not consume physical pages. */
    MEM32(0x15000)=0x1234abcd;
    CHECK(!pilot_io_device_resident(0x10000,0x20000));test_memory_snapshot(after);
    CHECK(after[2]<=before[2]-16 && after[3]==before[3]+65536 && after[4]==before[4]);
    CHECK(MEM32(0x15000)==0x1234abcd && test_write_faults(0x20000));
    uint32_t resident[9];memcpy(resident,after,36);
    CHECK(!pilot_io_device_resident(0x10000,0x20000));test_memory_snapshot(after);
    CHECK(!memcmp(resident,after,36));
    uint32_t address=0x15000;call_kernel(173,&address,1);uint32_t pa=g_eax,value=0;
    CHECK(pa && !pilot_io_device_pinned_dma(pa,&value,4,0));
    uint32_t pin[]={address,4,0};call_kernel(175,pin,3);
    CHECK(pilot_io_device_pinned_dma(pa,&value,4,0) && value==0x1234abcd);
    value=0xabcdef12;CHECK(pilot_io_device_pinned_dma(pa,&value,4,1) && MEM32(address)==value);
    pin[2]=1;call_kernel(175,pin,3);CHECK(!pilot_io_device_pinned_dma(pa,&value,4,0));
    /* The original kernel may move this live, now-unpinned image page to
     * make a constrained contiguous request possible. Caller bytes and VA
     * stay stable, while DMA must use the new original physical address. */
    uint32_t contiguous=physical_allocate(4096,pa,pa+4095,4096,PAGE_READWRITE);
    CHECK(contiguous==0x80000000u+pa && MEM32(address)==0xabcdef12);
    MEM32(contiguous)=0x99887766;CHECK(MEM32(address)==0xabcdef12);
    call_kernel(173,&address,1);uint32_t moved_pa=g_eax;CHECK(moved_pa!=pa);
    pin[2]=0;call_kernel(175,pin,3);
    CHECK(pilot_io_device_pinned_dma(moved_pa,&value,4,0) && value==0xabcdef12);
    CHECK(!physical_allocate(4096,moved_pa,moved_pa+4095,4096,PAGE_READWRITE));
    CHECK(MEM32(address)==0xabcdef12 && MEM32(contiguous)==0x99887766);
    pin[2]=1;call_kernel(175,pin,3);physical_free(contiguous);
    test_memory_snapshot(after);CHECK(!memcmp(resident,after,36));
    /* Repeat actual section-like residency/release. Both physical counters
     * and caller access return to the pre-load state on every iteration. */
    for(unsigned i=0;i<32;++i) {
        commit(0x20000,4096);MEM32(0x20000)=i+1;
        CHECK(!pilot_io_device_resident(0x20000,4096));test_memory_snapshot(after);
        CHECK(after[2]==resident[2]-1 && after[3]==resident[3]+4096 && MEM32(0x20000)==i+1);
        CHECK(!pilot_io_device_image_release(0x20000,4096));
        CHECK(test_write_faults(0x20000));test_memory_snapshot(after);CHECK(!memcmp(resident,after,36));
    }
    fprintf(stderr,"PASS: original statistics ABI/bounds, reserve/commit/PTE costs, allocation lifetime counters, resident loader bytes, shared CPU/DMA backing and 32 residency/release cycles\n");
#else
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
    /* Original buddy allocation may put the two size classes on separate
     * pages. Placement is not promised by ExAllocatePool's caller contract. */
    unsigned pool_pages=(small>>12)==(second>>12)?1:2;
    CHECK(after[6]==before[6]+pool_pages && after[2]==before[2]-pool_pages && after[3]==before[3]);
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
#endif
}
