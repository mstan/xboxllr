/* Calendar calls retain the actual original CPU's CALL/RET frame. Fixed NT
 * epoch/leap-day cases are independent expectations, not adapter output. */
#if defined(PILOT_SCHEDULING_LLE)
#define THREAD_CALENDAR_CPU 0x1f000u
#define CALENDAR_BYTES(c,...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
static void test_memory_snapshot(uint32_t out[9]);
static void test_thread_calendar_cpu_lle(void)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    struct ThreadChildCode code={0},*c=&code;
    CALENDAR_BYTES(c,0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,80,0xdd,0x47,112,0x89,0x67,56,
        0xbb,0x44,0x33,0x22,0x11,0xbd,0x77,0x66,0x55,0x44,
        0xbe,0x55,0x44,0x33,0x22,0x9c,0x58,0x89,0x47,64);
    child_output(c,8);child_output(c,0);child_call(c,305);
    child_output(c,24);child_output(c,8);child_call(c,304);
    CALENDAR_BYTES(c,0x88,0x47,72);
    child_output(c,48);child_output(c,32);child_call(c,304);
    CALENDAR_BYTES(c,0x88,0x47,73,0x89,0x67,60,0x9c,0x58,0x89,0x47,68,
        0x0f,0x11,0x47,96,0xdd,0x5f,120,
        0x89,0x9f,128,0,0,0,0x89,0xaf,132,0,0,0,0x89,0xb7,136,0,0,0,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    memcpy(memory+THREAD_CALENDAR_CPU,c->bytes,c->size);
    CHECK(!pilot_io_device_resident(0x1f000,4096));
    const uint64_t ticks[]={0,116444736000000000ull,125963012967890000ull};
    const int16_t expected[][8]={{1601,1,1,0,0,0,0,1},
        {1970,1,1,0,0,0,0,4},{2000,2,29,12,34,56,789,2}};
    const int16_t invalid[]={2001,2,29,0,0,0,0,0};
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t threads=MEM32(stats+4),handles=MEM32(stats+8),before[9],after[9];
    test_memory_snapshot(before);
    for(unsigned life=0;life<3;++life) {
        uint32_t context=0x1b000;memset(memory+context,0xa5,160);
        MEM64(context)=ticks[life];memcpy(memory+context+32,invalid,sizeof(invalid));
        MEM64(context+48)=0x0123456789abcdefull;
        MEM32(context+76)=0;double fp=12345.6789;memcpy(memory+context+112,&fp,8);
        fprintf(stderr,"[CALENDAR CPU CASE] life=%u ordinal305 actual CALL then paired304\n",life);
        thread_cpu_create_join(THREAD_CALENDAR_CPU,context,life&1);
        CHECK(!memcmp(memory+context+8,expected[life],16));
        CHECK(MEM64(context+24)==ticks[life] && MEM8(context+72)==1 && !MEM8(context+73));
        CHECK(MEM64(context+48)==0x0123456789abcdefull);
        CHECK(MEM32(context+56)==MEM32(context+60) && MEM32(context+76)==1);
        CHECK((MEM32(context+64)&0x600)==(MEM32(context+68)&0x600));
        CHECK(MEM32(context+128)==0x11223344 && MEM32(context+132)==0x44556677 && MEM32(context+136)==0x22334455);
        CHECK(!memcmp(memory+context+80,memory+context+96,16) && !memcmp(memory+context+112,memory+context+120,8));
        CHECK(MEM32(context+140)==0xa5a5a5a5 && MEM64(context+152)==0xa5a5a5a5a5a5a5a5ull);
        CHECK(!memcmp(memory+THREAD_CALENDAR_CPU,c->bytes,c->size));
        CHECK(!event_lle_call(256,&stats) && MEM32(stats+4)==threads && MEM32(stats+8)==handles);
        test_memory_snapshot(after);CHECK(!memcmp(before,after,sizeof(before)));
    }
    fprintf(stderr,"PASS: three original CPU calendar304/305 lives; independent epoch/1970/leap-day fields, invalid-date untouched output, RET8/ESP/callee GPR/IF/DF/x87/XMM/canaries and exact memory/thread/handle cleanup; no title/hardware/GPU/audio\n");
}
#undef CALENDAR_BYTES
#endif
