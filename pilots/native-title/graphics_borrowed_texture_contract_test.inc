/* Caller-created XPR-style headers and a real contiguous allocation, independent
 * of the native CreateTexture entry. Distinct mip bytes expose offset mistakes. */
static uint32_t test_graphics_borrowed_textures(const char *failure)
{
    const uint32_t header=0x19600,formats[]={6,7,0xc,0xe,0xf};
    for(unsigned variant=0;variant<sizeof(formats)/sizeof(*formats);++variant) {
        uint32_t format=formats[variant],block=format==0xc?8:(format==0xe || format==0xf?16:0);
        unsigned widths[]={8,4,2,1},heights[]={4,2,1,1};
        uint32_t offsets[5]={0};
        for(unsigned level=0;level<4;++level)
            offsets[level+1]=offsets[level]+(block?((widths[level]+3)/4)*((heights[level]+3)/4)*block:widths[level]*heights[level]*4);
        uint32_t data=physical_allocate(offsets[4],0,0x03ffffff,4096,PAGE_READWRITE);CHECK(data);
        for(unsigned level=0;level<4;++level)memset(memory+data+offsets[level],0x30+level,offsets[level+1]-offsets[level]);
        MEM32(header)=0x01040003;MEM32(header+4)=data&0x0fffffffu;MEM32(header+8)=0;
        MEM32(header+12)=0x0234002a|(format<<8);MEM32(header+16)=0;
        if(failure && !strcmp(failure,"borrowed-shape"))MEM32(header+12)|=4; /* cube */
        if(failure && !strcmp(failure,"borrowed-extent"))MEM32(header+4)=(data&0x0fffffffu)+4096-16;
        uint32_t binding[]={0,header};call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
        unsigned slot=graphics_texture_slot(header);struct GraphicsTextureObject *texture=&graphics_textures[slot];
        CHECK(texture->guest_owned && texture->cpu_borrowed && texture->data==data && texture->bytes==offsets[4]);
        CHECK(physical_find(data,offsets[4])->borrowers==1 && texture->bindings==1);
        CHECK(MEM32(header+4)==(data&0x0fffffffu) && MEM32(header+12)==(0x0234002a|(format<<8)));
        if(failure && !strcmp(failure,"borrowed-free"))physical_free(data);
        if(failure && !strcmp(failure,"borrowed-change")) {
            MEM32(header+12)^=0x100000;
            graphics_texture_sample_prepare(slot);CHECK(0);
        }
        for(unsigned level=0;level<4;++level) {
            unsigned char image[128];unsigned bytes=offsets[level+1]-offsets[level];
            CHECK(!pilot_gfx_texture_readback(texture->native,level,image,sizeof(image)));
            for(unsigned i=0;i<bytes;++i)CHECK(image[i]==(format==7 && i%4==3?255:0x30+level));
        }
        /* Retained CPU pointer writes need no extra Lock or rebind. */
        memset(memory+data,0x54,offsets[1]);graphics_texture_sample_prepare(slot);
        unsigned char image[128];CHECK(!pilot_gfx_texture_readback(texture->native,0,image,sizeof(image)));
        for(unsigned i=0;i<offsets[1];++i)CHECK(image[i]==(format==7 && i%4==3?255:0x54));
        /* Original inline AddRef must remain authoritative at the HLE boundary. */
        ++MEM32(header);CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==3);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==2);
        CHECK(call_graphics(PILOT_D3D_D3DResource_Release,&header)==1);
        CHECK((MEM32(header)&0xffff)==1 && physical_find(data,1)->borrowers==1);
        binding[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
        AcquireSRWLockExclusive(&graphics_lock);graphics_textures[slot].refs=0;
        graphics_texture_collect(slot);ReleaseSRWLockExclusive(&graphics_lock);
        /* Dispose the host view while retaining the authored caller's real
         * reference. Final XDK allocator release has a separate fixture. */
        CHECK(!graphics_textures[slot].header && !physical_find(data,1)->borrowers);
        CHECK(MEM32(header+4)==(data&0x0fffffffu) && !test_pool_owned(header));
        if(variant+1==sizeof(formats)/sizeof(*formats)) {
            /* Device teardown releases the host borrow, never caller storage. */
            MEM32(header)=0x01040003;binding[1]=header;
            call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
            binding[1]=0;call_graphics(PILOT_D3D_D3DDevice_SetTexture,binding);
            puts("PASS borrowed texture owner: caller headers, real cached physical alias, five formats/four distinct mips, retained CPU edits, inline refs, bound lifetime and teardown retention");
            return data;
        }
        physical_free(data);
    }
    CHECK(0);return 0;
}
