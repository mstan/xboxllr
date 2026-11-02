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
    unsigned char version_reference[80*64*4],version_actual[80*64*4];
    unsigned char constant_reference[80*64*4],constant_actual[80*64*4];
    const unsigned scales[]={1,2,3,4,8};
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
        /* The first completed child is now reusable, while report29 still
         * owns its cached native count. A different second count catches
         * premature reuse or stale child results without a test-only API. */
        CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
        CHECK(!pilot_gfx_visibility_begin());CHECK(!visibility_quad(0,0,40,20,.2f));
        /* Force queued HD execution while the native counter is still open.
         * The whole command list must be excluded, not just immediate draws. */
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==64*scale && physical[1]==64*scale);
        CHECK(!pilot_gfx_visibility_end(31));CHECK(!visibility_expect(31,800,NULL));
        CHECK(!visibility_expect(29,400,NULL));CHECK(!visibility_expect(31,800,NULL));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==64*scale && physical[1]==64*scale);
        fprintf(stderr,"REUSED QUERY SCALE: axis=%u cached=400 samples=800 physical=%ux%u\n",scale,physical[0],physical[1]);
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
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==80*scale && physical[1]==64*scale);
        uint64_t indexed_bytes=(uint64_t)physical[0]*physical[1]*4;
        CHECK(indexed_bytes<=80ull*1024*1024);
        unsigned char *indexed_before=malloc((size_t)indexed_bytes);CHECK(indexed_before);
        CHECK(!pilot_gfx_reference_target_readback(0,indexed_before,(uint32_t)indexed_bytes,physical));
        /* The same retained texture also crosses the real indexed fan path.
         * Its prepared IA snapshot must preserve index/base/stream state. */
        struct {float x,y,z,w,u,v;} retained_fan[4];
        memcpy(&retained_fan[0],&retained_quad[0],sizeof(retained_fan[0]));
        memcpy(&retained_fan[1],&retained_quad[1],sizeof(retained_fan[0]));
        memcpy(&retained_fan[2],&retained_quad[3],sizeof(retained_fan[0]));
        memcpy(&retained_fan[3],&retained_quad[2],sizeof(retained_fan[0]));
        /* A distinct inset, reflected patch makes a missing HD indexed draw
         * observable even though the preceding quad already covered the RT. */
        retained_fan[0].x=retained_fan[3].x=7.46875f;
        retained_fan[1].x=retained_fan[2].x=47.46875f;
        retained_fan[0].y=retained_fan[1].y=11.46875f;
        retained_fan[2].y=retained_fan[3].y=51.46875f;
        for(unsigned i=0;i<4;++i)retained_fan[i].u=64-retained_fan[i].u;
        CHECK(!pilot_gfx_draw_memory(7,4,retained_fan,sizeof(retained_fan[0])));
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
        unsigned reset_covered=0,indexed_changed=0;
        CHECK(reset_bytes==indexed_bytes);
        for(uint64_t i=4;i<reset_bytes;i+=4) {
            if(memcmp(reset_enhanced+i,reset_enhanced,4))++reset_covered;
            if(memcmp(reset_enhanced+i,indexed_before+i,4))++indexed_changed;
        }
        CHECK(reset_covered>256 && indexed_changed>128);
        fprintf(stderr,"INDEXED SCALE: axis=%u changed_enhanced_pixels=%u\n",scale,indexed_changed);
        free(indexed_before);free(reset_enhanced);
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
        /* Retained resident vertices must preserve old draws across both
         * full and range writes, including writes inside a native query. */
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_DISABLE);
        pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_DISABLE);
        pilot_gfx_pixel_fixed();CHECK(!pilot_gfx_fvf_prepare(0x44,.53125f,.53125f));
        struct {float x,y,z,w;uint32_t color;} owned_vertices[8];
        /* The untouched tail must survive the later partial writer's COW.
         * Native and HD consumers draw it only AFTER that partial update. */
        memset(owned_vertices,0,sizeof(owned_vertices));
        for(unsigned v=0;v<4;++v) {
            owned_vertices[v+4].x=(float)(2+((v&1)?20:0));
            owned_vertices[v+4].y=(float)(40+((v&2)?20:0));
            owned_vertices[v+4].z=.2f;owned_vertices[v+4].w=1;
            owned_vertices[v+4].color=0xffffff00u;
        }
        uint32_t owned=0;CHECK(!pilot_gfx_vertex_create(sizeof(owned_vertices),&owned));
        pilot_gfx_vertex_bind(0,owned,sizeof(owned_vertices[0]));
        CHECK(!pilot_gfx_clear(3,0xff000000,1,0));
        const unsigned version_x[]={2,38,44},version_y[]={2,30,4};
        const uint32_t version_color[]={0xffff0000u,0xff00ff00u,0xff0000ffu};
        CHECK(!pilot_gfx_visibility_begin());
        for(unsigned version=0;version<3;++version) {
            for(unsigned v=0;v<4;++v) {
                owned_vertices[v].x=(float)(version_x[version]+((v&1)?20:0));
                owned_vertices[v].y=(float)(version_y[version]+((v&2)?20:0));
                owned_vertices[v].z=.2f;owned_vertices[v].w=1;owned_vertices[v].color=version_color[version];
            }
            if(version<2)pilot_gfx_vertex_upload(owned,owned_vertices,sizeof(owned_vertices));
            else pilot_gfx_vertex_upload_range(owned,owned_vertices,0,4*sizeof(owned_vertices[0]));
            CHECK(!pilot_gfx_draw_vertices(6,0,4));
        }
        CHECK(!pilot_gfx_draw_vertices(6,4,4));
        CHECK(!pilot_gfx_visibility_end(32));CHECK(!visibility_expect(32,1600,NULL));
        CHECK(!pilot_gfx_readback(version_actual,sizeof(version_actual)));
        if(scale==1)memcpy(version_reference,version_actual,sizeof(version_reference));
        CHECK(!memcmp(version_reference,version_actual,sizeof(version_actual)));
        CHECK(!native_scale_save(scale,6,version_actual,sizeof(version_actual)));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==80*scale && physical[1]==64*scale);
        uint64_t version_bytes=(uint64_t)physical[0]*physical[1]*4;
        CHECK(version_bytes<=80ull*1024*1024);
        unsigned char *version_hd=malloc((size_t)version_bytes);CHECK(version_hd);
        CHECK(!pilot_gfx_reference_target_readback(0,version_hd,(uint32_t)version_bytes,physical));
        for(unsigned version=0;version<3;++version) {
            unsigned x=(version_x[version]+5)*scale+scale/2,y=(version_y[version]+5)*scale+scale/2;
            const unsigned char *pixel=version_hd+((size_t)y*physical[0]+x)*4;
            CHECK(pixel[0]==(version==0?255:0) && pixel[1]==(version==1?255:0) && pixel[2]==(version==2?255:0));
        }
        {
            const unsigned char *untouched=version_hd+((size_t)(45*scale+scale/2)*physical[0]+7*scale+scale/2)*4;
            CHECK(untouched[0]==255 && untouched[1]==255 && untouched[2]==0);
        }
        free(version_hd);pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(owned);
        fprintf(stderr,"VERTEX VERSIONS SCALE: axis=%u native_bytes=0 red_green_blue=3 untouched_yellow=1 samples=1600\n",scale);
        /* Immutable geometry, three writes of the SAME programmable constant
         * buffer. Neither vertex uploads nor target switches can accidentally
         * serialize the retained jobs between those constant versions. */
        const uint32_t constant_program[]={0,0x0020001b,0x08000000,0x0000f800,
            0,0x0020001b|(96u<<13),0x0c000000,0x0000f819};
        struct PilotVertexElement constant_elements[16]={0};
        constant_elements[0]=(struct PilotVertexElement){0x42,0,0};
        uint32_t constant_shader=0,constant_vertices=0;float constant_positions[24][4];
        for(unsigned version=0;version<3;++version)for(unsigned strip=0;strip<2;++strip)for(unsigned v=0;v<4;++v) {
            float *position=constant_positions[version*8+strip*4+v];
            position[0]=(float)(version_x[version]+strip*12+((v&1)?8:0));
            position[1]=(float)(version_y[version]+((v&2)?20:0));position[2]=.2f;position[3]=1;
        }
        CHECK(!pilot_gfx_shader_create(constant_program,2,constant_elements,&constant_shader));
        CHECK(!pilot_gfx_vertex_create(sizeof(constant_positions),&constant_vertices));
        pilot_gfx_vertex_upload(constant_vertices,constant_positions,sizeof(constant_positions));
        pilot_gfx_vertex_bind(0,constant_vertices,sizeof(constant_positions[0]));
        CHECK(!pilot_gfx_shader_bind(constant_shader));
        const float constant_scale[]={2.f/80,-2.f/64,1,1},constant_offset[]={-1,1,0,0};
        CHECK(!pilot_gfx_shader_raster(constant_scale,constant_offset));
        pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
        pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);pilot_gfx_set_state(D3DRS_STENCILENABLE,0);
        pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
        pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
        pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));CHECK(!pilot_gfx_visibility_begin());
        pilot_gfx_set_state(D3DRS_SHADEMODE,2);pilot_gfx_batch_allow(1);
        for(unsigned version=0;version<3;++version) {
            const float color[]={version==0?1.f:0.f,version==1?1.f:0.f,version==2?1.f:0.f,1};
            pilot_gfx_shader_constants(96,1,color);CHECK(!pilot_gfx_shader_flush());
            CHECK(!pilot_gfx_draw_vertices(6,version*8,4));
            CHECK(!pilot_gfx_draw_vertices(6,version*8+4,4));
        }
        /* Exercise replay while the parent query is open, not merely after
         * End has already made exclusion unnecessary. */
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(!pilot_gfx_visibility_end(33));CHECK(!visibility_expect(33,960,NULL));
        pilot_gfx_batch_allow(0);
        CHECK(!pilot_gfx_readback(constant_actual,sizeof(constant_actual)));
        if(scale==1)memcpy(constant_reference,constant_actual,sizeof(constant_reference));
        CHECK(!memcmp(constant_reference,constant_actual,sizeof(constant_actual)));
        CHECK(!native_scale_save(scale,7,constant_actual,sizeof(constant_actual)));
        CHECK(!pilot_gfx_reference_target_readback(0,NULL,0,physical));
        CHECK(physical[0]==80*scale && physical[1]==64*scale);
        unsigned char *constant_hd=malloc((size_t)version_bytes);CHECK(constant_hd);
        CHECK(!pilot_gfx_reference_target_readback(0,constant_hd,(uint32_t)version_bytes,physical));
        for(unsigned version=0;version<3;++version) {
            unsigned x=(version_x[version]+5)*scale+scale/2,y=(version_y[version]+5)*scale+scale/2;
            const unsigned char *pixel=constant_hd+((size_t)y*physical[0]+x)*4;
            CHECK(pixel[0]==(version==0?255:0) && pixel[1]==(version==1?255:0) && pixel[2]==(version==2?255:0));
        }
        free(constant_hd);
        if(scale==2) {
            CHECK(!pilot_gfx_clear(1,0xff000000,1,0));CHECK(!pilot_gfx_visibility_begin());
            constant_hd=malloc((size_t)version_bytes);CHECK(constant_hd);
            int probe=pilot_gfx_native_packet_rollback_probe(constant_hd,(uint32_t)version_bytes,physical);
            CHECK(probe<=0);
            if(probe==0) {
                CHECK(!pilot_gfx_visibility_end(34));CHECK(!visibility_expect(34,320,NULL));
                CHECK(!pilot_gfx_readback(constant_actual,sizeof(constant_actual)));
                CHECK(!native_scale_save(scale,8,constant_actual,sizeof(constant_actual)));
                for(unsigned strip=0;strip<2;++strip) {
                    unsigned x=version_x[0]+strip*12+5,y=version_y[0]+5;
                    const unsigned char *native=constant_actual+((size_t)y*80+x)*4;
                    const unsigned char *enhanced=constant_hd+((size_t)(y*scale+scale/2)*physical[0]+x*scale+scale/2)*4;
                    CHECK(!native[0] && !native[1] && native[2]==255);
                    CHECK(!enhanced[0] && !enhanced[1] && enhanced[2]==255);
                }
                CHECK(!native_scale_save(scale,9,constant_hd,(size_t)version_bytes));free(constant_hd);
                fprintf(stderr,"PACKET ADMISSION ROLLBACK: axis=2 prior_next_pixels=2 reserved_copy=1 pins_released=1 same_slot_reused=1 samples=320\n");
                CHECK(!pilot_gfx_native_present_resize_probe(physical[0],physical[1]));
            } else {
                free(constant_hd);CHECK(!pilot_gfx_visibility_end(34));
            }
        }
        pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(constant_vertices);
        pilot_gfx_shader_destroy(constant_shader);
        fprintf(stderr,"CONSTANT INDEX VERSIONS SCALE: axis=%u native_bytes=0 red_green_blue=3 samples=960\n",scale);
        pilot_gfx_texture_destroy(alias);pilot_gfx_texture_destroy(depth);pilot_gfx_close();
    }
    fprintf(stderr,"%s: native fractional-edge/blend/depth/stencil and target-alias byte identity across1/2/3/4/8x, real HD content, exact native queries and retained-alias sampling after80x64 Reset/real Present\n",differences?"FAIL":"PASS");
    return differences?1:0;
}
#endif
