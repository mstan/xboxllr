static int test_visual_default_depth(void)
{
    for(unsigned pass=0;pass<3;++pass) {
        struct PilotVisualMods options;pilot_visual_mods_default(&options);
        options.internal_height=pass==1?2160:1080;options.supersample=pass==2?2:1;
        options.aspect_numerator=16;options.aspect_denominator=9;CHECK(pilot_gfx_visual_mods(&options));
        uint32_t pp[17]={640,480,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};CHECK(!pilot_gfx_create(pp));
        uint32_t lookup=0;CHECK(!pilot_gfx_texture_create_usage(128,128,1,0x12,1,&lookup));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        pilot_gfx_set_state(D3DRS_ZENABLE,1);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);pilot_gfx_set_state(D3DRS_ZFUNC,D3DCMP_LESS);
        pilot_gfx_set_state(D3DRS_STENCILENABLE,1);pilot_gfx_set_state(D3DRS_STENCILFUNC,D3DCMP_ALWAYS);
        pilot_gfx_set_state(D3DRS_STENCILFAIL,1);pilot_gfx_set_state(D3DRS_STENCILZFAIL,1);pilot_gfx_set_state(D3DRS_STENCILPASS,3);
        pilot_gfx_set_state(D3DRS_STENCILMASK,255);pilot_gfx_set_state(D3DRS_STENCILWRITEMASK,255);
        pilot_gfx_set_state(D3DRS_STENCILREF,0xa5);CHECK(!pilot_gfx_clear(7,0xff000000,1,0x96));
        struct {float x,y,z,w;uint32_t color;} triangle[]={{10,10,.25f,1,0xff00ff00},{300,10,.25f,1,0xff00ff00},{10,300,.25f,1,0xff00ff00}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
        for(unsigned transition=0;transition<4;++transition) {
            unsigned native=!(transition&1);CHECK(!pilot_gfx_targets(native?lookup:0,UINT32_MAX));
            uint32_t viewport[]={0,0,native?128:640,native?128:480,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
            CHECK(!pilot_gfx_clear(1,0xff000000,0,0));
            /* Far fragments must fail against retained main-scene depth on
             * the native effect, and against effect depth back in the scene. */
            for(unsigned i=0;i<3;++i){triangle[i].z=.75f;triangle[i].color=0xffff0000;}
            CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
            unsigned char *color=malloc(640*480*4);CHECK(color);
            if(native) {
                uint32_t bgra[128*128];CHECK(!pilot_gfx_texture_readback(lookup,0,bgra,sizeof(bgra)));CHECK(bgra[30*128+30]==0xff000000);
            } else {CHECK(!pilot_gfx_readback(color,640*480*4));CHECK(color[(30*640+30)*4]==0);}
            free(color);
            if(!transition) {
                uint32_t shape[3],*inherited=malloc(640*480*4);CHECK(inherited);
                CHECK(!pilot_gfx_reference_target_readback(1,inherited,640*480*4,shape));
                CHECK(shape[0]==640 && shape[1]==480 && inherited[30*640+30]>>24==0xa5);
                free(inherited);
                pilot_gfx_set_state(D3DRS_STENCILREF,0x3c);
                for(unsigned i=0;i<3;++i){triangle[i].z=.125f;triangle[i].color=0xff00ff00;}
                CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,triangle,sizeof(triangle[0]),0x44));
            }
            uint32_t shape[3];CHECK(!pilot_gfx_reference_target_readback(1,NULL,0,shape));
            CHECK((native && shape[0]==640 && shape[1]==480) || !native);
            uint32_t *depth=malloc((size_t)shape[0]*shape[1]*4);CHECK(depth);
            CHECK(!pilot_gfx_reference_target_readback(1,depth,shape[0]*shape[1]*4,shape));
            unsigned x=(unsigned)((30.5*shape[0])/640),y=(unsigned)((30.5*shape[1])/480);
            CHECK(depth[y*shape[0]+x]>>24==0x3c && abs((int)(depth[y*shape[0]+x]&0xffffff)-0x200000)<=1);
            x=(unsigned)((500.5*shape[0])/640);y=(unsigned)((400.5*shape[1])/480);
            CHECK(depth[y*shape[0]+x]==0x96ffffff);free(depth);
        }
        /* Timestamp only warmed GPU migration. No readback or guest draw is
         * inside the interval; include both directions used by a boost frame. */
        ID3D11Device *device=d3d8_GetD3D11Device();ID3D11DeviceContext *ctx=d3d8_GetD3D11Context();
        ID3D11Query *clock=NULL,*begin=NULL,*end=NULL;D3D11_QUERY_DESC query={D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        CHECK(!ID3D11Device_CreateQuery(device,&query,&clock));query.Query=D3D11_QUERY_TIMESTAMP;
        CHECK(!ID3D11Device_CreateQuery(device,&query,&begin));CHECK(!ID3D11Device_CreateQuery(device,&query,&end));
        ID3D11DeviceContext_Begin(ctx,(ID3D11Asynchronous*)clock);ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)begin);
        for(unsigned repeat=0;repeat<20;++repeat){CHECK(!pilot_gfx_targets(lookup,UINT32_MAX));CHECK(!pilot_gfx_targets(0,UINT32_MAX));}
        ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)end);ID3D11DeviceContext_End(ctx,(ID3D11Asynchronous*)clock);
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing={0};UINT64 start=0,finish=0;ULONGLONG deadline=GetTickCount64()+10000;HRESULT hr;
        while((hr=ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)clock,&timing,sizeof(timing),0))==S_FALSE && GetTickCount64()<deadline)Sleep(1);
        CHECK(hr==S_OK && !timing.Disjoint && timing.Frequency);
        CHECK(!ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)begin,&start,sizeof(start),0));
        CHECK(!ID3D11DeviceContext_GetData(ctx,(ID3D11Asynchronous*)end,&finish,sizeof(finish),0));
        fprintf(stderr,"DEPTH GRID GPU: pass=%u twenty warmed roundtrips, mean %.3f ms/roundtrip\n",pass,(finish-start)*1000.0/timing.Frequency/20);
        ID3D11Query_Release(clock);ID3D11Query_Release(begin);ID3D11Query_Release(end);
        CHECK(!pilot_gfx_targets(lookup,UINT32_MAX));
        CHECK(!visual_reset_preserves_window(pp));pilot_gfx_texture_destroy(lookup);
        pilot_gfx_close();
    }
    fprintf(stderr,"PASS: 128x128 boost effect borrows default depth at 1080p/2160p/SSAA2, repeated GPU grid migration preserves real Z occlusion and all stencil bits, reset/close\n");
    return 0;
}
