/* CPU-owned Xbox texture bytes and guest headers, backed by actual native
 * texture storage. Original XDK routines and XbD3D8Types supply layout facts.
 * Addresses are guest allocations; no native pointer enters a resource header.
 */
#define GRAPHICS_TEXTURE_OBJECTS 512
struct GraphicsTextureObject {
    uint32_t header,data,bytes,width,height,levels,format,refs,native;
    unsigned parent,level; /* parent is slot+1 for a surface view, zero for texture */
};
static struct GraphicsTextureObject graphics_textures[GRAPHICS_TEXTURE_OBJECTS];

static unsigned graphics_texture_slot(uint32_t header)
{
    for(unsigned i=0;i<GRAPHICS_TEXTURE_OBJECTS;++i)
        if(graphics_textures[i].header==header && header) return i;
    stop("unowned texture resource",header);
}
static unsigned graphics_texture_unused(void)
{
    for(unsigned i=0;i<GRAPHICS_TEXTURE_OBJECTS;++i)
        if(!graphics_textures[i].header) return i;
    stop("texture resource slots exhausted",GRAPHICS_TEXTURE_OBJECTS);
}
static uint32_t graphics_log2(uint32_t value)
{
    uint32_t result=0; while(value>1) { ++result; value>>=1; } return result;
}
static uint32_t graphics_texture_bpp(const struct GraphicsTextureObject *texture)
{
    return texture->format<=5?2:4;
}
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
    return graphics_level_width(texture,level)*graphics_level_height(texture,level)*graphics_texture_bpp(texture);
}
static uint32_t graphics_level_offset(const struct GraphicsTextureObject *texture,unsigned level)
{
    uint32_t offset=0; for(unsigned i=0;i<level;++i)offset+=graphics_level_bytes(texture,i); return offset;
}
static void graphics_texture_header(struct GraphicsTextureObject *object)
{
    MEM32(object->header)=(object->parent?0x01050000u:0x01040000u)|object->refs;
    MEM32(object->header+4)=object->data; MEM32(object->header+8)=0;
    MEM32(object->header+12)=0x29u|(object->format<<8)|(object->levels<<16)|
        (graphics_log2(object->width)<<20)|(graphics_log2(object->height)<<24);
    MEM32(object->header+16)=0;
    if(object->parent) MEM32(object->header+20)=graphics_textures[object->parent-1].header;
}
static uint32_t graphics_texture_allocate(const uint32_t *args)
{
    uint32_t width=args[0],height=args[1],levels=args[3],format=args[5];
    if(args[2]!=1 || args[4] || args[6]!=3 || format<4 || format>7)
        stop("unqualified texture type/usage/format",format);
    if(!width || !height || width>4096 || height>4096 || (width&(width-1)) || (height&(height-1)))
        stop("unqualified texture dimensions",width);
    unsigned max_levels=graphics_log2(width>height?width:height)+1;
    if(!levels)levels=max_levels;
    if(levels>max_levels)stop("texture mip chain exceeds dimensions",levels);
    struct GraphicsTextureObject texture={0};
    texture.width=width; texture.height=height; texture.levels=levels; texture.format=format; texture.refs=1;
    texture.bytes=graphics_level_offset(&texture,levels);
    unsigned slot=graphics_texture_unused(); uint32_t bytes=4096;
    if(vm_allocate(&texture.header,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)) return 0;
    bytes=texture.bytes;
    if(vm_allocate(&texture.data,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)) {
        bytes=0; if(vm_free(&texture.header,&bytes,MEM_RELEASE))stop("texture header cleanup failed",texture.header);
        return 0;
    }
    int32_t result=pilot_gfx_texture_create(width,height,levels,format,&texture.native);
    if(result<0) {
        bytes=0; if(vm_free(&texture.data,&bytes,MEM_RELEASE))stop("texture data cleanup failed",texture.data);
        bytes=0; if(vm_free(&texture.header,&bytes,MEM_RELEASE))stop("texture header cleanup failed",texture.header);
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
static uint32_t graphics_texture_ref(unsigned slot,int acquire)
{
    struct GraphicsTextureObject *object=&graphics_textures[slot];
    if(!object->header || !object->refs || (acquire && object->refs==0xffff))
        stop("invalid texture reference lifetime",object->header);
    if(acquire)++object->refs; else --object->refs;
    MEM32(object->header)=(MEM32(object->header)&~0xffffu)|object->refs;
    if(object->refs)return object->refs;
    unsigned parent=object->parent;
    if(parent)graphics_texture_upload(&graphics_textures[parent-1],object->level);
    else {
        pilot_gfx_texture_destroy(object->native);
        uint32_t bytes=0; if(vm_free(&object->data,&bytes,MEM_RELEASE))stop("texture data release failed",object->data);
    }
    uint32_t bytes=0;
    if(vm_free(&object->header,&bytes,MEM_RELEASE))stop("texture header release failed",object->header);
    memset(object,0,sizeof(*object));
    if(parent)graphics_texture_ref(parent-1,0);
    return 0;
}
static uint32_t graphics_texture_surface(uint32_t header,unsigned level)
{
    unsigned parent=graphics_texture_slot(header),slot=graphics_texture_unused();
    struct GraphicsTextureObject *texture=&graphics_textures[parent];
    if(texture->parent || level>=texture->levels)stop("invalid texture surface level",level);
    struct GraphicsTextureObject surface={0}; uint32_t bytes=4096;
    if(vm_allocate(&surface.header,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return 0;
    surface.parent=parent+1; surface.level=level; surface.levels=1; surface.refs=1;
    surface.width=graphics_level_width(texture,level); surface.height=graphics_level_height(texture,level);
    surface.format=texture->format; surface.data=texture->data+graphics_level_offset(texture,level);
    surface.bytes=graphics_level_bytes(texture,level);
    graphics_texture_ref(parent,1); graphics_texture_header(&surface); graphics_textures[slot]=surface;
    return surface.header;
}
static void graphics_texture_desc(uint32_t header,uint32_t destination)
{
    struct GraphicsTextureObject *object=&graphics_textures[graphics_texture_slot(header)];
    if(!object->parent)stop("surface description requires a surface",header);
    uint32_t description[]={object->format,1,0,object->bytes,0x11,object->width,object->height};
    memcpy(graphics_pointer(destination,sizeof(description)),description,sizeof(description));
}
static void graphics_texture_lock(uint32_t header,unsigned level,uint32_t destination,uint32_t rectangle,uint32_t flags)
{
    struct GraphicsTextureObject *object=&graphics_textures[graphics_texture_slot(header)];
    if(level>=object->levels || (flags&~0xf0u))stop("unqualified texture lock flags/level",flags);
    uint32_t width=graphics_level_width(object,level),height=graphics_level_height(object,level);
    uint32_t pitch=width*graphics_texture_bpp(object),data=object->data+graphics_level_offset(object,level);
    if(rectangle) {
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
