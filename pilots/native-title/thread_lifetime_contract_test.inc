/* Source-derived ownership checks using actual host threads and, when
 * selected, real original VM reservations/PFNs. No title or device output. */
#ifdef PILOT_USB_LLE
static int test_write_faults(uint32_t va);
#endif
static HANDLE lifetime_gate,lifetime_marked;
static void test_thread_lifetime_body(void)
{
    uint32_t context=MEM32(g_esp+4),base=g_fs_base;
    CHECK(MEM32(g_esp+8)==0xdecafbad && !MEM32(base+0x1f00));
    uint32_t tls=MEM32(MEM32(base+0x28)+0x28),bytes=MEM32(context+4);
    if(bytes) {
        CHECK(tls==base+0x100000u-bytes);
        for(unsigned i=0;i<bytes;++i)CHECK(!MEM8(tls+i));
        memset(memory+tls,0x6b,bytes);
    } else CHECK(!tls);
    MEM32(base+0x1f00)=MEM32(context);
    if(MEM32(context+8)) {
        /* Hold the real host transport after its normal finished marker.
         * That marker alone must never permit storage/slot reclamation. */
        thread_finished();CHECK(SetEvent(lifetime_marked));
        CHECK(WaitForSingleObject(lifetime_gate,5000)==WAIT_OBJECT_0);
    }
    g_eax=0x73000000u+MEM32(context);
}
static struct PilotThread *lifetime_create(unsigned iteration,unsigned hold,uint32_t *handle,HANDLE *host)
{
    const uint32_t context=0x19b00u;
    MEM32(context)=iteration;MEM32(context+4)=iteration%3?20:0;MEM32(context+8)=hold;
    uint32_t args[]={0x19a00,32,iteration&1?0x20000:0x10000,MEM32(context+4),
                     0x19a04,context,0xdecafbad,0,0,0x12680};
    call_kernel(255,args,10);CHECK(!g_eax);*handle=MEM32(args[0]);
    struct PilotThread *thread=thread_from_handle(*handle,host);CHECK(thread);
    return thread;
}
static void lifetime_join(HANDLE host,unsigned iteration)
{
    CHECK(WaitForSingleObject(host,5000)==WAIT_OBJECT_0);
    DWORD status=0;CHECK(GetExitCodeThread(host,&status) && status==0x73000000u+iteration);
}
static void lifetime_stale(uint32_t handle)
{
    uint32_t out=0x19a08,control[]={handle,out};MEM32(out)=0xfeedface;
    call_kernel(224,control,2);CHECK(g_eax==0xc0000008u && MEM32(out)==0xfeedface);
    uint32_t ref[]={handle,PILOT_THREAD_OBJECT_TYPE,out};
    call_kernel(246,ref,3);CHECK(g_eax==0xc0000008u && MEM32(out)==0xfeedface);
    call_kernel(187,&handle,1);CHECK(g_eax==0xc0000008u);
}
static void test_thread_lifetimes(void)
{
    uint32_t handle=0;HANDLE host=NULL;
    /* Establish lazy native thread/engine resources before exact baselines. */
    struct PilotThread *thread=lifetime_create(0,0,&handle,&host);
    lifetime_join(host,0);call_kernel(187,&handle,1);CHECK(!g_eax);thread_reap();
    DWORD baseline=0,after=0;CHECK(GetProcessHandleCount(GetCurrentProcess(),&baseline));
    uint64_t committed=vm_committed(0x60000000u,0x4000000u);
    uint32_t previous=handle;
    for(unsigned i=1;i<=96;++i) {
        thread=lifetime_create(i,0,&handle,&host);
        CHECK(handle!=previous);
        lifetime_stale(previous);lifetime_join(host,i);
        CHECK(thread->finished && thread->handle && thread->execution_handle);
        call_kernel(187,&handle,1);CHECK(!g_eax);thread_reap();
        CHECK(!thread->object && !thread->execution_handle);
        CHECK(vm_committed(0x60000000u,0x4000000u)==committed);
        CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && after==baseline);
        previous=handle;
    }
    /* A closed handle with a live object reference cannot donate its slot. */
    thread=lifetime_create(97,0,&handle,&host);lifetime_join(host,97);
    uint32_t out=0x19a08,ref[]={handle,PILOT_THREAD_OBJECT_TYPE,out};
    call_kernel(246,ref,3);CHECK(!g_eax);uint32_t object=MEM32(out);
    HANDLE retained=thread->reference_handle;CHECK(retained);
    call_kernel(187,&handle,1);CHECK(!g_eax);thread_reap();
    CHECK(thread->object==object && thread->object_refs==1 && thread->execution_handle);
    CHECK(WaitForSingleObject(retained,0)==WAIT_OBJECT_0);
    call_kernel(124,&object,1);CHECK(!g_eax); /* finished object remains queryable */
    uint32_t peer_handle=0;HANDLE peer_host=NULL;
    struct PilotThread *peer=lifetime_create(98,0,&peer_handle,&peer_host);
    CHECK(peer!=thread);lifetime_join(peer_host,98);
    call_kernel(187,&peer_handle,1);CHECK(!g_eax);
    g_ecx=object;call_kernel(250,NULL,0);CHECK(!thread->object);thread_reap();
    lifetime_stale(handle);
    /* Real host exit, rather than the earlier finished marker, owns reuse. */
    lifetime_gate=CreateEventW(NULL,FALSE,FALSE,NULL);
    lifetime_marked=CreateEventW(NULL,FALSE,FALSE,NULL);CHECK(lifetime_gate && lifetime_marked);
    thread=lifetime_create(99,1,&handle,&host);
    CHECK(WaitForSingleObject(lifetime_marked,5000)==WAIT_OBJECT_0);
    call_kernel(187,&handle,1);CHECK(!g_eax);thread_reap();
    CHECK(thread->finished && thread->object && WaitForSingleObject(thread->execution_handle,0)==WAIT_TIMEOUT);
    /* Do not rewrite the held worker's supplied context while it still runs. */
    CHECK(SetEvent(lifetime_gate));lifetime_join(host,99);thread_reap();CHECK(!thread->object);
    CHECK(CloseHandle(lifetime_gate) && CloseHandle(lifetime_marked));
#ifdef PILOT_USB_LLE
    /* The stack registry itself also reuses lifetimes; real pinning postpones
     * release without mutating source storage or poisoning the transport. */
    const uint32_t stack=0x63ef0000u,bytes=0x10000u;
    for(unsigned i=0;i<3;++i) {
        commit(stack,bytes);MEM32(stack)=0x12345678;
        CHECK(!pilot_io_device_stack(memory,stack,bytes));
        CHECK(!pilot_io_device_resident(stack,bytes));
        uint32_t pin[]={stack,4096,0},result=0;
        CHECK(!pilot_io_device_lle(175,memory,pin,&result));
        int released=-1;
        CHECK(!pilot_io_device_stack_release(memory,stack,bytes,&released) && !released);
        CHECK(MEM32(stack)==0x12345678);
        pin[2]=1;CHECK(!pilot_io_device_lle(175,memory,pin,&result));
        CHECK(!pilot_io_device_stack_release(memory,stack,bytes,&released) && released);
        CHECK(test_write_faults(stack));
        CHECK(VirtualFree(memory+stack,bytes,MEM_DECOMMIT));
    }
#endif
    CHECK(vm_committed(0x60000000u,0x4000000u)==committed);
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && after==baseline);
    fprintf(stderr,"PASS: 96 reused native lifetimes with fresh identities, exact handle/committed-byte baselines, zeroed stack/TLS/context, retained finished objects, close-before-exit ownership and original stack pin/release\n");
}
