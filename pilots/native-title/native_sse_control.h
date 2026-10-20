#ifndef PILOT_NATIVE_SSE_CONTROL_H
#define PILOT_NATIVE_SSE_CONTROL_H
#include <stdint.h>
#include <fenv.h>
#if !defined(PILOT_SSE_FORCE_PORTABLE) && (defined(_M_X64) || defined(__x86_64__))
/* SSE is part of the x64 ABI. Other CPU/ABI targets use the C environment. */
#include <xmmintrin.h>
#endif

/* Read only: never change rounding, exception masks, sticky state or FTZ/DAZ.
 * The portable native profile retains masked reset controls and represents
 * standard C exception flags. Denormal status and alternate denormal controls
 * need a separate qualified owner before guest writes can be admitted. */
static inline uint32_t pilot_native_mxcsr_read(void)
{
#if !defined(PILOT_SSE_FORCE_PORTABLE) && (defined(_M_X64) || defined(__x86_64__))
    return (uint32_t)_mm_getcsr();
#else
    uint32_t value=0x1f80u;
    switch(fegetround()) {
    case FE_TONEAREST:break;
#ifdef FE_DOWNWARD
    case FE_DOWNWARD:value|=0x2000u;break;
#endif
#ifdef FE_UPWARD
    case FE_UPWARD:value|=0x4000u;break;
#endif
#ifdef FE_TOWARDZERO
    case FE_TOWARDZERO:value|=0x6000u;break;
#endif
    default:return UINT32_MAX;
    }
    int flags=fetestexcept(FE_ALL_EXCEPT);
#ifdef FE_INVALID
    if(flags&FE_INVALID)value|=1u;
#endif
#ifdef FE_DIVBYZERO
    if(flags&FE_DIVBYZERO)value|=4u;
#endif
#ifdef FE_OVERFLOW
    if(flags&FE_OVERFLOW)value|=8u;
#endif
#ifdef FE_UNDERFLOW
    if(flags&FE_UNDERFLOW)value|=16u;
#endif
#ifdef FE_INEXACT
    if(flags&FE_INEXACT)value|=32u;
#endif
    return value;
#endif
}
#endif
