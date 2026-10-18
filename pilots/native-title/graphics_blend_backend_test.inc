#ifdef PILOT_D3D_PHOBOS
/* Authored input-only quarter taps. The normal default draw entries select
 * the correction; there is no fixture identifier or shader override. */
struct BlendVertex {float x,y,z,w;uint32_t color;float uv[8];};
static const uint32_t blend_code[]={
    0,0x006f201b,0x0836106c,0x3070f800,0,0x0020021b,0x0836106c,0x2070f818,
    0,0x00200415,0x0836106c,0x2070c848,0,0x00200615,0x0836106c,0x2070c850,
    0,0x00200815,0x0836106c,0x2070c858,0,0x00200a15,0x0836106c,0x2070c861};
static int blend_prepare(uint32_t shader,uint32_t texture,unsigned address_w)
{
    uint32_t ps[57]={0};
    const uint32_t alpha[]={0xd4d81010,0xd4d930dc,0xd4da30dc,0xd4db30dc};
    const uint32_t rgb[]={0xc4c80000,0xc4c920cc,0xc4ca20cc,0xc4cb20cc};
    for(unsigned i=0;i<4;++i) {ps[i]=alpha[i];ps[34+i]=rgb[i];ps[26+i]=ps[45+i]=i?0xc00:0xc0;}
    ps[53]=0x11104;ps[54]=0x8421;CHECK(!pilot_gfx_pixel_program(ps,0x8421));
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));
    uint32_t viewport[]={0,0,640,480,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    pilot_gfx_set_state(D3DRS_ZENABLE,0);pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,1);pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);
    pilot_gfx_set_state(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    pilot_gfx_set_state(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);pilot_gfx_set_state(D3DRS_BLENDOP,1);
    pilot_gfx_set_state(D3DRS_COLORWRITEENABLE,15);pilot_gfx_set_state(D3DRS_SHADEMODE,2);
    pilot_gfx_set_state(D3DRS_FILLMODE,D3DFILL_SOLID);pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);
    pilot_gfx_set_state(D3DRS_FOGENABLE,0);pilot_gfx_set_state(D3DRS_EDGEANTIALIAS,0);
    pilot_gfx_set_state(D3DRS_STENCILENABLE,1);pilot_gfx_set_state(D3DRS_STENCILFUNC,8);
    pilot_gfx_set_state(D3DRS_STENCILFAIL,1);pilot_gfx_set_state(D3DRS_STENCILZFAIL,1);
    pilot_gfx_set_state(D3DRS_STENCILPASS,3);pilot_gfx_set_state(D3DRS_STENCILREF,0);
    pilot_gfx_set_state(D3DRS_STENCILMASK,0xf0);pilot_gfx_set_state(D3DRS_STENCILWRITEMASK,0xf0);
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,texture);pilot_gfx_texture_sampling_format(stage,0x12);
        CHECK(pilot_gfx_xbox_texture_state(stage,0,3) && pilot_gfx_xbox_texture_state(stage,1,3));
        /* Zero is the omitted extension's effective WRAP default; one is
         * explicit WRAP. PROJECT2D consumes neither W addressing value. */
        pilot_gfx_set_texture_state(stage,31,address_w);
        CHECK(pilot_gfx_xbox_texture_state(stage,3,2) && pilot_gfx_xbox_texture_state(stage,4,2));
        CHECK(pilot_gfx_xbox_texture_state(stage,5,stage<2?2:0));
        float bias=stage<2?-1.2f:0;uint32_t bits;memcpy(&bits,&bias,4);
        CHECK(pilot_gfx_xbox_texture_state(stage,6,bits));
        CHECK(pilot_gfx_xbox_texture_state(stage,7,0) && pilot_gfx_xbox_texture_state(stage,8,1));
        pilot_gfx_set_texture_state(stage,D3DTSS_TEXCOORDINDEX,0);
    }
    CHECK(!pilot_gfx_shader_bind(shader));
    const float constant[]={.53125f,.53125f,0,0};pilot_gfx_shader_constants(121,1,constant);
    const float scale[]={2.0f/640,-2.0f/480,1.0f/16777215,1},offset[]={-1,1,0,0};
    CHECK(!pilot_gfx_shader_raster(scale,offset));return 0;
}
static int blend_pixels(const unsigned char expected[4])
{
    size_t bytes=640u*480u*4u;unsigned char *pixels=(unsigned char*)malloc(bytes);CHECK(pixels);
    CHECK(!pilot_gfx_readback(pixels,(uint32_t)bytes));
    for(unsigned i=0;i<640u*480u;++i)CHECK(!memcmp(pixels+i*4,expected,4));
    free(pixels);return 0;
}
static int blend_draw_check(const struct BlendVertex vertices[4],unsigned bound,unsigned gray,
                            const unsigned char expected[4],uint32_t copies)
{
    CHECK(!pilot_gfx_clear(7,0xff000000u|gray*0x10101u,.25f,0x5a));
    uint32_t metadata[3],render[2];CHECK(pilot_gfx_render_size(render));
    size_t depth_pixels=(size_t)render[0]*render[1],depth_bytes=depth_pixels*4;
    uint32_t *before_depth=(uint32_t*)malloc(depth_bytes);
    uint32_t *after_depth=(uint32_t*)malloc(depth_bytes);CHECK(before_depth && after_depth);
    CHECK(!pilot_gfx_reference_target_readback(1,before_depth,(uint32_t)depth_bytes,metadata));
    CHECK(metadata[0]==render[0] && metadata[1]==render[1] &&
        (metadata[2]==DXGI_FORMAT_R24G8_TYPELESS || metadata[2]==DXGI_FORMAT_D24_UNORM_S8_UINT));
    for(size_t i=0;i<depth_pixels;++i)CHECK((before_depth[i]>>24)==0x5a);
    if(bound)CHECK(!pilot_gfx_draw_vertices(6,2,4));
    else CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(*vertices)));
    CHECK(!blend_pixels(expected));struct PilotBlendStats stats;pilot_gfx_blend_stats(&stats);
    CHECK(stats.copies==copies && stats.draws==copies && stats.allocations==1 && stats.shader_compilations==1);
    ID3D11ShaderResourceView *view=NULL;
    ID3D11DeviceContext_PSGetShaderResources(d3d8_GetD3D11Context(),4,1,&view);CHECK(!view);
    CHECK(!pilot_gfx_reference_target_readback(1,after_depth,(uint32_t)depth_bytes,metadata));
    CHECK(metadata[0]==render[0] && metadata[1]==render[1] &&
        (metadata[2]==DXGI_FORMAT_R24G8_TYPELESS || metadata[2]==DXGI_FORMAT_D24_UNORM_S8_UINT));
    /* Exact preservation of every depth bit from the established pre-draw
     * clear storage, plus source-decided masked REPLACE of stencil F0 to zero.
     * These bytes are never used as color expected values or uploaded again. */
    for(size_t i=0;i<depth_pixels;++i)CHECK(after_depth[i]==(before_depth[i]&0x0fffffffu));
    free(after_depth);free(before_depth);
    return 0;
}
static int blend_feedback_interleave(unsigned size)
{
    for(unsigned stage=0;stage<4;++stage)pilot_gfx_texture_bind(stage,0);
    pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);pilot_gfx_set_state(D3DRS_STENCILENABLE,0);
    uint32_t ps[57]={0};ps[8]=8;ps[9]=0x1800;ps[54]=1;CHECK(!pilot_gfx_pixel_program(ps,1));
    uint32_t target;CHECK(!pilot_gfx_texture_create_usage(size,size,1,0x12,1,&target));
    CHECK(!pilot_gfx_texture_clear(target,0xff123456));
    pilot_gfx_texture_bind(0,target);CHECK(!pilot_gfx_targets(target,0));
    D3D11_TEXTURE2D_DESC descriptor;
    ID3D11Texture2D_GetDesc(pilot_donor_texture((D3D8Texture*)d3d8_GetStageTexture(0)),&descriptor);
    CHECK(descriptor.Width==size && descriptor.Height==size && descriptor.Format==DXGI_FORMAT_B8G8R8A8_UNORM);
    uint32_t viewport[]={0,0,size,size,0,0x3f800000};CHECK(!pilot_gfx_viewport(viewport));
    CHECK(pilot_gfx_xbox_texture_state(0,5,0) && pilot_gfx_xbox_texture_state(0,6,0));
    CHECK(!pilot_gfx_fvf_prepare(0x104,.53125f,.53125f));
    struct {float x,y,z,w,u,v;} quad[]={
        {-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,(float)size,0},
        {size-.5f,size-.5f,0,1,(float)size,(float)size},{-.5f,size-.5f,0,1,0,(float)size}};
    CHECK(pilot_gfx_feedback_mask()==1);CHECK(!pilot_gfx_draw_memory(8,4,quad,sizeof(*quad)));
    uint32_t observed[32*32];CHECK(!pilot_gfx_texture_readback(target,0,observed,size*size*4));
    for(unsigned i=0;i<size*size;++i)CHECK(observed[i]==0xff123456);
    pilot_gfx_texture_bind(0,0);CHECK(!pilot_gfx_targets(0,UINT32_MAX));pilot_gfx_texture_destroy(target);
    return 0;
}
static int test_float_blend(const uint32_t parameters[17])
{
    CHECK(pilot_gfx_width()==640 && pilot_gfx_height()==480);
    /* The output-only default depth allocation is typed D24S8, while
     * sampled depth storage must be typeless for its depth/stencil views. */
    uint32_t depth_metadata[3],render[2];CHECK(pilot_gfx_render_size(render));
    CHECK(!pilot_gfx_reference_target_readback(1,NULL,0,depth_metadata));
    CHECK(depth_metadata[0]==render[0] && depth_metadata[1]==render[1] &&
          depth_metadata[2]==DXGI_FORMAT_D24_UNORM_S8_UINT);
    struct PilotBlendStats initial;pilot_gfx_blend_stats(&initial);
    CHECK(!initial.draws && !initial.copies && !initial.allocations && !initial.shader_compilations && !initial.original_compilations);
    struct PilotVertexElement elements[16]={{0x42,0,0},{0x40,0,16},{0x22,0,20},{0x22,0,28},{0x22,0,36},{0x22,0,44}};
    uint32_t shader;CHECK(!pilot_gfx_shader_create(blend_code,6,elements,&shader));
    struct BlendVertex vertices[4]={
        {-.5f,479.5f,0,1,0x40404040,{1.25f,.75f,1.25f,.75f,1.25f,.75f,1.25f,.75f}},
        {-.5f,-.5f,0,1,0x40404040,{1.25f,.75f,1.25f,.75f,1.25f,.75f,1.25f,.75f}},
        {639.5f,479.5f,0,1,0x40404040,{1.25f,.75f,1.25f,.75f,1.25f,.75f,1.25f,.75f}},
        {639.5f,-.5f,0,1,0x40404040,{1.25f,.75f,1.25f,.75f,1.25f,.75f,1.25f,.75f}}};
    struct BlendVertex storage[6]={0};memcpy(storage+2,vertices,sizeof(vertices));
    uint32_t buffer;CHECK(!pilot_gfx_vertex_create(sizeof(storage),&buffer));
    pilot_gfx_vertex_upload(buffer,storage,sizeof(storage));pilot_gfx_vertex_bind(0,buffer,sizeof(*vertices));
    uint32_t texture;CHECK(!pilot_gfx_texture_create_usage(2,2,1,0x2e,2,&texture));
    D3D11_TEXTURE2D_DESC source_descriptor;
    pilot_gfx_texture_bind(0,texture);
    ID3D11Texture2D_GetDesc(pilot_donor_texture((D3D8Texture*)d3d8_GetStageTexture(0)),&source_descriptor);
    CHECK(source_descriptor.Format==DXGI_FORMAT_R24G8_TYPELESS);
    uint32_t color_texture;CHECK(!pilot_gfx_texture_create_usage(2,2,1,0x12,1,&color_texture));
    pilot_gfx_texture_bind(0,color_texture);
    ID3D11Texture2D_GetDesc(pilot_donor_texture((D3D8Texture*)d3d8_GetStageTexture(0)),&source_descriptor);
    CHECK(source_descriptor.Format==DXGI_FORMAT_B8G8R8A8_UNORM);
    uint32_t payload[32]={0};uint32_t copies=0;
    const unsigned char wanted[2][4]={{0,0,0,207},{21,21,21,243}};
    for(unsigned authored=0;authored<2;++authored)for(unsigned source=0;source<2;++source) {
        payload[0]=authored?0x0c7f7f7f:0x40010101;payload[1]=authored?0x0c7f7f7f:0x40020202;
        payload[16]=authored?0x0d7f7f7f:0x41010101;payload[17]=authored?0x0d7f7f7f:0x41020202;
        uint32_t sampled=source?color_texture:texture;
        pilot_gfx_texture_upload(sampled,0,payload,sizeof(payload));
        CHECK(!blend_prepare(shader,sampled,authored));
        CHECK(pilot_gfx_shader_blend_rectangle(vertices,sizeof(vertices),sizeof(*vertices),6,4));
        CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
        for(unsigned bound=0;bound<2;++bound)CHECK(!blend_draw_check(vertices,bound,authored?16:0,wanted[authored],++copies));
    }
    CHECK(!blend_prepare(shader,texture,0));
    for(unsigned stage=0;stage<4;++stage)pilot_gfx_set_texture_state(stage,D3DTSS_TEXCOORDINDEX,stage);
    CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    CHECK(!blend_draw_check(vertices,0,16,wanted[1],++copies));
    CHECK(!blend_prepare(shader,texture,0));
    /* CPU mirror changed while its actual GPU buffer still contains the old
     * value: the draw cannot be admitted until a successful flush. */
    float changed[]={.5f,.5f,0,0};pilot_gfx_shader_constants(121,1,changed);
    CHECK(!pilot_gfx_shader_blend_rectangle(vertices,sizeof(vertices),sizeof(*vertices),6,4));
    CHECK(!pilot_gfx_shader_flush());CHECK(pilot_gfx_shader_blend_rectangle(vertices,sizeof(vertices),sizeof(*vertices),6,4));
    CHECK(!blend_prepare(shader,texture,0));
    /* Read-only partial admission controls use the same predicate as the
     * default draw and cannot bypass the separately parsed PS proof. */
    const unsigned guarded_states[][3]={
        {D3DRS_ALPHATESTENABLE,1,0},{D3DRS_COLORWRITEENABLE,7,15},
        {D3DRS_ZENABLE,1,0},{D3DRS_ZWRITEENABLE,1,0},{D3DRS_FOGENABLE,1,0},
        {D3DRS_SHADEMODE,1,2},{D3DRS_CULLMODE,2,1},{D3DRS_FILLMODE,2,3},
        {D3DRS_STENCILFUNC,1,8},{D3DRS_STENCILWRITEMASK,255,0xf0},
        {D3DRS_SRCBLEND,D3DBLEND_ZERO,D3DBLEND_SRCALPHA}};
    for(unsigned i=0;i<sizeof(guarded_states)/sizeof(*guarded_states);++i) {
        pilot_gfx_set_state(guarded_states[i][0],guarded_states[i][1]);
        CHECK(!pilot_gfx_blend_candidate_geometry(6,2,4));
        pilot_gfx_set_state(guarded_states[i][0],guarded_states[i][2]);
        CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    }
    pilot_gfx_set_texture_state(0,D3DTSS_ADDRESSU,1);CHECK(!pilot_gfx_blend_candidate_geometry(6,2,4));
    pilot_gfx_set_texture_state(0,D3DTSS_ADDRESSU,3);
    pilot_gfx_set_texture_state(0,D3DTSS_MINFILTER,1);CHECK(!pilot_gfx_blend_candidate_geometry(6,2,4));
    pilot_gfx_set_texture_state(0,D3DTSS_MINFILTER,2);
    pilot_gfx_set_texture_state(0,31,3);CHECK(!pilot_gfx_blend_candidate_geometry(6,2,4));
    pilot_gfx_set_texture_state(0,31,0);CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    CHECK(!pilot_gfx_blend_candidate_geometry(6,3,4) && !pilot_gfx_blend_candidate_geometry(5,2,4));
    uint32_t depth_alias;CHECK(!pilot_gfx_texture_create_usage(640,480,1,0x2e,2,&depth_alias));
    CHECK(!pilot_gfx_targets(0,depth_alias));
    for(unsigned i=0;i<4;++i){pilot_gfx_texture_bind(i,depth_alias);pilot_gfx_texture_sampling_format(i,0x12);}
    CHECK(pilot_gfx_feedback_mask()==0xf00 && !pilot_gfx_blend_candidate_geometry(6,2,4));
    CHECK(!pilot_gfx_targets(0,UINT32_MAX));
    for(unsigned i=0;i<4;++i){pilot_gfx_texture_bind(i,texture);pilot_gfx_texture_sampling_format(i,0x12);}
    pilot_gfx_texture_destroy(depth_alias);CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    uint32_t color_alias;CHECK(!pilot_gfx_texture_alias(PILOT_GFX_SURFACE,0,0x12,&color_alias));
    for(unsigned i=0;i<4;++i)pilot_gfx_texture_bind(i,color_alias);
    CHECK(pilot_gfx_feedback_mask()==15 && !pilot_gfx_blend_candidate_geometry(6,2,4));
    for(unsigned i=0;i<4;++i){pilot_gfx_texture_bind(i,texture);pilot_gfx_texture_sampling_format(i,0x12);}
    pilot_gfx_texture_destroy(color_alias);CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    /* Linear Xbox textures cannot own mip chains in this supported API. */
    uint32_t mip_chain=0;
    CHECK(pilot_gfx_texture_create_usage(4,4,2,0x12,0,&mip_chain)==E_NOTIMPL && !mip_chain);
    CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    /* A normal feedback draw uses another size/storage format without evicting
     * this dedicated destination slot. Revisit the warmed default entry after
     * each interleave, including an authoritative UP with no bound stream. */
    uint32_t normal_copies=0,normal_allocations=0;
    for(unsigned size=16;size<=32;size*=2) {
        CHECK(!blend_feedback_interleave(size));
        pilot_donor_feedback_stats(&normal_copies,&normal_allocations);
        CHECK(normal_copies==(size==16?1:2) && normal_allocations==(size==16?1:2));
        CHECK(!blend_prepare(shader,texture,0));
        pilot_gfx_vertex_bind(0,0,0);
        CHECK(!blend_draw_check(vertices,0,16,wanted[1],++copies));
        pilot_gfx_vertex_bind(0,buffer,sizeof(*vertices));
    }
    /* Exact OM pointer, factors and mask are borrowed/restored; no extra COM
     * reference survives a draw. The sampler snapshot slot is unbound. */
    ID3D11BlendState *before=NULL,*after=NULL;FLOAT factors_before[4],factors_after[4];UINT mask_before,mask_after;
    ID3D11DeviceContext *context=d3d8_GetD3D11Context();
    ID3D11DeviceContext_OMGetBlendState(context,&before,factors_before,&mask_before);
    struct PilotBlendStats warm;pilot_gfx_blend_stats(&warm);
    CHECK(!blend_draw_check(vertices,1,16,wanted[1],++copies));
    ID3D11DeviceContext_OMGetBlendState(context,&after,factors_after,&mask_after);
    CHECK(before==after && mask_before==mask_after && !memcmp(factors_before,factors_after,sizeof(factors_before)));
    if(before)ID3D11BlendState_Release(before);if(after)ID3D11BlendState_Release(after);
    struct PilotBlendStats revisited;pilot_gfx_blend_stats(&revisited);
    CHECK(revisited.original_compilations==warm.original_compilations);
    struct BlendVertex invalid[4];memcpy(invalid,vertices,sizeof(invalid));invalid[3]=invalid[0];
    CHECK(!pilot_gfx_shader_blend_rectangle(invalid,sizeof(invalid),sizeof(*invalid),6,4));
    invalid[3]=vertices[3];invalid[3].x=319.5f;invalid[3].y=399.5f;
    CHECK(!pilot_gfx_shader_blend_rectangle(invalid,sizeof(invalid),sizeof(*invalid),6,4));
    invalid[3]=vertices[3];invalid[0].x=.123f;
    CHECK(!pilot_gfx_shader_blend_rectangle(invalid,sizeof(invalid),sizeof(*invalid),6,4));
    CHECK(!pilot_gfx_fvf_prepare(0x104,.53125f,.53125f));
    CHECK(!pilot_gfx_shader_blend_rectangle(vertices,sizeof(vertices),sizeof(*vertices),6,4));
    CHECK(!blend_prepare(shader,texture,0));
    /* Negative routing controls use opaque white. Covered output is exact
     * regardless of fixed-target fractional precision, and the copy counter
     * independently proves that wrong semantics/overlap/state are rejected. */
    for(unsigned i=0;i<32;++i)payload[i]=0xffffffff;
    pilot_gfx_texture_upload(texture,0,payload,sizeof(payload));
    memcpy(invalid,vertices,sizeof(invalid));invalid[3]=invalid[0];
    CHECK(!pilot_gfx_clear(7,0xff0000ff,.25f,0x5a));
    CHECK(!pilot_gfx_draw_memory(6,4,invalid,sizeof(*invalid)));
    struct PilotBlendStats rejected;pilot_gfx_blend_stats(&rejected);CHECK(rejected.copies==copies);
    unsigned char *sample=(unsigned char*)malloc(640u*480u*4);CHECK(sample);
    CHECK(!pilot_gfx_readback(sample,640u*480u*4));
    const unsigned char white[]={255,255,255,255},blue[]={0,0,255,255};
    CHECK(!memcmp(sample+(360*640+160)*4,white,4) && !memcmp(sample+(120*640+480)*4,blue,4));free(sample);
    uint32_t wrong_code[24];memcpy(wrong_code,blend_code,sizeof(wrong_code));
    wrong_code[1]=(wrong_code[1]&~(15u<<21))|(1u<<21); /* MOV, without c121 ADD */
    uint32_t wrong;CHECK(!pilot_gfx_shader_create(wrong_code,6,elements,&wrong));
    CHECK(!pilot_gfx_shader_bind(wrong));CHECK(!pilot_gfx_shader_flush());
    CHECK(!pilot_gfx_shader_blend_rectangle(vertices,sizeof(vertices),sizeof(*vertices),6,4));
    CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(*vertices)));
    pilot_gfx_blend_stats(&rejected);CHECK(rejected.copies==copies);
    CHECK(!blend_prepare(shader,texture,0));pilot_gfx_shader_destroy(wrong);
    /* Relative c[a0+121] is not the proved direct c121 operand, even when
     * this authored program's initial a0 makes its pixels coincide. */
    memcpy(wrong_code,blend_code,sizeof(wrong_code));wrong_code[3]|=2;
    CHECK(!pilot_gfx_shader_create(wrong_code,6,elements,&wrong));
    CHECK(!pilot_gfx_shader_bind(wrong));CHECK(!pilot_gfx_shader_flush());
    CHECK(!pilot_gfx_shader_blend_rectangle(vertices,sizeof(vertices),sizeof(*vertices),6,4));
    CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(*vertices)));
    CHECK(!blend_pixels(white));pilot_gfx_blend_stats(&rejected);CHECK(rejected.copies==copies);
    CHECK(!blend_prepare(shader,texture,0));pilot_gfx_shader_destroy(wrong);
    pilot_gfx_set_state(D3DRS_SRCBLEND,D3DBLEND_ZERO);
    CHECK(!pilot_gfx_clear(7,0xff123456,.25f,0x5a));CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(*vertices)));
    const unsigned char zero[]={0,0,0,0};CHECK(!blend_pixels(zero));
    pilot_gfx_blend_stats(&rejected);CHECK(rejected.copies==copies);
    CHECK(!blend_prepare(shader,texture,0));
    CHECK(pilot_gfx_xbox_render_state(159,1));
    CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(*vertices)));CHECK(!blend_pixels(white));
    pilot_gfx_blend_stats(&rejected);CHECK(rejected.copies==copies);
    CHECK(pilot_gfx_xbox_render_state(159,0));
    uint32_t wrong_ps[57]={0};wrong_ps[8]=8;wrong_ps[9]=0x1800;wrong_ps[54]=1;
    CHECK(!pilot_gfx_pixel_program(wrong_ps,1));CHECK(!pilot_gfx_draw_memory(6,4,vertices,sizeof(*vertices)));
    CHECK(!blend_pixels(white));pilot_gfx_blend_stats(&rejected);CHECK(rejected.copies==copies);
    /* Restore input B from authored bytes, never from the observed target. */
    memset(payload,0,sizeof(payload));payload[0]=payload[1]=0x0c7f7f7f;payload[16]=payload[17]=0x0d7f7f7f;
    pilot_gfx_texture_upload(texture,0,payload,sizeof(payload));CHECK(!blend_prepare(shader,texture,0));
    /* A malformed persistent mirror must not suppress a valid transient UP;
     * after UP the exact old IA binding/mirror still rejects a bound draw. */
    memcpy(storage+2,vertices,sizeof(vertices));storage[5]=storage[2];
    pilot_gfx_vertex_upload(buffer,storage,sizeof(storage));
    CHECK(!pilot_gfx_blend_candidate_geometry(6,2,4));
    CHECK(!blend_draw_check(vertices,0,16,wanted[1],++copies));
    CHECK(!pilot_gfx_blend_candidate_geometry(6,2,4));
    memcpy(storage+2,vertices,sizeof(vertices));pilot_gfx_vertex_upload(buffer,storage,sizeof(storage));
    CHECK(pilot_gfx_blend_candidate_geometry(6,2,4));
    /* Successful same-shape reset preserves the dedicated warmed slot and
     * canonical shader. Invalid parameters must preserve those owned caches. */
    uint32_t bad[17];memcpy(bad,parameters,sizeof(bad));bad[0]=0;CHECK(pilot_gfx_reset(bad)<0);
    CHECK(!pilot_gfx_reset(parameters));CHECK(!blend_prepare(shader,texture,0));
    CHECK(!blend_draw_check(vertices,0,16,wanted[1],++copies));
    fprintf(stderr,"DEFAULT BLEND draws=%u copies=%u; expected warm allocation/variant=1\n",copies,copies);
    for(unsigned stage=0;stage<4;++stage)pilot_gfx_texture_bind(stage,0);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(buffer);
    pilot_gfx_shader_destroy(shader);pilot_gfx_texture_destroy(texture);pilot_gfx_texture_destroy(color_texture);pilot_gfx_pixel_fixed();
    fprintf(stderr,"PASS: default float blend exact Fraction pixels, bound/UP draws, current constants, geometry and reset lifetime\n");
    return 0;
}
#endif
