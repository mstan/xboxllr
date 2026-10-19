#ifndef PILOT_GRAPHICS_SURFACES_H
#define PILOT_GRAPHICS_SURFACES_H
#include <stdint.h>
#include <stddef.h>

typedef int32_t (*PilotSurfaceRef)(void *,unsigned,int);
enum { PILOT_SURFACE_BAD_COMMON=1, PILOT_SURFACE_BAD_COUNT, PILOT_SURFACE_OVERFLOW,
       PILOT_SURFACE_UNDERFLOW, PILOT_SURFACE_BACKEND };
static inline int pilot_surface_validate(uint32_t common,unsigned tracked,int operation)
{
    if((common&UINT32_C(0xffff0000))!=UINT32_C(0x010d0000))return PILOT_SURFACE_BAD_COMMON;
    if(tracked>65535u || operation < -1 || operation > 1)return PILOT_SURFACE_BAD_COUNT;
    if(operation>0 && (common&65535u)==65535u)return PILOT_SURFACE_OVERFLOW;
    if(operation<0 && !(common&65535u))return PILOT_SURFACE_UNDERFLOW;
    return 0;
}
static inline int pilot_surface_adjust(unsigned index,uint32_t *common,unsigned *tracked,
                                      int operation,PilotSurfaceRef backend,void *context)
{
    int error=pilot_surface_validate(*common,*tracked,operation);if(error)return error;
    unsigned guest=*common&65535u;
    while(*tracked!=guest) {
        int acquire=*tracked<guest;unsigned expected=acquire?*tracked+1:*tracked-1;
        int32_t result=backend(context,index,acquire);
        if(result<0 || (unsigned)result!=expected)return PILOT_SURFACE_BACKEND;
        *tracked=expected;
    }
    if(operation) {
        unsigned expected=operation>0?*tracked+1:*tracked-1;
        int32_t result=backend(context,index,operation>0);
        if(result<0 || (unsigned)result!=expected)return PILOT_SURFACE_BACKEND;
        *tracked=expected;*common=(*common&UINT32_C(0xffff0000))|expected;
    }
    return 0;
}
static inline int pilot_surface_sync_set(uint32_t common[3],unsigned tracked[3],
                                        PilotSurfaceRef backend,void *context)
{
    /* A corrupt later header cannot cause earlier backend reference changes. */
    for(unsigned i=0;i<3;++i){int error=pilot_surface_validate(common[i],tracked[i],0);if(error)return error;}
    for(unsigned i=0;i<3;++i){int error=pilot_surface_adjust(i,&common[i],&tracked[i],0,backend,context);if(error)return error;}
    return 0;
}
typedef struct PilotSurfaceLayout {
    uint32_t count,colors,depth,current_color,current_depth;
} PilotSurfaceLayout;
enum { PILOT_SURFACE_CLEAR, PILOT_SURFACE_PUBLISH, PILOT_SURFACE_CURRENT };
static inline int pilot_surface_project(uint32_t *device,uint32_t device_bytes,const PilotSurfaceLayout *layout,
                                       uint32_t headers,uint32_t color,uint32_t depth,unsigned mode)
{
    if(!layout)return 0;
    uint32_t offsets[6]={layout->count,layout->colors,layout->colors+4,layout->depth,layout->current_color,layout->current_depth};
    if(!device || mode>PILOT_SURFACE_CURRENT)return 0;
    for(unsigned i=0;i<6;++i) {
        if((offsets[i]&3) || offsets[i]>device_bytes || device_bytes-offsets[i]<4)return 0;
        for(unsigned j=0;j<i;++j)if(offsets[i]==offsets[j])return 0;
    }
    if(mode==PILOT_SURFACE_CLEAR) {
        for(unsigned i=0;i<6;++i)device[offsets[i]/4]=0;
    } else {
        if(!headers || headers>UINT32_MAX-96 || (headers&3))return 0;
        if(mode==PILOT_SURFACE_PUBLISH) {
            /* Existing native presentation owner accepts one back buffer:
             * two real color allocations, followed by its depth allocation. */
            device[layout->count/4]=2;device[layout->colors/4]=headers;
            device[layout->colors/4+1]=headers+32;device[layout->depth/4]=headers+64;
        }
        device[layout->current_color/4]=color;device[layout->current_depth/4]=depth;
    }
    return 1;
}
#endif
