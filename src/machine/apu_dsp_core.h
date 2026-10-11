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
/* EP has3072 X words,256 Y words,4096 P words and no MIX alias. */
XlleDspCore* xlle_dsp_core_create_ep(uint32_t* p,uint32_t* x,uint32_t* y,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault);
void xlle_dsp_core_destroy(XlleDspCore* core);
/* CPU state/cache boundaries; neither resets the caller's memory/peripherals. */
void xlle_dsp_core_reset(XlleDspCore* core);
void xlle_dsp_core_invalidate_program(XlleDspCore* core);
void xlle_dsp_core_begin(XlleDspCore* core,uint32_t pc);
void xlle_dsp_core_run(XlleDspCore* core,uint32_t cycles);
// Serialized device-work boundaries use the same actual CPU budget/registers.
int xlle_dsp_core_add_budget(XlleDspCore* core,uint32_t cycles);
int xlle_dsp_core_step_budget(XlleDspCore* core);
int32_t xlle_dsp_core_budget(XlleDspCore* core);
void xlle_dsp_core_result(XlleDspCore* core,uint32_t* pc,uint32_t* power,uint32_t* sp);
/* Vendor frame yield is a callback-safe halt request, separate from STOP.
 * Discard only unspent positive budget between runs; preserve instruction
 * debt, actual elapsed cycles, registers, stack and next PC. */
void xlle_dsp_core_set_halted(XlleDspCore* core,int halted);
int xlle_dsp_core_halted(XlleDspCore* core);
uint32_t xlle_dsp_core_discard_unused_budget(XlleDspCore* core);
uint32_t xlle_dsp_core_cycles(XlleDspCore* core);
#ifdef __cplusplus
}
#endif
