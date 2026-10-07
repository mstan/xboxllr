/* Exercise native ownership/ABI contracts without a game executing. */
#define main pilot_unused_main
#include "runtime.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); ExitProcess(1); } } while (0)
static LONG test_counter;
static uint32_t test_lock=0x14000, test_worker_address=0x12000;

static void test_worker(void)
{
    CHECK(MEM32(g_esp+4)==0x12345678 && MEM32(g_esp+8)==0x87654321);
    CHECK(MEM32(MEM32(g_fs_base+0x28)+0x28)==MEM32(g_fs_base+4)-20);
    CHECK(MEM32(MEM32(g_fs_base+4)-20)==0);
    g_eax=99; /* caller registers must remain private */
    for (unsigned i=0;i<10000;++i) {
        critical_section(277,test_lock);
        critical_section(277,test_lock);
        CHECK(MEM32(test_lock+20)==2);
        ++test_counter;
        critical_section(294,test_lock);
        critical_section(294,test_lock);
    }
}

recomp_func_t recomp_lookup(uint32_t va) { return va==test_worker_address ? test_worker : NULL; }

static void call_kernel(unsigned ordinal,const uint32_t *args,unsigned argc)
{
    g_esp=0x18000;
    MEM32(g_esp)=0x12340;
    for (unsigned i=0;i<argc;++i) MEM32(g_esp+4+i*4)=args[i];
    next_kernel_ordinal=ordinal;
    kernel_dispatch();
    CHECK(g_esp==0x18000+4+argc*4);
}

int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    memory=VirtualAlloc(NULL,(SIZE_T)0x100000000ull,MEM_RESERVE,PAGE_NOACCESS);
    CHECK(memory!=NULL);
    g_xbox_mem_offset=(ptrdiff_t)memory;
    commit(0x10000,0x10000);
    g_esp=0x18000; MEM32(g_esp)=0x12340;
    image_size=0x200; image_bytes=calloc(1,image_size); CHECK(image_bytes!=NULL);
    if (argc==2) {
        if (!strcmp(argv[1],"instruction")) recomp_unimpl("test unsupported instruction",0x12340);
        if (!strcmp(argv[1],"indirect")) recomp_icall_fail_log(0x12340);
        if (!strcmp(argv[1],"kernel")) { next_kernel_ordinal=366; kernel_dispatch(); }
        return 1;
    }
    critical_section(291,test_lock);
    CHECK(MEM32(test_lock+16)==0xffffffffu && MEM32(test_lock+20)==0);
    uint32_t args[10]={0x19000,0,0x10000,20,0x19004,0x12345678,0x87654321,1,0,0x12000};
    call_kernel(255,args,10); CHECK(g_eax==0);
    uint32_t handle1=MEM32(0x19000),tid1=MEM32(0x19004);
    HANDLE host1=threads[1].handle;
    CHECK(WaitForSingleObject(host1,0)==WAIT_TIMEOUT && test_counter==0);
    call_kernel(255,args,10); CHECK(g_eax==0);
    HANDLE host2=threads[2].handle;
    CHECK(MEM32(0x19000)!=handle1 && MEM32(0x19004)!=tid1);
    g_eax=42;
    CHECK(ResumeThread(host1)==1 && ResumeThread(host2)==1);
    CHECK(WaitForSingleObject(host1,5000)==WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(host2,5000)==WAIT_OBJECT_0);
    CHECK(test_counter==20000 && g_eax==42);
    CHECK(MEM32(test_lock+16)==0xffffffffu && MEM32(test_lock+20)==0 && MEM32(test_lock+24)==0);
    args[0]=handle1; call_kernel(187,args,1); CHECK(g_eax==0);
    call_kernel(187,args,1); CHECK(g_eax==0xc0000008u);

    /* User settings: real payload, checksum, typed/short-buffer behavior. */
    have_eeprom=1;
    uint32_t language=6,checksum=~language;
    memcpy(eeprom+0x90,&language,4); memcpy(eeprom+0x60,&checksum,4);
    uint32_t query[]={7,0x19100,0x19104,4,0x19108};
    call_kernel(24,query,5);
    CHECK(g_eax==0 && MEM32(0x19100)==4 && MEM32(0x19104)==6 && MEM32(0x19108)==4);
    query[3]=3; MEM32(0x19104)=0xaabbccdd;
    call_kernel(24,query,5); CHECK(g_eax==0xc0000023u && MEM32(0x19104)==0xaabbccdd);
    query[3]=4; eeprom[0x90]^=1;
    call_kernel(24,query,5); CHECK(g_eax==0xc000009cu && MEM32(0x19104)==0xaabbccdd);
    fprintf(stderr,"PASS: isolated threads/TLS/registers, recursive exclusion, handles, EEPROM ABI\n");
    return 0;
}
