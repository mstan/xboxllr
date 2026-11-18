/* XLR shim: replaces box64 box64cpu_util.h (guest stack helpers through XLR_G2H) */
#pragma once
#include "xlr_box64_shim.h"
#include "x64emu_private.h"

/* ---- guest stack (box64cpu_util.h) --------------------------------------- */
#ifndef TEST_INTERPRETER
#define XLR_SP (emu->regs[4].dword[0])
static inline void Push16(x64emu_t* emu, uint16_t v) { XLR_SP -= 2; *(uint16_t*)XLR_G2H(XLR_SP) = v; }
static inline void Push_32(x64emu_t* emu, uint32_t v) { XLR_SP -= 4; *(uint32_t*)XLR_G2H(XLR_SP) = v; }
static inline void Push32(x64emu_t* emu, uint32_t v) { XLR_SP -= 4; *(uint32_t*)XLR_G2H(XLR_SP) = v; }
static inline void Push64(x64emu_t* emu, uint64_t v) { XLR_SP -= 8; *(uint64_t*)XLR_G2H(XLR_SP) = v; }
static inline uint16_t Pop16(x64emu_t* emu) { uint16_t v = *(uint16_t*)XLR_G2H(XLR_SP); XLR_SP += 2; return v; }
static inline uint32_t Pop_32(x64emu_t* emu) { uint32_t v = *(uint32_t*)XLR_G2H(XLR_SP); XLR_SP += 4; return v; }
static inline uint32_t Pop32(x64emu_t* emu) { uint32_t v = *(uint32_t*)XLR_G2H(XLR_SP); XLR_SP += 4; return v; }
static inline uint64_t Pop64(x64emu_t* emu) { uint64_t v = *(uint64_t*)XLR_G2H(XLR_SP); XLR_SP += 8; return v; }
#endif

