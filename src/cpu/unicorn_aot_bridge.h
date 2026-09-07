#ifndef XLLE_UNICORN_AOT_BRIDGE_H
#define XLLE_UNICORN_AOT_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    XLLE_UNICORN_AOT_ACCESS_READ = 0,
    XLLE_UNICORN_AOT_ACCESS_WRITE = 1,
    XLLE_UNICORN_AOT_ACCESS_FETCH = 2,
};

/*
 * Private bridge for xlle's AOT executor. These helpers are compiled into the
 * pinned Unicorn/QEMU x86 target and intentionally expose only fail-closed
 * questions to the C++ CPU floor.
 *
 * xlle_unicorn_aot_context_ok returns nonzero only for the bounded flat 32-bit
 * protected-mode context currently modeled by the generated AOT runtime. It
 * rejects interrupt shadow, debug/watchpoint, VM86/long-mode/SMM/SVM/MPX and
 * non-flat CS/DS/SS state. Unicorn remains the owner of all untouched hidden and
 * FP/SIMD state.
 *
 * xlle_unicorn_aot_ram_ptr returns a direct host RAM pointer only when the exact
 * access is already resident in QEMU's direct or victim TLB for the current MMU
 * mode. It never refills the TLB, walks page tables, services MMIO, handles
 * watchpoints, or performs guest memory effects. Writes may carry NOTDIRTY:
 * their caller must explicitly invalidate physical translated code after writing.
 */
int xlle_unicorn_aot_context_ok(void* engine);
void* xlle_unicorn_aot_ram_ptr(void* engine, uint32_t linear, size_t size,
                               unsigned access);
// Physical RAM invalidation without translating a guest virtual address or
// filling its TLB. uc_mem_write in the pinned core only copies bytes.
void xlle_unicorn_invalidate_ram(void* engine, uint32_t physical, size_t size);
// Initial mapping only: identical host-backed physical ranges share one RAM
// identity for TCG code lookup and invalidation, retaining separate lifetimes.
int xlle_unicorn_share_ram_identity(void* engine, uint32_t original, uint32_t alias);
void xlle_unicorn_protect_code_aliases(void* engine, uint64_t first, uint64_t second);
int xlle_unicorn_irq_inhibited(void* engine);
typedef struct xlle_unicorn_restore_record {
    uint64_t pc;
    uint32_t size, total, completed;
} xlle_unicorn_restore_record;
typedef void (*xlle_unicorn_restore_callback)(void*, const xlle_unicorn_restore_record*);
void xlle_unicorn_set_restore_callback(void* engine, xlle_unicorn_restore_callback callback,
                                        void* opaque);
void xlle_unicorn_record_restore(void* engine, uint64_t pc, uint32_t size,
                                 uint32_t total, uint32_t completed, int exiting);

#ifdef __cplusplus
}
#endif

#endif /* XLLE_UNICORN_AOT_BRIDGE_H */
