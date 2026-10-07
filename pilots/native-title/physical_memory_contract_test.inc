static void test_physical_memory(void)
{
    uint64_t before=vm_committed(0,0x100000000ull);
    uint32_t args[]={4097,0x30001,0x4ffff,0x10000,0x404};
    call_kernel(166,args,5);uint32_t first=g_eax;
    CHECK(first==0x80040000u && !MEM32(first) && !MEM32(first+4096));
    CHECK(vm_committed(0,0x100000000ull)==before+8192);
    MEM32(first)=0x12345678;MEM32(first+4096)=0xdeadbeef;
    call_kernel(166,args,5);CHECK(!g_eax && MEM32(first)==0x12345678);
    args[2]=0x7ffff;call_kernel(166,args,5);uint32_t second=g_eax;
    CHECK(second==0x80050000u && MEM32(first+4096)==0xdeadbeef);
    uint32_t exhausted[]={64u*1024*1024,0,0xffffffffu,0,4};
    call_kernel(166,exhausted,5);CHECK(!g_eax && MEM32(first)==0x12345678);
    call_kernel(171,&first,1);CHECK(test_write_faults(first));
    call_kernel(166,args,5);CHECK(g_eax==first && !MEM32(first) && !MEM32(first+4096));
    call_kernel(171,&first,1);call_kernel(171,&second,1);
    uint32_t length=1;call_kernel(165,&length,1);first=g_eax;
    CHECK(first && !test_write_faults(first));call_kernel(171,&first,1);
    CHECK(vm_committed(0,0x100000000ull)==before);
    fprintf(stderr,"PASS: contiguous allocation ABI, alignment/bounds/non-overlap, actual writes, RAM budget, release protection and zeroed reuse\n");
}
