/* Independent prefix/ASCII expectations exercise unchanged original helpers
 * through an actual retained thread CALL/RET, including caller-owned buffers. */
#if defined(PILOT_SCHEDULING_LLE)
#define THREAD_ORDINARY_CPU 0x1f200u
#define ORDINARY_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void test_memory_snapshot(uint32_t out[9]);
static void ordinary_context(unsigned ordinal,uint32_t first,uint32_t second,uint32_t third)
{
    uint32_t context=0x1b000;memset(memory+context,0xa5,160);
    MEM32(context+4)=first;MEM32(context+8)=second;MEM32(context+12)=third;
    MEM32(context+20)=0xfe000000u+ordinal*4;MEM32(context+76)=0;
    double fp=12345.6789;memcpy(memory+context+112,&fp,8);
}
static void ordinary_return(uint32_t result)
{
    uint32_t context=0x1b000;
    CHECK(MEM32(context+24)==result && MEM32(context+76)==1);
    CHECK(MEM32(context+56)==MEM32(context+60));
    CHECK((MEM32(context+64)&0x600)==(MEM32(context+68)&0x600));
    CHECK(MEM32(context+128)==0x11223344 && MEM32(context+132)==0x44556677 && MEM32(context+136)==0x22334455);
    CHECK(!memcmp(memory+context+80,memory+context+96,16) && !memcmp(memory+context+112,memory+context+120,8));
    CHECK(MEM32(context+140)==0xa5a5a5a5 && MEM64(context+152)==0xa5a5a5a5a5a5a5a5ull);
}
static void test_thread_ordinary_helpers_cpu_lle(unsigned ordinal,uint32_t allocate)
{
    CHECK((ordinal==269 || ordinal==308) && (!allocate || (ordinal==308 && (allocate&255))));
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    struct ThreadChildCode code={0},*c=&code;
    ORDINARY_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,80,0xdd,0x47,112,0x89,0x67,56,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,
        0xbe,0x55,0x44,0x33,0x22,0x9c,0x58,0x89,0x47,64);
    child_push_field(c,12);child_push_field(c,8);child_push_field(c,4);
    ORDINARY_BYTES(c,0x8b,0x47,20,0xff,0xd0,0x89,0x47,24,
        0x89,0x67,60,0x9c,0x58,0x89,0x47,68,
        0x0f,0x11,0x47,96,0xdd,0x5f,120,
        0x89,0x9f,128,0,0,0,0x89,0xaf,132,0,0,0,0x89,0xb7,136,0,0,0,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    memcpy(memory+THREAD_ORDINARY_CPU,c->bytes,c->size);
    CHECK(!pilot_io_device_resident(0x1f000,4096));
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8),before[9],after[9];test_memory_snapshot(before);
    for(unsigned life=0;life<(allocate?1u:3u);++life) {
        unsigned cases=ordinal==269?8:4;
        for(unsigned item=0;item<(allocate?1u:cases);++item) {
            uint8_t source[32],destination[32],source_descriptor[16],destination_descriptor[16];
            uint32_t expected=0,descriptor=0x1b400,unicode=0x1b440;
            if(ordinal==269) {
                const unsigned bytes[]={0,1,3,4,12,13,12,12};
                const uint32_t results[]={0,0,0,4,12,12,0,4};
                uint32_t buffer=0x1b203+life;
                memset(memory+0x1b200,0x6a,32);
                for(unsigned i=0;i<3;++i)MEM32(buffer+i*4)=0x13572468;
                if(item>=6)MEM32(buffer+(item==6?0:4))=0x89abcdef;
                memcpy(source,memory+0x1b200,32);expected=results[item];
                ordinary_context(269,buffer,bytes[item],0x13572468);
            } else {
                const unsigned capacities[]={5,3,0,5};
                const uint16_t text[]={'A',0,'B','Z'};
                uint32_t capacity=capacities[item],input=0x1b503+life,output=0x1b603+life;
                memset(memory+0x1b500,0xa5,32);memcpy(memory+input,text,sizeof(text));
                memset(memory+0x1b600,0x6a,32);
                memset(memory+descriptor,0xa5,16);MEM16(descriptor+2)=capacity;MEM32(descriptor+4)=output;
                memset(memory+unicode,0xa5,16);MEM16(unicode)=sizeof(text);MEM16(unicode+2)=sizeof(text);MEM32(unicode+4)=input;
                memcpy(source,memory+0x1b500,32);memcpy(source_descriptor,memory+unicode,16);
                memcpy(destination,memory+0x1b600,32);memcpy(destination_descriptor,memory+descriptor,16);
                ordinary_context(308,descriptor,unicode,allocate?allocate:(item==3?0x100:0));
                if(!allocate) {
                    uint16_t length=capacity==3?2:4;memcpy(destination_descriptor,&length,2);
                    if(capacity) {
                        const uint8_t converted[]={'A',0,'B','Z',0};
                        memcpy(destination+output-0x1b600,converted,length);
                        destination[output-0x1b600+length]=0;
                    }
                    expected=capacity==5?0:0x80000005u;
                }
            }
            fprintf(stderr,"[ORDINARY CPU CASE] ordinal=%u life=%u item=%u allocate=%u actual CALL/RET\n",ordinal,life,item,allocate);
            if(allocate) {
                uint32_t out=0x19800,birth[]={out,0,THREAD_ORDINARY_CPU,0x1b000,0};CHECK(!event_lle_call(254,birth));
                uint32_t handle=MEM32(out),ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
                uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
                uint32_t join[]={object,0,0,0,0},result=0xfeedcafe;
                const char *error=pilot_io_device_lle(159,memory,join,&result);
                CHECK(error && !strcmp(error,"device LLE allocated Unicode destination requires pool ownership"));
                CHECK(result==0xfeedcafe && !MEM32(0x1b000+76) && MEM32(0x1b000+24)==0xa5a5a5a5);
                CHECK(!memcmp(source,memory+0x1b500,32) && !memcmp(source_descriptor,memory+unicode,16));
                CHECK(!memcmp(destination,memory+0x1b600,32) && !memcmp(destination_descriptor,memory+descriptor,16));
                CHECK(!memcmp(memory+THREAD_ORDINARY_CPU,c->bytes,c->size));
                fprintf(stderr,"PASS: allocated Unicode low-byte TRUE rejects before original mutation; caller result/descriptors/bytes unchanged, actual join unfinished\n");
                return; /* The rejected original caller is terminal; do not resume it. */
            }
            thread_cpu_create_join(THREAD_ORDINARY_CPU,0x1b000,(life+item)&1);ordinary_return(expected);
            if(ordinal==269)CHECK(!memcmp(source,memory+0x1b200,32));
            else {
                CHECK(!memcmp(source,memory+0x1b500,32) && !memcmp(source_descriptor,memory+unicode,16));
                CHECK(!memcmp(destination,memory+0x1b600,32) && !memcmp(destination_descriptor,memory+descriptor,16));
            }
            CHECK(!memcmp(memory+THREAD_ORDINARY_CPU,c->bytes,c->size));
            CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==threads && MEM32(stats+8)==handles);
            test_memory_snapshot(after);CHECK(!memcmp(before,after,sizeof(before)));
        }
    }
    fprintf(stderr,"PASS: original%u independent prefix/ASCII/capacity/embedded-NUL/unaligned caller-buffer cases; RET12/ESP/callee GPR/IF/DF/x87/XMM/canaries and exact memory/thread/handle cleanup; no title/hardware/GPU/audio\n",ordinal);
}
#undef ORDINARY_BYTES
#endif
