/* Actual production state/cache bridge with an authored no-GPU backend. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "graphics_initializer.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);exit(1);}}while(0)
#define PILOT_D3D_RENDER_STATES 256
#define PILOT_D3D_TEXTURE_STATES 2048
#define PILOT_D3D_DEVICE_OBJECT 4096
#define PILOT_D3D_SHADER_CONSTANT_MODE_OFFSET 16
#define PILOT_D3D_SNAPSHOT_TEXTURES 32
#define PILOT_D3D_SNAPSHOT_PIXEL_SHADER 64
#define PILOT_D3D_SNAPSHOT_VERTEX_SHADER 68
#define PILOT_D3D_YUV_STATE 160
#define PILOT_D3D_PRESENT_INTERVAL (PILOT_D3D_RENDER_STATES+127*4)
static unsigned char storage[8192],*memory=storage;
#define MEM32(a) (*(uint32_t*)(memory+(a)))
static uint32_t graphics_parameters[17],pilot_d3d_render_defaults[109],pilot_d3d_texture_defaults[32];
static struct PilotInitializerState owner;
static jmp_buf failure;
static unsigned failures,calls,interval;
static void stop(const char *reason,uint32_t value)
{(void)reason;(void)value;++failures;longjmp(failure,1);}
static int pilot_gfx_xbox_render_state(uint32_t state,uint32_t value)
{
    switch(state) {
    case 65:case 112:case 117:case 137:case 142:case 149:
        if(!pilot_initializer_observe(&owner,state,value))return 0;break;
    default:break;
    }
    ++calls;return 1;
}
static int pilot_gfx_xbox_texture_state(uint32_t stage,uint32_t state,uint32_t value)
{(void)stage;(void)state;(void)value;return 1;}
static void graphics_present_interval(uint32_t value){interval=value;}
#include "graphics_state_cache.inc"
#include "graphics_state.inc"
int main(void)
{
    if(setjmp(failure))CHECK(0);
    graphics_parameters[8]=1;graphics_state_init();graphics_texture_state(0,3,1,1);
    const uint32_t packets[][3]={
        {0x4037c,0x1d01,66},{0x4035c,1,64},{0x40300,0,60},{0x40344,1,62},{0x40348,0,63},
        {0x40354,0x203,57},{0x40340,0,61},{0x4033c,0x207,58},{0x40310,1,65},{0x40304,1,59}};
    for(unsigned i=0;i<sizeof(packets)/sizeof(*packets);++i) {
        uint32_t state=0;CHECK(pilot_graphics_simple_decode(packets[i][0],&state) && state==packets[i][2]);
        uint32_t before=MEM32(PILOT_D3D_RENDER_STATES+state*4);
        graphics_simple_state(packets[i][0],packets[i][1]);
        CHECK(MEM32(PILOT_D3D_RENDER_STATES+state*4)==before); /* caller publishes cache */
        MEM32(PILOT_D3D_RENDER_STATES+state*4)=packets[i][1];
    }
    CHECK(owner.precision_policy==PILOT_COLOR_NATIVE_RGBA8 && owner.dither==1);
    graphics_render_state(149,0,1);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+149*4)==0);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+77*4)==0x80000000 && MEM32(PILOT_D3D_RENDER_STATES+78*4)==0x80000000);
    for(unsigned i=79;i<=81;++i)CHECK(!MEM32(PILOT_D3D_RENDER_STATES+i*4));
    graphics_render_state(142,0,1);graphics_render_state(137,0,1);
    graphics_render_state(142,1,1);CHECK(owner.normalize==1 && MEM32(PILOT_D3D_RENDER_STATES+142*4)==1);
    graphics_render_state(112,2,1);graphics_render_state(117,0x3f800000,1);
    graphics_sync_deferred();CHECK(!interval);
    CHECK(owner.ambient_source==2 && owner.point_min==0x3f800000 && owner.controls==15 && owner.zero_bias);
    const uint32_t rejected[][2]={{65,2},{149,1},{142,2},{137,1},{112,1},{117,0x40000000}};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(*rejected);++i) {
        unsigned char saved[sizeof(storage)];memcpy(saved,storage,sizeof(saved));
        struct PilotInitializerState previous=owner;unsigned previous_calls=calls,previous_failures=failures;
        if(!setjmp(failure)){graphics_render_state(rejected[i][0],rejected[i][1],1);CHECK(0);}
        CHECK(failures==previous_failures+1 && calls==previous_calls && !memcmp(saved,storage,sizeof(saved)) && !memcmp(&owner,&previous,sizeof(owner)));
    }
    unsigned previous_calls=calls;
    if(!setjmp(failure)){graphics_simple_state(0x4030c,1);CHECK(0);}CHECK(calls==previous_calls);
    CHECK(pilot_initializer_consumer(1,5,0) && pilot_initializer_consumer(0,6,0x144));
    CHECK(!pilot_initializer_consumer(0,5,0x42) && !pilot_initializer_consumer(0,5,0x54));
    CHECK(pilot_initializer_consumer(2,5,0)); /* typed fixed declaration; owner checks lighting */
    CHECK(!pilot_initializer_consumer(1,1,0) && !pilot_initializer_consumer(3,5,0));
    CHECK(pilot_initializer_format(6) && pilot_initializer_format(7) && pilot_initializer_format(0x12) && pilot_initializer_format(0x1e));
    CHECK(!pilot_initializer_format(5) && !pilot_initializer_format(0x2e));
    uint32_t small[4]={1,2,3,4},original[4];memcpy(original,small,sizeof(small));
    CHECK(!pilot_initializer_cache(small,4,149,0) && !memcmp(small,original,sizeof(small)));
    puts("PASS production initializer packets/cache, original negative-zero roles, rejected-value no-mutation and consumer/format guards");return 0;
}
