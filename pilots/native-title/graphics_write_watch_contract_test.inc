static void test_graphics_texture_write_watch(void)
{
#ifdef PILOT_MEMORY_WRITE_WATCH
    CHECK(pilot_write_watch_initialize(memory,(SIZE_T)0x100000000ull));
    uint32_t args[]={32,32,1,1,0,6,3};
    uint32_t header=call_graphics(PILOT_D3D_D3DDevice_CreateTexture2,args);CHECK(header);
    unsigned slot=graphics_texture_slot(header);struct GraphicsTextureObject *texture=&graphics_textures[slot];
    CHECK(texture->bytes==4096 && texture->levels==1);
    uint32_t *source=(uint32_t*)(memory+texture->data),pixels[32*32],other[32*32];
    for(unsigned i=0;i<32*32;++i)source[i]=0xffc02040u;
    graphics_texture_sample_prepare(slot);
    CHECK(texture->cpu_owner.snapshot.valid && pilot_write_watch_reusable(&texture->cpu_owner.snapshot));
    uint64_t revision=texture->cpu_owner.gpu_revision;
    graphics_texture_sample_prepare(slot);CHECK(texture->cpu_owner.gpu_revision==revision);
    CHECK(!pilot_gfx_texture_readback(texture->native,0,pixels,sizeof(pixels)));
    for(unsigned i=0;i<32*32;++i)CHECK(pixels[i]==0xffc02040u);
    uint64_t faults=pilot_write_watch_faults();
    for(unsigned i=0;i<32*32;++i)source[i]=0xff10a060u;
    CHECK(pilot_write_watch_faults()>faults && !pilot_write_watch_reusable(&texture->cpu_owner.snapshot));
    graphics_texture_sample_prepare(slot);
    CHECK(!pilot_gfx_texture_readback(texture->native,0,pixels,sizeof(pixels)));
    for(unsigned i=0;i<32*32;++i)CHECK(pixels[i]==0xff10a060u);
    /* Native-only mutation must invalidate prepared GPU content even though
     * the CPU pages themselves have not changed. */
    for(unsigned i=0;i<32*32;++i)other[i]=0xff2030d0u;
    pilot_gfx_texture_upload(texture->native,0,other,sizeof(other));
    CHECK(texture->cpu_owner.gpu_revision!=pilot_gfx_texture_cpu_snapshot_revision(texture->native));
    graphics_texture_sample_prepare(slot);
    CHECK(!pilot_gfx_texture_readback(texture->native,0,pixels,sizeof(pixels)));
    for(unsigned i=0;i<32*32;++i)CHECK(pixels[i]==0xff10a060u);
    /* Aliases share image writes even after the alias wrapper is retired.
     * Keep the existing byte-check result: unchanged original CPU shadow
     * does not overwrite the other view's native write. */
    uint32_t alias=0;
    CHECK(!pilot_gfx_texture_alias(PILOT_GFX_TEXTURE,texture->native,0x12,&alias));
    uint64_t shared_revision=texture->cpu_owner.gpu_revision;
    pilot_gfx_texture_upload(alias,0,other,sizeof(other));pilot_gfx_texture_destroy(alias);
    CHECK(pilot_gfx_texture_cpu_snapshot_revision(texture->native)!=shared_revision);
    graphics_texture_sample_prepare(slot);
    CHECK(!pilot_gfx_texture_readback(texture->native,0,pixels,sizeof(pixels)));
    for(unsigned i=0;i<32*32;++i)CHECK(pixels[i]==0xff2030d0u);
    CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==0);
    pilot_write_watch_finish();
    fprintf(stderr,"PASS texture write tracking: actual GPU images, direct retained-pointer writes, unchanged reuse, native-only content changes and resource retirement\n");
#endif
}
