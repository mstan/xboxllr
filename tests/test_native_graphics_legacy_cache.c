/* Exercise the production cache and complete Reset transaction using authored
 * host-operation fixtures. No title data, original code, GPU or game needed. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
static uint32_t memory_words[4096];
static unsigned char *memory=(unsigned char*)memory_words;
#define MEM32(address) memory_words[(address)/4]
#define PILOT_D3D_RENDER_STATES 16u
#define PILOT_D3D_RENDER_STATE_MAP_VERSION 1
#define PILOT_D3D_GUEST_RENDER_STATE_COUNT 144
#define PILOT_D3D_LEGACY_SAMPLE_TYPE_CACHE_VERSION 1
#define PILOT_D3D_LEGACY_SAMPLE_TYPE_INDEX 134u
static int16_t pilot_d3d_render_state_map[166];
static uint16_t pilot_d3d_render_state_inverse[144];
static jmp_buf stopped;
static int expecting_stop;
static void stop(const char *reason,uint32_t value)
{
    (void)value;
    if(!expecting_stop){fprintf(stderr,"unexpected guard: %s\n",reason);exit(1);}
    longjmp(stopped,1);
}
#include "graphics_state_cache.inc"
#include "graphics_presentation.h"
#define CHECK(value) do{if(!(value)){fprintf(stderr,"check line %d: %s\n",__LINE__,#value);return 1;}}while(0)
#define GUARD(action) do{expecting_stop=1;if(!setjmp(stopped)){action;fprintf(stderr,"missing guard line %d\n",__LINE__);return 1;}expecting_stop=0;}while(0)

/* These fixtures supply allocation/backend outcomes to the real Reset owner.
 * They do not replace any production implementation or admit guest success. */
static uint32_t graphics_parameters[17],graphics_data[4],graphics_headers=4096;
static unsigned graphics_surface_refs[4],graphics_swap_submitted;
static int dispatch_in_dpc,graphics_clock_closing,graphics_resource_waiters,graphics_resource_retiring;
static uint64_t graphics_swap_deadline;
static int allocation_calls,allocation_fail_at,free_calls;
static int32_t reset_result;
#define MEM_RESERVE 1
#define PAGE_NOACCESS 1
#define MEM_RELEASE 2
#define PILOT_SURFACE_CLEAR 0
#define PILOT_SURFACE_PUBLISH 1
#define PILOT_D3D_PRESENT_INTERVAL 2048
static void *graphics_pointer(uint32_t pointer,unsigned bytes)
{if(pointer+bytes>sizeof(memory_words))stop("fixture pointer",pointer);return memory+pointer;}
static int32_t graphics_parameters_check(const uint32_t p[17])
{return pilot_presentation_profile_admits(p,2)?0:(int32_t)0x80070057u;}
static int vm_allocate(uint32_t *pointer,uint32_t zero,uint32_t *bytes,unsigned kind,unsigned access)
{
    (void)zero;(void)bytes;(void)kind;(void)access;
    if(++allocation_calls==allocation_fail_at)return 1;
    *pointer=(uint32_t)(8192+allocation_calls*128);return 0;
}
static int vm_free(uint32_t *pointer,uint32_t *bytes,unsigned kind)
{(void)bytes;(void)kind;++free_calls;*pointer=0;return 0;}
static void graphics_present_drain(void){}
static uint32_t graphics_fence_insert(void){return 1;}
static void graphics_fence_wait(uint32_t value){(void)value;}
static int32_t pilot_gfx_reset(const uint32_t p[17]){(void)p;return reset_result;}
static void graphics_surfaces_project(unsigned mode){(void)mode;}
static void graphics_set_targets(uint32_t color,uint32_t depth){(void)color;(void)depth;}
static int32_t pilot_gfx_clear(unsigned flags,uint32_t color,float depth,uint32_t stencil)
{(void)flags;(void)color;(void)depth;(void)stencil;return 0;}
static void graphics_persistence_clear(void){}
#include "graphics_reset.inc"

int main(void)
{
    memset(memory_words,0x5a,sizeof(memory_words));
    for(unsigned i=0;i<166;++i)pilot_d3d_render_state_map[i]=-1;
    for(unsigned i=0;i<82;++i)pilot_d3d_render_state_map[i]=(int16_t)i;
    for(unsigned i=92;i<=125;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-10);
    for(unsigned i=136;i<=153;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-20);
    for(unsigned i=156;i<=157;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-21);
    for(unsigned i=159;i<=165;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-22);
    for(unsigned i=0;i<166;++i)if(pilot_d3d_render_state_map[i]>=0)
        pilot_d3d_render_state_inverse[pilot_d3d_render_state_map[i]]=(uint16_t)i;
    pilot_d3d_render_state_inverse[134]=UINT16_MAX;
    graphics_render_map_check();
    graphics_render_write(147,0x900);CHECK(MEM32(PILOT_D3D_RENDER_STATES+127*4)==0x900);
    graphics_render_write(160,1);CHECK(MEM32(PILOT_D3D_RENDER_STATES+138*4)==1);
    CHECK(graphics_render_from_guest(135)==156 && graphics_render_from_guest(143)==165);
    CHECK(graphics_render_read(154)==0 && graphics_render_read(155)==0);
    GUARD(graphics_render_from_guest(134));GUARD(graphics_render_write(154,1));
    pilot_d3d_render_state_map[154]=134;GUARD(graphics_render_map_check());pilot_d3d_render_state_map[154]=-1;
    pilot_d3d_render_state_inverse[134]=154;GUARD(graphics_render_map_check());pilot_d3d_render_state_inverse[134]=UINT16_MAX;
    graphics_render_map_check();
    graphics_render_presentation_cache(0x11);CHECK(MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x11);
    graphics_render_presentation_cache(0x2222);CHECK(MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x2222);
    GUARD(graphics_render_presentation_cache(0));CHECK(MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x2222);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+144*4)==0x5a5a5a5a);
    uint32_t values[166];graphics_render_copy(values);CHECK(values[147]==0x900 && values[160]==1);
    FILE *file=tmpfile();CHECK(file && graphics_render_capture(file));CHECK(!fclose(file));

    uint32_t p[17]={64,32,6,1,0x11,1,0,0,0,0,0,60,1,0,0,0,0};
    memcpy(memory+1024,p,sizeof(p));CHECK(graphics_reset(1024)==0);
    CHECK(graphics_parameters[4]==0x11 && MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x11);
    p[4]=0x2222;memcpy(memory+1024,p,sizeof(p));reset_result=(int32_t)0x80004005u;
    CHECK(graphics_reset(1024)==0x80004005u);
    CHECK(graphics_parameters[4]==0x11 && MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x11);
    reset_result=0;allocation_fail_at=allocation_calls+1;
    CHECK(graphics_reset(1024)==0x8007000eu);
    CHECK(graphics_parameters[4]==0x11 && MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x11);
    allocation_fail_at=0;CHECK(graphics_reset(1024)==0);
    CHECK(graphics_parameters[4]==0x2222 && MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x2222);
    p[4]=0;memcpy(memory+1024,p,sizeof(p));CHECK(graphics_reset(1024)==0x80070057u);
    CHECK(graphics_parameters[4]==0x2222 && MEM32(PILOT_D3D_RENDER_STATES+134*4)==0x2222);
    CHECK(free_calls>0);
    puts("PASS legacy cache: source layout, explicit guest-only stop, accepted sample projection and actual Reset commit/failure paths");
    return 0;
}
