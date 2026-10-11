#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* C bridge to the pinned MIT DSP core. The caller owns these fixed GP banks
 * for the entire lifetime; peripheral callbacks must terminate on invalid I/O. */
typedef struct XlleDspCore XlleDspCore;
typedef uint32_t (*XlleDspRead)(void*,uint32_t);
typedef void (*XlleDspWrite)(void*,uint32_t,uint32_t);
typedef void (*XlleDspFault)(void*,const char*,uint32_t);
XlleDspCore* xlle_dsp_core_create(uint32_t* p,uint32_t* x,uint32_t* y,uint32_t* mix,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault);
void xlle_dsp_core_destroy(XlleDspCore* core);
/* CPU state/cache boundaries; neither resets the caller's memory/peripherals. */
void xlle_dsp_core_reset(XlleDspCore* core);
void xlle_dsp_core_invalidate_program(XlleDspCore* core);
void xlle_dsp_core_begin(XlleDspCore* core,uint32_t pc);
void xlle_dsp_core_run(XlleDspCore* core,uint32_t cycles);
void xlle_dsp_core_result(XlleDspCore* core,uint32_t* pc,uint32_t* power,uint32_t* sp);
#ifdef __cplusplus
}
#endif
