/* Actual NT waits and owned worker lifetimes; no fabricated completion flags. */
static void native_wait_call(unsigned ordinal,const uint32_t *args,unsigned count)
{
    g_ebx=0xabc001;g_esi=0xabc002;g_edi=0xabc003;g_ebp=0xabc004;
    call_kernel(ordinal,args,count);
    CHECK(g_ebx==0xabc001 && g_esi==0xabc002 && g_edi==0xabc003 && g_ebp==0xabc004);
}
static volatile LONG delay_peer_ran;
static DWORD WINAPI native_delay_peer(void *unused)
{
    (void)unused;scheduler_enter(1);InterlockedIncrement(&delay_peer_ran);scheduler_retire();return 0;
}
static void test_native_delay(void)
{
    const uint32_t interval=0x1a000;
    uint32_t args[]={0xabcd0001u,0x12340000u,interval};MEM64(interval)=0;
    native_wait_call(99,args,3);CHECK(!g_eax && !MEM64(interval));
    args[0]=0xaaaa0000u;native_wait_call(99,args,3);CHECK(!g_eax);
    args[0]=1;MEM64(interval)=(uint64_t)-300000;
    DWORD old;CHECK(VirtualProtect(memory+interval,4096,PAGE_READONLY,&old));
    scheduler_enabled=1;scheduler_publish(0,0,7);scheduler_enter(0);scheduler_publish(1,0,0);
    HANDLE peer=CreateThread(NULL,0,native_delay_peer,NULL,0,NULL);CHECK(peer);
    uint64_t start=dispatch_monotonic();native_wait_call(99,args,3);
    uint64_t elapsed=dispatch_monotonic()-start;
    /* Same explicit 1ms native-expiration tolerance as the event wait contract.
     * No upper bound assumes an unloaded host; completion is the real NT wait. */
    CHECK(!g_eax && elapsed>=290000 && MEM64(interval)==(uint64_t)-300000);
    CHECK(delay_peer_ran==1 && scheduler_owner==1 && !scheduled[0].blocked);
    scheduler_retire();scheduler_enabled=0;
    CHECK(WaitForSingleObject(peer,5000)==WAIT_OBJECT_0 && CloseHandle(peer));
    CHECK(VirtualProtect(memory+interval,4096,old,&old));
    CHECK(scheduler_kernel_blocks(99) && scheduler_kernel_blocks(230));
    fprintf(stderr,"PASS native delay RET12/byte arguments/read-only interval, real30ms wait and lower-priority guest progress while parked; elapsed100ns=%llu\n",(unsigned long long)elapsed);
}
static uint32_t native_wait_event(unsigned state)
{
    uint32_t args[]={0x19400,0,1,state};native_wait_call(189,args,4);CHECK(!g_eax);return MEM32(args[0]);
}
static void native_wait_state(uint32_t event,unsigned state)
{
    uint32_t args[]={event,0x19408};native_wait_call(209,args,2);
    CHECK(!g_eax && MEM32(args[1])==1 && MEM32(args[1]+4)==state);
}
static void test_native_signal_wait_polls(void)
{
    const uint32_t timeout=0x19420;MEM64(timeout)=0;
    DWORD before,after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    for(unsigned kind=0;kind<5;++kind) {
        uint32_t first=native_wait_event(0),second=native_wait_event(kind==1 || kind==4);
        uint32_t args[]={first,second,0x12340001u,0xabcd0000u,timeout};
        uint32_t expected=kind==0?0x102:kind==1 || kind==2?0:0xc0000008u;
        if(kind==2)args[1]=first;
        if(kind==3)args[1]=0x300fffffu;
        if(kind==4)args[0]=0x300fffffu;
        native_wait_call(230,args,5);CHECK(g_eax==expected);
        native_wait_state(first,kind<2?1:0);
        native_wait_state(second,kind==4?1:0);
        native_wait_call(187,&first,1);CHECK(!g_eax);
        native_wait_call(187,&second,1);CHECK(!g_eax);
    }
    CHECK(!wait_handles && GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
}
static void test_native_worker_waits(void)
{
    test_native_signal_wait_polls();
    uint32_t id=0,status;HANDLE host=NULL;
    struct PilotThread *thread=lifetime_create(0,0,&id,&host);
    lifetime_join(host,0);native_wait_call(187,&id,1);CHECK(!g_eax);thread_reap();
    DWORD before,after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    uint64_t committed=vm_committed(0x60000000u,0x4000000u);
    uint32_t event=native_wait_event(0),timeout=0x19420,array=0x19430;
    lifetime_gate=event_duplicate(event,&status);CHECK(lifetime_gate && !status);
    lifetime_marked=CreateEventW(NULL,FALSE,FALSE,NULL);CHECK(lifetime_marked);
    thread=lifetime_create(100,1,&id,&host);
    CHECK(WaitForSingleObject(lifetime_marked,5000)==WAIT_OBJECT_0 && thread->finished);
    CHECK(WaitForSingleObject(host,0)==WAIT_TIMEOUT);
    uint32_t single[]={id,0,timeout},extended[]={id,0x12340001u,0x12340000u,timeout};
    MEM64(timeout)=0;native_wait_call(233,single,3);CHECK(g_eax==0x102);
    native_wait_call(234,extended,4);CHECK(g_eax==0x102);
    MEM32(array)=id;MEM32(array+4)=event;
    uint32_t multiple[]={2,array,1,1,0,timeout};native_wait_call(235,multiple,6);CHECK(g_eax==0x102);
    HANDLE retained=thread_wait_duplicate(id,&status);CHECK(retained && !status);
    uint32_t signal_wait[]={event,id,1,0,0};native_wait_call(230,signal_wait,5);CHECK(!g_eax);
    lifetime_join(retained,100);
    /* A completed but still-open real thread is a valid WaitAll participant. */
    uint32_t set[]={event,0};native_wait_call(225,set,2);CHECK(!g_eax);
    multiple[2]=0;native_wait_call(235,multiple,6);CHECK(!g_eax);
    native_wait_state(event,0);
    uint32_t old_id=id;native_wait_call(187,&id,1);CHECK(!g_eax);thread_reap();CHECK(!thread->object);
    CHECK(WaitForSingleObject(retained,0)==WAIT_OBJECT_0 && CloseHandle(retained));
    thread=lifetime_create(101,1,&id,&host);CHECK(id!=old_id);
    CHECK(WaitForSingleObject(lifetime_marked,5000)==WAIT_OBJECT_0);
    /* The previous identity cannot become the newly reused slot's handle. */
    single[0]=old_id;native_wait_call(233,single,3);CHECK(g_eax==0xc0000008u);
    extended[0]=old_id;native_wait_call(234,extended,4);CHECK(g_eax==0xc0000008u);
    /* A late invalid thread must leave the earlier event unconsumed. */
    uint32_t probe_event=native_wait_event(1);
    MEM32(array)=probe_event;MEM32(array+4)=old_id;multiple[2]=1;
    native_wait_call(235,multiple,6);CHECK(g_eax==0xc0000008u);
    native_wait_state(probe_event,1);
    native_wait_call(187,&probe_event,1);CHECK(!g_eax);
    retained=thread_wait_duplicate(id,&status);CHECK(retained && !status);
    native_wait_call(187,&id,1);CHECK(!g_eax);thread_reap();
    CHECK(thread->object && WaitForSingleObject(retained,0)==WAIT_TIMEOUT);
    CHECK(!thread_wait_duplicate(id,&status) && status==0xc0000008u);
    CHECK(SetEvent(lifetime_gate));lifetime_join(retained,101);thread_reap();CHECK(!thread->object);
    CHECK(WaitForSingleObject(retained,0)==WAIT_OBJECT_0 && CloseHandle(retained));
    CHECK(CloseHandle(lifetime_gate) && CloseHandle(lifetime_marked));
    native_wait_call(187,&event,1);CHECK(!g_eax && !wait_handles);
    CHECK(vm_committed(0x60000000u,0x4000000u)==committed);
    CHECK(GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    fprintf(stderr,"PASS native230 original five poll outcomes/RET20, real worker paired signal/exit,233/234/235 thread waits, nonvolatile registers, stale/reused identities, close/reap-surviving references and exact handle/VM baselines\n");
}
static void test_native_wait_guard(const char *name)
{
    uint32_t args[]={1,0,0x19420,0,0};MEM64(args[2])=(uint64_t)-10000;
    if(!strcmp(name,"native-wait-dpc99"))dispatch_in_dpc=1;
    else if(!strcmp(name,"native-wait-dpc230")) {
        dispatch_in_dpc=1;call_kernel(230,args,5);CHECK(0);
    } else if(!strcmp(name,"native-wait-thread-dpc")) {
        uint32_t status;dispatch_in_dpc=1;(void)thread_wait_duplicate(0x40000001u,&status);CHECK(0);
    } else if(!strcmp(name,"native-wait-delay-alertable"))args[1]=1;
    else if(!strcmp(name,"native-wait-delay-absolute"))MEM64(args[2])=1;
    else if(!strcmp(name,"native-wait-delay-range"))args[2]=0xfffffffcu;
    else if(!strcmp(name,"native-wait-signal-blocking")) {
        args[0]=native_wait_event(0);args[1]=native_wait_event(0);args[2]=1;
        call_kernel(230,args,5);CHECK(0);
    }
    else if(!strcmp(name,"native-wait-signal-mutant")) {
        uint32_t create[]={0x19400,0,0};call_kernel(192,create,3);CHECK(!g_eax);
        args[0]=MEM32(create[0]);args[1]=0x300fffffu;args[2]=1;
        args[4]=0x19420;MEM64(args[4])=0;
        call_kernel(230,args,5);CHECK(0);
    } else CHECK(!strcmp(name,"native-wait-dpc99"));
    call_kernel(99,args,3);CHECK(0);
}
