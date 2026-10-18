#ifdef PILOT_D3D_PHOBOS
#include "d3d8_display.h"
HRESULT pilot_d3d_visual_resolve(RhiView*,unsigned,unsigned,RhiView*,D3D8DisplayFit,unsigned,unsigned);
HRESULT pilot_d3d_interpolate_image(uint64_t,RhiView*);
static int test_visual_depth_mismatch(void)
{
    struct PilotVisualMods options;pilot_visual_mods_default(&options);options.internal_height=1080;
    CHECK(pilot_gfx_visual_mods(&options));
    uint32_t pp[17]={640,480,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};CHECK(!pilot_gfx_create(pp));
    uint32_t lookup;CHECK(!pilot_gfx_texture_create_usage(2,2,1,0x12,1,&lookup));CHECK(!pilot_gfx_targets(lookup,UINT32_MAX));
    uint32_t viewport[]={0,0,2,2,0,0x3f800000u};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    struct {float x,y,z,w;uint32_t color;} triangle[]={{0,0,.5f,1,0xffffffff},{2,0,.5f,1,0xffffffff},{0,2,.5f,1,0xffffffff}};
    CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44)); /* depthless lookup is valid */
    pilot_gfx_set_state(D3DRS_ZENABLE,1);
    pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44);
    fprintf(stderr,"FAIL: incompatible depth raster was accepted\n");return 1;
}
static int test_visual_interpolation(void)
{
    for(unsigned rate=60;rate<=120;rate+=60) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);options.frame_mode=PILOT_FRAMES_INTERPOLATED;options.frame_rate=rate;
        CHECK(pilot_gfx_visual_mods(&options));pilot_gfx_frame_scope(1);
        uint32_t pp[17]={64,48,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};CHECK(!pilot_gfx_create(pp));
        CHECK(!pilot_gfx_clear(3,0xffff0000,1,0) && !pilot_gfx_present());
        CHECK(!pilot_gfx_clear(3,0xff0000ff,1,0) && !pilot_gfx_present());
        struct PilotInterpolationStats info;pilot_gfx_interpolation_stats(&info);
        CHECK(info.sources==2 && info.presents==0 && info.history==2 && info.current>info.previous);
        RhiImageDesc desc={0};desc.type=RHI_IMAGE_2D;desc.width=info.width;desc.height=info.height;desc.depth=desc.mip_levels=desc.samples=1;
        desc.format=RHI_FORMAT_R8G8B8A8_UNORM;desc.usage=RHI_USAGE_DEFAULT;desc.bind=RHI_BIND_RENDER_TARGET|RHI_BIND_SAMPLED;
        RhiImage *output=rhi_image_create(&desc,NULL);CHECK(output);RhiView *view=rhi_view_create(output,RHI_VIEW_RENDER_TARGET,NULL);CHECK(view);
        struct PilotGpuProgress before,after;pilot_gfx_completion_stats(&before);
        uint64_t sample=info.current+(info.current-info.previous)/2;
        CHECK(!pilot_d3d_interpolate_image(sample,view));
        unsigned char *pixels=malloc((size_t)info.width*info.height*4),guest[64*48*4];CHECK(pixels);
        CHECK(!rhi_image_readback(output,0,pixels,info.width*4));
        D3D8DisplayFit fit=d3d8_display_fit(4,3,info.width,info.height);
        for(unsigned y=0;y<info.height;++y)for(unsigned x=0;x<info.width;++x) {
            unsigned char *pixel=pixels+((size_t)y*info.width+x)*4;
            int inside=x>=fit.x && x<fit.x+fit.w && y>=fit.y && y<fit.y+fit.h;
            CHECK(abs((int)pixel[0]-(inside?128:0))<=1 && pixel[1]==0 && abs((int)pixel[2]-(inside?128:0))<=1);
        }
        CHECK(!pilot_gfx_surface_readback(1,guest,sizeof(guest)));
        for(unsigned i=0;i<64*48;++i)CHECK(guest[i*4]==0 && guest[i*4+1]==0 && guest[i*4+2]==255);
        pilot_gfx_completion_stats(&after);CHECK(before.submitted==after.submitted);
        CHECK(!pilot_gfx_interpolate(sample));pilot_gfx_interpolation_stats(&info);CHECK(info.sources==2 && info.presents==1);
        CHECK(!pilot_d3d_interpolate_image(info.current+10000000,view));CHECK(!rhi_image_readback(output,0,pixels,info.width*4));
        unsigned center=((info.height/2)*info.width+info.width/2)*4;
        CHECK(pixels[center]==0 && pixels[center+2]==255); /* stall holds current, never extrapolates */
        pilot_gfx_frame_scope(0);CHECK(pilot_gfx_interpolate(0)==S_FALSE);
        pilot_gfx_frame_scope(2);pilot_gfx_interpolation_stats(&info);CHECK(!info.history);
        CHECK(!pilot_gfx_clear(3,0xff00ff00,1,0) && !pilot_gfx_present());
        CHECK(!pilot_d3d_interpolate_image(0,view) && !rhi_image_readback(output,0,pixels,info.width*4));
        CHECK(pixels[center]==0 && pixels[center+1]==255 && pixels[center+2]==0); /* no cross-scope blend */
        CHECK(!pilot_gfx_reset(pp));pilot_gfx_interpolation_stats(&info);CHECK(!info.history);
        free(pixels);rhi_view_destroy(view);rhi_image_destroy(output);pilot_gfx_close();
    }
    fprintf(stderr,"PASS: actual GPU blended midpoint/stall/cut/reset at 60/120, original front buffer and guest submission count unchanged\n");return 0;
}
static int test_visual_aa(void)
{
    unsigned char images[2][64*64*4];
    for(unsigned aa=0;aa<2;++aa) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);options.antialiasing=aa;
        CHECK(pilot_gfx_visual_mods(&options));
        uint32_t pp[17]={64,64,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};CHECK(!pilot_gfx_create(pp));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
        struct {float x,y,z,w;uint32_t color;} triangle[]={
            {8,8,.5f,1,0xffffffff},{56,8,.5f,1,0xffffffff},{8,56,.5f,1,0xffffffff}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
        unsigned char before[64*64*4],after[64*64*4];CHECK(!pilot_gfx_readback(before,sizeof(before)));
        CHECK(before[(16*64+16)*4]==255);
        RhiImageDesc desc={0};desc.type=RHI_IMAGE_2D;desc.width=desc.height=64;desc.depth=desc.mip_levels=desc.samples=1;
        desc.format=RHI_FORMAT_R8G8B8A8_UNORM;desc.usage=RHI_USAGE_DEFAULT;desc.bind=RHI_BIND_RENDER_TARGET|RHI_BIND_SAMPLED;
        RhiImage *output=rhi_image_create(&desc,NULL);CHECK(output);RhiView *view=rhi_view_create(output,RHI_VIEW_RENDER_TARGET,NULL);CHECK(view);
        D3D8DisplayFit fit={0,0,64,64};
        CHECK(!pilot_d3d_visual_resolve(d3d8_GetSceneView(),64,64,view,fit,64,64));
        CHECK(!rhi_image_readback(output,0,images[aa],64*4));
        CHECK(!pilot_gfx_readback(after,sizeof(after)) && !memcmp(before,after,sizeof(before)));
        if(aa) {
            /* Resume the already-bound guest draw without any donor rebind.
             * A pass that restores outputs but leaks its shaders/layout/
             * blend state fails this real continuation. */
            CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
            ID3D11DeviceContext_Draw(d3d8_GetD3D11Context(),3,0);
            CHECK(!pilot_gfx_readback(after,sizeof(after)) && !memcmp(before,after,sizeof(before)));
        }
        CHECK(!pilot_gfx_present());rhi_view_destroy(view);rhi_image_destroy(output);pilot_gfx_close();
    }
    unsigned changed=0;
    for(unsigned i=0;i<64*64;++i)if(memcmp(images[0]+i*4,images[1]+i*4,3))++changed;
    CHECK(changed>0 && changed<64*64/3);
    CHECK(images[0][(16*64+16)*4]==255 && images[1][(16*64+16)*4]==255);
    CHECK(images[0][(60*64+60)*4]==0 && images[1][(60*64+60)*4]==0);
    fprintf(stderr,"PASS: real AA changes edge pixels, preserves flat areas and guest framebuffer, and presents/recreates\n");return 0;
}
static int visual_target_shape(unsigned width,unsigned height)
{
    ID3D11RenderTargetView *color=NULL;ID3D11DepthStencilView *depth=NULL;
    ID3D11DeviceContext_OMGetRenderTargets(d3d8_GetD3D11Context(),1,&color,&depth);
    CHECK(color && depth);
    ID3D11Resource *c=NULL,*d=NULL;ID3D11RenderTargetView_GetResource(color,&c);ID3D11DepthStencilView_GetResource(depth,&d);
    D3D11_TEXTURE2D_DESC cd,dd;ID3D11Texture2D_GetDesc((ID3D11Texture2D*)c,&cd);ID3D11Texture2D_GetDesc((ID3D11Texture2D*)d,&dd);
    ID3D11Resource_Release(c);ID3D11Resource_Release(d);ID3D11RenderTargetView_Release(color);ID3D11DepthStencilView_Release(depth);
    CHECK(cd.Width==width && cd.Height==height && dd.Width>=width && dd.Height>=height);
    return 0;
}
static int test_visual_mod_resources(void)
{
    uint32_t pp[17]={640,480,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
    for(unsigned pass=0;pass<3;++pass) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);
        options.internal_height=pass==1?2160:1080;options.supersample=pass==2?2:1;
        options.aspect_numerator=16;options.aspect_denominator=9;options.anisotropy=8;
        CHECK(pilot_gfx_visual_mods(&options));CHECK(!pilot_gfx_create(pp));
        CHECK(!pilot_gfx_visual_mods(&options)); /* immutable while device lives */
        uint32_t physical[2],metadata[3];CHECK(pilot_gfx_render_size(physical));
        CHECK(physical[0]==(pass?3840:1920) && physical[1]==(pass?2160:1080));
        CHECK(pilot_gfx_width()==640 && pilot_gfx_height()==480);
        CHECK(!visual_target_shape(physical[0],physical[1]));
        uint32_t viewport[]={0,0,640,480,0,0x3f800000u},queried[6];
        CHECK(!pilot_gfx_viewport(viewport) && !pilot_gfx_get_viewport(queried) && !memcmp(viewport,queried,sizeof(viewport)));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        CHECK(!pilot_gfx_clear(3,0xff0000ff,1,0));
        struct {float x,y,z,w;uint32_t color;} triangle[]={
            {100,100,.5f,1,0xff00ff00},{500,100,.5f,1,0xff00ff00},{100,380,.5f,1,0xff00ff00}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,metadata));
        CHECK(metadata[0]==physical[0] && metadata[1]==physical[1]);
        size_t bytes=(size_t)metadata[0]*metadata[1]*4;unsigned char *pixels=malloc(bytes);CHECK(pixels);
        CHECK(!pilot_gfx_reference_target_readback(0,pixels,(uint32_t)bytes,metadata));
        unsigned row=physical[1]*200/480,last_green=0,first_green=physical[0];
        for(unsigned x=0;x<physical[0];++x)if(pixels[((size_t)row*physical[0]+x)*4+1]==255) {
            if(first_green==physical[0])first_green=x;last_green=x;
        }
        CHECK(first_green>=physical[0]*99/640 && first_green<=physical[0]*101/640);
        CHECK(last_green>physical[0]*350/640 && last_green<physical[0]*360/640);
        if(!pass)CHECK((last_green+1)%3!=0); /* magnifying 640-wide pixels fails */
        free(pixels);
        unsigned char *guest=malloc(640*480*4);CHECK(guest);
        CHECK(!pilot_gfx_surface_readback(0,guest,640*480*4));
        CHECK(guest[(200*640+200)*4+1]==255 && guest[(20*640+20)*4+2]==255);free(guest);
        uint32_t color=0,depth=0,alias=0;
        CHECK(!pilot_gfx_texture_create_usage(320,240,1,0x12,1,&color));
        CHECK(!pilot_gfx_texture_create_usage(320,240,1,0x2e,2,&depth));
        CHECK(!pilot_gfx_targets(color,depth));
        CHECK(!visual_target_shape(physical[0]/2,physical[1]/2));
        uint32_t *input=malloc(320*240*4),*output=malloc(320*240*4);CHECK(input && output);
        for(unsigned i=0;i<320*240;++i)input[i]=0xff123456u^(i&255);
        pilot_gfx_texture_upload(color,0,input,320*240*4);
        CHECK(!pilot_gfx_texture_readback(color,0,output,320*240*4));CHECK(!memcmp(input,output,320*240*4));
        for(unsigned i=0;i<320*240;++i)input[i]=0x12345678u+i;
        pilot_gfx_texture_upload(depth,0,input,320*240*4);
        CHECK(!pilot_gfx_texture_readback(depth,0,output,320*240*4));CHECK(!memcmp(input,output,320*240*4));
        free(input);free(output);
        CHECK(!pilot_gfx_texture_alias(PILOT_GFX_SURFACE,0,0x12,&alias));
        pilot_gfx_texture_destroy(alias);
        CHECK(!pilot_gfx_targets(0,UINT32_MAX));pilot_gfx_texture_destroy(color);pilot_gfx_texture_destroy(depth);
        if(!pass)CHECK(!test_float_blend(pp));
        CHECK(!pilot_gfx_reset(pp));CHECK(!visual_target_shape(physical[0],physical[1]));
        pilot_gfx_close();
    }
    struct PilotVisualMods original;pilot_visual_mods_default(&original);CHECK(pilot_gfx_visual_mods(&original));
    CHECK(!pilot_gfx_create(pp));uint32_t size[2];CHECK(pilot_gfx_render_size(size) && size[0]==640 && size[1]==480);
    pilot_gfx_close();
    fprintf(stderr,"PASS: actual 1080p/2160p/SSAA raster, original guest sizes/readback, scaled color/depth CPU roundtrip, aliases/reset and original restoration\n");
    CHECK(!test_visual_aa());return test_visual_interpolation();
}
#endif
