/* Maintained C/C++ bridge around mborgerson/dsp56300's unmodified C ABI.
 * This does not supply a GP firmware, bootstrap result, or DMA completion. */
#include "apu_dsp_core.h"
#include "dsp56300.h"
#include <stdlib.h>
#include <limits.h>
struct XlleDspCore {
    Dsp56300Jit* dsp;
    void* opaque;
    XlleDspFault fault;
    XlleDspRead lookup;
};
static uint32_t unmapped_read(void* opaque,uint32_t address) {
    XlleDspCore* core=opaque;core->fault(core->opaque,"unmapped DSP read",address);abort();
}
static void unmapped_write(void* opaque,uint32_t address,uint32_t value) {
    XlleDspCore* core=opaque;(void)value;core->fault(core->opaque,"unmapped DSP write",address);abort();
}
static uint32_t lookup_read(void* opaque,uint32_t address) {
    XlleDspCore* core=opaque;
    return core->lookup?core->lookup(core->opaque,address):unmapped_read(core,address);
}
static void lookup_write(void* opaque,uint32_t address,uint32_t value) {
    XlleDspCore* core=opaque;(void)value;
    core->fault(core->opaque,"unqualified EP high Y write",address);abort();
}
#define BUFFER(a,b,c) {a,b,DSP56300_REGION_BUFFER,{.buffer={c,0}}}
#define GUARD(a,b) {a,b,DSP56300_REGION_CALLBACK,{.callback={core,unmapped_read,unmapped_write}}}
static XlleDspCore* create_instance(int gp,uint32_t* p,uint32_t* x,uint32_t* y,uint32_t* mix,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault,XlleDspRead lookup) {
    if(!p || !x || !y || (gp && !mix) || !read || !write || !fault)return NULL;
    XlleDspCore* core=calloc(1,sizeof(*core));if(!core)return NULL;
    core->opaque=opaque;core->fault=fault;core->lookup=lookup;
    Dsp56300MemoryRegion pr[]={BUFFER(0,4096,p),GUARD(4096,0x1000000)};
    Dsp56300MemoryRegion xr[]={BUFFER(0,4096,x),GUARD(4096,0x1400),BUFFER(0x1400,0x1800,mix),
        {0x1800,0x1000000,DSP56300_REGION_CALLBACK,{.callback={opaque,read,write}}}};
    Dsp56300MemoryRegion yr[4]={BUFFER(0,2048,y),GUARD(2048,0x1000000)};
    if(!gp) {
        xr[0]=(Dsp56300MemoryRegion)BUFFER(0,3072,x);
        xr[1]=(Dsp56300MemoryRegion)GUARD(3072,0xffff80);
        xr[2]=(Dsp56300MemoryRegion){0xffff80,0x1000000,DSP56300_REGION_CALLBACK,{.callback={opaque,read,write}}};
        yr[0]=(Dsp56300MemoryRegion)BUFFER(0,256,y);
        yr[1]=(Dsp56300MemoryRegion)GUARD(256,0x800);
        yr[2]=(Dsp56300MemoryRegion){0x800,0x1000,DSP56300_REGION_CALLBACK,{.callback={core,lookup_read,lookup_write}}};
        yr[3]=(Dsp56300MemoryRegion)GUARD(0x1000,0x1000000);
    }
    Dsp56300CreateInfo info={.memory_map={xr,gp?4:3,yr,gp?2:4,pr,2}};
    core->dsp=dsp56300_create(&info);if(!core->dsp){free(core);return NULL;}return core;
}
XlleDspCore* xlle_dsp_core_create(uint32_t* p,uint32_t* x,uint32_t* y,uint32_t* mix,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault) {
    return create_instance(1,p,x,y,mix,opaque,read,write,fault,NULL);
}
XlleDspCore* xlle_dsp_core_create_ep(uint32_t* p,uint32_t* x,uint32_t* y,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault) {
    return create_instance(0,p,x,y,NULL,opaque,read,write,fault,NULL);
}
XlleDspCore* xlle_dsp_core_create_ep_lookup(uint32_t* p,uint32_t* x,uint32_t* y,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault,XlleDspRead lookup) {
    return create_instance(0,p,x,y,NULL,opaque,read,write,fault,lookup);
}
void xlle_dsp_core_destroy(XlleDspCore* core){if(core){dsp56300_destroy(core->dsp);free(core);}}
void xlle_dsp_core_reset(XlleDspCore* core){dsp56300_reset(core->dsp);}
void xlle_dsp_core_invalidate_program(XlleDspCore* core){dsp56300_invalidate_cache(core->dsp);}
void xlle_dsp_core_begin(XlleDspCore* core,uint32_t pc) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    state.pc=pc;state.power_state=0;state.halt_requested=false;dsp56300_set_state(core->dsp,&state);
}
void xlle_dsp_core_run(XlleDspCore* core,uint32_t cycles){dsp56300_run(core->dsp,cycles);}
int xlle_dsp_core_add_budget(XlleDspCore* core,uint32_t cycles) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    const int64_t next=(int64_t)state.cycle_budget+cycles;
    if(next>INT_MAX)return 0;
    state.cycle_budget=(int32_t)next;dsp56300_set_state(core->dsp,&state);return 1;
}
int xlle_dsp_core_step_budget(XlleDspCore* core) {
    const uint32_t before=dsp56300_cycle_count(core->dsp);
    dsp56300_step(core->dsp);
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    const uint32_t consumed=state.cycle_count-before;
    const int64_t next=(int64_t)state.cycle_budget-consumed;
    if(!consumed || consumed>INT_MAX || next<INT_MIN)return 0;
    state.cycle_budget=(int32_t)next;dsp56300_set_state(core->dsp,&state);return 1;
}
int32_t xlle_dsp_core_budget(XlleDspCore* core) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);return state.cycle_budget;
}
void xlle_dsp_core_result(XlleDspCore* core,uint32_t* pc,uint32_t* power,uint32_t* sp) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    *pc=state.pc;*power=state.power_state;*sp=state.registers[DSP56300_REG_SP];
}
void xlle_dsp_core_set_halted(XlleDspCore* core,int halted) {
    dsp56300_set_halt_requested(core->dsp,halted!=0);
}
int xlle_dsp_core_halted(XlleDspCore* core){return dsp56300_halt_requested(core->dsp);}
uint32_t xlle_dsp_core_discard_unused_budget(XlleDspCore* core) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    if(state.cycle_budget<=0)return 0;
    uint32_t unused=(uint32_t)state.cycle_budget;
    state.cycle_budget=0;dsp56300_set_state(core->dsp,&state);return unused;
}
uint32_t xlle_dsp_core_cycles(XlleDspCore* core){return dsp56300_cycle_count(core->dsp);}
