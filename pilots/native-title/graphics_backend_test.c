#include "graphics_donor.h"
#include "graphics_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL graphics line %d: %s\n",__LINE__,#c); return 1; } } while (0)
#include "graphics_bump_backend_test.inc"
#include "graphics_draw_backend_test.inc"
#include "graphics_completion_backend_test.inc"
#include "graphics_fvf_backend_test.inc"
#include "graphics_flat_backend_test.inc"
#include "graphics_pixel_backend_test.inc"
#include "graphics_movie_backend_test.inc"
#include "graphics_visibility_backend_test.inc"
#include "graphics_feedback_backend_test.inc"
static int test_transform_pipeline(void)
{
    static const D3DTRANSFORMSTATETYPE states[]={D3DTS_VIEW,D3DTS_PROJECTION,D3DTS_TEXTURE0,D3DTS_TEXTURE1,
        D3DTS_TEXTURE2,D3DTS_TEXTURE3,D3DTS_WORLD,D3DTS_WORLD1,D3DTS_WORLD2,D3DTS_WORLD3};
    float matrix[16]={0};for(unsigned i=0;i<4;++i)matrix[i*5]=1;
    for(unsigned i=0;i<10;++i){matrix[12]=i*.125f;CHECK(pilot_gfx_transform(i,matrix)==0);CHECK(!memcmp(d3d8_GetTransform(states[i]),matrix,64));}
    matrix[12]=.5f;CHECK(pilot_gfx_transform(6,matrix)==0);
    matrix[12]=-.25f;CHECK(pilot_gfx_transform(0,matrix)==0);
    matrix[12]=0;matrix[0]=.5f;CHECK(pilot_gfx_transform(1,matrix)==0);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
    struct {float x,y,z;uint32_t color;} vertices[]={{-.5f,.5f,.5f,0xffff0000},{.5f,.5f,.5f,0xffff0000},{0,-.5f,.5f,0xffff0000}};
    CHECK(pilot_gfx_clear(D3DCLEAR_TARGET,0xff0000ff,1,0)==0);
    CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x42)==0);
    unsigned char pixels[64*64*4];CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    unsigned inside=(24*64+36)*4,outside=(20*64+20)*4;
    CHECK(pixels[inside]==255 && pixels[inside+2]==0 && pixels[outside]==0 && pixels[outside+2]==255);
    fprintf(stderr,"PASS: Xbox transform enum mapping and real donor world/view/projection GPU pixels\n");return 0;
}
static int test_resource_bindings(void)
{
    uint32_t texture=0;CHECK(pilot_gfx_texture_create(2,2,1,0x12,&texture)==0);
    uint32_t data[32];for(unsigned i=0;i<32;++i)data[i]=0xff123456;
    pilot_gfx_texture_prepare_sample(texture,0,data,sizeof(data),1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    pilot_gfx_set_texture_state(3,D3DTSS_COLOROP,D3DTOP_DISABLE);
    pilot_gfx_set_texture_state(0,D3DTSS_TEXCOORDINDEX,0);
    pilot_gfx_set_texture_state(0,D3DTSS_MAXMIPLEVEL,0);
    pilot_gfx_texture_bind(0,texture);pilot_gfx_texture_bind(3,texture);
    CHECK(d3d8_GetTSS(0)[D3DTSS_COLOROP]==D3DTOP_SELECTARG1 && d3d8_GetTSS(3)[D3DTSS_COLOROP]==D3DTOP_DISABLE);
    ID3D11ShaderResourceView *views[4]={0};ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
    ID3D11DeviceContext_PSGetShaderResources(ctx,0,4,views);
    CHECK(views[0] && views[3]==views[0] && !d3d8_GetStageTexture(1) && !d3d8_GetStageTexture(2));
    /* A renderer may bind an internal neutral sampling view for an empty
     * logical stage. Assert guest state and pixels, not that implementation. */
    for(unsigned i=0;i<4;++i)if(views[i]) {
        ID3D11Device *owner=NULL;ID3D11ShaderResourceView_GetDevice(views[i],&owner);
        CHECK(owner==d3d8_GetD3D11Device());ID3D11Device_Release(owner);
        ID3D11ShaderResourceView_Release(views[i]);
    }
    struct {float x,y,z,w;uint32_t color;float u,v;} vertices[]={
        {8,8,.5f,1,0xffffffff,.5f,.5f},{56,8,.5f,1,0xffffffff,.5f,.5f},{32,56,.5f,1,0xffffffff,.5f,.5f}};
    unsigned char pixels[64*64*4];unsigned center=(24*64+32)*4;
    CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x144)==0);
    CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[center]==0x12 && pixels[center+1]==0x34 && pixels[center+2]==0x56);
    for(unsigned i=0;i<32;++i)data[i]=0xff987654;
    pilot_gfx_texture_prepare_sample(texture,0,data,sizeof(data),1);
    CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x144)==0);
    CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[center]==0x98 && pixels[center+1]==0x76 && pixels[center+2]==0x54);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_bind(3,0);
    CHECK(d3d8_GetTSS(0)[D3DTSS_COLOROP]==D3DTOP_SELECTARG1);
    for(unsigned i=0;i<4;++i)CHECK(!d3d8_GetStageTexture(i));
    pilot_gfx_texture_destroy(texture);
    uint32_t target=0;CHECK(pilot_gfx_texture_create_usage(4,4,1,0x12,1,&target)==0);
    CHECK(pilot_gfx_texture_clear(target,0xff336699)==0);pilot_gfx_texture_bind(0,target);
#ifdef PILOT_D3D_PHOBOS
    CHECK(!pilot_gfx_targets(target,0) && pilot_gfx_feedback_mask()==1);
    CHECK(!pilot_gfx_targets(0,UINT32_MAX) && !pilot_gfx_feedback_mask());
#else
    CHECK(pilot_gfx_targets(target,0)==E_INVALIDARG);
#endif
    CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x144)==0);
    CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
    CHECK(pixels[center]==0x33 && pixels[center+1]==0x66 && pixels[center+2]==0x99);
    pilot_gfx_texture_bind(0,0);pilot_gfx_texture_destroy(target);
    uint32_t a=0,b=0;CHECK(pilot_gfx_vertex_create(64,&a)==0 && pilot_gfx_vertex_create(128,&b)==0);
    pilot_gfx_vertex_bind(0,a,16);pilot_gfx_vertex_bind(15,a,0);pilot_gfx_vertex_bind(0,b,32);
    ID3D11Buffer *buffers[16]={0};UINT strides[16]={0},offsets[16]={0};
    ID3D11DeviceContext_IAGetVertexBuffers(ctx,0,16,buffers,strides,offsets);
    CHECK(buffers[0] && buffers[15] && buffers[0]!=buffers[15]);
    CHECK(strides[0]==32 && strides[15]==0 && offsets[0]==0 && offsets[15]==0);
    D3D11_BUFFER_DESC desc;ID3D11Buffer_GetDesc(buffers[0],&desc);CHECK(desc.ByteWidth==128);
    ID3D11Buffer_GetDesc(buffers[15],&desc);CHECK(desc.ByteWidth==64);
    ID3D11Buffer_Release(buffers[0]);ID3D11Buffer_Release(buffers[15]);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_bind(15,0,0);
    ID3D11DeviceContext_IAGetVertexBuffers(ctx,0,16,buffers,strides,offsets);CHECK(!buffers[0] && !buffers[15]);
    pilot_gfx_vertex_destroy(a);pilot_gfx_vertex_destroy(b);
    fprintf(stderr,"PASS: actual sampled GPU pixels and changes, preserved texture state, SRV unbind and independent native vertex streams\n");return 0;
}
int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    uint32_t pp[17]={64,64,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
    unsigned char pixels[64*64*4];
    for (unsigned pass=0;pass<3;++pass) {
        CHECK(pilot_gfx_create(pp)>=0);
        struct PilotGpuProgress progress;pilot_gfx_completion_stats(&progress);
        CHECK(!progress.submitted && !progress.completed && !progress.pending_queries && !progress.created_queries);
        CHECK(pilot_gfx_width()==64 && pilot_gfx_height()==64);
        CHECK(pilot_gfx_clear(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff0000ff,1,0)>=0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255);
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
        pilot_gfx_set_state(D3DRS_ZENABLE,0);
        pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        CHECK(pilot_gfx_xbox_texture_state(0,2,3)); /* W clamp; not COLOR SIGN. */
        CHECK(pilot_gfx_xbox_texture_state(0,0,4)); /* Exercise actual border addressing. */
        CHECK(pilot_gfx_xbox_texture_state(0,6,0x3f000000));
        CHECK(pilot_gfx_xbox_texture_state(0,7,2));
        CHECK(pilot_gfx_xbox_texture_state(0,5,0));
        CHECK(pilot_gfx_xbox_texture_state(0,29,0xff123456));
        struct { float x,y,z,w; uint32_t color; } vertices[]={
            {8,8,0.5f,1,0xffff0000}, {56,8,0.5f,1,0xffff0000}, {32,56,0.5f,1,0xffff0000}};
        CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x44)>=0);
        ID3D11SamplerState *sampler=NULL;
        ID3D11DeviceContext_PSGetSamplers(d3d8_GetD3D11Context(),0,1,&sampler);
        CHECK(sampler!=NULL);
        D3D11_SAMPLER_DESC desc; ID3D11SamplerState_GetDesc(sampler,&desc);
        ID3D11SamplerState_Release(sampler);
        CHECK(desc.AddressW==D3D11_TEXTURE_ADDRESS_CLAMP && desc.MipLODBias==0.5f);
        CHECK(desc.MinLOD==2 && desc.MaxLOD==2);
        fprintf(stderr,"SAMPLER border=%g,%g,%g,%g\n",desc.BorderColor[0],desc.BorderColor[1],desc.BorderColor[2],desc.BorderColor[3]);
        CHECK(fabsf(desc.BorderColor[0]-18.0f/255)<1e-6f && fabsf(desc.BorderColor[1]-52.0f/255)<1e-6f &&
              fabsf(desc.BorderColor[2]-86.0f/255)<1e-6f && desc.BorderColor[3]==1);
        CHECK(pilot_gfx_xbox_texture_state(0,3,1) && pilot_gfx_xbox_texture_state(0,4,1) && pilot_gfx_xbox_texture_state(0,5,2));
        CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x44)>=0);
        ID3D11DeviceContext_PSGetSamplers(d3d8_GetD3D11Context(),0,1,&sampler);
        CHECK(sampler!=NULL);ID3D11SamplerState_GetDesc(sampler,&desc);ID3D11SamplerState_Release(sampler);
        CHECK(desc.Filter==D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR && desc.MinLOD==2 && desc.MaxLOD>100);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))>=0);
        unsigned center=(24*64+32)*4;
        fprintf(stderr,"GPU center RGBA=%u,%u,%u,%u\n",pixels[center],pixels[center+1],pixels[center+2],pixels[center+3]);
        CHECK(pixels[center]>240 && pixels[center+1]<10 && pixels[center+2]<10);
        CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255);
        CHECK(pilot_gfx_present()>=0);
        CHECK(test_transform_pipeline()==0);
        CHECK(test_bump_pipeline()==0);
        CHECK(test_resource_bindings()==0);
        CHECK(test_fvf_pipeline()==0);
        CHECK(test_native_draw_pipeline()==0);
        CHECK(test_flat_pipeline()==0);
        CHECK(test_resource_completion()==0);
#ifdef PILOT_D3D_PHOBOS
        CHECK(test_combiner_memory_draw()==0);
        CHECK(test_feedback()==0);
        CHECK(test_movie_textures()==0);
#endif
        /* Teardown also owns queries which the caller has not retired. A fast
         * GPU may finish immediately, so pending=true is not required. */
        CHECK(test_visibility()==0);
        CHECK(pilot_gfx_clear(1,0xff123456,1,0)==0);
        pilot_gfx_resource_busy(PILOT_GFX_SURFACE,0);
        pilot_gfx_close();
        pilot_gfx_completion_stats(&progress);
        CHECK(!progress.submitted && !progress.completed && !progress.pending_queries && !progress.created_queries);
    }
    fprintf(stderr,"PASS: actual donor D3D11 create/clear/triangle/pixel-readback/present/recreate\n"); return 0;
}
