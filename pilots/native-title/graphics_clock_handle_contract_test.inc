/* Observe real native timer ownership in the test translation only. The
 * driver may asynchronously open unrelated handles after device creation;
 * those do not establish a timer leak or its absence. Preserve API results. */
static SRWLOCK test_clock_handle_lock=SRWLOCK_INIT;
static HANDLE test_clock_handles[64];
static unsigned test_clock_timers_created,test_clock_timers_closed,test_clock_timers_active;
static void test_clock_timer_record(HANDLE handle)
{
    if(!handle)return;
    AcquireSRWLockExclusive(&test_clock_handle_lock);
    unsigned i=0;while(i<64 && test_clock_handles[i])++i;CHECK(i<64);
    test_clock_handles[i]=handle;++test_clock_timers_created;++test_clock_timers_active;
    ReleaseSRWLockExclusive(&test_clock_handle_lock);
}
static HANDLE WINAPI test_clock_timer_ex(LPSECURITY_ATTRIBUTES attributes,LPCWSTR name,DWORD flags,DWORD access)
{
    HANDLE handle=CreateWaitableTimerExW(attributes,name,flags,access);DWORD error=GetLastError();
    test_clock_timer_record(handle);SetLastError(error);return handle;
}
static HANDLE WINAPI test_clock_timer_legacy(LPSECURITY_ATTRIBUTES attributes,BOOL manual,LPCWSTR name)
{
    HANDLE handle=CreateWaitableTimerW(attributes,manual,name);DWORD error=GetLastError();
    test_clock_timer_record(handle);SetLastError(error);return handle;
}
static BOOL WINAPI test_clock_handle_close(HANDLE handle)
{
    AcquireSRWLockExclusive(&test_clock_handle_lock);
    BOOL result=CloseHandle(handle);DWORD error=GetLastError();
    if(result)for(unsigned i=0;i<64;++i)if(test_clock_handles[i]==handle) {
        test_clock_handles[i]=NULL;CHECK(test_clock_timers_active);
        --test_clock_timers_active;++test_clock_timers_closed;break;
    }
    ReleaseSRWLockExclusive(&test_clock_handle_lock);SetLastError(error);return result;
}
static void test_clock_handle_snapshot(unsigned counts[3])
{
    AcquireSRWLockShared(&test_clock_handle_lock);
    counts[0]=test_clock_timers_created;counts[1]=test_clock_timers_closed;counts[2]=test_clock_timers_active;
    ReleaseSRWLockShared(&test_clock_handle_lock);
}
