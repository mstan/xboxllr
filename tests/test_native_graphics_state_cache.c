/* Source-grounded compact SDK cache fixture. Test the production cache owner,
 * including direct original inline writes and untouched adjacent globals. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdlib.h>
static uint32_t memory_words[256];
#define MEM32(address) memory_words[(address)/4]
#define PILOT_D3D_RENDER_STATES 16u
#ifdef TEST_COMPACT_CACHE
#define PILOT_D3D_RENDER_STATE_MAP_VERSION 1
#define PILOT_D3D_GUEST_RENDER_STATE_COUNT 146
static int16_t pilot_d3d_render_state_map[166];
static uint16_t pilot_d3d_render_state_inverse[146];
#endif
static jmp_buf stopped;
static int expecting_stop;
static void stop(const char *reason,uint32_t value)
{
    (void)value;
    if(!expecting_stop){fprintf(stderr,"unexpected guard: %s\n",reason);exit(1);}
    longjmp(stopped,1);
}
#include "graphics_state_cache.inc"
#define CHECK(value) do{if(!(value)){fprintf(stderr,"check line %d: %s\n",__LINE__,#value);return 1;}}while(0)
#define GUARD(action) do{expecting_stop=1;if(!setjmp(stopped)){action;fprintf(stderr,"missing guard line %d\n",__LINE__);return 1;}expecting_stop=0;}while(0)
int main(void)
{
    memset(memory_words,0x5a,sizeof(memory_words));
#ifdef TEST_COMPACT_CACHE
    /* Original simple/deferred boundaries are82/117. The original complex
     * dispatch table has no later SAMPLEALPHA slot; its last index is145. */
    for(unsigned i=0;i<166;++i)pilot_d3d_render_state_map[i]=-1;
    for(unsigned i=0;i<82;++i)pilot_d3d_render_state_map[i]=(int16_t)i;
    for(unsigned i=92;i<=126;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-10);
    for(unsigned i=136;i<=157;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-19);
    for(unsigned i=159;i<166;++i)pilot_d3d_render_state_map[i]=(int16_t)(i-20);
    for(unsigned i=0;i<166;++i)if(pilot_d3d_render_state_map[i]>=0)
        pilot_d3d_render_state_inverse[pilot_d3d_render_state_map[i]]=(uint16_t)i;
    graphics_render_map_check();
    MEM32(PILOT_D3D_RENDER_STATES+82*4)=1; /* Original inline FOGENABLE. */
    CHECK(graphics_render_read(92)==1);
    graphics_render_write(147,0x900); /* Shared semantic CULLMODE. */
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+128*4)==0x900);
    graphics_render_write(136,0x12345678); /* Shared PSTEXTUREMODES. */
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+117*4)==0x12345678);
    CHECK(graphics_render_from_guest(135)==154 && graphics_render_from_guest(139)==159);
    CHECK(graphics_render_from_guest(145)==165);
    CHECK(graphics_render_read(158)==0 && graphics_render_read(82)==0);
    CHECK(MEM32(PILOT_D3D_RENDER_STATES+146*4)==0x5a5a5a5a);
    GUARD(graphics_render_write(158,1));
    GUARD(graphics_render_from_guest(146));
    pilot_d3d_render_state_map[92]=128;GUARD(graphics_render_map_check());pilot_d3d_render_state_map[92]=82;
    pilot_d3d_render_state_map[165]=146;GUARD(graphics_render_map_check());pilot_d3d_render_state_map[165]=145;
    pilot_d3d_render_state_inverse[82]=93;GUARD(graphics_render_map_check());pilot_d3d_render_state_inverse[82]=92;
    graphics_render_map_check();
#else
    graphics_render_map_check();
    graphics_render_write(147,0x900);CHECK(MEM32(PILOT_D3D_RENDER_STATES+147*4)==0x900);
    MEM32(PILOT_D3D_RENDER_STATES+92*4)=1;CHECK(graphics_render_read(92)==1);
    CHECK(graphics_render_from_guest(165)==165);
    GUARD(graphics_render_from_guest(166));
#endif
    GUARD(graphics_render_read(166));
    uint32_t values[166];graphics_render_copy(values);CHECK(values[92]==1 && values[147]==0x900);
    FILE *file=tmpfile();CHECK(file && graphics_render_capture(file));rewind(file);
    uint32_t captured[166];CHECK(fread(captured,sizeof(captured),1,file)==1);CHECK(!memcmp(values,captured,sizeof(values)));CHECK(fgetc(file)==EOF);CHECK(!fclose(file));
    puts("PASS semantic render cache: original inline writes, compact/identity storage, guards and normalized capture");
    return 0;
}
