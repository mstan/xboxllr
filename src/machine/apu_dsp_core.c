/* Maintained C/C++ bridge around mborgerson/dsp56300's unmodified C ABI.
 * This does not supply a GP firmware, bootstrap result, or DMA completion. */
#include "apu_dsp_core.h"
#include "dsp56300.h"
#include <stdlib.h>
struct XlleDspCore {
    Dsp56300Jit* dsp;
    void* opaque;
    XlleDspFault fault;
};
static uint32_t unmapped_read(void* opaque,uint32_t address) {
    XlleDspCore* core=opaque;core->fault(core->opaque,"unmapped DSP read",address);abort();
}
static void unmapped_write(void* opaque,uint32_t address,uint32_t value) {
    XlleDspCore* core=opaque;(void)value;core->fault(core->opaque,"unmapped DSP write",address);abort();
}
#define BUFFER(a,b,c) {a,b,DSP56300_REGION_BUFFER,{.buffer={c,0}}}
#define GUARD(a,b) {a,b,DSP56300_REGION_CALLBACK,{.callback={core,unmapped_read,unmapped_write}}}
XlleDspCore* xlle_dsp_core_create(uint32_t* p,uint32_t* x,uint32_t* y,uint32_t* mix,
    void* opaque,XlleDspRead read,XlleDspWrite write,XlleDspFault fault) {
    if(!p || !x || !y || !mix || !read || !write || !fault)return NULL;
    XlleDspCore* core=calloc(1,sizeof(*core));if(!core)return NULL;
    core->opaque=opaque;core->fault=fault;
    Dsp56300MemoryRegion pr[]={BUFFER(0,4096,p),GUARD(4096,0x1000000)};
    Dsp56300MemoryRegion xr[]={BUFFER(0,4096,x),GUARD(4096,0x1400),BUFFER(0x1400,0x1800,mix),
        {0x1800,0x1000000,DSP56300_REGION_CALLBACK,{.callback={opaque,read,write}}}};
    Dsp56300MemoryRegion yr[]={BUFFER(0,2048,y),GUARD(2048,0x1000000)};
    Dsp56300CreateInfo info={.memory_map={xr,4,yr,2,pr,2}};
    core->dsp=dsp56300_create(&info);if(!core->dsp){free(core);return NULL;}return core;
}
void xlle_dsp_core_destroy(XlleDspCore* core){if(core){dsp56300_destroy(core->dsp);free(core);}}
void xlle_dsp_core_begin(XlleDspCore* core,uint32_t pc) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    state.pc=pc;state.power_state=0;state.halt_requested=false;dsp56300_set_state(core->dsp,&state);
}
void xlle_dsp_core_run(XlleDspCore* core,uint32_t cycles){dsp56300_run(core->dsp,cycles);}
void xlle_dsp_core_result(XlleDspCore* core,uint32_t* pc,uint32_t* power,uint32_t* sp) {
    Dsp56300State state;dsp56300_get_state(core->dsp,&state);
    *pc=state.pc;*power=state.power_state;*sp=state.registers[DSP56300_REG_SP];
}
