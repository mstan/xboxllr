/* Real competing host workers exercise CPU ordering and ownership, including
 * a runnable worker intentionally delayed before it reaches its entry point. */
static HANDLE scheduling_gate,scheduling_release,scheduling_high_done,scheduling_low_done;
static volatile LONG scheduling_count,scheduling_interrupts;
static unsigned scheduling_order[4];
static DWORD WINAPI scheduling_worker(void *argument)
{
    unsigned slot=(unsigned)(uintptr_t)argument;
    if(slot==1)CHECK(WaitForSingleObject(scheduling_gate,2000)==WAIT_OBJECT_0);
    scheduler_enter(slot);
    unsigned n=(unsigned)InterlockedIncrement(&scheduling_count)-1;
    CHECK(n<4);scheduling_order[n]=slot;
    if(slot==1) {
        int blocked=scheduler_block_begin();CHECK(blocked);
        CHECK(WaitForSingleObject(scheduling_release,2000)==WAIT_OBJECT_0);
        scheduler_block_end(blocked);
        n=(unsigned)InterlockedIncrement(&scheduling_count)-1;
        CHECK(n<4);scheduling_order[n]=slot;
    }
    scheduler_retire();SetEvent(slot==1?scheduling_high_done:scheduling_low_done);return 0;
}
static DWORD WINAPI scheduling_interrupt(void *unused)
{
    (void)unused;scheduler_callback_enter();
    CHECK(scheduler_owner==SCHEDULER_CALLBACK_SLOT+1);
    InterlockedIncrement(&scheduling_interrupts);scheduler_retire();return 0;
}
static HANDLE poll_started,poll_lower_done,poll_interrupt_done;
static volatile LONG poll_counter,poll_lower_ran,poll_entered;
static DWORD WINAPI poll_lower(void *unused)
{
    (void)unused;scheduler_enter(1);InterlockedIncrement(&poll_lower_ran);
    scheduler_retire();SetEvent(poll_lower_done);return 0;
}
static DWORD WINAPI poll_interrupt(void *unused)
{
    (void)unused;scheduler_callback_enter();
    CHECK(scheduler_owner==SCHEDULER_CALLBACK_SLOT+1);
    InterlockedIncrement(&poll_counter);scheduler_retire();SetEvent(poll_interrupt_done);return 0;
}
static DWORD WINAPI poll_controller(void *argument)
{
    CHECK(WaitForSingleObject(poll_started,2000)==WAIT_OBJECT_0);
    Sleep(25);CHECK(poll_entered>0 && poll_counter==0 && poll_lower_ran==0);
    if(argument) {
        /* Suspension must wake the parked owner and reach its checkpoint;
         * only then may the otherwise lower-priority guest run. */
        scheduler_control(0,1,7);
        CHECK(WaitForSingleObject(poll_lower_done,2000)==WAIT_OBJECT_0);
        scheduler_control(0,0,7);
    }
    HANDLE interrupt=CreateThread(NULL,0,poll_interrupt,NULL,0,NULL);CHECK(interrupt);
    CHECK(WaitForSingleObject(interrupt,2000)==WAIT_OBJECT_0);CloseHandle(interrupt);return 0;
}
static void test_scheduler_poll_wait(void)
{
#if !defined(PILOT_SCHEDULING_LLE) && !defined(PILOT_USB_LLE)
    scheduler_enabled=0;pilot_native_poll_wait(); /* portable semantic fallback */
    for(unsigned suspend=0;suspend<2;++suspend) {
        poll_counter=poll_lower_ran=poll_entered=0;
        poll_started=CreateEventW(NULL,TRUE,FALSE,NULL);
        poll_lower_done=CreateEventW(NULL,TRUE,FALSE,NULL);
        poll_interrupt_done=CreateEventW(NULL,TRUE,FALSE,NULL);
        CHECK(poll_started && poll_lower_done && poll_interrupt_done);
        scheduler_enabled=1;scheduler_publish(0,0,7);scheduler_enter(0);
        scheduler_publish(1,0,0);
        HANDLE lower=CreateThread(NULL,0,poll_lower,NULL,0,NULL);CHECK(lower);
        HANDLE controller=CreateThread(NULL,0,poll_controller,(void*)(uintptr_t)suspend,0,NULL);CHECK(controller);
        while(!poll_counter) {
            scheduler_checkpoint();
            if(!poll_counter) {
                CHECK(InterlockedIncrement(&poll_entered)<1000);
                SetEvent(poll_started);pilot_native_poll_wait();
            }
        }
        CHECK(poll_counter==1 && poll_lower_ran==(LONG)suspend);
        CHECK(scheduler_owner==1 && !scheduled[0].blocked && !scheduled[0].suspended);
        CHECK(WaitForSingleObject(poll_interrupt_done,2000)==WAIT_OBJECT_0);
        CHECK(WaitForSingleObject(controller,2000)==WAIT_OBJECT_0);
        /* Equal-priority readiness must remain on the existing checkpoint
         * path rather than sleep forever on an already-published request. */
        if(!suspend){scheduler_control(1,0,7);pilot_native_poll_wait();}
        scheduler_retire();
        CHECK(WaitForSingleObject(lower,2000)==WAIT_OBJECT_0 && poll_lower_ran==1);
        CloseHandle(lower);CloseHandle(controller);CloseHandle(poll_started);
        CloseHandle(poll_lower_done);CloseHandle(poll_interrupt_done);scheduler_enabled=0;
    }
    fprintf(stderr,"PASS: native poll parks host, retains guest readiness/priority, observes callback publication and suspension, and bypasses pending equal-priority requests\n");
#endif
}
static void test_guest_scheduler(void)
{
    scheduler_enabled=1;scheduler_publish(0,0,16);scheduler_enter(0);
    CHECK(scheduled[0].priority==15 && scheduler_effective_priority(7)==15 && scheduler_effective_priority(-16)==1);
    scheduling_gate=CreateEventW(NULL,TRUE,FALSE,NULL);
    scheduling_release=CreateEventW(NULL,TRUE,FALSE,NULL);
    scheduling_high_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    scheduling_low_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    CHECK(scheduling_gate && scheduling_release && scheduling_high_done && scheduling_low_done);
    scheduler_publish(1,1,2);scheduler_publish(2,0,1);
    HANDLE high=CreateThread(NULL,0,scheduling_worker,(void*)(uintptr_t)1,0,NULL);
    HANDLE low=CreateThread(NULL,0,scheduling_worker,(void*)(uintptr_t)2,0,NULL);CHECK(high && low);
    /* A nonblocking resume immediately makes the spinner eligible. A second
     * suspend/resume before the host wakes must not leave a phantom waiter. */
    scheduler_control(1,0,2);scheduler_control(1,1,2);scheduler_control(1,0,2);
    ULONGLONG end=GetTickCount64()+550;
    while(GetTickCount64()<end){scheduler_checkpoint();Sleep(1);}
    CHECK(!scheduling_count); /* no starvation escape through priority 15 */
    int blocked=scheduler_block_begin();CHECK(blocked);
    CHECK(!scheduler_block_begin()); /* a nested native wait cannot restore twice */
    Sleep(30);CHECK(!scheduling_count); /* lower cannot beat the delayed spinner */
    SetEvent(scheduling_gate);
    CHECK(WaitForSingleObject(scheduling_low_done,2000)==WAIT_OBJECT_0);
    CHECK(scheduling_count==2 && scheduling_order[0]==1 && scheduling_order[1]==2);
    scheduler_block_end(blocked);
    scheduler_control(1,1,2);SetEvent(scheduling_release);
    blocked=scheduler_block_begin();Sleep(30);
    CHECK(WaitForSingleObject(scheduling_high_done,0)==WAIT_TIMEOUT && scheduling_count==2);
    /* The test controller models an external resume while all guests wait. */
    scheduler_control(1,0,2);
    CHECK(WaitForSingleObject(scheduling_high_done,2000)==WAIT_OBJECT_0);
    scheduler_block_end(blocked);CHECK(scheduling_count==3 && scheduling_order[2]==1);
    /* Interrupt execution cannot silently overlap after a host timeout. */
    HANDLE interrupt=CreateThread(NULL,0,scheduling_interrupt,NULL,0,NULL);CHECK(interrupt);
    Sleep(150);CHECK(!scheduling_interrupts);
    scheduler_checkpoint();CHECK(scheduling_interrupts==1);
    CHECK(WaitForSingleObject(interrupt,2000)==WAIT_OBJECT_0);CloseHandle(interrupt);
    /* Query saturation 16 and delta 7 have the same effective priority; the
     * equal worker must get a real quantum rather than being starved forever. */
    scheduler_publish(3,0,7);ResetEvent(scheduling_low_done);
    HANDLE equal=CreateThread(NULL,0,scheduling_worker,(void*)(uintptr_t)3,0,NULL);CHECK(equal);
    ULONGLONG deadline=GetTickCount64()+2000;
    while(scheduling_count<4 && GetTickCount64()<deadline)scheduler_checkpoint();
    CHECK(scheduling_count==4 && scheduling_order[3]==3);
    CHECK(WaitForSingleObject(equal,2000)==WAIT_OBJECT_0);CloseHandle(equal);
    scheduler_retire();CHECK(!scheduler_owner);
    CHECK(WaitForSingleObject(high,2000)==WAIT_OBJECT_0 && WaitForSingleObject(low,2000)==WAIT_OBJECT_0);
    CloseHandle(high);CloseHandle(low);CloseHandle(scheduling_gate);CloseHandle(scheduling_release);
    CloseHandle(scheduling_high_done);CloseHandle(scheduling_low_done);
    scheduler_enabled=0;
    test_scheduler_poll_wait();
    fprintf(stderr,"PASS: guest CPU priority exclusion beyond donor timeout, immediate resume readiness, suspend-before-wake, blocking handoff, exclusive interrupts and equal-priority progress\n");
}
