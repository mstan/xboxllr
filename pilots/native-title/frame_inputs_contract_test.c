/* Authored RAM/GPU-storage fixture for the real compact writer. No title,
 * renderer, audio or hardware is executed. Python independently checks files.
 */
#include <windows.h>
#include <bcrypt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics_backend.h"

static unsigned char ram[65536],*memory=ram;
#define MEM32(a) (*(uint32_t*)(memory+(a)))
#define PILOT_D3D_DEVICE_OBJECT 0x6000
#define PILOT_D3D_SNAPSHOT_VERTEX_SHADER 0
#define PILOT_D3D_SNAPSHOT_VERTEX_OBJECT 4
#define PILOT_D3D_SNAPSHOT_PIXEL_SHADER 8
#define PILOT_D3D_PIXEL_PACKED_CONSTANTS 0x40
#define PILOT_D3D_SNAPSHOT_TEXTURES 0x90
#define PILOT_D3D_STREAMS 0x6300
#define PILOT_D3D_RENDER_STATES 0x6400
#define PILOT_D3D_TEXTURE_STATES 0x6800
#define SCREEN_OFFSET 0x6200
#define DEPTH_SCALE 0x6208
#define TRANSFORM_FIELD(field) field
enum {PILOT_D3D_D3DDevice_Clear,PILOT_D3D_D3DDevice_SetRenderTarget,
      PILOT_D3D_D3D_CommonSetRenderTarget,PILOT_D3D_D3DDevice_Swap,
      PILOT_D3D_Direct3D_CreateDevice,PILOT_D3D_D3DDevice_Reset,
      PILOT_D3D_D3DDevice_Release,PILOT_D3D_D3DDevice_InsertCallback,
      DRAW_BOUND,DRAW_UP};
static const struct {const char *name;} pilot_d3d_calls[]={
    {"D3DDevice_Clear"},{"D3DDevice_SetRenderTarget"},{"D3D_CommonSetRenderTarget"},
    {"D3DDevice_Swap"},{"Direct3D_CreateDevice"},{"D3DDevice_Reset"},
    {"D3DDevice_Release"},{"D3DDevice_InsertCallback"},{"D3DDevice_DrawVertices"},{"D3DDevice_DrawVerticesUP"}};
struct GraphicsTextureObject {
    uint32_t header,data,bytes,width,height,levels,format,refs,native,usage,cpu_exposed;
    unsigned parent,level,bindings,guest_owned,alias_kind,alias_source,alias_format,alias_size,family_exposed;
};
struct GraphicsVertexObject {uint32_t header,data,bytes,refs,native,external,bindings;};
struct GraphicsShader {uint32_t header,native,constant_mask[6],constants[192][4],program[136][4];};
static struct GraphicsTextureObject graphics_textures[4];
static unsigned graphics_texture_capacity=4;
static struct GraphicsVertexObject vertex={0x3000,0x4000,88,1,1,0,1};
static struct GraphicsShader shader={0};
static uint32_t graphics_parameters[]={2,2},graphics_headers=0x100,graphics_target=0x100,graphics_depth=0x140;
static struct {const char *directory;uint32_t calls;} graphics_frame_audit;
static uint32_t back[]={0x10203040,0x50607080,0x90a0b0c0,0xd0e0f000};
static uint32_t offscreen[]={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
static uint32_t front[]={0x01020304,0x05060708,0x090a0b0c,0x0d0e0f00};
static uint32_t zeta[]={0x12345678,0x23456789,0x3456789a,0x456789ab};
static int fail_readback;
__declspec(noreturn) static void stop(const char *why,uint32_t arg)
{fprintf(stderr,"fixture stopped: %s (%u)\n",why,arg);exit(2);}
static void *graphics_pointer(uint32_t address,uint32_t bytes)
{if(!address || (uint64_t)address+bytes>sizeof(ram))stop("fixture range",address);return memory+address;}
static struct GraphicsVertexObject *graphics_vertex(uint32_t header)
{if(header!=vertex.header)stop("fixture vertex",header);return &vertex;}
static struct GraphicsShader *graphics_shader(uint32_t handle)
{if(handle!=shader.header+1)stop("fixture shader",handle);return &shader;}
static unsigned graphics_level_width(const struct GraphicsTextureObject *t,unsigned level)
{unsigned n=t->width>>level;return n?n:1;}
static unsigned graphics_level_height(const struct GraphicsTextureObject *t,unsigned level)
{unsigned n=t->height>>level;return n?n:1;}
static uint32_t graphics_texture_block(const struct GraphicsTextureObject *t)
{return t->format==12?8:t->format==14 || t->format==15?16:0;}
static uint32_t graphics_texture_bpp(const struct GraphicsTextureObject *t)
{return t->format<=5?2:4;}
static void graphics_gpu_barrier(void){}
int32_t pilot_gfx_reference_target_readback(unsigned depth,void *data,uint32_t capacity,uint32_t shape[3])
{
    shape[0]=shape[1]=2;shape[2]=depth?44:87;
    if(data) {if(fail_readback)return -1;if(capacity!=16)return -1;
        memcpy(data,depth?zeta:graphics_target==graphics_headers?back:offscreen,16);}
    return 0;
}
int32_t pilot_gfx_texture_readback(uint32_t handle,uint32_t level,void *data,uint32_t capacity)
{if(level || capacity!=16 || (handle!=7 && handle!=15 && handle!=16))return -1;
 memcpy(data,handle==7?offscreen:handle==15?back:front,16);return 0;}
int32_t pilot_gfx_get_viewport(uint32_t viewport[6])
{uint32_t value[]={0,0,2,2,0,0x3f800000};memcpy(viewport,value,sizeof(value));return 0;}
void pilot_gfx_shader_capture_constants(void *data){memset(data,0,3072);}
#include "graphics_frame_inputs.inc"

static FILE *ledger;
static void returned(unsigned entry,const uint32_t *args,unsigned count)
{
    ++graphics_frame_audit.calls;graphics_frame_inputs_after(entry);
    fprintf(ledger,"{\"event\":\"call\",\"sequence\":%u,\"name\":\"%s\",\"args\":[",graphics_frame_audit.calls,pilot_d3d_calls[entry].name);
    for(unsigned i=0;i<count;++i)fprintf(ledger,"%s%u",i?",":"",args[i]);
    fprintf(ledger,"],\"shader\":%u,\"target\":%u,\"depth\":%u,\"capture_sequence\":0,\"return_eax\":0,\"input_sequence\":%u}\n",
        MEM32(PILOT_D3D_DEVICE_OBJECT),graphics_target,graphics_depth,
        entry==DRAW_BOUND || entry==DRAW_UP?graphics_frame_inputs.input_sequence:0);
}
static void draw(unsigned up,unsigned fvf)
{
    uint32_t args[]={fvf?8:5,up?0:fvf?0:1,fvf?4:3},extents[16]={fvf?24:16};
    MEM32(PILOT_D3D_DEVICE_OBJECT)=fvf?0x104:shader.header+1;
    MEM32(PILOT_D3D_STREAMS)=fvf?32:24;vertex.bytes=fvf?120:88;
    uint32_t stride=up?(fvf?24:16):0,address=up?0x5000:0;
    uint32_t abi[]={args[0],args[2],address,stride};
    graphics_frame_inputs_before(up?DRAW_UP:DRAW_BOUND,up?abi:args);
    graphics_frame_inputs_draw(args,address,stride,1,extents);
    offscreen[0]++; /* a changed target observation must never resupply its root */
    returned(up?DRAW_UP:DRAW_BOUND,up?abi:args,up?4:3);
}
int main(int argc,char **argv)
{
    char directory[MAX_PATH],path[MAX_PATH];int own=argc==1;
    if(own) {
        char temporary[MAX_PATH];if(!GetTempPathA(sizeof(temporary),temporary) ||
            !GetTempFileNameA(temporary,"fip",0,directory) || !DeleteFileA(directory) ||
            !CreateDirectoryA(directory,NULL))return 1;
    } else if(strlen(argv[1])>=sizeof(directory))return 1;
    else strcpy(directory,argv[1]);
    graphics_frame_audit.directory=directory;fail_readback=argc>2;
    if(_putenv_s("XLLE_PILOT_FRAME_INPUTS","1"))return 1;
    shader.header=0x2000;MEM32(shader.header+8)=1;
    shader.program[0][1]=0x0020001b;shader.program[0][2]=0x08000000;shader.program[0][3]=0x0000f801;
    for(unsigned i=0;i<16;++i)MEM32(shader.header+0x1c+i*16)=i?2:0x42;
    MEM32(PILOT_D3D_DEVICE_OBJECT)=shader.header+1;MEM32(PILOT_D3D_DEVICE_OBJECT+4)=0x2800;
    MEM32(PILOT_D3D_STREAMS+8)=vertex.header;MEM32(DEPTH_SCALE)=0x4b7fffff;
    for(unsigned i=0;i<160;++i)memory[0x4000+i]=memory[0x5000+i]=(unsigned char)i;
    graphics_textures[0]=(struct GraphicsTextureObject){.header=0x1000,.data=0x7000,.bytes=128,.width=2,.height=2,.levels=1,.format=0x12,.native=7,.usage=1};
    graphics_textures[1]=(struct GraphicsTextureObject){.header=0x1100,.width=2,.height=2,.levels=1,.format=0x12,.parent=1};
    graphics_textures[2]=(struct GraphicsTextureObject){.header=0x1200,.width=2,.height=2,.levels=1,.format=0x12,.native=15,.usage=1,.guest_owned=1,.alias_kind=PILOT_GFX_SURFACE};
    graphics_textures[3]=(struct GraphicsTextureObject){.header=0x1300,.width=2,.height=2,.levels=1,.format=0x12,.native=16,.usage=1,.guest_owned=1,.alias_kind=PILOT_GFX_SURFACE,.alias_source=1};
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES)=0x1200;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+4)=0x1300;
    snprintf(path,sizeof(path),"%s/frame.jsonl",directory);ledger=fopen(path,"wb");if(!ledger)return 1;
    fprintf(ledger,"{\"event\":\"begin\",\"schema\":1,\"swap\":17,\"width\":2,\"height\":2,\"target\":256,\"depth\":320}\n");
    graphics_frame_inputs_open(17);
    uint32_t clear[]={0,0,0xf3,0,0x3f800000,0};graphics_frame_inputs_before(PILOT_D3D_D3DDevice_Clear,clear);
    memset(back,0,sizeof(back));returned(PILOT_D3D_D3DDevice_Clear,clear,6);
    uint32_t bind[]={0x1100,0x140};graphics_frame_inputs_before(PILOT_D3D_D3DDevice_SetRenderTarget,bind);
    graphics_target=0x1100;returned(PILOT_D3D_D3DDevice_SetRenderTarget,bind,2);
    draw(0,0);draw(1,0);
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES)=0x1000;
    draw(0,1);draw(1,1);
    uint32_t swap[]={0};graphics_frame_inputs_before(PILOT_D3D_D3DDevice_Swap,swap);graphics_frame_inputs_close();
    fprintf(ledger,"{\"event\":\"end\",\"swap\":18,\"calls\":6,\"draws\":4,\"captured\":0,\"clears\":1,\"target_sets\":1,\"inputs\":4}\n");
    fclose(ledger);
    if(graphics_frame_inputs.draws!=4 || graphics_frame_inputs.target_count!=3)return 1;
    if(own) {DeleteFileA(path);snprintf(path,sizeof(path),"%s/frame-inputs.bin",directory);DeleteFileA(path);RemoveDirectoryA(directory);}
    puts("PASS: actual compact writer, four draw kinds, roots/observations and aliases");return 0;
}
