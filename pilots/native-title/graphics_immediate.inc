#include "graphics_immediate.h"
static struct PilotImmediateBatch graphics_immediate;
#ifdef PILOT_D3D_IMMEDIATE_VERSION
#if PILOT_D3D_IMMEDIATE_VERSION != 1
#error Unsupported immediate vertex ABI
#endif
static int graphics_immediate_dispatch(unsigned entry,const uint32_t *args)
{
    int begin=entry==PILOT_D3D_IMMEDIATE_BEGIN_ENTRY,end=entry==PILOT_D3D_IMMEDIATE_END_ENTRY;
    int two=entry==PILOT_D3D_IMMEDIATE_VERTEX_DATA2F_ENTRY,four=entry==PILOT_D3D_IMMEDIATE_VERTEX_DATA4F_ENTRY;
    if(!begin && !end && !two && !four) {
        if(graphics_immediate.primitive)stop("unqualified graphics call inside immediate batch",entry);
        return 0;
    }
    uint32_t *flags=(uint32_t*)(memory+PILOT_D3D_DEVICE_OBJECT+8);
    if(begin) {
        uint32_t fvf=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
        if((fvf&15)!=4 || (*flags&0x1800) || !pilot_immediate_begin(&graphics_immediate,args[0]))
            stop("unqualified immediate Begin state/primitive",args[0]);
        *flags|=0x800;return 1;
    }
    if(end) {
        if(!graphics_immediate.primitive || !(*flags&0x800))stop("immediate End outside Begin",0);
        /* Original End optionally invokes the driver's internal SetFence(1).
         * This is not the caller-visible InsertFence ABI; do not invent that
         * cache/ownership transition until a source-bound consumer needs it. */
        if(*flags&0x1000)stop("unqualified immediate End internal fence",*flags);
        uint32_t primitive=graphics_immediate.primitive,count=graphics_immediate.count;
        if((primitive==5 && count%3) || (primitive==8 && count%4))
            stop("incomplete immediate list/quad",count);
        if(count>=3) {
            uint32_t draw[]={primitive,0,count};
            graphics_draw_submit_source(draw,0,sizeof(PilotImmediateVertex),NULL,0,0,graphics_immediate.vertices);
        }
        pilot_immediate_end(&graphics_immediate);*flags&=~0x1800u;return 1;
    }
    uint32_t reg=args[0];if(four && reg==UINT32_MAX)reg=0;
    if(reg>=16 || (!reg && !graphics_immediate.primitive))stop("unqualified immediate attribute register/emission",args[0]);
    float values[4]={0,0,0,1};memcpy(values,args+1,two?8:16);
    if(!pilot_gfx_vertex_current_set(reg,values))stop("immediate current attribute write failed",reg);
    if(!reg) {
        float snapshot[16][4];
        for(unsigned i=0;i<16;++i)if(!pilot_gfx_vertex_current_get(i,snapshot[i]))stop("immediate attribute snapshot failed",i);
        if(!pilot_immediate_append(&graphics_immediate,snapshot))stop("immediate vertex allocation/span exhausted",graphics_immediate.count);
    }
    return 1;
}
#endif
