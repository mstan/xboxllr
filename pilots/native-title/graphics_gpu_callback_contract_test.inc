static HANDLE gpu_test_entered,gpu_test_release;
static LONG gpu_test_count;
static DWORD gpu_test_caller;
static uint64_t gpu_test_serial;
static void test_gpu_callback(void)
{
    CHECK(dispatch_in_dpc && MEM8(g_fs_base+0x24)==2 && GetCurrentThreadId()!=gpu_test_caller);
    uint32_t context=MEM32(g_esp+4);CHECK(context==(uint32_t)InterlockedIncrement(&gpu_test_count));
    if(context==1){
        AcquireSRWLockExclusive(&graphics_lock);
        CHECK(!pilot_gfx_checkpoint_busy(gpu_test_serial));
        unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        CHECK(pixels[0]==255 && pixels[1]==0 && pixels[2]==0);
        ReleaseSRWLockExclusive(&graphics_lock);
        CHECK(SetEvent(gpu_test_entered));CHECK(WaitForSingleObject(gpu_test_release,2000)==WAIT_OBJECT_0);
    }
    /* A real guest reentrant query must neither deadlock nor clobber caller TLS. */
    uint32_t saved=g_esp;g_esp-=8;MEM32(g_esp)=0;MEM32(g_esp+4)=0x19700;
    pilot_d3d_dispatch(PILOT_D3D_D3DDevice_GetDisplayMode);CHECK(g_esp==saved);
    g_eax=0xbadc0de;g_ebx=0x11223344;g_esp+=4;
}
static void test_graphics_gpu_callbacks(void)
{
    gpu_test_count=0;gpu_test_caller=GetCurrentThreadId();
    gpu_test_entered=CreateEventW(NULL,TRUE,FALSE,NULL);gpu_test_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(gpu_test_entered && gpu_test_release);
    uint32_t clear[]={0,0,0xf1,0xffff0000,0x3f800000,0};
    CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
    AcquireSRWLockExclusive(&graphics_lock);gpu_test_serial=pilot_gfx_checkpoint();ReleaseSRWLockExclusive(&graphics_lock);
    uint32_t args[]={1,0x12360,1};g_eax=0x1234567;
    call_graphics(PILOT_D3D_D3DDevice_InsertCallback,args);
    CHECK(g_eax==0x1234567 && WaitForSingleObject(gpu_test_entered,2000)==WAIT_OBJECT_0);
    for(unsigned i=2;i<=70;++i){args[0]=i&1;args[2]=i;call_graphics(PILOT_D3D_D3DDevice_InsertCallback,args);}
    CHECK(gpu_test_count==1);CHECK(SetEvent(gpu_test_release));
    clear[3]=0xff0000ff;CHECK(call_graphics(PILOT_D3D_D3DDevice_Clear,clear)==0);
    CHECK(gpu_test_count==70 && g_ebx==0x12345678);
    AcquireSRWLockExclusive(&graphics_lock);
    CHECK(graphics_gpu_delivered==70 && !graphics_gpu_head && !graphics_gpu_tail && !graphics_gpu_running);
    ReleaseSRWLockExclusive(&graphics_lock);dispatch_gpu_quiesce();
    CHECK(CloseHandle(gpu_test_entered) && CloseHandle(gpu_test_release));
    fprintf(stderr,"PASS: 70 FIFO GPU callbacks, real WRITE completion/pixels, async delivery without Swap, READ, cdecl context/TLS, reentrant queries and ordering barrier\n");
}
