/* Same counted ANSI bytes execute unchanged original279/316 on actual threads.
 * Compare the native ABI against those returned results, including all byte
 * values, embedded NULs and the full16-bit length. No desktop RTL oracle. */
#define THREAD_STRING_COMPARE_CPU 0x13a00u
#define THREAD_STRING_UPPER_CPU 0x13b00u
static void thread_string_loop_code(uint32_t entry,unsigned ordinal)
{
    struct ThreadChildCode code={0},*c=&code;
    const uint8_t prologue[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x8b,0x5f,8,0x8b,0x77,4};
    child_emit(c,prologue,sizeof(prologue));unsigned loop=c->size;
    if(ordinal==279) {
        const uint8_t push[]={0xff,0x73,12,0xff,0x73,8};child_emit(c,push,sizeof(push));
    }
    const uint8_t push[]={0xff,0x73,4};child_emit(c,push,sizeof(push));child_call(c,ordinal);
    const uint8_t result[]={0x0f,0xb6,0xc0,0x89,0x43,16,0x83,0xc3,24,0x4e,0x0f,0x85};
    child_emit(c,result,sizeof(result));child_word(c,(uint32_t)(loop-(c->size+4)));
    const uint8_t finish[]={0xc7,0x47,12,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    child_emit(c,finish,sizeof(finish));CHECK(c->size<0x100);memcpy(memory+entry,c->bytes,c->size);
}
static void string_compare_record(uint32_t table,unsigned row,uint32_t left,uint16_t left_bytes,
    uint32_t right,uint16_t right_bytes,uint32_t insensitive)
{
    uint32_t record=table+row*24,strings=table+0x5000+row*16;
    MEM16(strings)=left_bytes;MEM16(strings+2)=0;MEM32(strings+4)=left;
    MEM16(strings+8)=right_bytes;MEM16(strings+10)=UINT16_MAX;MEM32(strings+12)=right;
    MEM32(record)=279;MEM32(record+4)=strings;MEM32(record+8)=strings+8;
    MEM32(record+12)=insensitive;MEM32(record+16)=0xbaadf00d;MEM32(record+20)=0x13572468;
}
static void test_thread_string_compare_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    thread_string_loop_code(THREAD_STRING_COMPARE_CPU,279);thread_string_loop_code(THREAD_STRING_UPPER_CPU,316);
    uint32_t vm[7],status=0,in[]={0,0x50000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0};
    CHECK(!pilot_io_device_vm(184,memory,in,vm,&status) && !status);uint32_t allocation=vm[0];
    uint32_t table=allocation+0x22000,upper=allocation+0x31000,context=0x1b000,stats=0x19700;
    MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    /* Real runtime clock permits long original loops to continue across bounded
     * CPU slices, including the actual PIT/PIC interrupt path. */
    CHECK(!pilot_io_device_clock_start());
    for(unsigned life=0;life<3;++life) {
        for(unsigned value=0;value<256;++value) {
            uint32_t record=upper+value*24;MEM32(record)=316;MEM32(record+4)=value;
            MEM32(record+16)=0xbaadf00d;MEM32(record+20)=0x24681357;
        }
        memset(memory+context,0,96);MEM32(context+4)=256;MEM32(context+8)=upper;
        MEM32(context+64)=THREAD_STRING_UPPER_CPU;thread_cpu_create_join(THREAD_STRING_UPPER_CPU,context,life&1);
        CHECK(MEM32(context+12)==1);
        unsigned rows=0;
        for(unsigned value=0;value<256;++value) {
            uint32_t record=upper+value*24,args[]={value};call_kernel(316,args,1);
            CHECK(g_eax==MEM32(record+16) && MEM32(record+20)==0x24681357);
            uint32_t left=allocation+3+value*2,right=allocation+0x503+value*2;
            MEM8(left)=value;MEM8(left+1)=0x13;MEM8(right)=MEM32(record+16);MEM8(right+1)=0x24;
            const uint32_t flags[]={0,1,255};
            for(unsigned flag=0;flag<3;++flag)string_compare_record(table,rows++,left,1,right,1,flags[flag]);
        }
        const struct {const char *left,*right;unsigned left_bytes,right_bytes;} cases[]={
            {"","",0,0},{"a","",1,0},{"","a",0,1},{"abc","ABC",3,3},
            {"a\0b","A\0B",3,3},{"a\0b","A\0C",3,3},{"prefix-tail","prefix",11,6}};
        for(unsigned item=0;item<sizeof(cases)/sizeof(cases[0]);++item) {
            uint32_t left=allocation+0x900+item*32,right=left+16;
            memcpy(memory+left,cases[item].left,cases[item].left_bytes);
            memcpy(memory+right,cases[item].right,cases[item].right_bytes);
            const uint32_t flags[]={0,1,0x100,0x101};
            for(unsigned flag=0;flag<4;++flag)string_compare_record(table,rows++,left,cases[item].left_bytes,
                right,cases[item].right_bytes,flags[flag]);
        }
        uint32_t left=allocation+0x1003,right=allocation+0x11003,bad=allocation+0x40000;
        memset(memory+left,'a',65535);memset(memory+right,'A',65535);
        memset(memory+bad,'a',65535);MEM8(bad+65534)='b';
        const uint32_t flags[]={0,1,0x100,0x101};
        for(unsigned item=0;item<3;++item)for(unsigned flag=0;flag<4;++flag) {
            uint32_t a=left,b=right;uint16_t bytes=65535;
            if(!item) {a=allocation+0xd00;b=a+0x100;bytes=256;
                for(unsigned value=0;value<256;++value){MEM8(a+value)=value;MEM8(b+value)=MEM32(upper+value*24+16);}}
            if(item==2)a=bad;
            string_compare_record(table,rows++,a,bytes,b,bytes,flags[flag]);
        }
        CHECK(rows==808);
        MEM32(context+4)=rows;MEM32(context+8)=table;MEM32(context+12)=0;
        MEM32(context+64)=THREAD_STRING_COMPARE_CPU;thread_cpu_create_join(THREAD_STRING_COMPARE_CPU,context,life&1);
        CHECK(MEM32(context+12)==1);
        for(unsigned row=0;row<rows;++row) {
            uint32_t record=table+row*24,args[]={MEM32(record+4),MEM32(record+8),MEM32(record+12)};
            uint32_t saved[4];memcpy(saved,memory+args[0],8);memcpy(saved+2,memory+args[1],8);
            call_kernel(279,args,3);CHECK(g_eax==MEM32(record+16) && MEM32(record+20)==0x13572468);
            CHECK(!memcmp(saved,memory+args[0],8) && !memcmp(saved+2,memory+args[1],8));
        }
        for(unsigned value=0;value<256;++value) {
            CHECK(MEM8(allocation+3+value*2)==value && MEM8(allocation+4+value*2)==0x13);
            CHECK(MEM8(allocation+0x503+value*2)==MEM32(upper+value*24+16) && MEM8(allocation+0x504+value*2)==0x24);
        }
        for(unsigned i=0;i<65535;++i)CHECK(MEM8(left+i)=='a' && MEM8(right+i)=='A' && MEM8(bad+i)==(i==65534?'b':'a'));
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(thread_cpu_calls==6);CHECK(!pilot_io_device_clock_bind(NULL,NULL));
    uint32_t release[]={allocation,0,MEM_RELEASE,0,0};CHECK(!pilot_io_device_vm(199,memory,release,vm,&status) && !status);
    fprintf(stderr,"PASS: three original/native counted-comparison lifetimes, each808 equality and256 uppercase cases, actual RET/Buffer/input canaries/65535-byte loops/clock slices and original cleanup\n");
}
