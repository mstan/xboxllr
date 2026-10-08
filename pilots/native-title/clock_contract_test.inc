static void test_system_clock(void)
{
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
    CHECK(g_ebx==0x12345678 && g_esi==0x23456789 && g_edi==0x3456789a && g_ebp==0x456789ab);
    fprintf(stderr,"PASS: shared system clock, real progression, calendar/leap conversions, invalid dates, output bounds and stdcall ABI\n");
}
