static void test_event_objects(void)
{
    const uint32_t out=0x19200,info=0x19210,timeout=0x19220;
    DWORD before,after;CHECK(GetProcessHandleCount(GetCurrentProcess(),&before));
    for(unsigned type=0;type<2;++type) {
        uint32_t create[]={out,0,type,0};call_kernel(189,create,4);CHECK(!g_eax);
        uint32_t id=MEM32(out),query[]={id,info},signal[]={id,info+8},wait[]={id,0,timeout};
        MEM64(timeout)=0;call_kernel(233,wait,3);CHECK(g_eax==0x102);
        call_kernel(209,query,2);CHECK(!g_eax && MEM32(info)==type && !MEM32(info+4));
        call_kernel(225,signal,2);CHECK(!g_eax && !MEM32(info+8));
        call_kernel(225,signal,2);CHECK(!g_eax && MEM32(info+8)==1);
        call_kernel(233,wait,3);CHECK(!g_eax);
        call_kernel(233,wait,3);CHECK(g_eax==(type?0x102:0));
        call_kernel(186,&id,1);CHECK(!g_eax);
        uint32_t wait_ex[]={id,0,0,timeout};MEM64(timeout)=(uint64_t)-100000;
        uint64_t begin=dispatch_monotonic();call_kernel(234,wait_ex,4);
        uint64_t elapsed=dispatch_monotonic()-begin;
        fprintf(stderr,"[EVENT WAIT] type=%u status=%08x requested_100ns=100000 elapsed_100ns=%llu\n",type,g_eax,(unsigned long long)elapsed);
        /* Explicit native timing tolerance: at most one Xbox-sized tick early.
         * Native NT early expiration was independently measured (.99). The
         * original IRQ0 expiry fixture establishes 1 ms resolution, not that
         * the Xbox actually returns early. See SCHEDULING-LLE.md. */
        CHECK(g_eax==0x102 && elapsed>=90000);
        /* An outstanding operation holds the real object even after the guest
         * handle is closed; a stale handle can never alias the next create. */
        uint32_t status;HANDLE retained=event_duplicate(id,&status);CHECK(retained && !status);
        call_kernel(187,&id,1);CHECK(!g_eax && SetEvent(retained));
        CHECK(WaitForSingleObject(retained,0)==WAIT_OBJECT_0 && CloseHandle(retained));
        call_kernel(209,query,2);CHECK(g_eax==0xc0000008u);
        call_kernel(187,&id,1);CHECK(g_eax==0xc0000008u);
    }
    CHECK(!event_handles && GetProcessHandleCount(GetCurrentProcess(),&after) && before==after);
    fprintf(stderr,"PASS: actual native event objects, manual/auto reset, previous state, query, NT deadlines, retained object lifetime and no handle growth\n");
}
