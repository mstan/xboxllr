/* Native console-only NtSetSystemTime. The documented two-pointer ABI and
 * accepted high-part bounds were cross-checked against the pinned Cxbx API
 * implementation; this independent implementation never changes host time. */
static void system_time_set_kernel(void)
{
    uint32_t input=MEM32(g_esp+4),output=MEM32(g_esp+8);
    g_eax=0xc0000005u;
    if(wait_range(input,8,0) && (!output || wait_range(output,8,1))) {
        uint64_t requested;memcpy(&requested,memory+input,8);
        uint32_t high=(uint32_t)(requested>>32);
        g_eax=0xc000000du;
        if(high && high<=0x20000000u) {
            uint64_t previous=dispatch_set_wall_time(requested);
            if(output)memcpy(memory+output,&previous,8);
            g_eax=0;
        }
    }
    g_esp+=12;
}
