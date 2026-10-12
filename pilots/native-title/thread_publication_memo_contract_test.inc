/* Actual CPU stores bypass all service mutation bookkeeping. A warm memo
 * must still detect changed original PTE/PDE bytes before the next service. */
static void test_thread_publication_memo_guard(unsigned kind)
{
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;uint32_t revision=0;
    CHECK(!usb_access(0xfed00000,0,&revision));
    uint32_t inputs[]={0x40000000u,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE,0};
    uint32_t output[7]={0},status=0;
    CHECK(!pilot_io_device_vm(184,memory,inputs,output,&status) && !status && output[0]==inputs[0]);
    uint32_t allocation=output[0];MEM32(allocation)=0x2468ace0u;
    if(kind==1) {
        uint32_t pin[]={allocation,4096,0};CHECK(!event_lle_call(175,pin));
    }
    uint32_t statistics=0x19700;MEM32(statistics)=12;
    CHECK(!event_lle_call(256,&statistics));
    uint64_t hits=0,misses=0;CHECK(!pilot_io_device_publication_memo_info(&hits,&misses));
    CHECK(!event_lle_call(256,&statistics));
    uint64_t warm_hits=0,warm_misses=0;
    CHECK(!pilot_io_device_publication_memo_info(&warm_hits,&warm_misses) && warm_hits>hits);
    struct ThreadChildCode code={0};struct ThreadChildCode *c=&code;
#define MEMO_BYTES(...) do {const uint8_t b[]={__VA_ARGS__};child_emit(c,b,sizeof(b));} while(0)
    MEMO_BYTES(0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,0x8b,0x07);
    if(kind!=2) {
        /* Original self-mapped PTE namespace, independently of host memo. */
        MEMO_BYTES(0xc1,0xe8,12,0xc1,0xe0,2,0x05);child_word(c,0xc0000000u);
        MEMO_BYTES(0x89,0xc2);
    } else {
        /* Read the real directory through its physical alias. */
        MEMO_BYTES(0x89,0xc1,0xc1,0xe9,22);
        MEMO_BYTES(0x8d,0x14,0x8d);child_word(c,0x8000f000u);
    }
    /* Clear actual PRESENT, then request a genuine kernel import. */
    MEMO_BYTES(0x83,0x22,0xfe,0xc7,0x47,8,1,0,0,0);
    child_push(c,0);child_call(c,97);
    MEMO_BYTES(0xc7,0x47,12,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0);
    CHECK(c->size<256);memcpy(memory+0x12d00,c->bytes,c->size);
    const uint32_t context=0x1b000,out=0x19800;
    memset(memory+context,0,128);MEM32(context)=allocation;MEM32(context+64)=0x12d00;
    uint32_t create[]={out,0,0x12d00,context,0};CHECK(!event_lle_call(254,create));
    uint32_t handle=MEM32(out),reference[]={handle,0,out+4};
    CHECK(!event_lle_call(246,reference));uint32_t object=MEM32(out+4);
    uint32_t join[]={object,0,0,0,0},result=0;
    const char *error=pilot_io_device_lle(159,memory,join,&result);
    const char *expected=kind==1?"device LLE pinned publication disappeared":
        "device LLE published mapping changed between calls";
    CHECK(error && !strcmp(error,expected));
    CHECK(MEM32(context+8)==1 && !MEM32(context+12));
    uint64_t final_hits=0,final_misses=0;
    CHECK(!pilot_io_device_publication_memo_info(&final_hits,&final_misses));
    CHECK(final_hits>warm_hits && final_misses>warm_misses);
    fprintf(stderr,"PASS: warm publication memo detects raw original %s store before service return; %s; hits=%llu misses=%llu\n",
        kind==0?"self-mapped PTE":kind==1?"self-mapped pinned PTE":"physical-alias directory",
        error,(unsigned long long)final_hits,(unsigned long long)final_misses);
#undef MEMO_BYTES
}
