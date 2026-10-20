/* Call a source-proved, native-compiled title allocator on the real caller's
 * guest stack, outside the renderer lock. Preserve the surrounding HLE CPU
 * context, including FP/SIMD state; this is not an interpreter fallback. */
#include <fenv.h>
static void graphics_allocator_call(uint32_t routine,const uint32_t *arguments,unsigned count)
{
    recomp_func_t body=recomp_lookup(routine);uint32_t top=graphics_abi_caller_stack();
    if(!body || count>2 || (count && !arguments) || dispatch_in_dpc || top<4096 || (top&3))stop("unqualified original allocator call",routine);
    /* Real thread stacks are committed by their owner. Query host protection
     * without treating an uncommitted page as a writable callback stack. */
    for(uint64_t at=((uint64_t)top-4096)&~4095ull;at<top;at+=4096) {
        MEMORY_BASIC_INFORMATION info;
        if(!VirtualQuery(memory+at,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect&(PAGE_NOACCESS|PAGE_GUARD)))stop("original allocator requires committed caller stack",top);
    }
    uint32_t regs[]={g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp,g_fs_base,g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form};
    RecompXmm xmm[]={g_xmm0,g_xmm1,g_xmm2,g_xmm3,g_xmm4,g_xmm5,g_xmm6,g_xmm7};
    RecompMmx mm[]={g_mm0,g_mm1,g_mm2,g_mm3,g_mm4,g_mm5,g_mm6,g_mm7};
    double fp[8];memcpy(fp,g_fp_stack,sizeof(fp));
    int fp_top=g_fp_top,fp_cmp=g_fp_cmp,df=g_df;
    uint16_t cw=g_fp_control_word,cc=g_fp_cc;fenv_t environment;
    if(fegetenv(&environment))stop("allocator CPU environment capture failed",routine);
    uint32_t frame=top-(count+1)*4;MEM32(frame)=0;
    if(count)memcpy(memory+frame+4,arguments,count*4);g_esp=frame;
    body();
    if(g_esp!=top || g_ebx!=regs[4] || g_esi!=regs[5] || g_edi!=regs[6] || g_fs_base!=regs[8])
        stop("original allocator stdcall ABI differs",routine);
    g_eax=regs[0];g_ecx=regs[1];g_edx=regs[2];g_esp=regs[3];g_ebx=regs[4];g_esi=regs[5];g_edi=regs[6];g_ebp=regs[7];
    g_fs_base=regs[8];g_seh_ebp=regs[9];g_icall_saved_esp=regs[10];g_icall_dispatch_form=regs[11];
    g_xmm0=xmm[0];g_xmm1=xmm[1];g_xmm2=xmm[2];g_xmm3=xmm[3];g_xmm4=xmm[4];g_xmm5=xmm[5];g_xmm6=xmm[6];g_xmm7=xmm[7];
    g_mm0=mm[0];g_mm1=mm[1];g_mm2=mm[2];g_mm3=mm[3];g_mm4=mm[4];g_mm5=mm[5];g_mm6=mm[6];g_mm7=mm[7];
    memcpy(g_fp_stack,fp,sizeof(fp));g_fp_top=fp_top;g_fp_cmp=fp_cmp;g_df=df;g_fp_control_word=cw;g_fp_cc=cc;
    if(fesetenv(&environment))stop("allocator CPU environment restore failed",routine);
}
