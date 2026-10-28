/* Exercise real XDK ABI transitions while native draws retain input ownership.
 * A display query observes CPU state; a changed pixel/vertex input must still
 * split real GPU work. The backend fixture separately covers those mutations. */
static void test_graphics_native_operation_owner(void)
{
    GRAPHICS_TEST_REQUIRE("native operation owner",PILOT_D3D_D3DDevice_GetDisplayMode,
        PILOT_D3D_D3DDevice_SetRenderState_CullMode,PILOT_D3D_D3DDevice_CreateVertexBuffer2,
        PILOT_D3D_D3DDevice_SetStreamSource,PILOT_D3D_D3DResource_Release);
    const uint32_t program[]={0,0x0020001b,0x08000000,0x0000f800,
        0,0x0020021b,0x08000000,0x0000f819};
    struct PilotVertexElement elements[16]={0};
    elements[0]=(struct PilotVertexElement){0x42,0,0};
    elements[1]=(struct PilotVertexElement){0x42,0,16};
    const struct {float position[4],color[4];} vertices[]={
        {{8,8,.5f,1},{1,0,0,1}},{{8,56,.5f,1},{1,0,0,1}},
        {{56,8,.5f,1},{1,0,0,1}},{{56,56,.5f,1},{1,0,0,1}}};
    uint32_t shader=0,length=sizeof(vertices);
    CHECK(!pilot_gfx_shader_create(program,2,elements,&shader));
    uint32_t header=call_graphics(PILOT_D3D_D3DDevice_CreateVertexBuffer2,&length);
    CHECK(header);
    struct GraphicsVertexObject *vertex=graphics_vertex(header);
    memcpy(memory+vertex->data,vertices,sizeof(vertices));
    pilot_gfx_vertex_upload(vertex->native,memory+vertex->data,sizeof(vertices));
    uint32_t bind[]={0,header,sizeof(vertices[0])};
    call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,bind);
    CHECK(!pilot_gfx_shader_bind(shader));pilot_gfx_pixel_fixed();
    CHECK(pilot_gfx_xbox_render_state(143,0));CHECK(pilot_gfx_xbox_render_state(64,0));
    CHECK(pilot_gfx_xbox_render_state(102,0));CHECK(pilot_gfx_xbox_render_state(103,0));
    CHECK(pilot_gfx_xbox_render_state(66,0x1d01));CHECK(pilot_gfx_xbox_render_state(92,0));
    CHECK(pilot_gfx_xbox_render_state(60,0));CHECK(pilot_gfx_xbox_render_state(59,0));
    CHECK(pilot_gfx_xbox_render_state(144,0));CHECK(pilot_gfx_xbox_render_state(67,0x01010101));
    for(unsigned stage=0;stage<4;++stage) {
        pilot_gfx_texture_bind(stage,0);
        CHECK(pilot_gfx_xbox_texture_state(stage,12,1));
    }
    uint32_t cull=0;call_graphics(PILOT_D3D_D3DDevice_SetRenderState_CullMode,&cull);
    float scale[]={2.f/64,-2.f/64,1,1},offset[]={-1,1,0,0};
    CHECK(!pilot_gfx_shader_raster(scale,offset));
    unsigned char baseline[64*64*4],batched[64*64*4];
    for(unsigned merged=0;merged<2;++merged) {
        CHECK(!pilot_gfx_clear(1,0xff000000,1,0));pilot_gfx_batch_allow(merged);
        struct PilotDrawBatchStats before,after;pilot_gfx_batch_stats(&before);
        for(unsigned i=0;i<65;++i) {
            uint32_t output=0x19e00;
            call_graphics(PILOT_D3D_D3DDevice_GetDisplayMode,&output);
            CHECK(MEM32(output)==64 && MEM32(output+4)==64);
            call_graphics(PILOT_D3D_D3DDevice_SetRenderState_CullMode,&cull);
            call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,bind);
            CHECK(!pilot_gfx_shader_raster(scale,offset));
            CHECK(!pilot_gfx_draw_vertices(6,0,4));
        }
        CHECK(!pilot_gfx_readback(merged?batched:baseline,sizeof(baseline)));
        pilot_gfx_batch_stats(&after);
        if(merged) {
            CHECK(after.queued_draws-before.queued_draws==65);
            CHECK(after.native_draws-before.native_draws==(pilot_gfx_native_operation_owner()?2u:65u));
        }
    }
    CHECK(!memcmp(baseline,batched,sizeof(baseline)));
    CHECK(batched[(24*64+24)*4]==255 && !batched[(24*64+24)*4+1]);
    pilot_gfx_batch_allow(0);bind[1]=0;
    call_graphics(PILOT_D3D_D3DDevice_SetStreamSource,bind);
    CHECK(!call_graphics(PILOT_D3D_D3DResource_Release,&header));
    pilot_gfx_shader_destroy(shader);
    fprintf(stderr,"PASS native operation owner: real ABI queries/setters/rebinds, exact GPU pixels, 65 original draws, capacity drain and retirement\n");
}
