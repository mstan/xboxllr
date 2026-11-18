/*
 * XLR replacement for box64 dynarec/native_lock.h.
 *
 * box64's LOCK-prefixed paths are written against load-linked/store-
 * conditional pairs: native_lock_read_X() then native_lock_write_X() which
 * returns non-zero when the store failed and the RMW must be retried.  On the
 * hosts xboxllr supports the same contract is met with a compare-and-swap
 * against the value the paired read observed (kept per thread).  Generated
 * native-title code performs guest LOCK operations with the same host
 * interlocked primitives (RECOMP_ATOMIC_* in recomp_types.h), so interpreted
 * and native guest code stay mutually atomic.
 *
 * Limitation (matches box64): misaligned LOCK RMW paths take a byte "lock"
 * only and are not atomic against concurrent writers of the other bytes.
 */
#ifndef XLR_NATIVE_LOCK_H
#define XLR_NATIVE_LOCK_H
#include <stdint.h>
#include "../shim/xlr_box64_shim.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define XLR_TLS __declspec(thread)
static inline uint8_t xlr_cas8(volatile void* p, uint8_t c, uint8_t v) { return (uint8_t)_InterlockedCompareExchange8((volatile char*)p, (char)v, (char)c); }
static inline uint16_t xlr_cas16(volatile void* p, uint16_t c, uint16_t v) { return (uint16_t)_InterlockedCompareExchange16((volatile short*)p, (short)v, (short)c); }
static inline uint32_t xlr_cas32(volatile void* p, uint32_t c, uint32_t v) { return (uint32_t)_InterlockedCompareExchange((volatile long*)p, (long)v, (long)c); }
static inline uint64_t xlr_cas64(volatile void* p, uint64_t c, uint64_t v) { return (uint64_t)_InterlockedCompareExchange64((volatile long long*)p, (long long)v, (long long)c); }
#else
#define XLR_TLS _Thread_local
#define XLR_CAS_IMPL(T, p, c, v) do { T e_ = (c); __atomic_compare_exchange_n((volatile T*)(p), &e_, (v), 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); return e_; } while (0)
static inline uint8_t xlr_cas8(volatile void* p, uint8_t c, uint8_t v) { XLR_CAS_IMPL(uint8_t, p, c, v); }
static inline uint16_t xlr_cas16(volatile void* p, uint16_t c, uint16_t v) { XLR_CAS_IMPL(uint16_t, p, c, v); }
static inline uint32_t xlr_cas32(volatile void* p, uint32_t c, uint32_t v) { XLR_CAS_IMPL(uint32_t, p, c, v); }
static inline uint64_t xlr_cas64(volatile void* p, uint64_t c, uint64_t v) { XLR_CAS_IMPL(uint64_t, p, c, v); }
#endif

extern XLR_TLS uint64_t xlr_lock_seen;
extern XLR_TLS uint64_t xlr_lock_seen_hi;

static inline uint8_t native_lock_read_b(void* p) { uint8_t v = *(volatile uint8_t*)p; xlr_lock_seen = v; return v; }
static inline int native_lock_write_b(void* p, uint8_t v) { return xlr_cas8(p, (uint8_t)xlr_lock_seen, v) != (uint8_t)xlr_lock_seen; }
static inline uint16_t native_lock_read_h(void* p) { uint16_t v = *(volatile uint16_t*)p; xlr_lock_seen = v; return v; }
static inline int native_lock_write_h(void* p, uint16_t v) { return xlr_cas16(p, (uint16_t)xlr_lock_seen, v) != (uint16_t)xlr_lock_seen; }
static inline uint32_t native_lock_read_d(void* p) { uint32_t v = *(volatile uint32_t*)p; xlr_lock_seen = v; return v; }
static inline int native_lock_write_d(void* p, uint32_t v) { return xlr_cas32(p, (uint32_t)xlr_lock_seen, v) != (uint32_t)xlr_lock_seen; }
static inline uint64_t native_lock_read_dd(void* p) { uint64_t v = *(volatile uint64_t*)p; xlr_lock_seen = v; return v; }
static inline int native_lock_write_dd(void* p, uint64_t v) { return xlr_cas64(p, xlr_lock_seen, v) != xlr_lock_seen; }
static inline uint8_t native_lock_get_b(void* p) { return *(volatile uint8_t*)p; }
static inline uint32_t native_lock_xchg_d(void* p, uint32_t v) { uint32_t o; do { o = *(volatile uint32_t*)p; } while (xlr_cas32(p, o, v) != o); return o; }
static inline uint64_t native_lock_xchg_dd(void* p, uint64_t v) { uint64_t o; do { o = *(volatile uint64_t*)p; } while (xlr_cas64(p, o, v) != o); return o; }
static inline uint8_t native_lock_xchg_b(void* p, uint8_t v) { uint8_t o; do { o = *(volatile uint8_t*)p; } while (xlr_cas8(p, o, v) != o); return o; }
static inline void native_lock_store_dd(void* p, uint64_t v) { native_lock_xchg_dd(p, v); }
/* storeifref2: store v if *p == ref; return the previous value (box64 semantics). */
static inline void* native_lock_storeifref2(void* p, void* v, void* ref) { return (void*)(uintptr_t)xlr_cas64(p, (uint64_t)(uintptr_t)ref, (uint64_t)(uintptr_t)v); }
/* 128-bit pairs (CMPXCHG16B) are 64-bit-mode only; serialize through the context lock. */
static inline void native_lock_read_dq(uint64_t* a, uint64_t* b, void* p) {
    xlr_mutex_lock(&my_context->mutex_lock);
    *a = ((volatile uint64_t*)p)[0]; *b = ((volatile uint64_t*)p)[1];
    xlr_lock_seen = *a; xlr_lock_seen_hi = *b;
}
static inline int native_lock_write_dq(uint64_t a, uint64_t b, void* p) {
    int fail = ((volatile uint64_t*)p)[0] != xlr_lock_seen || ((volatile uint64_t*)p)[1] != xlr_lock_seen_hi;
    if (!fail) { ((volatile uint64_t*)p)[0] = a; ((volatile uint64_t*)p)[1] = b; }
    xlr_mutex_unlock(&my_context->mutex_lock);
    return fail;
}
#endif
