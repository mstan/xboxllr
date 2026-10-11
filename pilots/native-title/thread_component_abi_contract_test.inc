/* Authored unchanged CPU callers exercise two complete selected entries:
 * RET8 with actual original file calls, then register arguments with RET0.
 * This qualifies transport, not a title's complete graphics lifecycle. */
#define THREAD_COMPONENT_ABI 0x13800u
#define THREAD_COMPONENT_STACK 0x13880u
#define THREAD_COMPONENT_REGS 0x138a0u
static unsigned component_abi_calls[2];
static int component_abi_failure;
static const char *test_component_abi_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(pilot_io_device_thread_component_active());
    CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_REGS+16,1,
        component_expected,0,test_component_abi_body));
    if(entry==THREAD_COMPONENT_STACK) {
        ++component_abi_calls[0];
        CHECK(MEM32(regs[4]+4)==0x1b000 && MEM32(regs[4]+8)==0x76543210);
        regs[0]=files_verify(component_expected);
        regs[4]+=component_abi_failure==1?4:12;
    } else {
        CHECK(entry==THREAD_COMPONENT_REGS);++component_abi_calls[1];
        CHECK(regs[1]==0x13579bdf && regs[2]==0x2468ace0);
        if(component_abi_failure==2)regs[3]^=1;
        regs[0]=regs[1]^regs[2];regs[4]+=4;
    }
    return NULL;
}
static void test_component_hash(const void *code,uint32_t bytes,char hex[65])
{
    unsigned char digest[32];
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)code,bytes,digest,32)>=0);
    for(unsigned i=0;i<32;++i)sprintf(hex+i*2,"%02x",digest[i]);
}
static void test_thread_component_abi_lle(const char *comparison,int failure_case)
{
    component_abi_failure=failure_case;install_fault_handler();
    CHECK(!pilot_io_device_files(memory));g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    const uint8_t stack_body[]={0xb8,0x10,0,0,0xc0,0xc2,8,0};
    const uint8_t regs_body[]={0xb8,0x10,0,0,0xc0,0xc3};
    const uint8_t caller[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,
        0x89,0x67,68,0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0x68,0x10,0x32,0x54,0x76,0x57,0xb8,0x80,0x38,1,0,0xff,0xd0,
        0x89,0x07,0x9c,0x5a,0x89,0x57,12,
        0xb9,0xdf,0x9b,0x57,0x13,0xba,0xe0,0xac,0x68,0x24,
        0xb8,0xa0,0x38,1,0,0xff,0xd0,0x89,0x47,4,
        0x89,0x67,72,0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0xc7,0x47,76,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_COMPONENT_STACK,stack_body,sizeof(stack_body));
    memcpy(memory+THREAD_COMPONENT_REGS,regs_body,sizeof(regs_body));
    memcpy(memory+THREAD_COMPONENT_ABI,caller,sizeof(caller));
    char stack_sha[65],regs_sha[65],wrong[65];
    test_component_hash(stack_body,sizeof(stack_body),stack_sha);
    test_component_hash(regs_body,sizeof(regs_body),regs_sha);
    memset(wrong,'0',64);wrong[64]=0;
    CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_STACK,sizeof(stack_body),wrong,8,test_component_abi_body));
    CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_STACK,sizeof(stack_body),stack_sha,3,test_component_abi_body));
    CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_STACK,sizeof(stack_body),stack_sha,44,test_component_abi_body));
    /* Reverse registration order checks that immutable lookup is sorted. */
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_REGS,sizeof(regs_body),regs_sha,0,test_component_abi_body));
    CHECK(!pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_STACK,sizeof(stack_body),stack_sha,8,test_component_abi_body));
    CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_STACK,sizeof(stack_body),stack_sha,8,test_component_abi_body));
    CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_STACK+1,1,stack_sha,0,test_component_abi_body));
    FILE *file=fopen(comparison,"rb");CHECK(file);
    BCRYPT_HASH_HANDLE hash=NULL;CHECK(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,NULL,0,NULL,0,0)>=0);
    unsigned char chunk[65536],digest[32];size_t bytes;
    while((bytes=fread(chunk,1,sizeof(chunk),file)))CHECK(BCryptHashData(hash,chunk,(ULONG)bytes,0)>=0);
    CHECK(!ferror(file));fclose(file);CHECK(BCryptFinishHash(hash,digest,32,0)>=0);BCryptDestroyHash(hash);
    char identity[65];for(unsigned i=0;i<32;++i)sprintf(identity+i*2,"%02x",digest[i]);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<(failure_case?1u:3u);++life) {
        uint32_t context=0x1b000;memset(memory+context,0,96);MEM32(context+64)=THREAD_COMPONENT_ABI;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        strcpy(component_expected,life==1?wrong:identity);
        if(failure_case==3)MEM8(THREAD_COMPONENT_REGS)=0x90;
        if(failure_case) {
            uint32_t out=0x19800,create[]={out,0,THREAD_COMPONENT_ABI,context,0};
            CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,failure_case==1?"selected helper completion outside its original CPU owner":
                failure_case==2?"selected helper changed undeclared caller register":
                "selected helper original bytes changed after binding"));
            CHECK(!MEM32(context+76) && component_abi_calls[0]==1 && component_abi_calls[1]==(failure_case==2?1u:0u));
            fprintf(stderr,"PASS: typed selected-entry terminal guard before original caller resumes: %s\n",error);return;
        }
        thread_cpu_create_join(THREAD_COMPONENT_ABI,context,life&1);
        CHECK(MEM32(context)==(life==1?0xc0000012u:0) && MEM32(context+4)==0x373f373f);
        CHECK(MEM32(context+8)==MEM32(context+12) && MEM32(context+68)==MEM32(context+72) && MEM32(context+76)==1);
        CHECK(!memcmp(memory+context+16,memory+context+32,16) && !memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_COMPONENT_STACK,stack_body,sizeof(stack_body)) && !memcmp(memory+THREAD_COMPONENT_REGS,regs_body,sizeof(regs_body)));
        CHECK(!event_lle_call(256,&stats));CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
        CHECK(pilot_io_device_thread_component_bind_abi(THREAD_COMPONENT_REGS+16,sizeof(regs_body),regs_sha,0,test_component_abi_body));
    }
    CHECK(component_abi_calls[0]==3 && component_abi_calls[1]==3 && thread_cpu_calls==3);
    fprintf(stderr,"PASS: two immutable typed entries across three original CPU lifetimes; RET8 file services, ECX/EDX RET0, ESP/flags/x87/XMM, sealed bindings and thread/handle baselines\n");
}
