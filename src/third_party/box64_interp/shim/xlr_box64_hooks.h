/*
 * Hooks the vendored box64 interpreter core calls back into xboxllr.
 * Implemented by src/cpu/x86_interp.c.
 */
#ifndef XLR_BOX64_HOOKS_H
#define XLR_BOX64_HOOKS_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host address of guest address 0 (native-title g_xbox_mem_offset). */
extern uintptr_t xlr_guest_base;

void xlr_interp_log(int level, const char* fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
