// SPDX-License-Identifier: MIT
/* XLR: verbatim excerpt of box64 src/emu/x64emu.c (applyFlushTo0 + UpdateFlags,
   upstream lines 648-1683 at 27c8275d8295f521bb40faf920788ac5116dbfb0).
   The rest of x64emu.c (process/emu lifetime, wrappers) is not used. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "box64stack.h"
#include "x64emu.h"
#include "box64cpu_util.h"
#include "x64emu_private.h"
#include "x87emu_private.h"
#include "box64context.h"
#include "x64run_private.h"
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

void applyFlushTo0(x64emu_t* emu)
{
    #if defined(__x86_64__) && !defined(__arm64ec__) && !defined(_M_ARM64EC)
    _mm_setcsr(_mm_getcsr() | (emu->mxcsr.x32&0x8040));
    #elif defined(__aarch64__) || defined(__arm64ec__) || defined(_M_ARM64EC)
    #if defined(__ANDROID__) || defined(__clang__)
    uint64_t fpcr;
    __asm__ __volatile__ ("mrs    %0, fpcr":"=r"(fpcr));
    #else
    uint64_t fpcr = __builtin_aarch64_get_fpcr();
    #endif
    fpcr &= ~((1<<24) | (1<<1));    // clear bit FZ (24) and AH (1)
    fpcr |= (emu->mxcsr.f.MXCSR_FZ)<<24;  // set FZ as mxcsr FZ
    fpcr |= ((emu->mxcsr.f.MXCSR_DAZ)^(emu->mxcsr.f.MXCSR_FZ))<<1; // set AH if DAZ different from FZ
    #if defined(__ANDROID__) || defined(__clang__)
    __asm__ __volatile__ ("msr    fpcr, %0"::"r"(fpcr));
    #else
    __builtin_aarch64_set_fpcr(fpcr);
    #endif
    #else
    // This does not applies to RISC-V and LoongArch, as they don't have flush to zero
    #endif
}

#define PARITY(x) (((emu->x64emu_parity_tab[(x) / 32] >> ((x) % 32)) & 1) == 0)
#define XOR2(x)   (((x) ^ ((x) >> 1)) & 0x1)
void UpdateFlags(x64emu_t* emu)
{
    uint64_t cc;
    uint64_t lo, hi;
    uint64_t bc;
    uint64_t cnt;

    switch (emu->df) {
        case d_none:
            return;
        case d_add8:
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x100, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u8 & emu->op2.u8) | ((~emu->res.u8) & (emu->op1.u8 | emu->op2.u8));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_add8b:
            CONDITIONAL_SET_FLAG(((uint16_t)emu->op1.u8 + emu->op2.u8) & 0x100, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u8 & emu->op2.u8) | ((~emu->res.u8) & (emu->op1.u8 | emu->op2.u8));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_add16:
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x10000, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u16 & emu->op2.u16) | ((~emu->res.u16) & (emu->op1.u16 | emu->op2.u16));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_add16b:
            CONDITIONAL_SET_FLAG(((uint32_t)emu->op1.u16 + emu->op2.u16) & 0x10000, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u16 & emu->op2.u16) | ((~emu->res.u16) & (emu->op1.u16 | emu->op2.u16));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_add32:
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x100000000LL, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u32 & emu->op2.u32) | ((~emu->res.u32) & (emu->op1.u32 | emu->op2.u32));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_add32b:
            lo = (emu->op2.u32 & 0xFFFF) + (emu->op1.u32 & 0xFFFF);
            hi = (lo >> 16) + (emu->op2.u32 >> 16) + (emu->op1.u32 >> 16);
            CONDITIONAL_SET_FLAG(hi & 0x10000, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u32 & emu->op2.u32) | ((~emu->res.u32) & (emu->op1.u32 | emu->op2.u32));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_add64:
            lo = (emu->op2.u64 & 0xFFFFFFFF) + (emu->op1.u64 & 0xFFFFFFFF);
            hi = (lo >> 32) + (emu->op2.u64 >> 32) + (emu->op1.u64 >> 32);
            CONDITIONAL_SET_FLAG(hi & 0x100000000L, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u64 & emu->op2.u64) | ((~emu->res.u64) & (emu->op1.u64 | emu->op2.u64));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_and8:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_and16:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_and32:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_and64:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(emu->res.u64 == 0, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_dec8:
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u8 & (~emu->op1.u8 | 1)) | (~emu->op1.u8 & 1);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_dec16:
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u16 & (~emu->op1.u16 | 1)) | (~emu->op1.u16 & 1);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_dec32:
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u32 & (~emu->op1.u32 | 1)) | (~emu->op1.u32 & 1);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_dec64:
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u64 & (~emu->op1.u64 | 1LL)) | (~emu->op1.u64 & 1LL);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_inc8:
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = ((1 & emu->op1.u8) | (~emu->res.u8)) & (1 | emu->op1.u8);
            CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_inc16:
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (1 & emu->op1.u16) | ((~emu->res.u16) & (1 | emu->op1.u16));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_inc32:
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (1 & emu->op1.u32) | ((~emu->res.u32) & (1 | emu->op1.u32));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_inc64:
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (1LL & emu->op1.u64) | ((~emu->res.u64) & (1LL | emu->op1.u64));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_imul8:
            lo = emu->res.u16 & 0xff;
            hi = (emu->res.u16 >> 8) & 0xff;
            if (((lo & 0x80) == 0 && hi == 0x00) || ((lo & 0x80) != 0 && hi == 0xFF)) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u8 >> 7) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_imul16:
            lo = (uint16_t)emu->res.u32;
            hi = (uint16_t)(emu->res.u32 >> 16);
            if (((lo & 0x8000) == 0 && hi == 0x00) || ((lo & 0x8000) != 0 && hi == 0xFFFF)) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u16 >> 15) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_imul32:
            if ((((emu->res.u32 & 0x80000000) == 0) && emu->op1.u32 == 0x00) || (((emu->res.u32 & 0x80000000) != 0) && emu->op1.u32 == 0xFFFFFFFF)) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u32 >> 31) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_imul64:
            if (((emu->res.u64 & 0x8000000000000000LL) == 0 && emu->op1.u64 == 0x00) || ((emu->res.u64 & 0x8000000000000000LL) != 0 && emu->op1.u64 == 0xFFFFFFFFFFFFFFFFLL)) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u64 >> 63) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_mul8:
            hi = (emu->res.u16 >> 8) & 0xff;
            if (hi == 0) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u8 >> 7) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_mul16:
            hi = (uint16_t)(emu->res.u32 >> 16);
            if (hi == 0) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u16 >> 15) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_mul32:
            if (emu->op1.u32 == 0) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u32 >> 31) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_mul64:
            if (emu->op1.u64 == 0) {
                CLEAR_FLAG(F_CF);
                CLEAR_FLAG(F_OF);
            } else {
                SET_FLAG(F_CF);
                SET_FLAG(F_OF);
            }
            if (!BOX64ENV(cputype)) {
                CONDITIONAL_SET_FLAG((emu->res.u64 >> 63) & 1, F_SF);
                CLEAR_FLAG(F_ZF);
                CLEAR_FLAG(F_AF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            }
            break;
        case d_or8:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_or16:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_or32:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_or64:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_neg8:
            CONDITIONAL_SET_FLAG(emu->op1.u8, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = emu->res.u8 | emu->op1.u8;
            CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_neg16:
            CONDITIONAL_SET_FLAG(emu->op1.u16, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = emu->res.u16 | emu->op1.u16;
            CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_neg32:
            CONDITIONAL_SET_FLAG(emu->op1.u32, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = emu->res.u32 | emu->op1.u32;
            CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_neg64:
            CONDITIONAL_SET_FLAG(emu->op1.u64, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = emu->res.u64 | emu->op1.u64;
            CONDITIONAL_SET_FLAG(XOR2(bc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_shl8:
            cnt = emu->op2.u8 & 0x1f;
            if (cnt > 0) {
                cc = emu->op1.u8 & (1 << (8 - cnt));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG(((emu->res.u8 >> 7) ^ ACCESS_FLAG(F_CF)) & 0x01, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u8 >> 6), F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_shl16:
            cnt = emu->op2.u16 & 0x1f;
            if (cnt > 0) {
                cc = emu->op1.u16 & (1 << (16 - cnt));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG(((emu->res.u16 >> 15) ^ ACCESS_FLAG(F_CF)) & 0x01, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u16 >> 14), F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_shl32:
            cnt = emu->op2.u32 & 0x1f;
            if (cnt > 0) {
                cc = emu->op1.u32 & (1 << (32 - cnt));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG(((emu->res.u32 >> 31) ^ ACCESS_FLAG(F_CF)) & 0x01, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u32 >> 30), F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_shl64:
            if (emu->op2.u64 > 0) {
                cc = emu->op1.u64 & (1LL << (64 - emu->op2.u64));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG(((emu->res.u64 >> 63) ^ ACCESS_FLAG(F_CF)) & 0x01, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u64 >> 62), F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_sar8:
            if (emu->op2.u8) {
                cc = (emu->op1.i8 >> (emu->op2.u8 - 1)) & 1;
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
                CLEAR_FLAG(F_OF);
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_sar16:
            if (emu->op2.u16) {
                cc = (emu->op1.i16 >> (emu->op2.u16 - 1)) & 1;
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                CLEAR_FLAG(F_OF);
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_sar32:
            if (emu->op2.u32) {
                cc = emu->op1.u32 & (1 << (emu->op2.u32 - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                CLEAR_FLAG(F_OF);
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_sar64:
            if (emu->op2.u64) {
                cc = emu->op1.u64 & (1LL << (emu->op2.u64 - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                CLEAR_FLAG(F_OF);
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_shr8:
            cnt = emu->op2.u8 & 0x1f;
            if (cnt > 0) {
                cc = emu->op1.u8 & (1 << (cnt - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG((emu->res.u8 >> 6) & 0x1, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG((emu->op1.u8 >> 7) & 0x1, F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_shr16:
            cnt = emu->op2.u16 & 0x1f;
            if (cnt > 0) {
                cc = emu->op1.u16 & (1 << (cnt - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG((emu->res.u16 >> 14) & 0x1, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG((emu->op1.u16 >> 15) & 0x1, F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            if (cnt == 1) {
                CONDITIONAL_SET_FLAG(emu->op1.u16 & 0x8000, F_OF);
            }
            break;
        case d_shr32:
            cnt = emu->op2.u32 & 0x1f;
            if (cnt > 0) {
                cc = emu->op1.u32 & (1 << (cnt - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG((emu->res.u32 >> 30) & 0x1, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG((emu->op1.u32 >> 31) & 0x1, F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_shr64:
            cnt = emu->op2.u64;
            if (cnt > 0) {
                cc = emu->op1.u64 & (1LL << (cnt - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if (BOX64ENV(cputype)) {
                    CONDITIONAL_SET_FLAG((emu->res.u64 >> 62) & 0x1, F_OF);
                    SET_FLAG(F_AF);
                } else {
                    CONDITIONAL_SET_FLAG((emu->op1.u64 >> 63) & 0x1, F_OF);
                    CLEAR_FLAG(F_AF);
                }
            }
            break;
        case d_shrd16:
            cnt = emu->op2.u16;
            if (cnt > 0) {
                // fill operand is stored in the upper half of op2
                if (cnt > 16)
                    cc = (emu->op2.u64 >> 16) & 0xffff & (1u << (cnt - 17));
                else
                    cc = emu->op1.u16 & (1 << (cnt - 1));
                if (cnt > 16 && BOX64ENV(cputype))
                    cc = 0;
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if BOX64ENV (cputype) {
                    CONDITIONAL_SET_FLAG(XOR2(emu->res.u16 >> 14), F_OF);
                } else {
                    CONDITIONAL_SET_FLAG((((emu->op2.u64 >> 16) & 0xffff) ^ (emu->op1.u16 >> 15)) & 1, F_OF);
                }
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_shrd32:
            cnt = emu->op2.u32;
            if (cnt > 0) {
                cc = emu->op1.u32 & (1 << (cnt - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if BOX64ENV (cputype) {
                    CONDITIONAL_SET_FLAG(XOR2(emu->res.u32 >> 30), F_OF);
                } else {
                    CONDITIONAL_SET_FLAG(((emu->res.u32 >> (32 - cnt)) ^ (emu->op1.u32 >> 31)) & 1, F_OF);
                }
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_shrd64:
            cnt = emu->op2.u64;
            if (cnt > 0) {
                cc = emu->op1.u64 & (1LL << (cnt - 1));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if BOX64ENV (cputype) {
                    CONDITIONAL_SET_FLAG(XOR2(emu->res.u64 >> 62), F_OF);
                } else {
                    CONDITIONAL_SET_FLAG(((emu->res.u64 >> (64 - cnt)) ^ (emu->op1.u64 >> 63)) & 1, F_OF);
                }
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_shld16:
            cnt = emu->op2.u16;
            if (cnt > 0) {
                if (cnt > 16)
                    cc = (emu->op2.u64 >> 16) & 0xffff & (1u << (32 - cnt));
                else
                    cc = emu->op1.u16 & (1 << (16 - cnt));
                if (cnt > 16 && BOX64ENV(cputype))
                    cc = 0;
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if BOX64ENV (cputype) {
                    if (cnt > 15)
                        CONDITIONAL_SET_FLAG(ACCESS_FLAG(F_CF), F_OF);
                    else
                        CONDITIONAL_SET_FLAG((ACCESS_FLAG(F_CF) ^ (emu->res.u16 >> 15)) & 1, F_OF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u16 >> 14), F_OF);
                }
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_shld32:
            cnt = emu->op2.u32;
            if (cnt > 0) {
                cc = emu->op1.u32 & (1 << (32 - cnt));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if BOX64ENV (cputype) {
                    CONDITIONAL_SET_FLAG((ACCESS_FLAG(F_CF) ^ (emu->res.u32 >> 31)) & 1, F_OF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u32 >> 30), F_OF);
                }
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_shld64:
            cnt = emu->op2.u64;
            if (cnt > 0) {
                cc = emu->op1.u64 & (1LL << (64 - cnt));
                CONDITIONAL_SET_FLAG(cc, F_CF);
                CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
                CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
                CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
                if BOX64ENV (cputype) {
                    CONDITIONAL_SET_FLAG((ACCESS_FLAG(F_CF) ^ (emu->res.u64 >> 63)) & 1, F_OF);
                } else {
                    CONDITIONAL_SET_FLAG(XOR2(emu->op1.u64 >> 62), F_OF);
                }
                if (BOX64ENV(cputype))
                    SET_FLAG(F_AF);
                else
                    CLEAR_FLAG(F_AF);
            }
            break;
        case d_sub8:
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u8 & (~emu->op1.u8 | emu->op2.u8)) | (~emu->op1.u8 & emu->op2.u8);
            CONDITIONAL_SET_FLAG(bc & 0x80, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_sub16:
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u16 & (~emu->op1.u16 | emu->op2.u16)) | (~emu->op1.u16 & emu->op2.u16);
            CONDITIONAL_SET_FLAG(bc & 0x8000, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_sub32:
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u32 & (~emu->op1.u32 | emu->op2.u32)) | (~emu->op1.u32 & emu->op2.u32);
            CONDITIONAL_SET_FLAG(bc & 0x80000000, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_sub64:
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u64 & (~emu->op1.u64 | emu->op2.u64)) | (~emu->op1.u64 & emu->op2.u64);
            CONDITIONAL_SET_FLAG(bc & 0x8000000000000000LL, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_xor8:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_xor16:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_xor32:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_xor64:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_CF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            break;
        case d_cmp8:
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG((emu->res.u8 & 0xff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u8 & (~emu->op1.u8 | emu->op2.u8)) | (~emu->op1.u8 & emu->op2.u8);
            CONDITIONAL_SET_FLAG(bc & 0x80, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_cmp16:
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG((emu->res.u16 & 0xffff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u16 & (~emu->op1.u16 | emu->op2.u16)) | (~emu->op1.u16 & emu->op2.u16);
            CONDITIONAL_SET_FLAG(bc & 0x8000, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_cmp32:
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u32 & (~emu->op1.u32 | emu->op2.u32)) | (~emu->op1.u32 & emu->op2.u32);
            CONDITIONAL_SET_FLAG(bc & 0x80000000, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_cmp64:
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u64 & (~emu->op1.u64 | emu->op2.u64)) | (~emu->op1.u64 & emu->op2.u64);
            CONDITIONAL_SET_FLAG(bc & 0x8000000000000000LL, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_tst8:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            CLEAR_FLAG(F_CF);
            break;
        case d_tst16:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u16, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            CLEAR_FLAG(F_CF);
            break;
        case d_tst32:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            CLEAR_FLAG(F_CF);
            break;
        case d_tst64:
            CLEAR_FLAG(F_OF);
            CLEAR_FLAG(F_AF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            CLEAR_FLAG(F_CF);
            break;
        case d_adc8:
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x100, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u8 & emu->op2.u8) | ((~emu->res.u8) & (emu->op1.u8 | emu->op2.u8));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_adc8b:
            if (emu->res.u8 == (emu->op1.u8 + emu->op2.u8)) {
                lo = (uint32_t)(emu->op1.u8) + (emu->op2.u8);
            } else {
                lo = 1 + (uint32_t)(emu->op1.u8) + (emu->op2.u8);
            }
            CONDITIONAL_SET_FLAG(lo & 0x100, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u8, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u8 & emu->op2.u8) | ((~emu->res.u8) & (emu->op1.u8 | emu->op2.u8));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_adc16:
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x10000, F_CF);
            CONDITIONAL_SET_FLAG((emu->res.u16 & 0xffff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u16 & emu->op2.u16) | ((~emu->res.u16) & (emu->op1.u16 | emu->op2.u16));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_adc16b:
            if (emu->res.u16 == (emu->op1.u16 + emu->op2.u16)) {
                lo = (uint32_t)(emu->op1.u16) + (emu->op2.u16);
            } else {
                lo = 1 + (uint32_t)(emu->op1.u16) + (emu->op2.u16);
            }
            CONDITIONAL_SET_FLAG(lo & 0x10000, F_CF);
            CONDITIONAL_SET_FLAG((emu->res.u16 & 0xffff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u16 & emu->op2.u16) | ((~emu->res.u16) & (emu->op1.u16 | emu->op2.u16));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_adc32:
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x100000000L, F_CF);
            CONDITIONAL_SET_FLAG((emu->res.u32 & 0xffffffff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op1.u32 & emu->op2.u32) | ((~emu->res.u32) & (emu->op1.u32 | emu->op2.u32));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_adc32b:
            if (emu->res.u32 == (emu->op1.u32 + emu->op2.u32)) {
                lo = (emu->op1.u32 & 0xFFFF) + (emu->op2.u32 & 0xFFFF);
            } else {
                lo = 1 + (emu->op1.u32 & 0xFFFF) + (emu->op2.u32 & 0xFFFF);
            }
            hi = (lo >> 16) + (emu->op1.u32 >> 16) + (emu->op2.u32 >> 16);
            CONDITIONAL_SET_FLAG(hi & 0x10000, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op2.u32 & emu->op1.u32) | ((~emu->res.u32) & (emu->op2.u32 | emu->op1.u32));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_adc64:
            if (emu->res.u64 == (emu->op1.u64 + emu->op2.u64)) {
                lo = (emu->op1.u64 & 0xFFFFFFFF) + (emu->op2.u64 & 0xFFFFFFFF);
            } else {
                lo = 1 + (emu->op1.u64 & 0xFFFFFFFF) + (emu->op2.u64 & 0xFFFFFFFF);
            }
            hi = (lo >> 32) + (emu->op1.u64 >> 32) + (emu->op2.u64 >> 32);
            CONDITIONAL_SET_FLAG(hi & 0x100000000L, F_CF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            cc = (emu->op2.u64 & emu->op1.u64) | ((~emu->res.u64) & (emu->op2.u64 | emu->op1.u64));
            CONDITIONAL_SET_FLAG(XOR2(cc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(cc & 0x8, F_AF);
            break;
        case d_sbb8:
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x80, F_SF);
            CONDITIONAL_SET_FLAG((emu->res.u8 & 0xff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u8 & (~emu->op1.u8 | emu->op2.u8)) | (~emu->op1.u8 & emu->op2.u8);
            CONDITIONAL_SET_FLAG(bc & 0x80, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 6), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_sbb16:
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x8000, F_SF);
            CONDITIONAL_SET_FLAG((emu->res.u16 & 0xffff) == 0, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u16 & (~emu->op1.u16 | emu->op2.u16)) | (~emu->op1.u16 & emu->op2.u16);
            CONDITIONAL_SET_FLAG(bc & 0x8000, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 14), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_sbb32:
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x80000000, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u32, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u32 & (~emu->op1.u32 | emu->op2.u32)) | (~emu->op1.u32 & emu->op2.u32);
            CONDITIONAL_SET_FLAG(bc & 0x80000000, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 30), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_sbb64:
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x8000000000000000LL, F_SF);
            CONDITIONAL_SET_FLAG(!emu->res.u64, F_ZF);
            CONDITIONAL_SET_FLAG(PARITY(emu->res.u8), F_PF);
            bc = (emu->res.u64 & (~emu->op1.u64 | emu->op2.u64)) | (~emu->op1.u64 & emu->op2.u64);
            CONDITIONAL_SET_FLAG(bc & 0x8000000000000000LL, F_CF);
            CONDITIONAL_SET_FLAG(XOR2(bc >> 62), F_OF);
            CONDITIONAL_SET_FLAG(bc & 0x8, F_AF);
            break;
        case d_rol8:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG((emu->res.u8 + (emu->res.u8 >> 7)) & 1, F_OF);
            else
                CONDITIONAL_SET_FLAG(XOR2(emu->op1.u8 >> 6), F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & 0x1, F_CF);
            break;
        case d_rol16:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG((emu->res.u16 + (emu->res.u16 >> 15)) & 1, F_OF);
            else
                CONDITIONAL_SET_FLAG(XOR2(emu->op1.u16 >> 14), F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & 0x1, F_CF);
            break;
        case d_rol32:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG((emu->res.u32 + (emu->res.u32 >> 31)) & 1, F_OF);
            else
                CONDITIONAL_SET_FLAG(XOR2(emu->op1.u32 >> 30), F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & 0x1, F_CF);
            break;
        case d_rol64:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG((emu->res.u64 + (emu->res.u64 >> 63)) & 1, F_OF);
            else
                CONDITIONAL_SET_FLAG(XOR2(emu->op1.u64 >> 62), F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & 0x1, F_CF);
            break;
        case d_ror8:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG(XOR2(emu->res.u8 >> 6), F_OF);
            else
                CONDITIONAL_SET_FLAG(((emu->op1.u8 >> 7) ^ emu->op1.u8) & 1, F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u8 & (1 << 7), F_CF);
            break;
        case d_ror16:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG(XOR2(emu->res.u16 >> 14), F_OF);
            else
                CONDITIONAL_SET_FLAG(((emu->op1.u16 >> 15) ^ emu->op1.u16) & 1, F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u16 & (1 << 15), F_CF);
            break;
        case d_ror32:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG(XOR2(emu->res.u32 >> 30), F_OF);
            else
                CONDITIONAL_SET_FLAG(((emu->op1.u32 >> 31) ^ emu->op1.u32) & 1, F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u32 & (1 << 31), F_CF);
            break;
        case d_ror64:
            if (BOX64ENV(cputype))
                CONDITIONAL_SET_FLAG(XOR2(emu->res.u64 >> 62), F_OF);
            else
                CONDITIONAL_SET_FLAG(((emu->op1.u64 >> 63) ^ emu->op1.u64) & 1, F_OF);
            CONDITIONAL_SET_FLAG(emu->res.u64 & (1LL << 63), F_CF);
            break;

        case d_unknown:
            printf_log(LOG_NONE, "%p trying to evaluate Unknown deferred Flags\n", (void*)R_RIP);
            break;
    }
    RESET_FLAGS(emu);
}

