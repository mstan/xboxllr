/* Authored CALL/RET callers test a build-selected complete helper on the
 * original thread/CPU. Identity comes from an independent host file digest;
 * actual FATX/IDE reads, VM pages and handles belong to the original kernel. */
#ifndef PILOT_FILES_LLE
#include "files_lle.inc"
#endif
#define THREAD_COMPONENT_CPU 0x13800u
#define THREAD_COMPONENT_HELPER 0x13880u
static char component_expected[65];
static unsigned component_calls;
static int component_failure_case;
static const char *test_component_body(uint32_t entry,uint32_t regs[8])
{
    CHECK(entry==THREAD_COMPONENT_HELPER && pilot_io_device_thread_component_active());
    ++component_calls;
    if(component_failure_case==1) {regs[3]^=1;regs[4]+=4;return NULL;}
    if(component_failure_case==3) {MEM32(regs[4])^=4;regs[4]+=4;return NULL;}
    regs[0]=files_verify(component_expected);regs[4]+=4;return NULL;
}
static void test_thread_component_cpu_lle(const char *comparison,int failure_case)
{
    component_failure_case=failure_case;
    CHECK(!pilot_io_device_thread_component_active());
    uint32_t unused=0;CHECK(pilot_io_device_thread_component_call(202,&unused,&unused));
    install_fault_handler();CHECK(!pilot_io_device_files(memory));
    g_fs_base=0x19000;MEM8(g_fs_base+0x24)=0;
    uint32_t revision=0;CHECK(!usb_access(0xfed00000u,0,&revision));
    /* EAX fallback error would trigger XAPI's optical failure. A selected
     * complete helper returns its own actual identity result instead. */
    const uint8_t helper[]={0xb8,0x10,0,0,0xc0,0xc3};
    const uint8_t caller[]={0x53,0x55,0x56,0x57,0x8b,0x7c,0x24,0x14,
        0x0f,0x10,0x47,16,0xdd,0x47,48,
        0xb8,0xff,0xff,0xff,0xff,0x83,0xc0,1,0x9c,0x5a,0x89,0x57,8,
        0xb9,0x80,0x38,1,0,0xff,0xd1,
        0x89,0x07,0x9c,0x5a,0x89,0x57,12,
        0x0f,0x11,0x47,32,0xdd,0x5f,56,
        0xc7,0x47,4,1,0,0,0,0x5f,0x5e,0x5d,0x5b,0xc2,4,0};
    memcpy(memory+THREAD_COMPONENT_HELPER,helper,sizeof(helper));
    memcpy(memory+THREAD_COMPONENT_CPU,caller,sizeof(caller));
    unsigned char digest[32];char boundary_sha[65];
    CHECK(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)helper,sizeof(helper),digest,32)>=0);
    for(unsigned i=0;i<32;++i)sprintf(boundary_sha+i*2,"%02x",digest[i]);
    char wrong[65];memset(wrong,'0',64);wrong[64]=0;
    CHECK(pilot_io_device_thread_component_bind(THREAD_COMPONENT_HELPER,sizeof(helper),wrong,test_component_body));
    CHECK(!component_calls && !memcmp(memory+THREAD_COMPONENT_HELPER,helper,sizeof(helper)));
    CHECK(!pilot_io_device_thread_component_bind(THREAD_COMPONENT_HELPER,sizeof(helper),boundary_sha,test_component_body));
    CHECK(pilot_io_device_thread_component_bind(THREAD_COMPONENT_HELPER,sizeof(helper),boundary_sha,test_component_body));
    FILE *file=fopen(comparison,"rb");CHECK(file);
    BCRYPT_HASH_HANDLE hash=NULL;CHECK(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE,&hash,NULL,0,NULL,0,0)>=0);
    unsigned char chunk[65536];size_t bytes;
    while((bytes=fread(chunk,1,sizeof(chunk),file)))CHECK(BCryptHashData(hash,chunk,(ULONG)bytes,0)>=0);
    CHECK(!ferror(file));fclose(file);CHECK(BCryptFinishHash(hash,digest,32,0)>=0);BCryptDestroyHash(hash);
    char identity[65];for(unsigned i=0;i<32;++i)sprintf(identity+i*2,"%02x",digest[i]);
    uint32_t stats=0x19700;MEM32(stats)=12;CHECK(!event_lle_call(256,&stats));
    uint32_t baseline_threads=MEM32(stats+4),baseline_handles=MEM32(stats+8);
    for(unsigned life=0;life<(failure_case?1:3);++life) {
        const uint32_t context=0x1b000;
        memset(memory+context,0,96);MEM32(context+64)=THREAD_COMPONENT_CPU;
        for(unsigned i=0;i<16;++i)MEM8(context+16+i)=(uint8_t)(0x35+i*7);
        double expected=12345.6789;memcpy(memory+context+48,&expected,8);
        strcpy(component_expected,life==1?wrong:identity);
        if(failure_case==2)MEM8(THREAD_COMPONENT_HELPER)=0x90;
        if(failure_case) {
            uint32_t out=0x19800,create[]={out,0,THREAD_COMPONENT_CPU,context,0};
            CHECK(!event_lle_call(254,create));uint32_t handle=MEM32(out);
            uint32_t ref[]={handle,0,out+4};CHECK(!event_lle_call(246,ref));uint32_t object=MEM32(out+4);
            uint32_t priority[]={object,29};CHECK(event_lle_call(148,priority)==8);
            uint32_t join[]={object,0,0,0,0},result=0;
            const char *error=pilot_io_device_lle(159,memory,join,&result);
            CHECK(error && !strcmp(error,failure_case==1?"selected helper changed undeclared caller register":
                failure_case==2?"selected helper original bytes changed after binding":
                "selected helper changed original return frame"));
            CHECK(!MEM32(context+4) && component_calls==(failure_case==2?0u:1u));
            fprintf(stderr,"PASS: selected helper terminal guard before original caller resumes: %s\n",error);return;
        }
        thread_cpu_create_join(THREAD_COMPONENT_CPU,context,life&1);
        CHECK(MEM32(context)==(life==1?0xc0000012u:0) && MEM32(context+4)==1);
        CHECK(MEM32(context+8)==MEM32(context+12));
        CHECK(!memcmp(memory+context+16,memory+context+32,16));
        CHECK(!memcmp(memory+context+48,memory+context+56,8));
        CHECK(!memcmp(memory+THREAD_COMPONENT_HELPER,helper,sizeof(helper)));
        CHECK(!event_lle_call(256,&stats));
        CHECK(MEM32(stats+4)==baseline_threads && MEM32(stats+8)==baseline_handles);
    }
    CHECK(component_calls==3 && thread_cpu_calls==3);
    fprintf(stderr,"PASS: three selected-helper original CPU lifetimes, actual HDD identity/wrong identity/reuse, CALL/RET registers/flags/x87/XMM and thread/handle baselines\n");
}
