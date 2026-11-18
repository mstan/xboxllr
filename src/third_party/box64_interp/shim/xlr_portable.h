/*
 * Portable replacements for the GNU C extensions the vendored box64 core used
 * (__int128, __builtin_*, case ranges, packed attributes).
 *
 * One code path for every compiler: MSVC, GCC and Clang produce bit-identical
 * guest results, and no helper executes an optional host instruction (POPCNT,
 * LZCNT) that a supported host might lack.  Compilers recognise these idioms
 * and emit the native instruction where it is baseline.
 */
#ifndef XLR_PORTABLE_H
#define XLR_PORTABLE_H
#include <stdint.h>

#if defined(__GNUC__) || defined(__clang__)
#define XLR_PACKED __attribute__((__packed__))
#else
#define XLR_PACKED
#endif

#if defined(__GNUC__) || defined(__clang__)
#define xlr_prefetch(p, rw, l) __builtin_prefetch((p), (rw), (l))
#else
#define xlr_prefetch(p, rw, l) ((void)(p))
#endif

/* Full memory barrier (MFENCE/LFENCE/SFENCE). */
#if defined(__GNUC__) || defined(__clang__)
#define xlr_mfence() __sync_synchronize()
#elif defined(_M_ARM64)
#include <intrin.h>
#define xlr_mfence() __dmb(_ARM64_BARRIER_ISH)
#else
#include <intrin.h>
static inline void xlr_mfence(void) { volatile long v_ = 0; _InterlockedExchange(&v_, 0); }
#endif

#ifndef M_LN2 /* MSVC defines it only with _USE_MATH_DEFINES */
#define M_LN2 0.69314718055994530942
#endif

static inline uint16_t xlr_bswap16(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }
static inline uint32_t xlr_bswap32(uint32_t v)
{
    return (v >> 24) | ((v >> 8) & 0x0000FF00u) | ((v << 8) & 0x00FF0000u) | (v << 24);
}
static inline uint64_t xlr_bswap64(uint64_t v)
{
    return ((uint64_t)xlr_bswap32((uint32_t)v) << 32) | xlr_bswap32((uint32_t)(v >> 32));
}

static inline int xlr_popcount32(uint32_t v)
{
    v = v - ((v >> 1) & 0x55555555u);
    v = (v & 0x33333333u) + ((v >> 2) & 0x33333333u);
    return (int)((((v + (v >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24);
}
static inline int xlr_popcount64(uint64_t v) { return xlr_popcount32((uint32_t)v) + xlr_popcount32((uint32_t)(v >> 32)); }

/* Count leading zeros; argument must be nonzero (as for __builtin_clz). */
static inline int xlr_clz32(uint32_t v)
{
    int n = 0;
    if (!(v & 0xFFFF0000u)) { n += 16; v <<= 16; }
    if (!(v & 0xFF000000u)) { n += 8; v <<= 8; }
    if (!(v & 0xF0000000u)) { n += 4; v <<= 4; }
    if (!(v & 0xC0000000u)) { n += 2; v <<= 2; }
    if (!(v & 0x80000000u)) { n += 1; }
    return n;
}
static inline int xlr_clz64(uint64_t v) { return (v >> 32) ? xlr_clz32((uint32_t)(v >> 32)) : 32 + xlr_clz32((uint32_t)v); }

/* 1 + index of least significant set bit, 0 for zero (as __builtin_ffs). */
static inline int xlr_ffs32(uint32_t v)
{
    int n = 1;
    if (!v) return 0;
    while (!(v & 1u)) { v >>= 1; ++n; }
    return n;
}

/* 64x64 -> 128 multiply. */
static inline uint64_t xlr_umul128(uint64_t a, uint64_t b, uint64_t* hi)
{
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return (mid << 32) | (uint32_t)p00;
}
static inline uint64_t xlr_smul128(int64_t a, int64_t b, uint64_t* hi)
{
    uint64_t lo = xlr_umul128((uint64_t)a, (uint64_t)b, hi);
    if (a < 0) *hi -= (uint64_t)b;
    if (b < 0) *hi -= (uint64_t)a;
    return lo;
}

/* Unsigned (hi:lo) / d.  Returns 0 (no result) when d == 0 or the quotient
   does not fit in 64 bits. */
static inline int xlr_udiv128(uint64_t hi, uint64_t lo, uint64_t d, uint64_t* q, uint64_t* r)
{
    uint64_t quot = 0, rem = hi;
    int i;
    if (d == 0 || hi >= d)
        return 0;
    for (i = 63; i >= 0; --i) {
        uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((lo >> i) & 1u);
        quot <<= 1;
        if (carry || rem >= d) {
            rem -= d;
            quot |= 1u;
        }
    }
    *q = quot;
    *r = rem;
    return 1;
}

/* Signed (hi:lo) / d with x86 IDIV semantics: truncating quotient, remainder
   takes the dividend's sign.  Returns 0 on divide error (zero divisor or a
   quotient outside int64). */
static inline int xlr_sdiv128(uint64_t hi, uint64_t lo, int64_t d, int64_t* q, int64_t* r)
{
    int neg_dvd = (int)(hi >> 63), neg_d = d < 0;
    uint64_t ahi = hi, alo = lo, ad = neg_d ? (uint64_t)0 - (uint64_t)d : (uint64_t)d, uq, ur;
    if (neg_dvd) {
        alo = (uint64_t)0 - lo;
        ahi = ~hi + (lo == 0);
    }
    if (!xlr_udiv128(ahi, alo, ad, &uq, &ur))
        return 0;
    if (neg_dvd != neg_d) {
        if (uq > (uint64_t)1 << 63)
            return 0;
        *q = (int64_t)((uint64_t)0 - uq);
    } else {
        if (uq > (uint64_t)INT64_MAX)
            return 0;
        *q = (int64_t)uq;
    }
    *r = neg_dvd ? (int64_t)((uint64_t)0 - ur) : (int64_t)ur;
    return 1;
}

#endif
