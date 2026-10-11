/* Independent observed original289/290 lengths at the16-bit boundary;
 * unchanged original helpers execute through real thread CALL/RET frames. */
#define THREAD_STRING_CPU 0x13900u
static void test_thread_string_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    const uint8_t caller[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0xff,0x77,16,0x8d,0x07,0x50,0x8b,0x47,20,0xff,0xd0,
        0xc7,0x47,24,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_STRING_CPU,caller,sizeof(caller));
    const uint32_t lengths[2][9]={{UINT32_MAX,0,1,255,65533,65534,65535,65536,65537},
        {UINT32_MAX,0,1,255,32765,32766,32767,32768,32769}};
    const uint16_t actual[2][9][2]={{{0,0},{0,1},{1,2},{255,256},{65533,65534},{65534,65535},{65535,0},{0,1},{1,2}},
        {{0,0},{0,2},{2,4},{510,512},{65530,65532},{65532,65534},{65534,0},{0,2},{2,4}}};
    uint32_t in[]={0,131072,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0},vm[7],status=0;
    CHECK(!pilot_io_device_vm(184,memory,in,vm,&status) && !status);uint32_t allocation=vm[0];
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned kind=0;kind<2;++kind)for(unsigned item=0;item<9;++item) {
        uint32_t count=lengths[kind][item],unit=kind+1,context=0x1b000;
        uint32_t source=count==UINT32_MAX?0:allocation+3;
        if(source) {
            for(uint32_t i=0;i<count;++i) {
                if(unit==1)MEM8(source+i)=0x80;else MEM16(source+i*2)=0x1234;
            }
            if(unit==1)MEM8(source+count)=0;else MEM16(source+count*2)=0;
        }
        memset(memory+context,0,96);memset(memory+context,0xa5,12);
        MEM32(context+16)=source;MEM32(context+20)=0xfe000000u+4*(289+kind);
        MEM32(context+64)=THREAD_STRING_CPU;
        thread_cpu_create_join(THREAD_STRING_CPU,context,item&1);
        CHECK(MEM16(context)==actual[kind][item][0] && MEM16(context+2)==actual[kind][item][1]);
        CHECK(MEM32(context+4)==source && MEM32(context+8)==0xa5a5a5a5 && MEM32(context+24)==1);
        /* Compare native construction through its actual kernel-dispatch ABI.
         * No host RTL call, string allocation or copied Buffer is permitted. */
        uint32_t out=context+32,args[]={out,source};memset(memory+out,0xa5,12);
        call_kernel(289+kind,args,2);
        CHECK(g_esp==0x18000+12 && !memcmp(memory+context,memory+out,12));
        if(source) {
            CHECK(unit==1?MEM8(source+count)==0:MEM16(source+count*2)==0);
            for(uint32_t i=0;i<count;++i)CHECK(unit==1?MEM8(source+i)==0x80:MEM16(source+i*2)==0x1234);
        }
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(thread_cpu_calls==18);uint32_t release[]={allocation,0,MEM_RELEASE,0,0};
    CHECK(!pilot_io_device_vm(199,memory,release,vm,&status) && !status);
    fprintf(stderr,"PASS:18 original-thread/native ANSI/UTF16 cases, null/empty/unaligned/16-bit wrap, actual Buffer/canaries/RET and original ownership cleanup\n");
}
