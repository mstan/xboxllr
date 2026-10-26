/* Shared native callback/CPU-service context. Portable C storage includes the
 * host floating environment as well as the generated x86 register files. */
#include <fenv.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
struct NativeCpuContext {
    uint32_t regs[13];
    RecompXmm xmm[8];RecompMmx mm[8];double fp[8];
    int fp_top,fp_cmp,df;uint16_t cw,cc;fenv_t environment;
#if defined(_M_X64) || defined(__x86_64__)
    unsigned mxcsr;
#endif
};
static void native_cpu_save(struct NativeCpuContext *c)
{
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_fs_base,g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form,next_kernel_ordinal};
    RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    RecompMmx mm[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    memcpy(c->regs,regs,sizeof(regs));memcpy(c->xmm,xmm,sizeof(xmm));memcpy(c->mm,mm,sizeof(mm));
    memcpy(c->fp,g_fp_stack,sizeof(c->fp));c->fp_top=g_fp_top;c->fp_cmp=g_fp_cmp;c->df=g_df;
    c->cw=g_fp_control_word;c->cc=g_fp_cc;
    if(fegetenv(&c->environment))stop("native CPU environment capture failed",0);
#if defined(_M_X64) || defined(__x86_64__)
    c->mxcsr=_mm_getcsr();
#endif
}
static void native_cpu_restore(const struct NativeCpuContext *c)
{
    const uint32_t *r=c->regs;
    g_eax=r[0];g_ecx=r[1];g_edx=r[2];g_esp=r[3];g_ebx=r[4];g_esi=r[5];g_edi=r[6];g_ebp=r[7];
    g_fs_base=r[8];g_seh_ebp=r[9];g_icall_saved_esp=r[10];g_icall_dispatch_form=r[11];next_kernel_ordinal=r[12];
    g_xmm0=c->xmm[0];g_xmm1=c->xmm[1];g_xmm2=c->xmm[2];g_xmm3=c->xmm[3];g_xmm4=c->xmm[4];g_xmm5=c->xmm[5];g_xmm6=c->xmm[6];g_xmm7=c->xmm[7];
    g_mm0=c->mm[0];g_mm1=c->mm[1];g_mm2=c->mm[2];g_mm3=c->mm[3];g_mm4=c->mm[4];g_mm5=c->mm[5];g_mm6=c->mm[6];g_mm7=c->mm[7];
    memcpy(g_fp_stack,c->fp,sizeof(c->fp));g_fp_top=c->fp_top;g_fp_cmp=c->fp_cmp;g_df=c->df;g_fp_control_word=c->cw;g_fp_cc=c->cc;
    if(fesetenv(&c->environment))stop("native CPU environment restore failed",0);
#if defined(_M_X64) || defined(__x86_64__)
    _mm_setcsr(c->mxcsr);
#endif
}
static uint32_t native_cpu_pure_call(uint32_t entry,uint32_t object,
                                     unsigned count,const uint32_t *arguments)
{
    /* Only source-closed, bounded, nonblocking arithmetic helpers may use
     * this service. Keep the CPU permit across its short host-owner critical
     * section; replay every pending suspension/preemption after returning. */
#ifdef PILOT_SCHEDULING_LLE
    (void)object;(void)count;(void)arguments;
    stop("native arithmetic service requires native CPU scheduling",entry);
    return 0;
#else
    recomp_func_t body=recomp_lookup(entry);uint32_t top=g_esp;
    /* Source-bound callers supply reviewed RET(4*count) helpers. Arguments
     * are guest DWORDs, including guest addresses, never host pointers. Copy
     * before touching the guest stack: a caller may have staged its arguments
     * there. Four arguments cover the original spatial table lookup ABI. */
    uint32_t copied[4];
    if(count>4 || (count && !arguments))
        stop("unqualified native arithmetic service arguments",entry);
    if(count)memcpy(copied,arguments,count*sizeof(*copied));
    if(!body || dispatch_in_dpc || top<512 || !wait_range(top-512,512,1))
        stop("unqualified native arithmetic service frame",entry);
    struct NativeCpuContext saved;native_cpu_save(&saved);
    volatile uint32_t *requests=g_pilot_suspend_count;g_pilot_suspend_count=NULL;
    g_ecx=object;g_fp_top=0;memset(g_fp_stack,0,sizeof(saved.fp));
    for(unsigned i=count;i;--i)PUSH32(g_esp,copied[i-1]);
    PUSH32(g_esp,0);body();
    if(g_esp!=top || g_ebx!=saved.regs[4] || g_esi!=saved.regs[5] || g_edi!=saved.regs[6] ||
       g_fs_base!=saved.regs[8] || next_kernel_ordinal!=saved.regs[12])
        stop("native arithmetic service changed its bounded ABI",entry);
    uint32_t result=g_eax;
    /* g_ebp/g_seh_ebp publish the latest generated frame, including a nested
     * callee's frame. The architectural EBP itself is local to that function.
     * Restore these publication caches; requiring them unchanged would reject
     * ordinary helpers that called a correctly balanced framed function. */
    native_cpu_restore(&saved);g_pilot_suspend_count=requests;
    return result;
#endif
}
static void native_cpu_pure_thiscall(uint32_t entry,uint32_t object)
{
    (void)native_cpu_pure_call(entry,object,0,NULL);
}
