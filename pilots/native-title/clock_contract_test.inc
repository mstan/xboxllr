static void test_system_clock(void)
{
    /* Install the real title handler before the first calendar call: engine
     * allocation and fault handling are part of integration correctness. */
    install_fault_handler();
    uint32_t out=0x19405; /* Also exercise unaligned guest output. */
    memset(memory+out-4,0xcd,16);
    uint64_t before=dispatch_wall_time();
    g_ebx=0x12345678;g_esi=0x23456789;g_edi=0x3456789a;g_ebp=0x456789ab;
    call_kernel(128,&out,1);
    uint64_t first;memcpy(&first,memory+out,8);
    CHECK(first>=before && first<=dispatch_wall_time());
    CHECK(first>116444736000000000ull); /* FILETIME epoch, not Unix seconds. */
    CHECK(MEM32(out-4)==0xcdcdcdcd && MEM32(out+8)==0xcdcdcdcd);
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    Sleep(20);
    before=dispatch_wall_time();call_kernel(128,&out,1);
    uint64_t second;memcpy(&second,memory+out,8);
    CHECK(second>=before && second<=dispatch_wall_time() && second>first);
    CHECK(MEM32(out-4)==0xcdcdcdcd && MEM32(out+8)==0xcdcdcdcd);
    const struct {uint64_t time;int16_t fields[8];} cases[]={
        {0,{1601,1,1,0,0,0,0,1}},
        {116444736000000000ull,{1970,1,1,0,0,0,0,4}},
        {125963012967890000ull,{2000,2,29,12,34,56,789,2}},
        {157521023999990000ull,{2100,3,1,23,59,59,999,1}},
        {252190368000010000ull,{2400,2,29,0,0,0,1,2}}};
    uint32_t args[]={0x19505,0x19605};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        uint64_t input=cases[i].time+9999;
        memcpy(memory+args[0],&input,8);memset(memory+args[1]-4,0xcd,24);
        call_kernel(305,args,2);
        CHECK(!memcmp(memory+args[1],cases[i].fields,16));
        CHECK(MEM32(args[1]-4)==0xcdcdcdcd && MEM32(args[1]+16)==0xcdcdcdcd);
        uint32_t inverse[]={args[1],args[0]};memset(memory+args[0]-4,0xcd,16);
        call_kernel(304,inverse,2);CHECK(g_eax==1);
        uint64_t result;memcpy(&result,memory+args[0],8);CHECK(result==cases[i].time);
        CHECK(MEM32(args[0]-4)==0xcdcdcdcd && MEM32(args[0]+8)==0xcdcdcdcd);
    }
    const int16_t invalid[][8]={{2001,2,29},{2100,2,29},{2000,0,1},{2000,1,1,24},{2000,1,1,0,0,0,1000}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        memcpy(memory+args[0],invalid[i],16);memset(memory+args[1]-4,0xcd,16);
        call_kernel(304,args,2);CHECK(!g_eax);
        for(unsigned n=0;n<16;++n)CHECK(memory[args[1]-4+n]==0xcd);
    }
    /* Original-kernel oracle independently qualifies these overlapping and
     * page-crossing buffers; run the same contract in either selected build. */
    uint32_t source=0x1affd;
    for(unsigned kind=0;kind<3;++kind) for(int offset=-16;offset<=16;++offset) {
        unsigned char expected[80];memset(expected,0xcd,sizeof(expected));
        uint64_t input=cases[2].time+9999;
        const void *value=kind==0?(const void*)&input:kind==1?(const void*)cases[2].fields:(const void*)invalid[0];
        unsigned input_size=kind==0?8:16;
        memcpy(expected+32,value,input_size);
        memcpy(memory+source-32,expected,sizeof(expected));
        if(kind==0)memcpy(expected+32+offset,cases[2].fields,16);
        if(kind==1)memcpy(expected+32+offset,&cases[2].time,8);
        uint32_t alias_args[]={source,(uint32_t)(source+offset)};
        call_kernel(kind==0?305:304,alias_args,2);
        if(kind!=0)CHECK(g_eax==(kind==1?1u:0u));
        CHECK(!memcmp(memory+source-32,expected,sizeof(expected)));
    }
    /* Actual title thread stacks occupy high guest addresses. Exercise the
     * adapter there as well as in the small contract-test allocation. */
    CHECK(VirtualAlloc(memory+0x601f0000u,0x20000,MEM_COMMIT,PAGE_READWRITE)==memory+0x601f0000u);
    uint32_t stack_args[]={0x601ffd38,0x601ffd28};
    memcpy(memory+stack_args[0],&cases[2].time,8);
    call_kernel(305,stack_args,2);
    CHECK(!memcmp(memory+stack_args[1],cases[2].fields,16));
    uint32_t stack_inverse[]={stack_args[1],stack_args[0]};
    call_kernel(304,stack_inverse,2);CHECK(g_eax==1);
    CHECK(!memcmp(memory+stack_args[0],&cases[2].time,8));
    CHECK(VirtualFree(memory+0x601f0000u,0x20000,MEM_DECOMMIT));
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    fprintf(stderr,"PASS: shared system clock, real progression, calendar/leap conversions, invalid dates, 99 overlapping/page-crossing cases, output bounds and stdcall ABI\n");
}
