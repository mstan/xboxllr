/* Real host workers, guest object references and cooperative suspend counts. */
static volatile LONG scheduler_counter,scheduler_exit,scheduler_stage;
static void test_scheduler_worker(void)
{
    uint32_t mode=MEM32(g_esp+4);
    if(mode) {
        InterlockedExchange(&scheduler_stage,1);
        uint32_t sp=g_esp;g_esp-=12;
        MEM32(g_esp)=0;MEM32(g_esp+4)=0xfffffffeu;MEM32(g_esp+8)=0;
        thread_object_kernel(231);
        CHECK(g_esp==sp && !g_eax);
        InterlockedExchange(&scheduler_stage,2);
    }
    while(!InterlockedCompareExchange(&scheduler_exit,0,0)) {
        PILOT_THREAD_CHECKPOINT();
        InterlockedIncrement(&scheduler_counter);
    }
}
static void scheduler_progress(void)
{
    LONG before=InterlockedCompareExchange(&scheduler_counter,0,0);
    ULONGLONG deadline=GetTickCount64()+2000;
    while(InterlockedCompareExchange(&scheduler_counter,0,0)==before && GetTickCount64()<deadline)Sleep(1);
    CHECK(InterlockedCompareExchange(&scheduler_counter,0,0)!=before);
}
static void scheduler_stopped(void)
{
    /* Allow the in-flight guest basic block to reach its safe point. */
    ULONGLONG deadline=GetTickCount64()+2000;
    for(;;) {
        LONG before=InterlockedCompareExchange(&scheduler_counter,0,0);
        Sleep(30);
        if(InterlockedCompareExchange(&scheduler_counter,0,0)==before)return;
        CHECK(GetTickCount64()<deadline);
    }
}
static void test_thread_objects(void)
{
    GROUP_AFFINITY caller_affinity;
    CHECK(GetThreadGroupAffinity(GetCurrentThread(),&caller_affinity));
    guest_cpu_affinity=caller_affinity;
    guest_cpu_affinity.Mask&=(~guest_cpu_affinity.Mask+1);guest_cpu_enabled=1;
    uint32_t args[]={0x19000,0,0x10000,20,0x19004,0,0,1,0,0x12200};
    call_kernel(255,args,10);CHECK(!g_eax);
    uint32_t handle=MEM32(0x19000),out=0x19008;
    HANDLE host=threads[1].handle;
    Sleep(20);CHECK(!scheduler_counter && WaitForSingleObject(host,0)==WAIT_TIMEOUT);
    GROUP_AFFINITY worker_affinity;
    CHECK(GetThreadGroupAffinity(host,&worker_affinity) && worker_affinity.Mask==guest_cpu_affinity.Mask &&
          worker_affinity.Group==guest_cpu_affinity.Group);
    uint32_t ref[]={handle,PILOT_THREAD_OBJECT_TYPE,out};
    MEM32(out)=0xfeedface;ref[1]++;
    call_kernel(246,ref,3);CHECK(g_eax==0xc0000024u && MEM32(out)==0xfeedface);
    ref[1]=PILOT_THREAD_OBJECT_TYPE;call_kernel(246,ref,3);CHECK(!g_eax);
    uint32_t object=MEM32(out);CHECK(object==threads[1].object && threads[1].object_refs==1);
    call_kernel(246,ref,3);CHECK(!g_eax && threads[1].object_refs==2);
    uint32_t control[]={object,2};call_kernel(143,control,2);CHECK(!g_eax);
    CHECK(GetThreadPriority(host)==THREAD_PRIORITY_HIGHEST);
    call_kernel(124,control,1);CHECK(g_eax==2);
    control[1]=1;call_kernel(144,control,2);CHECK(!g_eax);
    BOOL boost;CHECK(GetThreadPriorityBoost(host,&boost) && boost);
    control[1]=0;call_kernel(144,control,2);CHECK(g_eax==1);
    call_kernel(143,control,2);CHECK(g_eax==2 && GetThreadPriority(host)==THREAD_PRIORITY_NORMAL);
    control[1]=32;call_kernel(143,control,2);CHECK(!g_eax);
    call_kernel(124,control,1);CHECK(g_eax==16);
    control[1]=(uint32_t)-32;call_kernel(143,control,2);CHECK(g_eax==16);
    call_kernel(124,control,1);CHECK(g_eax==(uint32_t)-16);
    control[1]=0;call_kernel(143,control,2);CHECK(g_eax==(uint32_t)-16);
    control[0]=handle;control[1]=out;
    call_kernel(231,control,2);CHECK(!g_eax && MEM32(out)==1);
    call_kernel(224,control,2);CHECK(!g_eax && MEM32(out)==2);
    Sleep(20);CHECK(!scheduler_counter);
    call_kernel(224,control,2);CHECK(!g_eax && MEM32(out)==1);scheduler_progress();
    call_kernel(224,control,2);CHECK(!g_eax && !MEM32(out));
    for(unsigned i=0;i<12;++i) {
        call_kernel(231,control,2);CHECK(!g_eax && !MEM32(out));scheduler_stopped();
        call_kernel(224,control,2);CHECK(!g_eax && MEM32(out)==1);scheduler_progress();
    }
    call_kernel(231,control,2);CHECK(!g_eax);scheduler_stopped();
    for(unsigned i=1;i<127;++i){call_kernel(231,control,2);CHECK(!g_eax && MEM32(out)==i);}
    MEM32(out)=0xfeedface;call_kernel(231,control,2);CHECK(g_eax==0xc000004au && MEM32(out)==0xfeedface);
    for(unsigned i=127;i>0;--i){call_kernel(224,control,2);CHECK(!g_eax && MEM32(out)==i);}
    scheduler_progress();
    /* Closing the guest handle leaves the independently retained object valid. */
    call_kernel(187,control,1);CHECK(!g_eax && !threads[1].handle);
    HANDLE retained=threads[1].reference_handle;CHECK(retained!=NULL);
    uint32_t query[]={object};call_kernel(124,query,1);CHECK(!g_eax);
    MEM32(out)=0xfeedface;call_kernel(224,control,2);CHECK(g_eax==0xc0000008u && MEM32(out)==0xfeedface);
    InterlockedExchange(&scheduler_exit,1);
    CHECK(WaitForSingleObject(retained,2000)==WAIT_OBJECT_0);
    g_ecx=object;call_kernel(250,NULL,0);CHECK(threads[1].object_refs==1);
    g_ecx=object;call_kernel(250,NULL,0);CHECK(!threads[1].object_refs && !threads[1].reference_handle);

    /* Self-suspend must release the ownership lock before a peer resumes it. */
    InterlockedExchange(&scheduler_exit,0);InterlockedExchange(&scheduler_counter,0);
    args[5]=1;args[7]=0;call_kernel(255,args,10);CHECK(!g_eax);
    control[0]=MEM32(0x19000);host=threads[2].handle;
    ULONGLONG deadline=GetTickCount64()+2000;
    for(;;) {
        AcquireSRWLockShared(&thread_lock);uint32_t suspended=threads[2].suspend_count;ReleaseSRWLockShared(&thread_lock);
        if(suspended)break;
        CHECK(GetTickCount64()<deadline);Sleep(1);
    }
    CHECK(scheduler_stage==1 && !scheduler_counter);
    call_kernel(224,control,2);CHECK(!g_eax && MEM32(out)==1);scheduler_progress();CHECK(scheduler_stage==2);
    InterlockedExchange(&scheduler_exit,1);CHECK(WaitForSingleObject(host,2000)==WAIT_OBJECT_0);
    MEM32(out)=0xfeedface;call_kernel(231,control,2);CHECK(g_eax==0xc000004bu && MEM32(out)==0xfeedface);
    call_kernel(187,control,1);CHECK(!g_eax);
    CHECK(GetThreadGroupAffinity(GetCurrentThread(),&worker_affinity) && worker_affinity.Mask==caller_affinity.Mask &&
          worker_affinity.Group==caller_affinity.Group);guest_cpu_enabled=0;
    fprintf(stderr,"PASS: real workers, guest-only CPU selection, initial/nested/self suspension, loop progress, count limit, priorities/saturation/boost, object references surviving handle close, error outputs\n");
}
