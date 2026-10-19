/* Include after the generated pilot_startup_bindings.h and kernel_profile.h
 * declarations. The build must explicitly select the export-only kernel mode. */
#if !defined(PILOT_STARTUP_NATIVE) || !PILOT_STARTUP_NATIVE || !defined(PILOT_NATIVE_KERNEL_EXPORT_ONLY) || !PILOT_NATIVE_KERNEL_EXPORT_ONLY
#error Native startup requires the explicit export-only kernel contract
#endif
#if defined(PILOT_IO_DEVICE_LLE) || defined(PILOT_USB_LLE) || defined(PILOT_SCHEDULING_LLE)
#error Native startup cannot own an original device or scheduler execution domain
#endif
#include "kernel_startup.h"
static PilotKernelStartup kernel_startup_owner;
static SRWLOCK kernel_startup_lock=SRWLOCK_INIT;
static void pilot_startup_ensure(void)
{
    if(!cpu_interrupt_flag || cpu_interrupt_shadow || PILOT_NATIVE_KERNEL_BUILD!=PILOT_STARTUP_KERNEL_BUILD ||
       MEM16(MEM32(PILOT_STARTUP_VERSION_IMPORT)+4)!=PILOT_NATIVE_KERNEL_BUILD ||
       MEM32(MEM32(PILOT_STARTUP_HARDWARE_IMPORT))!=PILOT_STARTUP_HARDWARE_FLAGS)
        stop("native kernel startup owner/profile mismatch",0);
    if(!kernel_startup_owner.initialized &&
       !pilot_kernel_startup_initialize(&kernel_startup_owner,
           PILOT_STARTUP_INITIAL_DESCRIPTOR,PILOT_STARTUP_INIT_START,
           PILOT_STARTUP_KERNEL_BUILD,1))
        stop("native kernel startup profile is unsupported",0);
}
void pilot_startup_setup(void)
{
    if(MEM32(g_esp)!=PILOT_STARTUP_APPLY_RETURN)
        stop("unreviewed native startup caller",MEM32(g_esp));
    AcquireSRWLockExclusive(&kernel_startup_lock);
    pilot_startup_ensure();
    uint64_t previous;
    if(!pilot_kernel_startup_exchange(&kernel_startup_owner,kernel_startup_owner.restricted,&previous))
        stop("native code selector setup failed",0);
    MEM32(PILOT_STARTUP_SAVED_LOW)=(uint32_t)previous;
    MEM32(PILOT_STARTUP_SAVED_HIGH)=(uint32_t)(previous>>32);
    g_eax=g_ecx=(uint32_t)previous;g_edx=(uint32_t)(previous>>32);
    ReleaseSRWLockExclusive(&kernel_startup_lock);g_esp+=4;
}
void pilot_startup_apply(void)
{
    /* Profile body proves all legacy instruction patches skip build5838.
     * Native exports have no mutable original-kernel instructions/debug monitor.
     * The actual remaining selector setup effect is retained by its owner. */
    /* FS is runtime-owned. Validate its native PRCB relationship before
     * following the guest pointer: an arbitrary retained pointer is not an
     * admitted debug-monitor owner, and nonzero would skip original setup. */
    if(!g_fs_base || (g_fs_base&3u) || g_fs_base>UINT32_MAX-0x27bu ||
       MEM32(g_fs_base+0x20)!=g_fs_base+0x28)
        stop("native startup PRCB ownership mismatch",g_fs_base);
    if(MEM32(g_fs_base+0x28+0x250))
        stop("native startup debug-monitor owner unsupported",0);
    pilot_startup_setup();
}
void pilot_startup_exchange(void)
{
    if((uint64_t)g_esp+12>UINT64_C(0x100000000))stop("invalid native selector exchange frame",g_esp);
    uint64_t requested=(uint64_t)MEM32(g_esp+4)|((uint64_t)MEM32(g_esp+8)<<32),previous;
    if(MEM32(g_esp)!=PILOT_STARTUP_SETUP_EXCHANGE_RETURN && MEM32(g_esp)!=PILOT_STARTUP_RESTORE_EXCHANGE_RETURN)
        stop("unreviewed native selector exchange caller",MEM32(g_esp));
    AcquireSRWLockExclusive(&kernel_startup_lock);pilot_startup_ensure();
    if(!pilot_kernel_startup_exchange(&kernel_startup_owner,requested,&previous))
        stop("unreviewed native code selector descriptor",0);
    g_eax=g_ecx=(uint32_t)previous;g_edx=(uint32_t)(previous>>32);
    ReleaseSRWLockExclusive(&kernel_startup_lock);g_esp+=12;
}
