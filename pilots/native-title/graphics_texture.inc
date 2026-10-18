/* CPU-owned Xbox texture bytes and guest headers, backed by actual native
 * texture storage. Original XDK routines and XbD3D8Types supply layout facts.
 * Addresses are guest allocations; no native pointer enters a resource header.
 */
struct GraphicsTextureObject {
    uint32_t header,data,bytes,width,height,levels,format,refs,native,usage,cpu_exposed;
    unsigned parent,level; /* parent is slot+1 for a surface view, zero for texture */
    unsigned bindings;
    unsigned guest_owned,alias_kind,alias_source,alias_format,alias_size;
    /* Conservative owner hint: a CPU lock on this texture or any surface
     * sets it. A handoff clears it only after clearing the whole family. */
    unsigned family_exposed;
};
static struct GraphicsTextureObject *graphics_textures;
static unsigned graphics_texture_capacity;
static unsigned graphics_texture_adopt_alias(uint32_t header);
static int graphics_texture_exposure_cache(void)
{
    static int enabled=-1;
    if(enabled<0) {
        const char *value=getenv("XLLE_PILOT_TEXTURE_EXPOSURE_CACHE");
        enabled=!(value && !strcmp(value,"0"));
        fprintf(stderr,"[TEXTURE EXPOSURE] owner_cache=%u\n",enabled);
    }
    return enabled;
}

static unsigned graphics_texture_slot(uint32_t header)
{
    for(unsigned i=0;i<graphics_texture_capacity;++i)
        if(graphics_textures[i].header==header && header) return i;
    return graphics_texture_adopt_alias(header);
}
static unsigned graphics_texture_unused(void)
{
    for(unsigned i=0;i<graphics_texture_capacity;++i)
        if(!graphics_textures[i].header) return i;
    unsigned old=graphics_texture_capacity;
    if(old>0xffffffffu/2/sizeof(*graphics_textures))stop("texture ownership capacity overflow",old);
    unsigned capacity=old?old*2:512;
    struct GraphicsTextureObject *next=realloc(graphics_textures,capacity*sizeof(*next));
    if(!next)stop("texture ownership allocation failed",capacity);
    memset(next+old,0,(capacity-old)*sizeof(*next));
    graphics_textures=next;graphics_texture_capacity=capacity;
    return old;
}
static uint32_t graphics_log2(uint32_t value)
{
    uint32_t result=0; while(value>1) { ++result; value>>=1; } return result;
}
static uint32_t graphics_texture_bpp(const struct GraphicsTextureObject *texture)
{
    return texture->format<=5 || texture->format==0x24 || texture->format==0x25?2:4;
}
static uint32_t graphics_texture_block(const struct GraphicsTextureObject *texture)
{
    return texture->format==0xc?8:(texture->format==0xe || texture->format==0xf?16:0);
}
static int graphics_texture_linear(const struct GraphicsTextureObject *texture)
{return texture->format==0x12 || texture->format==0x1e || texture->format==0x2e || texture->format==0x24 || texture->format==0x25;}
static uint32_t graphics_level_width(const struct GraphicsTextureObject *texture,unsigned level)
{
    uint32_t width=texture->width>>level; return width?width:1;
}
static uint32_t graphics_level_height(const struct GraphicsTextureObject *texture,unsigned level)
{
    uint32_t height=texture->height>>level; return height?height:1;
}
static uint32_t graphics_level_bytes(const struct GraphicsTextureObject *texture,unsigned level)
{
    uint32_t block=graphics_texture_block(texture);
    if(block)return ((graphics_level_width(texture,level)+3)/4)*((graphics_level_height(texture,level)+3)/4)*block;
    uint32_t pitch=graphics_level_width(texture,level)*graphics_texture_bpp(texture);
    if(graphics_texture_linear(texture))pitch=(pitch+63)&~63u;
    return pitch*graphics_level_height(texture,level);
}
static uint32_t graphics_level_offset(const struct GraphicsTextureObject *texture,unsigned level)
{
    uint32_t offset=0; for(unsigned i=0;i<level;++i)offset+=graphics_level_bytes(texture,i); return offset;
}
static void graphics_texture_header(struct GraphicsTextureObject *object)
{
    MEM32(object->header)=(object->parent?0x01050000u:0x01040000u)|object->refs;
    MEM32(object->header+4)=object->data; MEM32(object->header+8)=0;
    MEM32(object->header+12)=0x29u|(object->format<<8)|(object->levels<<16);
    if(graphics_texture_linear(object)) {
        uint32_t pitch=(object->width*graphics_texture_bpp(object)+63)&~63u;
        MEM32(object->header+16)=(object->width-1)|((object->height-1)<<12)|((pitch/64-1)<<24);
    } else {
        MEM32(object->header+12)|=(graphics_log2(object->width)<<20)|(graphics_log2(object->height)<<24);
        MEM32(object->header+16)=0;
    }
    if(object->parent) MEM32(object->header+20)=graphics_textures[object->parent-1].header;
}
static uint32_t graphics_texture_allocate(const uint32_t *args)
{
    uint32_t width=args[0],height=args[1],levels=args[3],format=args[5],usage=args[4];
    int yuv=format==0x24 || format==0x25;
    int linear=format==0x12 || format==0x1e || format==0x2e || yuv;
    if(yuv && (usage || (width&1)))stop("unqualified packed YUV texture dimensions/usage",width);
    if(args[2]!=1 || (format==0x2e?usage!=2:(usage && (usage!=1 || !linear))) || args[6]!=3 || !(linear || (format>=4 && format<=7) || format==0xc || format==0xe || format==0xf))
        stop("unqualified texture type/usage/format",format);
    if(!width || !height || width>4096 || height>4096 || (!linear && ((width&(width-1)) || (height&(height-1)))))
        stop("unqualified texture dimensions",width);
    if(!linear && format>=0xc && (width<4 || height<4))stop("compressed texture base below native block size",width);
    unsigned max_levels=linear?1:graphics_log2(width>height?width:height)+1;
    if(!levels)levels=max_levels;
    if(levels>max_levels)stop("texture mip chain exceeds dimensions",levels);
    struct GraphicsTextureObject texture={0};
    texture.width=width; texture.height=height; texture.levels=levels; texture.format=format; texture.refs=1;texture.usage=usage;
    texture.bytes=graphics_level_offset(&texture,levels);
    /* Headers are small guest objects, not private 4 KiB allocations. Reuse
     * the maintained pool service, including its original-kernel selection,
     * without changing header bytes or caller/binding ownership. */
    unsigned slot=graphics_texture_unused();
    texture.header=pool_allocate(24,0x48584554u);if(!texture.header)return 0;
    /* Original XDK consumers (including the XPR save-image writer) derive a
     * cached CPU pointer by setting Data bit31. Ordinary virtual allocations
     * cannot satisfy that contract. Use the existing owned contiguous service
     * so Data and Data|0x80000000 name the same real CPU bytes. */
    texture.data=physical_allocate(texture.bytes,0,0x03ffffffu,4096,PAGE_READWRITE);
    if(!texture.data) {
        pool_free(texture.header);
        return 0;
    }
    int32_t result=pilot_gfx_texture_create_usage(width,height,levels,format,usage,&texture.native);
    if(result<0) {
        physical_free(texture.data);
        pool_free(texture.header);
        if((uint32_t)result!=0x8007000eu)stop("native texture creation failed",(uint32_t)result);
        return 0;
    }
    graphics_texture_header(&texture); graphics_textures[slot]=texture;
    fprintf(stderr,"[TEXTURE CREATE] header=%08x data=%08x %ux%u levels=%u format=%02x bytes=%u\n",
        texture.header,texture.data,width,height,levels,format,texture.bytes);
    return texture.header;
}
static void graphics_texture_upload(struct GraphicsTextureObject *texture,unsigned level)
{
    if(level>=texture->levels)stop("texture upload level out of range",level);
    uint32_t offset=graphics_level_offset(texture,level);
    pilot_gfx_texture_upload(texture->native,level,memory+texture->data+offset,graphics_level_bytes(texture,level));
}
static uint32_t graphics_texture_ref(unsigned slot,int acquire);
static unsigned graphics_texture_adopt_alias(uint32_t header)
{
    graphics_pointer(header,20);
    uint32_t common=MEM32(header),data=MEM32(header+4),format=MEM32(header+12),size=MEM32(header+16);
    unsigned kind=0,source=0;uint32_t expected_size=0,expected_format=0;
    if(((common>>16)&7)!=4 || !(common&0xffff) || ((format>>16)&15)!=1 || (format&0xf4)!=0x20)
        stop("unqualified caller-owned texture header",header);
    for(unsigned i=0;i<2;++i)if(data==graphics_data[i]) {
        kind=PILOT_GFX_SURFACE;source=i;expected_size=MEM32(graphics_headers+i*32+16);expected_format=(MEM32(graphics_headers+i*32+12)>>8)&255;
        break;
    }
    if(!kind)for(unsigned i=0;i<graphics_texture_capacity;++i) {
        struct GraphicsTextureObject *t=&graphics_textures[i];
        if(t->header && !t->parent && !t->guest_owned && t->usage==1 && t->data==data) {
            kind=PILOT_GFX_TEXTURE;source=t->native;expected_size=MEM32(t->header+16);expected_format=t->format;break;
        }
    }
    if(!kind || size!=expected_size || ((format>>8)&255)!=expected_format || expected_format!=0x12)
        stop("unowned or incompatible texture storage alias",header);
    struct GraphicsTextureObject t={0};t.header=header;t.data=data;t.refs=common&0xffff;
    t.width=(size&4095)+1;t.height=((size>>12)&4095)+1;t.format=expected_format;t.levels=1;t.usage=1;
    t.bytes=((size>>24)+1)*64*t.height;t.guest_owned=1;t.alias_kind=kind;t.alias_source=source;
    t.alias_format=format;t.alias_size=size;
    if(pilot_gfx_texture_alias(kind,source,t.format,&t.native)<0)stop("native texture storage alias failed",header);
    unsigned slot=graphics_texture_unused();graphics_textures[slot]=t;
    fprintf(stderr,"[TEXTURE ALIAS] header=%08x data=%08x %ux%u kind=%u source=%u\n",header,data,t.width,t.height,kind,source);
    return slot;
}
static unsigned graphics_texture_refresh_alias(unsigned slot)
{
    struct GraphicsTextureObject *t=&graphics_textures[slot];uint32_t header=t->header;
    if(!t->guest_owned || (t->data==MEM32(header+4) && t->alias_format==MEM32(header+12) && t->alias_size==MEM32(header+16)))return slot;
    unsigned bindings=t->bindings,stages=0;
    for(unsigned i=0;i<4;++i)if(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+i*4)==header){
        stages|=1u<<i;pilot_gfx_texture_bind(i,0);
    }
    pilot_gfx_texture_destroy(t->native);memset(t,0,sizeof(*t));
    slot=graphics_texture_adopt_alias(header);graphics_textures[slot].bindings=bindings;
    for(unsigned i=0;i<4;++i)if(stages&(1u<<i))pilot_gfx_texture_bind(i,graphics_textures[slot].native);
    return slot;
}
static void graphics_texture_collect(unsigned slot)
{
    struct GraphicsTextureObject *object=&graphics_textures[slot];
    if(object->refs || object->bindings)return;
    if(object->guest_owned){pilot_gfx_texture_destroy(object->native);memset(object,0,sizeof(*object));return;}
    unsigned parent=object->parent;
    if(parent) {
        if(!object->usage || object->cpu_exposed)graphics_texture_upload(&graphics_textures[parent-1],object->level);
    }
    else {
        pilot_gfx_texture_destroy(object->native);
        physical_free(object->data);
    }
    pool_free(object->header);
    memset(object,0,sizeof(*object));
    if(parent)graphics_texture_ref(parent-1,0);
}
static uint32_t graphics_texture_ref(unsigned slot,int acquire)
{
    struct GraphicsTextureObject *object=&graphics_textures[slot];
    if(!object->header || (!object->refs && (!acquire || !object->bindings)) || (acquire && object->refs==0xffff))
        stop("invalid texture reference lifetime",object->header);
    if(acquire)++object->refs; else --object->refs;
    uint32_t refs=object->refs;
    MEM32(object->header)=(MEM32(object->header)&~0xffffu)|refs;
    graphics_texture_collect(slot);return refs;
}
static void graphics_texture_binding_ref(uint32_t header,int acquire)
{
    unsigned slot=graphics_texture_slot(header);struct GraphicsTextureObject *object=&graphics_textures[slot];
    if(object->parent || (acquire?object->bindings==15:!object->bindings))stop("invalid texture binding ownership",header);
    if(acquire)++object->bindings;
    else {--object->bindings;MEM32(header+8)=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_FENCE);}
    MEM32(header)=(MEM32(header)&~0x780000u)|(object->bindings<<19);
    graphics_texture_collect(slot);
}
static void graphics_texture_sample_prepare(unsigned slot)
{
    struct GraphicsTextureObject *texture=&graphics_textures[slot];int exposed=!texture->usage || texture->cpu_exposed;
    if(texture->guest_owned)return; /* donor RHI view shares the live GPU storage */
    int scan=!graphics_texture_exposure_cache() || (texture->usage && texture->family_exposed);
    /* Ordinary CPU-owned textures still compare their actual bytes at every
     * consumption. Their exposure is unconditional; a child scan adds nothing. */
    if(scan)for(unsigned i=0;i<graphics_texture_capacity;++i)
        if(graphics_textures[i].parent==slot+1)exposed|=graphics_textures[i].cpu_exposed;
    for(unsigned level=0;level<texture->levels;++level)
        pilot_gfx_texture_prepare_sample(texture->native,level,memory+texture->data+graphics_level_offset(texture,level),
            graphics_level_bytes(texture,level),exposed);
    if(texture->usage && scan) {
        for(unsigned i=0;i<graphics_texture_capacity;++i)
            if(i==slot || graphics_textures[i].parent==slot+1)graphics_textures[i].cpu_exposed=0;
        texture->family_exposed=0;
    }
}
static void graphics_texture_bind(unsigned stage,uint32_t header)
{
    if(stage>=4)stop("invalid texture stage",stage);
    uint32_t cache=PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4,old=MEM32(cache),native=0,mode=0x80000000u;
    if(header) {
        unsigned slot=graphics_texture_refresh_alias(graphics_texture_slot(header));struct GraphicsTextureObject *texture=&graphics_textures[slot];
        if(texture->parent)stop("texture binding requires a texture",header);
        graphics_texture_sample_prepare(slot);native=texture->native;
        mode=MEM32(header+12)&0x20f4u;
        if((mode&0x2000u) && texture->format>=0x2a && texture->format<=0x31)mode=(mode&~0x2000u)|0x40000000u;
    }
    if(graphics_diagnostic(GRAPHICS_DIAGNOSTIC_FEEDBACK_TRACE))fprintf(stderr,
        "[TEXTURE BIND TRACE] return=%08x stage=%u header=%08x native=%u color=%08x depth=%08x\n",
        MEM32(g_esp),stage,header,native,graphics_target,graphics_depth);
    pilot_gfx_texture_bind(stage,native);
    if(header)pilot_gfx_texture_sampling_format(stage,(MEM32(header+12)>>8)&255);
    /* Acquire first so an internal-only self-rebind remains alive. */
    if(header!=old) {
        if(header)graphics_texture_binding_ref(header,1);
        MEM32(cache)=header;
        if(old)graphics_texture_binding_ref(old,0);
    } else if(old)MEM32(old+8)=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_FENCE);
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_RESOURCE_TEXTURE_MODE+stage*4)=mode;
}
static void graphics_texture_aliases_close(void)
{
    for(unsigned i=0;i<graphics_texture_capacity;++i)if(graphics_textures[i].guest_owned) {
        if(graphics_textures[i].bindings)stop("closing a bound texture alias",graphics_textures[i].header);
        pilot_gfx_texture_destroy(graphics_textures[i].native);memset(&graphics_textures[i],0,sizeof(graphics_textures[i]));
    }
}
static uint32_t graphics_texture_surface(uint32_t header,unsigned level)
{
    unsigned parent=graphics_texture_slot(header),slot=graphics_texture_unused();
    struct GraphicsTextureObject *texture=&graphics_textures[parent];
    if(texture->guest_owned)stop("caller-owned GPU alias surface view outside qualified scope",header);
    if(texture->parent || level>=texture->levels)stop("invalid texture surface level",level);
    struct GraphicsTextureObject surface={0};
    surface.header=pool_allocate(24,0x48584554u);if(!surface.header)return 0;
    surface.parent=parent+1; surface.level=level; surface.levels=1; surface.refs=1;
    surface.width=graphics_level_width(texture,level); surface.height=graphics_level_height(texture,level);
    surface.format=texture->format; surface.data=texture->data+graphics_level_offset(texture,level);
    surface.bytes=graphics_level_bytes(texture,level);
    surface.usage=texture->usage;
    graphics_texture_ref(parent,1); graphics_texture_header(&surface); graphics_textures[slot]=surface;
    return surface.header;
}
static void graphics_texture_desc(uint32_t header,unsigned level,uint32_t destination)
{
    uint32_t format,flags=0;int known=0;
    if(header==graphics_headers || header==graphics_headers+32 || header==graphics_headers+64)format=(MEM32(header+12)>>8)&255;
    else format=graphics_textures[graphics_texture_slot(header)].format;
    for(unsigned i=0;i<sizeof(pilot_d3d_surface_formats)/sizeof(pilot_d3d_surface_formats[0]);++i)
        if(pilot_d3d_surface_formats[i].format==format) {flags=pilot_d3d_surface_formats[i].flags;known=1;break;}
    if(!known)stop("unqualified surface description format",format);
    uint32_t usage=level?0:((flags&0x80)?1:((flags&0x40)?2:0));
    if(header==graphics_headers || header==graphics_headers+32 || header==graphics_headers+64) {
        if(level)stop("display surface has no mip levels",level);
        uint32_t size=MEM32(header+16),width=(size&0xfff)+1,height=((size>>12)&0xfff)+1,pitch=((size>>24)+1)*64;
        uint32_t description[]={format,1,usage,pitch*height,graphics_parameters[4],width,height};
        memcpy(graphics_pointer(destination,sizeof(description)),description,sizeof(description));return;
    }
    struct GraphicsTextureObject *object=&graphics_textures[graphics_texture_slot(header)];
    if(level>=object->levels)stop("texture description level out of range",level);
    uint32_t description[]={object->format,object->parent?1:3,usage,graphics_level_bytes(object,level),0x11,
        graphics_level_width(object,level),graphics_level_height(object,level)};
    memcpy(graphics_pointer(destination,sizeof(description)),description,sizeof(description));
}
static void graphics_texture_lock(uint32_t header,unsigned level,uint32_t destination,uint32_t rectangle,uint32_t flags)
{
    struct GraphicsTextureObject *object=&graphics_textures[graphics_texture_slot(header)];
    if(object->guest_owned)stop("caller-owned GPU alias CPU lock outside qualified scope",header);
    if(level>=object->levels || (flags&~0xf0u))stop("unqualified texture lock flags/level",flags);
    uint32_t width=graphics_level_width(object,level),height=graphics_level_height(object,level);
    uint32_t pitch=width*graphics_texture_bpp(object),data=object->data+graphics_level_offset(object,level);
    if(graphics_texture_linear(object))pitch=(pitch+63)&~63u;
    if(object->usage) {
        struct GraphicsTextureObject *texture=object->parent?&graphics_textures[object->parent-1]:object;
        pilot_gfx_texture_download(texture->native,memory+texture->data,texture->bytes);
        object->cpu_exposed=1;
        texture->family_exposed=1;
    }
    uint32_t block=graphics_texture_block(object);
    if(block)pitch=((width+3)/4)*block;
    if(rectangle) {
        if(block)stop("compressed texture subrectangle lock outside scope",rectangle);
        uint32_t rect[4]; memcpy(rect,graphics_pointer(rectangle,16),16);
        if(rect[0]>=rect[2] || rect[1]>=rect[3] || rect[2]>width || rect[3]>height)
            stop("invalid texture lock rectangle",rectangle);
        data+=rect[1]*pitch+rect[0]*graphics_texture_bpp(object);
    }
    /* Sample-only textures have no GPU writes to wait for. Xbox unlock can be
     * inline/no-work; expose the actual shared CPU bytes and upload at handoff.
     * TILED/cache aliases share this same canonical HLE guest pointer. */
    uint32_t result[]={pitch,data};
    memcpy(graphics_pointer(destination,sizeof(result)),result,sizeof(result));
}
