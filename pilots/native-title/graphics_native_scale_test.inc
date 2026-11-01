#ifdef PILOT_D3D_PHOBOS
static int native_scale_save(unsigned scale,unsigned kind,const void *bytes,size_t size)
{
    const char *root=getenv("XLLE_NATIVE_SCALE_EVIDENCE");if(!root)return 0;
    char path[1024];int count=snprintf(path,sizeof(path),"%s/scale-%u-kind-%u.bin",root,scale,kind);
    CHECK(count>0 && (size_t)count<sizeof(path));FILE *file=NULL;
#ifdef _MSC_VER
    CHECK(!fopen_s(&file,path,"wb") && file);
#else
    file=fopen(path,"wb");CHECK(file);
#endif
    CHECK(fwrite(bytes,1,size,file)==size);CHECK(!fclose(file));return 0;
}
static int test_native_scale_coherence(void)
{
    unsigned char reference[3][64*64*4],actual[3][64*64*4];unsigned differences=0;
    unsigned char retained_alias[64*64*4],retained_check[64*64*4],reset_reference[80*64*4],reset_actual[80*64*4];
    unsigned char query_reference[64*64*4],query_actual[64*64*4];
    const unsigned scales[]={1,2,3,4,8,16,32,64};
    for(unsigned leg=0;leg<sizeof(scales)/sizeof(*scales);++leg) {
        unsigned scale=scales[leg];
        struct PilotVisualMods options;pilot_visual_mods_default(&options);options.supersample=scale;
        CHECK(pilot_gfx_visual_mods(&options));
        uint32_t pp[17]={64,64,D3DFMT_X8R8G8B8,1,0x11,1,0,0,1,D3DFMT_D24S8,0,60,1};
        CHECK(!pilot_gfx_create(pp));uint32_t color=0,depth=0,alias=0;
        CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x12,1,&color));
        CHECK(!pilot_gfx_texture_create_usage(64,64,1,0x2e,2,&depth));
        CHECK(!pilot_gfx_targets(color,depth));uint32_t viewport[]={0,0,64,64,0,0x3f800000u};CHECK(!pilot_gfx_viewport(viewport));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_LIGHTING,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        pilot_gfx_set_state(D3DRS_ZENABLE,1);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);
        pilot_gfx_set_state(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,1);
        pilot_gfx_set_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);pilot_gfx_set_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        pilot_gfx_set_state(D3DRS_STENCILENABLE,1);pilot_gfx_set_state(D3DRS_STENCILFUNC,D3DCMP_ALWAYS);
        pilot_gfx_set_state(D3DRS_STENCILFAIL,1);pilot_gfx_set_state(D3DRS_STENCILZFAIL,1);
        pilot_gfx_set_state(D3DRS_STENCILPASS,3);pilot_gfx_set_state(D3DRS_STENCILREF,7);
        /* Blinx can begin color drawing without a full color clear. The
         * companion must already be seeded before that first native draw. */
        CHECK(!pilot_gfx_clear(6,0,1,0));
        struct {float x,y,z,w;uint32_t color;} no_color_clear[]={
            {0,0,.5f,1,0xffffffff},{48,0,.5f,1,0xffffffff},{0,48,.5f,1,0xffffffff}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,no_color_clear,sizeof(no_color_clear[0]),0x44));
        uint32_t seeded[3];CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,seeded));
        CHECK(seeded[0]==64*scale && seeded[1]==64*scale);
        CHECK(!pilot_gfx_clear(7,0xff101820,1,0));
        struct {float x,y,z,w;uint32_t color;} back[]={
            {-1.125f,6.375f,.7f,1,0xff2030c0},{61.375f,2.625f,.7f,1,0xffa0d030},{11.625f,61.125f,.7f,1,0xffd03090}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,back,sizeof(back[0]),0x44));
        pilot_gfx_set_state(D3DRS_STENCILREF,11);
        struct {float x,y,z,w;uint32_t color;} front[]={
            {8.375f,9.125f,.22f,1,0x8040f020},{55.625f,18.375f,.63f,1,0x80f03040},{27.125f,56.625f,.41f,1,0x802040f0}};
        CHECK(!pilot_gfx_draw_up(D3DPT_TRIANGLELIST,1,front,sizeof(front[0]),0x44));
        uint32_t physical[3];CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK((scale==1 && physical[0]==64 && physical[1]==64) || (scale>1 && physical[0]>64 && physical[1]>64));
        CHECK(!pilot_gfx_texture_alias(PILOT_GFX_TEXTURE,color,0x12,&alias));
        CHECK(!pilot_gfx_texture_readback(color,0,actual[0],sizeof(actual[0])));
        CHECK(!pilot_gfx_texture_readback(depth,0,actual[1],sizeof(actual[1])));
        CHECK(!pilot_gfx_texture_readback(alias,0,actual[2],sizeof(actual[2])));
        CHECK(!memcmp(actual[0],actual[2],sizeof(actual[0])));
        uint64_t enhanced_bytes=(uint64_t)physical[0]*physical[1]*4;
        CHECK(enhanced_bytes<=64*1024*1024);
        unsigned char *enhanced=malloc((size_t)enhanced_bytes);CHECK(enhanced);
        CHECK(!pilot_gfx_reference_target_readback(0,enhanced,(uint32_t)enhanced_bytes,physical));
        unsigned covered=0,not_nearest=0;
        for(unsigned y=0;y<physical[1];++y)for(unsigned x=0;x<physical[0];++x) {
            unsigned char *pixel=enhanced+((size_t)y*physical[0]+x)*4;
            if(memcmp(pixel,enhanced,4))++covered;
            unsigned sx=(unsigned)(((uint64_t)x*2+1)*64/(physical[0]*2ull));
            unsigned sy=(unsigned)(((uint64_t)y*2+1)*64/(physical[1]*2ull));
            if(memcmp(pixel,actual[0]+(sy*64+sx)*4,4))++not_nearest;
        }
        CHECK(covered>256);
        if(scale>1)CHECK(not_nearest>256); /* Real HD draw, not a scaled native image. */
        if(scale<=16)CHECK(!native_scale_save(scale,3,enhanced,(size_t)enhanced_bytes));
        fprintf(stderr,"ENHANCED SCALE: axis=%u physical=%ux%u covered=%u non_nearest=%u\n",scale,physical[0],physical[1],covered,not_nearest);
        free(enhanced);
        for(unsigned kind=0;kind<2;++kind) {
            unsigned varied=0;
            for(unsigned i=4;i<sizeof(actual[kind]);++i)if(actual[kind][i]!=actual[kind][i%4])++varied;
            CHECK(varied>256); /* Real color/depth coverage, not an empty clear. */
        }
        if(scale==1)memcpy(reference,actual,sizeof(reference));
        unsigned mismatch[3]={0};
        for(unsigned kind=0;kind<3;++kind) {
            CHECK(!native_scale_save(scale,kind,actual[kind],sizeof(actual[kind])));
            for(unsigned i=0;i<sizeof(actual[kind]);++i)if(actual[kind][i]!=reference[kind][i])++mismatch[kind];
            differences+=mismatch[kind];
        }
        fprintf(stderr,"NATIVE SCALE: axis=%u physical=%ux%u color_bytes=%u depth_bytes=%u alias_bytes=%u\n",scale,physical[0],physical[1],mismatch[0],mismatch[1],mismatch[2]);
        /* Exact child-query subtraction keeps the guest count native while
         * the enhanced pass retains color, depth and actual stencil writes. */
        pilot_gfx_set_state(D3DRS_STENCILENABLE,1);
        CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
        CHECK(!pilot_gfx_visibility_begin());CHECK(!visibility_quad(0,0,20,20,.2f));
        CHECK(!pilot_gfx_visibility_end(29));CHECK(!visibility_expect(29,400,NULL));
        CHECK(!pilot_gfx_texture_readback(depth,0,query_actual,sizeof(query_actual)));
        if(scale==1)memcpy(query_reference,query_actual,sizeof(query_reference));
        unsigned query_mismatch=0;
        for(unsigned i=0;i<sizeof(query_actual);++i)if(query_actual[i]!=query_reference[i])++query_mismatch;
        differences+=query_mismatch;CHECK(!native_scale_save(scale,5,query_actual,sizeof(query_actual)));
        fprintf(stderr,"QUERY NATIVE SCALE: axis=%u samples=400 depth_stencil_bytes=%u\n",scale,query_mismatch);
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==64*scale && physical[1]==64*scale);
        fprintf(stderr,"EXCLUDED QUERY SCALE: axis=%u samples=400 physical=%ux%u\n",scale,physical[0],physical[1]);
        CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
        CHECK(!visibility_quad(0,0,20,20,.2f));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==64*scale && physical[1]==64*scale);
        /* A native visibility-only draw does not change either image. Its
         * exact400 samples need no HD replay or child exclusion allocation. */
        pilot_gfx_set_state(D3DRS_STENCILENABLE,0);
        pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,0);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
        CHECK(!pilot_gfx_visibility_begin());CHECK(!visibility_quad(0,0,20,20,.2f));
        CHECK(!pilot_gfx_visibility_end(30));CHECK(!visibility_expect(30,400,NULL));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==64*scale && physical[1]==64*scale);
        fprintf(stderr,"READONLY QUERY SCALE: axis=%u samples=400 physical=%ux%u\n",scale,physical[0],physical[1]);
        pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,1);
        /* The map and canonical image survive destruction of their original
         * handle and a real device Reset while the logical alias remains. */
        CHECK(!pilot_gfx_texture_readback(alias,0,retained_alias,sizeof(retained_alias)));
        CHECK(!pilot_gfx_targets(0,UINT32_MAX));pilot_gfx_texture_destroy(color);
        CHECK(!pilot_gfx_texture_readback(alias,0,retained_check,sizeof(retained_check)));
        CHECK(!memcmp(retained_alias,retained_check,sizeof(retained_alias)));
        pp[0]=80; /* A real guest-size change; the retained alias remains64x64. */
        CHECK(!visual_reset_preserves_window(pp));
        CHECK(!pilot_gfx_texture_readback(alias,0,retained_check,sizeof(retained_check)));
        CHECK(!memcmp(retained_alias,retained_check,sizeof(retained_alias)));
        /* Sample the old native/HD alias into the new main target. All state
         * is explicit; this is authored geometry and existing FVF lowering,
         * not a CPU resample or an assumed RGBA/BGRA interpretation. */
        CHECK(!pilot_gfx_targets(0,UINT32_MAX));
        viewport[2]=80;viewport[3]=64;CHECK(!pilot_gfx_viewport(viewport));
        pilot_gfx_pixel_fixed();
        pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
        pilot_gfx_set_state(D3DRS_STENCILENABLE,0);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
        pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_FOGENABLE,0);
        pilot_gfx_set_state(D3DRS_LIGHTING,0);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
        pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);pilot_gfx_set_state(D3DRS_SHADEMODE,2);
        for(unsigned stage=0;stage<4;++stage) {
            pilot_gfx_texture_bind(stage,0);
            pilot_gfx_set_texture_state(stage,D3DTSS_COLOROP,D3DTOP_DISABLE);
            pilot_gfx_set_texture_state(stage,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
            pilot_gfx_set_texture_state(stage,D3DTSS_TEXCOORDINDEX,stage);
            pilot_gfx_set_texture_state(stage,24,0); /* D3D8 TEXTURETRANSFORMFLAGS: DISABLE. */
        }
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        pilot_gfx_set_texture_state(0,D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP);
        pilot_gfx_set_texture_state(0,D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP);
        pilot_gfx_set_texture_state(0,D3DTSS_MINFILTER,D3DTEXF_LINEAR);
        pilot_gfx_set_texture_state(0,D3DTSS_MAGFILTER,D3DTEXF_LINEAR);
        pilot_gfx_set_texture_state(0,D3DTSS_MIPFILTER,D3DTEXF_NONE);
        pilot_gfx_set_texture_state(0,D3DTSS_MAXMIPLEVEL,0);
        CHECK(!pilot_gfx_clear(3,0xff182838,1,0));pilot_gfx_texture_bind(0,alias);
        CHECK(!pilot_gfx_fvf_prepare(0x104,.53125f,.53125f));
        struct {float x,y,z,w,u,v;} retained_quad[]={
            {-.53125f,-.53125f,0,1,0,0},{79.46875f,-.53125f,0,1,64,0},
            {-.53125f,63.46875f,0,1,0,64},{79.46875f,63.46875f,0,1,64,64}};
        CHECK(!pilot_gfx_draw_memory(6,4,retained_quad,sizeof(retained_quad[0])));
        CHECK(!pilot_gfx_readback(reset_actual,sizeof(reset_actual)));
        unsigned reset_varied=0,reset_mismatch=0;
        for(unsigned i=4;i<sizeof(reset_actual);++i)if(reset_actual[i]!=reset_actual[i%4])++reset_varied;
        CHECK(reset_varied>256);
        if(scale==1)memcpy(reset_reference,reset_actual,sizeof(reset_reference));
        for(unsigned i=0;i<sizeof(reset_actual);++i)if(reset_actual[i]!=reset_reference[i])++reset_mismatch;
        differences+=reset_mismatch;
        CHECK(!native_scale_save(scale,4,reset_actual,sizeof(reset_actual)));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==80*scale && physical[1]==64*scale); /* No native fallback can pass HD. */
        uint64_t reset_bytes=(uint64_t)physical[0]*physical[1]*4;CHECK(reset_bytes<=80ull*1024*1024);
        unsigned char *reset_enhanced=malloc((size_t)reset_bytes);CHECK(reset_enhanced);
        CHECK(!pilot_gfx_reference_target_readback(0,reset_enhanced,(uint32_t)reset_bytes,physical));
        unsigned reset_covered=0;
        for(uint64_t i=4;i<reset_bytes;i+=4)if(memcmp(reset_enhanced+i,reset_enhanced,4))++reset_covered;
        CHECK(reset_covered>256);free(reset_enhanced);
        /* visual_reset_preserves_window called SetWindowPos, but only real
         * Present exercises the DXGI resize path for that user-sized client. */
        HWND reset_window=NULL;EnumWindows(visual_find_window,(LPARAM)&reset_window);CHECK(reset_window);
        RECT window_before,window_after,client;
        CHECK(GetWindowRect(reset_window,&window_before) && GetClientRect(reset_window,&client));
        CHECK(!pilot_gfx_present());
        uint32_t presented[2];CHECK(pilot_gfx_render_size(presented));
        CHECK(presented[0]==80*scale && presented[1]==64*scale);
        CHECK(GetWindowRect(reset_window,&window_after) && !memcmp(&window_before,&window_after,sizeof(window_before)));
        DXGI_SWAP_CHAIN_DESC swapchain;CHECK(SUCCEEDED(IDXGISwapChain_GetDesc(d3d8_GetSwapChain(),&swapchain)));
        CHECK(swapchain.BufferDesc.Width==(UINT)(client.right-client.left) && swapchain.BufferDesc.Height==(UINT)(client.bottom-client.top));
        fprintf(stderr,"RESET SCALE: axis=%u guest=80x64 retained_alias=64x64 physical=%ux%u native_bytes=%u native_varied=%u enhanced_covered=%u swapchain=%ux%u\n",
            scale,physical[0],physical[1],reset_mismatch,reset_varied,reset_covered,swapchain.BufferDesc.Width,swapchain.BufferDesc.Height);
        pilot_gfx_texture_bind(0,0);
        pilot_gfx_texture_destroy(alias);pilot_gfx_texture_destroy(depth);pilot_gfx_close();
    }
    fprintf(stderr,"%s: native fractional-edge/blend/depth/stencil and target-alias byte identity across1/2/3/4/8/16/32/64x, real HD content, exact native queries and retained-alias sampling after80x64 Reset/real Present\n",differences?"FAIL":"PASS");
    return differences?1:0;
}
#endif
