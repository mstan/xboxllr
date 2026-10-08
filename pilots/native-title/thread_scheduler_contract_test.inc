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
    fprintf(stderr,"PASS: guest CPU priority exclusion beyond donor timeout, immediate resume readiness, suspend-before-wake, blocking handoff, exclusive interrupts and equal-priority progress\n");
}
