#ifdef PILOT_D3D_PHOBOS
#include "graphics_visual_state.inc"
static int test_radial_native_companion(void)
{
    unsigned char reference[64*64*4],native[64*64*4];
    for(unsigned scale=1;scale<=2;++scale) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);options.supersample=scale;
        CHECK(pilot_gfx_visual_mods(&options));
        uint32_t pp[17]={64,64,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
        CHECK(!pilot_gfx_create(pp));CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ZENABLE,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        struct {float x,y,z,w;uint32_t color;} edge[]={{8,8,.5f,1,0xffffffff},{24,8,.5f,1,0xffffffff},{8,56,.5f,1,0xffffffff}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,edge,sizeof(edge[0]),0x44));
        uint32_t physical[3];CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==64*scale && physical[1]==64*scale);
        unsigned bytes=physical[0]*physical[1]*4;
        unsigned char *before=malloc(bytes),*after=malloc(bytes);CHECK(before && after);
        CHECK(!pilot_gfx_reference_target_readback(0,before,bytes,physical));
        CHECK(!pilot_gfx_visibility_begin());CHECK(pilot_gfx_radial_blur(.06f));
        CHECK(!pilot_gfx_visibility_end(99));CHECK(!visibility_expect(99,64*64,NULL));
        CHECK(!pilot_gfx_readback(native,sizeof(native)));
        uint32_t blurred[3];CHECK(!pilot_gfx_reference_target_readback(0,after,bytes,blurred));
        CHECK(blurred[0]==physical[0] && blurred[1]==physical[1]);
        unsigned changed=0,not_nearest=0;
        for(unsigned y=0;y<physical[1];++y)for(unsigned x=0;x<physical[0];++x) {
            unsigned at=(y*physical[0]+x)*4;
            changed+=memcmp(before+at,after+at,4)!=0;
            unsigned native_at=((y/scale)*64+x/scale)*4;
            not_nearest+=memcmp(after+at,native+native_at,4)!=0;
        }
        CHECK(changed>16); /* A stale coherent companion must fail this. */
        if(scale==1)memcpy(reference,native,sizeof(reference));
        else {CHECK(!memcmp(reference,native,sizeof(reference)));CHECK(not_nearest>16);}
        free(before);free(after);pilot_gfx_close();
    }
    fprintf(stderr,"PASS: radial native byte identity, real enhanced edge changes and native-only visibility count\n");return 0;
}
static int test_radial_blur(void)
{
    /* The backend's compile definition is private to its library. Reuse the
     * existing explicit runtime qualification control for this paired check. */
    const char *planes=getenv("XLLE_PILOT_NATIVE_RENDER_PLANES");
    if(planes && !strcmp(planes,"1"))CHECK(!test_radial_native_companion());
    for(unsigned variant=0;variant<3;++variant) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);
        options.internal_height=variant==1?1080:variant==2?2160:0;
        options.supersample=variant==1?2:1;
        options.aspect_numerator=21;options.aspect_denominator=9;
        CHECK(pilot_gfx_visual_mods(&options));
        uint32_t pp[17]={640,480,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
        CHECK(!pilot_gfx_create(pp));CHECK(!pilot_gfx_clear(3,0xff204060,.375f,93));
        CHECK(!pilot_gfx_radial_blur(-1) && !pilot_gfx_radial_blur(NAN) && !pilot_gfx_radial_blur(.1f));
        struct PilotVisualState before,after;pilot_visual_state_save(&before);
        CHECK(pilot_gfx_radial_blur(.032f));pilot_visual_state_save(&after);
        CHECK(!memcmp(&before,&after,sizeof(before)));pilot_visual_state_restore(&after);pilot_visual_state_restore(&before);
        unsigned char *pixels=malloc(640*480*4);CHECK(pixels && !pilot_gfx_readback(pixels,640*480*4));
        for(unsigned i=0;i<640*480;++i)CHECK(pixels[i*4]==32 && pixels[i*4+1]==64 && pixels[i*4+2]==96);
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_ZENABLE,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        struct {float x,y,z,w;uint32_t color;} triangle[]={{290,210,.5f,1,0xffff0000},{350,210,.5f,1,0xffff0000},{320,270,.5f,1,0xffff0000}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
        CHECK(!pilot_gfx_readback(pixels,640*480*4));unsigned center=(230*640+320)*4;
        CHECK(pixels[center]==255 && !pixels[center+1] && !pixels[center+2]);
        CHECK(pilot_gfx_radial_blur(.032f));CHECK(!pilot_gfx_readback(pixels,640*480*4));
        CHECK(pixels[center]==255 && !pixels[center+1] && !pixels[center+2]);
        /* An original HUD draw following the pass must keep its own state. */
        for(unsigned i=0;i<3;++i)triangle[i].color=0xff00ff00;
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
        CHECK(!pilot_gfx_readback(pixels,640*480*4));CHECK(!pixels[center] && pixels[center+1]==255 && !pixels[center+2]);free(pixels);
        ID3D11Device *device=d3d8_GetD3D11Device();ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
        D3D11_QUERY_DESC desc={D3D11_QUERY_TIMESTAMP_DISJOINT,0};ID3D11Query *clock,*begin,*end;
        CHECK(SUCCEEDED(ID3D11Device_CreateQuery(device,&desc,&clock)));desc.Query=D3D11_QUERY_TIMESTAMP;
        CHECK(SUCCEEDED(ID3D11Device_CreateQuery(device,&desc,&begin)) && SUCCEEDED(ID3D11Device_CreateQuery(device,&desc,&end)));
        ID3D11DeviceContext_Begin(ctx,(ID3D11Asynchronous*)clock);ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)begin);
        for(unsigned i=0;i<12;++i)CHECK(pilot_gfx_radial_blur(.032f));
        ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)end);ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)clock);ID3D11DeviceContext_Flush(ctx);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing;uint64_t t0,t1;unsigned waited=0;
        while(ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)clock,&timing,sizeof(timing),0)==S_FALSE && waited++<5000)Sleep(1);
        CHECK(waited<5000 && !timing.Disjoint && timing.Frequency);
        CHECK(ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)begin,&t0,sizeof(t0),0)==S_OK && ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)end,&t1,sizeof(t1),0)==S_OK);
        fprintf(stderr,"[RADIAL GPU] variant=%u render=%ux%u ms=%.3f\n",variant,pilot_gfx_width(),pilot_gfx_height(),(t1-t0)*1000.0/timing.Frequency/12);
        ID3D11Query_Release(clock);ID3D11Query_Release(begin);ID3D11Query_Release(end);pilot_gfx_close();
    }
    fprintf(stderr,"PASS: wide native radial pass, uniform color, sharp center, pipeline restoration and subsequent HUD draw\n");return 0;
}
#endif
