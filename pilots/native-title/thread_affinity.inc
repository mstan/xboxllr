/* Phobos's RECOMP_GUEST_ONE_CPU is the behavioral reference: serialize guest
 * execution on one host CPU while native renderer/audio workers stay free.
 * Independently authored diagnostic option, selected once at startup. This
 * does not establish exact Xbox priority/preemption or interrupt ordering. */
static GROUP_AFFINITY guest_cpu_affinity;
static int guest_cpu_enabled;
static void thread_guest_cpu_configure(void)
{
    const char *value=getenv("XLLE_PILOT_GUEST_CPU");if(!value || !*value)return;
    char *end;unsigned long cpu=strtoul(value,&end,10);DWORD_PTR allowed,system;
    if(*end || cpu>=sizeof(DWORD_PTR)*8 || !GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system) ||
       !(allowed&((DWORD_PTR)1<<cpu)) || !GetThreadGroupAffinity(GetCurrentThread(),&guest_cpu_affinity))
        stop("invalid/unavailable guest CPU selection",(uint32_t)cpu);
    guest_cpu_affinity.Mask=(DWORD_PTR)1<<cpu;guest_cpu_enabled=1;
    fprintf(stderr,"[GUEST CPU] group=%u processor=%lu; native workers remain unrestricted\n",guest_cpu_affinity.Group,cpu);
}
static void thread_guest_cpu_enter(void)
{
    if(guest_cpu_enabled && !SetThreadGroupAffinity(GetCurrentThread(),&guest_cpu_affinity,NULL))
        stop("guest CPU affinity failed",GetLastError());
}
