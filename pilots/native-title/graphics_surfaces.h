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
static inline int pilot_surface_sync_mask(uint32_t common[3],unsigned tracked[3],unsigned mask,
                                         PilotSurfaceRef backend,void *context)
{
    /* A corrupt later header cannot cause earlier backend reference changes. */
    if(mask!=3 && mask!=7 && mask!=11 && mask!=15)return PILOT_SURFACE_BAD_COUNT;
    unsigned count=mask&8?4:3;
    for(unsigned i=0;i<count;++i) {
        if(!(mask&(1u<<i))){if(common[i] || tracked[i])return PILOT_SURFACE_BAD_COMMON;continue;}
        int error=pilot_surface_validate(common[i],tracked[i],0);if(error)return error;
    }
    for(unsigned i=0;i<count;++i)if(mask&(1u<<i)){int error=pilot_surface_adjust(i,&common[i],&tracked[i],0,backend,context);if(error)return error;}
    return 0;
}
static inline int pilot_surface_sync_set(uint32_t common[3],unsigned tracked[3],PilotSurfaceRef backend,void *context)
{return pilot_surface_sync_mask(common,tracked,7,backend,context);}

typedef struct PilotSurfaceLayout {
    uint32_t count,colors,depth,current_color,current_depth;
} PilotSurfaceLayout;
enum { PILOT_SURFACE_CLEAR, PILOT_SURFACE_PUBLISH, PILOT_SURFACE_CURRENT };
static inline int pilot_surface_project_mask(uint32_t *device,uint32_t device_bytes,const PilotSurfaceLayout *layout,
                                            uint32_t headers,uint32_t color,uint32_t depth,unsigned mode,unsigned mask)
{
    if(!layout || (mask!=3 && mask!=7 && mask!=11 && mask!=15))return 0;
    uint32_t offsets[7]={layout->count,layout->colors,layout->colors+4,layout->depth,layout->current_color,layout->current_depth,layout->colors+8};
    unsigned fields=mask&8?7:6;
    if(!device || mode>PILOT_SURFACE_CURRENT)return 0;
    for(unsigned i=0;i<fields;++i) {
        if((offsets[i]&3) || offsets[i]>device_bytes || device_bytes-offsets[i]<4)return 0;
        for(unsigned j=0;j<i;++j)if(offsets[i]==offsets[j])return 0;
    }
    if(mode==PILOT_SURFACE_CLEAR) {
        for(unsigned i=0;i<fields;++i)device[offsets[i]/4]=0;
    } else {
        if(!headers || headers>UINT32_MAX-128 || (headers&3))return 0;
        if(mode==PILOT_SURFACE_PUBLISH) {
            /* Existing native presentation owner accepts one back buffer:
             * two real color allocations, followed by its depth allocation. */
            device[layout->count/4]=mask&8?3:2;device[layout->colors/4]=headers;
            device[layout->colors/4+1]=headers+32;device[layout->depth/4]=(mask&4)?headers+64:0;
            if(mask&8)device[layout->colors/4+2]=headers+96;
        }
        device[layout->current_color/4]=color;device[layout->current_depth/4]=depth;
    }
    return 1;
}
static inline int pilot_surface_project(uint32_t *device,uint32_t device_bytes,const PilotSurfaceLayout *layout,
                                       uint32_t headers,uint32_t color,uint32_t depth,unsigned mode)
{return pilot_surface_project_mask(device,device_bytes,layout,headers,color,depth,mode,7);}
#endif
