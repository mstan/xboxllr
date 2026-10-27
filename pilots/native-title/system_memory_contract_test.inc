static void test_system_memory(void)
{
    uint64_t before=vm_committed(0,UINT64_C(0x100000000));
    uint32_t args[]={4097,PAGE_READWRITE},saved[]={g_ebx,g_esi,g_edi,g_ebp};
    call_kernel(167,args,2);uint32_t first=g_eax;
    CHECK(first==SYSTEM_MEMORY_BASE && !(first&4095) && !MEM32(first) && !MEM32(first+4096));
    CHECK(g_ebx==saved[0] && g_esi==saved[1] && g_edi==saved[2] && g_ebp==saved[3]);
    CHECK(vm_committed(0,UINT64_C(0x100000000))==before+8192);
    MEM32(first)=0x12345678;MEM32(first+4096)=0xabcdef01;
    args[0]=1;args[1]=PAGE_READONLY;call_kernel(167,args,2);uint32_t second=g_eax;
    CHECK(second==first+8192 && !MEM32(second) && test_write_faults(second));
    uint32_t release[]={first+1,0};call_kernel(172,release,2);CHECK(!g_eax);
    release[0]=first;release[1]=12288;call_kernel(172,release,2);CHECK(!g_eax && MEM32(first)==0x12345678);
    release[1]=1;call_kernel(172,release,2);CHECK(g_eax==1 && test_write_faults(first));
    CHECK(MEM32(first+4096)==0xabcdef01 && test_write_faults(second));
    args[1]=PAGE_READWRITE;call_kernel(167,args,2);CHECK(g_eax==first && !MEM32(first));MEM32(first)=0xdeadbeef;
    release[0]=first+4096;release[1]=0;call_kernel(172,release,2);CHECK(g_eax==1 && test_write_faults(first+4096));
    CHECK(MEM32(first)==0xdeadbeef);
    release[0]=first;call_kernel(172,release,2);CHECK(g_eax==1 && test_write_faults(first));
    call_kernel(172,release,2);CHECK(!g_eax);release[0]=second;call_kernel(172,release,2);CHECK(g_eax==1);
    CHECK(vm_committed(0,UINT64_C(0x100000000))==before);
    args[0]=5*4096;call_kernel(167,args,2);first=g_eax;CHECK(first);
    MEM32(first)=1;MEM32(first+4*4096)=5;
    release[0]=first+4096;release[1]=2*4096;call_kernel(172,release,2);CHECK(g_eax==2);
    release[0]=first;release[1]=0;call_kernel(172,release,2);CHECK(g_eax==1);
    CHECK(test_write_faults(first) && MEM32(first+4*4096)==5);
    release[0]=first+3*4096;call_kernel(172,release,2);CHECK(g_eax==2);
    CHECK(vm_committed(0,UINT64_C(0x100000000))==before);
    const uint32_t invalid[][2]={{0,4},{UINT32_MAX,4},{4096,1},{4096,0x604},{4096,0x1004}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i){call_kernel(167,invalid[i],2);CHECK(!g_eax);}
    uint64_t service=vm_service_bytes;vm_service_bytes=64ull*1024*1024-before;
    args[0]=4096;call_kernel(167,args,2);CHECK(!g_eax);vm_service_bytes=service;
    CHECK(vm_committed(0,UINT64_C(0x100000000))==before);
    puts("PASS system memory: stdcall ABI, shared budget, real CPU bytes/protection, partial and full release, invalid spans, zeroed reuse");
}
