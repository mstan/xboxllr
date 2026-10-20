/* Actual asynchronous file data, host alertable wake and generated-native
 * callback transport. No replacement completion/clock provider. */
static uint32_t file_apc_test_iosb,file_apc_test_mode,file_apc_test_thread;
static LONG file_apc_test_callbacks;
static void file_apc_test_kernel(unsigned ordinal,const uint32_t *args,unsigned count);
static void test_file_apc_callback(void)
{
    CHECK(current_thread_slot && GetCurrentThreadId()==file_apc_test_thread && !dispatch_in_dpc);
    CHECK(!scheduler_enabled || scheduler_owner==scheduler_slot+1);
    uint32_t first=MEM32(g_esp+4),second=MEM32(g_esp+8),third=MEM32(g_esp+12);
    if(file_apc_test_mode==1)CHECK(first==0 && second==8 && third==file_apc_test_iosb);
    else CHECK(first==0x9abcdef0 && second==file_apc_test_iosb && !third);
    const unsigned char expected[]={3,4,5,6,7,8,9,0xff};
    CHECK(!MEM32(file_apc_test_iosb) && MEM32(file_apc_test_iosb+4)==8);
    CHECK(!memcmp(memory+file_apc_test_iosb+32,expected,sizeof(expected)));
    CHECK(InterlockedIncrement(&file_apc_test_callbacks)==1);
    if(file_apc_test_mode==3) {
        uint32_t status=0;file_apc_test_kernel(258,&status,1);CHECK(0);
    }
    g_eax=0xaabbccdd;g_ecx=0x11223344;g_edx=0x55667788;
    g_fp_stack[0]=987.0;g_fp_top=4;g_fp_cmp=3;g_fp_control_word=0x0c7f;g_fp_cc=0x4500;g_df=1;
    memset(&g_xmm0,0xad,sizeof(g_xmm0));memset(&g_mm0,0xbc,sizeof(g_mm0));
    CHECK(!fesetround(FE_DOWNWARD));g_esp+=16;
}
static void file_apc_test_kernel(unsigned ordinal,const uint32_t *args,unsigned count)
{
    uint32_t top=g_esp;g_esp-=4+count*4;MEM32(g_esp)=0x12340;
    if(count)memcpy(memory+g_esp+4,args,count*4);next_kernel_ordinal=ordinal;
    kernel_dispatch();CHECK(g_esp==top);
}
static void test_file_apc_worker(void)
{
    uint32_t file=MEM32(g_esp+4),mode=MEM32(g_esp+8),top=g_esp;
    uint32_t iosb=top-64,offset=top-48,timeout=top-40;
    g_esp=top-512;MEM64(offset)=3;MEM64(timeout)=(uint64_t)-50000000;
    memset(memory+iosb,0xcc,48);MEM64(offset)=3;MEM64(timeout)=(uint64_t)-50000000;
    file_apc_test_iosb=iosb;file_apc_test_mode=mode;file_apc_test_thread=GetCurrentThreadId();
    InterlockedExchange(&file_apc_test_callbacks,0);
    uint32_t read[]={file,0,mode==1?0xfe0003a0u:0x15920u,mode==1?0x15920u:0x9abcdef0u,iosb,iosb+32,8,offset};
    file_apc_test_kernel(219,read,8);CHECK(!g_eax || g_eax==0x103);
    uint32_t wait[]={file,0,timeout};file_apc_test_kernel(233,wait,3);CHECK(!g_eax);
    CHECK(!file_apc_test_callbacks && !MEM32(iosb) && MEM32(iosb+4)==8);
    if(mode==2){g_eax=0;return;} /* Exit cancels a queued, undelivered APC. */
    g_ecx=0x7abcdef0;g_edx=0x12345670;g_fp_stack[0]=123.5;g_fp_top=1;g_fp_cmp=2;
    g_fp_control_word=0x027f;g_fp_cc=0x100;g_df=0;
    memset(&g_xmm0,0x5a,sizeof(g_xmm0));memset(&g_mm0,0x6b,sizeof(g_mm0));
    RecompXmm expected_xmm=g_xmm0;RecompMmx expected_mm=g_mm0;
    CHECK(!fesetround(FE_TONEAREST));wait[1]=1;
    file_apc_test_kernel(233,wait,3);
    CHECK(g_eax==0xc0 && file_apc_test_callbacks==1 && !native_apc_ready);
    CHECK(g_ecx==0x7abcdef0 && g_edx==0x12345670 && g_fp_stack[0]==123.5 && g_fp_top==1 && g_fp_cmp==2);
    CHECK(g_fp_control_word==0x027f && g_fp_cc==0x100 && !g_df && fegetround()==FE_TONEAREST);
    CHECK(!memcmp(&g_xmm0,&expected_xmm,sizeof(g_xmm0)) && !memcmp(&g_mm0,&expected_mm,sizeof(g_mm0)));
    MEM64(timeout)=0;uint32_t delay[]={1,1,timeout};file_apc_test_kernel(99,delay,3);
    CHECK(!g_eax && file_apc_test_callbacks==1);g_eax=0;
}
static void test_file_apcs(void)
{
#ifndef PILOT_SCHEDULING_LLE
    CHECK(!scheduler_enabled);scheduler_enabled=1;scheduler_publish(0,0,0);scheduler_enter(0);
    DWORD baseline=0,after=0;
    for(unsigned cycle=0;cycle<9;++cycle) {
        uint32_t out=0x1d080,iosb=0x1d090;
        uint32_t create[]={out,GENERIC_READ|SYNCHRONIZE,0x1a000,iosb,0,0,1,1,0x840};
        test_media_name("D:\\asset.bin",0);call_kernel(190,create,9);CHECK(!g_eax);uint32_t file=MEM32(out);
        uint32_t args[]={out+4,0,0x10000,0,0,file,cycle%4,0,0,0x15900};
        call_kernel(255,args,10);CHECK(!g_eax);uint32_t thread=MEM32(out+4);
        uint32_t wait[]={thread,0,0};call_kernel(233,wait,3);CHECK(!g_eax);
        CHECK(file_apc_test_callbacks==(cycle%4==2?0:1));
        call_kernel(187,&thread,1);CHECK(!g_eax);thread_reap();
        call_kernel(187,&file,1);CHECK(!g_eax);
        for(unsigned i=0;i<64;++i)CHECK(!native_apc_head[i] && !native_apc_tail[i]);
        CHECK(GetProcessHandleCount(GetCurrentProcess(),&after));
        if(!cycle)baseline=after;else CHECK(after==baseline);
    }
    scheduler_retire();scheduler_enabled=0;
    fprintf(stderr,"PASS native file APC real reads/requesting-thread/alertable-only/CPU-permit, original232 and direct arguments, once-only, FP/SIMD restore, exit cancellation and retained handle baselines\n");
#endif
}
