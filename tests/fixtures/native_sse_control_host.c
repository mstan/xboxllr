/* Host contract for the read-only guest MXCSR observation helper. */
#if defined(PILOT_SSE_CROSS_COMPILE_PROBE)
#include "native_sse_control.h"

uint32_t native_sse_control_probe(void)
{
    return pilot_native_mxcsr_read();
}
#else
#include "native_sse_control.h"
#include <fenv.h>
#include <stdio.h>

#if (defined(_M_X64) || defined(__x86_64__)) && !defined(PILOT_SSE_FORCE_PORTABLE)
#include <xmmintrin.h>
#define NATIVE_MXCSR_AVAILABLE 1
#else
#define NATIVE_MXCSR_AVAILABLE 0
#endif

int main(void)
{
    static const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
#if !NATIVE_MXCSR_AVAILABLE
    static const uint32_t controls[]={0x1f80,0x3f80,0x5f80,0x7f80};
#endif
    fenv_t saved;if(fegetenv(&saved))return 5;
    const int raised=FE_INVALID|FE_DIVBYZERO|FE_OVERFLOW|FE_UNDERFLOW|FE_INEXACT;
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);++i)for(unsigned status=0;status<2;++status) {
        if(feclearexcept(FE_ALL_EXCEPT) || fesetround(modes[i]) || (status && feraiseexcept(raised))) {
            fprintf(stderr,"fenv setup failed for mode %u\n",i);return 1;
        }
        int round_before=fegetround();
        int flags_before=fetestexcept(FE_ALL_EXCEPT);
        if((flags_before&raised)!=(status?raised:0))return 6;
#if NATIVE_MXCSR_AVAILABLE
        uint32_t csr_before=(uint32_t)_mm_getcsr();
#endif
        uint32_t observed=pilot_native_mxcsr_read();
        if(fegetround()!=round_before || fetestexcept(FE_ALL_EXCEPT)!=flags_before) {
            fprintf(stderr,"query changed fenv state for mode %u\n",i);return 2;
        }
#if NATIVE_MXCSR_AVAILABLE
        uint32_t csr_after=(uint32_t)_mm_getcsr();
        if(observed!=csr_before || csr_after!=csr_before) {
            fprintf(stderr,"native MXCSR mismatch for mode %u: before=%08x result=%08x after=%08x\n",
                i,csr_before,observed,csr_after);return 3;
        }
#else
        uint32_t expected=controls[i]|(status?0x3du:0);
        if(observed!=expected) {
            fprintf(stderr,"portable MXCSR mismatch for mode %u: expected=%08x result=%08x flags=%x\n",
                i,expected,observed,flags_before);return 4;
        }
#endif
    }
    if(fesetenv(&saved))return 7;
    puts(NATIVE_MXCSR_AVAILABLE?
        "PASS native MXCSR: all rounding modes, raised flags, exact read and unchanged state":
        "PASS portable MXCSR: all rounding modes, represented flags and unchanged fenv");
    return 0;
}
#endif
