#include "d3d8_xbox.h"
#include "graphics_backend.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL graphics line %d: %s\n",__LINE__,#c); return 1; } } while (0)
int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    uint32_t pp[17]={64,64,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
    unsigned char pixels[64*64*4];
    for (unsigned pass=0;pass<3;++pass) {
        CHECK(pilot_gfx_create(pp)>=0);
        CHECK(pilot_gfx_width()==64 && pilot_gfx_height()==64);
        CHECK(pilot_gfx_clear(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff0000ff,1,0)>=0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))>=0);
        CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255);
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
        pilot_gfx_set_state(D3DRS_ZENABLE,0);
        pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        struct { float x,y,z,w; uint32_t color; } vertices[]={
            {8,8,0.5f,1,0xffff0000}, {56,8,0.5f,1,0xffff0000}, {32,56,0.5f,1,0xffff0000}};
        CHECK(pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,vertices,sizeof(vertices[0]),0x44)>=0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))>=0);
        unsigned center=(24*64+32)*4;
        fprintf(stderr,"GPU center RGBA=%u,%u,%u,%u\n",pixels[center],pixels[center+1],pixels[center+2],pixels[center+3]);
        CHECK(pixels[center]>240 && pixels[center+1]<10 && pixels[center+2]<10);
        CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255);
        CHECK(pilot_gfx_present()>=0);
        pilot_gfx_close();
    }
    fprintf(stderr,"PASS: actual donor D3D11 create/clear/triangle/pixel-readback/present/recreate\n"); return 0;
}
