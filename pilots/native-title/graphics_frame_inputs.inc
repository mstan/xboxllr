/* Private complete-frame inputs. Included only in the diagnostic build.
 * Assets are content-addressed; target roots are supplied only once. Later
 * target images are observations, never replacement starting expectations.
 * Storage creation/reset inside this interval is explicitly unsupported.
 */
#define FRAME_INPUT_MAGIC 0x49464c58u
#define FRAME_INPUT_MAX_ASSETS 32768u
#define FRAME_INPUT_MAX_BYTES (1024ull*1024*1024)
static void graphics_gpu_barrier(void);
struct GraphicsFrameAsset { uint32_t id,bytes; unsigned char hash[32]; };
struct GraphicsFrameTarget { uint32_t id,header,key,level,role,shape[3]; };
static struct {
    HANDLE file;
    struct GraphicsFrameAsset *assets;
    struct GraphicsFrameTarget targets[64];
    uint32_t asset_count,target_count,draws,input_sequence,root_color,root_depth,root_ready;
    uint64_t bytes;
} graphics_frame_inputs;

static void graphics_frame_inputs_write(const void *data,uint32_t bytes)
{
    DWORD written;
    if(graphics_frame_inputs.bytes+bytes>FRAME_INPUT_MAX_BYTES ||
       !WriteFile(graphics_frame_inputs.file,data,bytes,&written,NULL) || written!=bytes)
        stop("frame input write/bound",bytes);
    graphics_frame_inputs.bytes+=bytes;
}
static void graphics_frame_inputs_record(uint32_t type,const void *data,uint32_t bytes)
{
    uint32_t header[]={type,bytes};
    graphics_frame_inputs_write(header,sizeof(header));graphics_frame_inputs_write(data,bytes);
}
static uint32_t graphics_frame_inputs_asset(const void *data,uint32_t bytes)
{
    if(!bytes)return 0;
    if(!data || bytes>64u*1024*1024)stop("frame input asset extent",bytes);
    unsigned char hash[32];uint32_t bucket;
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,NULL,0,(PUCHAR)data,bytes,hash,32)<0)
        stop("frame input hash",0);
    memcpy(&bucket,hash,4);bucket&=65535;
    while(graphics_frame_inputs.assets[bucket].id) {
        struct GraphicsFrameAsset *entry=&graphics_frame_inputs.assets[bucket];
        if(entry->bytes==bytes && !memcmp(entry->hash,hash,32))return entry->id;
        bucket=(bucket+1)&65535;
    }
    if(graphics_frame_inputs.asset_count==FRAME_INPUT_MAX_ASSETS)stop("frame input asset count",0);
    struct GraphicsFrameAsset *entry=&graphics_frame_inputs.assets[bucket];
    entry->id=++graphics_frame_inputs.asset_count;entry->bytes=bytes;memcpy(entry->hash,hash,32);
    uint32_t record[]={1,40+bytes,entry->id,bytes};
    graphics_frame_inputs_write(record,sizeof(record));graphics_frame_inputs_write(hash,32);
    graphics_frame_inputs_write(data,bytes);return entry->id;
}
static struct GraphicsTextureObject *graphics_frame_inputs_texture(uint32_t header)
{
    /* Never adopt an alias or change exposure/ownership from a diagnostic. */
    for(unsigned i=0;i<graphics_texture_capacity;++i)
        if(header && graphics_textures[i].header==header)return &graphics_textures[i];
    stop("frame input unowned texture",header);
}
static uint32_t graphics_frame_inputs_key(const struct GraphicsTextureObject *texture)
{
    if(texture->guest_owned) {
        if(texture->alias_kind==PILOT_GFX_SURFACE && texture->alias_source<2)
            return texture->alias_source?0xfffffffcu:0xfffffffdu;
        if(texture->alias_kind==PILOT_GFX_TEXTURE)return texture->alias_source;
        stop("frame input unknown alias",texture->header);
    }
    return texture->native;
}
static uint32_t graphics_frame_inputs_find_target(uint32_t key,uint32_t level)
{
    for(unsigned i=0;i<graphics_frame_inputs.target_count;++i)
        if(graphics_frame_inputs.targets[i].key==key && graphics_frame_inputs.targets[i].level==level)
            return graphics_frame_inputs.targets[i].id;
    return 0;
}
static uint32_t graphics_frame_inputs_target(unsigned depth,uint32_t call,int observe)
{
    uint32_t guest=depth?graphics_depth:graphics_target,shape[3];
    int32_t status=pilot_gfx_reference_target_readback(depth,NULL,0,shape);
    if(status<0 || (status==1)!=(!guest))stop("frame input target shape",guest);
    if(status==1)return 0;
    uint64_t bytes=(uint64_t)shape[0]*shape[1]*4;
    if(!shape[0] || !shape[1] || bytes>64u*1024*1024)stop("frame input target extent",guest);
    uint32_t key=depth?0xfffffffeu:0xfffffffdu,level=0;
    if(guest!=graphics_headers && guest!=graphics_headers+64) {
        struct GraphicsTextureObject *view=graphics_frame_inputs_texture(guest);
        struct GraphicsTextureObject *base=view->parent?&graphics_textures[view->parent-1]:view;
        key=graphics_frame_inputs_key(base);level=view->parent?view->level:0;
    }
    uint32_t id=graphics_frame_inputs_find_target(key,level);
    if(observe && !id)stop("frame input observation lacks target root",guest);
    if(id) {
        struct GraphicsFrameTarget *target=&graphics_frame_inputs.targets[id-1];
        if(target->role!=depth || memcmp(shape,target->shape,sizeof(shape)))
            stop("frame input target identity changed",guest);
        if(!observe)return id;
    } else {
        if(graphics_frame_inputs.target_count==64)stop("frame input target count",guest);
        id=++graphics_frame_inputs.target_count;
        struct GraphicsFrameTarget *target=&graphics_frame_inputs.targets[id-1];
        *target=(struct GraphicsFrameTarget){id,guest,key,level,depth,{shape[0],shape[1],shape[2]}};
    }
    void *pixels=malloc((size_t)bytes);if(!pixels)stop("frame input target allocation",guest);
    uint32_t actual[3];
    if(pilot_gfx_reference_target_readback(depth,pixels,(uint32_t)bytes,actual)<0 || memcmp(shape,actual,sizeof(shape)))
        stop("frame input target readback",guest);
    uint32_t asset=graphics_frame_inputs_asset(pixels,(uint32_t)bytes);free(pixels);
    if(observe) {uint32_t record[]={id,call,asset};graphics_frame_inputs_record(3,record,sizeof(record));}
    else {
        uint32_t record[]={id,call,guest,key,level,depth,shape[0],shape[1],shape[2],asset,
            MEM32(guest),MEM32(guest+4),MEM32(guest+8),MEM32(guest+12),MEM32(guest+16)};
        graphics_frame_inputs_record(2,record,sizeof(record));
    }
    return id;
}
static void graphics_frame_inputs_open(uint32_t swap)
{
    const char *enabled=getenv("XLLE_PILOT_FRAME_INPUTS");
    if(!enabled || !*enabled || !strcmp(enabled,"0"))return;
    if(strcmp(enabled,"1"))stop("frame input option",0);
    char path[1024];
    if(snprintf(path,sizeof(path),"%s/frame-inputs.bin",graphics_frame_audit.directory)>=(int)sizeof(path))
        stop("frame input path",0);
    graphics_frame_inputs.file=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    if(graphics_frame_inputs.file==INVALID_HANDLE_VALUE)stop("frame input create",GetLastError());
    graphics_frame_inputs.assets=calloc(65536,sizeof(*graphics_frame_inputs.assets));
    if(!graphics_frame_inputs.assets)stop("frame input table allocation",0);
    uint32_t header[]={FRAME_INPUT_MAGIC,2,swap,graphics_parameters[0],graphics_parameters[1],graphics_target,graphics_depth,0};
    graphics_frame_inputs_write(header,sizeof(header));
}
static void graphics_frame_inputs_before(unsigned entry,const uint32_t *arguments)
{
    graphics_frame_inputs.input_sequence=0;
    if(!graphics_frame_inputs.file)return;
    const char *name=pilot_d3d_calls[entry].name;
    if(strstr(name,"CreateTexture") || strstr(name,"CreateCubeTexture") || strstr(name,"CreateVolumeTexture") ||
       entry==PILOT_D3D_Direct3D_CreateDevice || entry==PILOT_D3D_D3DDevice_Reset || entry==PILOT_D3D_D3DDevice_Release ||
       strstr(name,"Surface_Lock") || strstr(name,"Texture_Lock"))
        stop("frame input storage lifetime/CPU target mutation outside scope",entry);
    if(!graphics_frame_inputs.root_ready) {
        /* InsertCallback ordinarily preserves its queued barrier. A root needs
         * settled GPU storage before this first returned operation. */
        if(entry==PILOT_D3D_D3DDevice_InsertCallback)graphics_gpu_barrier();
        graphics_frame_inputs.root_color=graphics_frame_inputs_target(0,0,0);
        graphics_frame_inputs.root_depth=graphics_frame_inputs_target(1,0,0);
        uint32_t root[]={graphics_frame_inputs.root_color,graphics_frame_inputs.root_depth};
        graphics_frame_inputs_record(7,root,sizeof(root));graphics_frame_inputs.root_ready=1;
    }
    uint32_t call=graphics_frame_audit.calls+1;
    if(entry==PILOT_D3D_D3DDevice_SetRenderTarget || entry==PILOT_D3D_D3D_CommonSetRenderTarget || entry==PILOT_D3D_D3DDevice_Swap) {
        graphics_frame_inputs_target(0,call,1);graphics_frame_inputs_target(1,call,1);
    }
    if(entry==PILOT_D3D_D3DDevice_Clear) {
        uint32_t record[]={call,arguments[0],arguments[1],arguments[2],arguments[3],arguments[4],arguments[5],
            graphics_frame_inputs_target(0,call,0),graphics_frame_inputs_target(1,call,0)};
        graphics_frame_inputs_record(5,record,sizeof(record));
    }
}
static void graphics_frame_inputs_after(unsigned entry)
{
    if(!graphics_frame_inputs.file)return;
    if(entry==PILOT_D3D_D3DDevice_SetRenderTarget || entry==PILOT_D3D_D3D_CommonSetRenderTarget) {
        uint32_t record[]={graphics_frame_audit.calls,graphics_frame_inputs_target(0,graphics_frame_audit.calls,0),
            graphics_frame_inputs_target(1,graphics_frame_audit.calls,0),graphics_target,graphics_depth,
            graphics_frame_inputs_asset(graphics_pointer(graphics_target,20),20),
            graphics_depth?graphics_frame_inputs_asset(graphics_pointer(graphics_depth,20),20):0};
        graphics_frame_inputs_record(6,record,sizeof(record));
    }
}
static uint32_t graphics_frame_inputs_sample(uint32_t header)
{
    if(!header)return 0;
    struct GraphicsTextureObject *t=graphics_frame_inputs_texture(header);
    struct GraphicsTextureObject *base=t->parent?&graphics_textures[t->parent-1]:t;
    uint32_t first=t->parent?t->level:0,key=graphics_frame_inputs_key(base);
    if(!t->levels || t->levels>13)stop("frame input mip count",header);
    uint32_t words[18+13*9]={header,t->native,t->alias_kind,t->alias_source,t->alias_format,t->alias_size,
        t->guest_owned,t->usage,t->format,t->width,t->height,t->levels,
        graphics_frame_inputs_asset(graphics_pointer(header,20),20),0,t->parent?base->header:0,key,first,0};
    if(!t->guest_owned)words[13]=graphics_frame_inputs_asset(graphics_pointer(t->data,t->bytes),t->bytes);
    for(unsigned level=0;level<t->levels;++level) {
        unsigned width=graphics_level_width(t,level),height=graphics_level_height(t,level);
        unsigned block=graphics_texture_block(t),yuv=t->format==0x24 || t->format==0x25;
        unsigned pitch=block?((width+3)/4)*block:width*(yuv?4:graphics_texture_bpp(t));
        unsigned rows=block?(height+3)/4:height;uint64_t bytes=(uint64_t)pitch*rows;
        if(!bytes || bytes>64u*1024*1024)stop("frame input mip extent",header);
        void *data=malloc((size_t)bytes);if(!data)stop("frame input mip allocation",header);
        if(pilot_gfx_texture_readback(base->native,first+level,data,(uint32_t)bytes)<0)
            stop("frame input mip readback",header);
        uint32_t mip[]={level,width,height,pitch,rows,(uint32_t)bytes,yuv,graphics_frame_inputs_asset(data,(uint32_t)bytes),
            graphics_frame_inputs_find_target(key,first+level)};
        memcpy(words+18+level*9,mip,sizeof(mip));free(data);
    }
    return graphics_frame_inputs_asset(words,(18+t->levels*9)*4);
}
static void graphics_frame_inputs_draw(const uint32_t args[3],uint32_t up_data,uint32_t up_stride,
    uint32_t streams,const uint32_t extents[16])
{
    if(!graphics_frame_inputs.file)return;
    if(graphics_frame_inputs.draws==32768 || args[2]>65536)stop("frame input draw extent",args[2]);
    struct {
        uint32_t id,call,kind,primitive,first,count,shader,up_data,up_stride,target,depth;
        uint32_t viewport[6],screen[2],depth_scale;
        uint32_t instructions,code,constants,elements,fvf_descriptor,render,texture,pixel_handle,pixel,packed;
        uint32_t streams[16][6],textures[4];
    } draw={0};
    draw.id=++graphics_frame_inputs.draws;draw.call=graphics_frame_audit.calls+1;
    draw.primitive=args[0];draw.first=args[1];draw.count=args[2];draw.up_data=up_data;draw.up_stride=up_stride;
    draw.shader=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER);
    draw.kind=(draw.shader&1?0:2)+(up_data?1:0);
    draw.target=graphics_frame_inputs_target(0,draw.call,0);draw.depth=graphics_frame_inputs_target(1,draw.call,0);
    if(pilot_gfx_get_viewport(draw.viewport)<0)stop("frame input viewport",0);
    draw.screen[0]=MEM32(TRANSFORM_FIELD(SCREEN_OFFSET));draw.screen[1]=MEM32(TRANSFORM_FIELD(SCREEN_OFFSET)+4);
    draw.depth_scale=MEM32(TRANSFORM_FIELD(DEPTH_SCALE));
    draw.render=graphics_frame_inputs_asset(memory+PILOT_D3D_RENDER_STATES,166*4);
    draw.texture=graphics_frame_inputs_asset(memory+PILOT_D3D_TEXTURE_STATES,128*4);
    draw.pixel_handle=MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_PIXEL_SHADER);
    if(draw.pixel_handle)draw.pixel=graphics_frame_inputs_asset(graphics_pointer(draw.pixel_handle+12,240),240);
    draw.packed=graphics_frame_inputs_asset(memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_PIXEL_PACKED_CONSTANTS,64);
    if(draw.shader&1) {
        struct GraphicsShader *shader=graphics_shader(draw.shader);uint32_t elements[16][3],constants[192][4];
        draw.instructions=MEM32(shader->header+8);
        if(!draw.instructions || draw.instructions>136)stop("frame input program extent",draw.instructions);
        draw.code=graphics_frame_inputs_asset(shader->program,draw.instructions*16);
        for(unsigned i=0;i<16;++i) {
            uint32_t descriptor=shader->header+0x14+i*16;
            elements[i][0]=MEM32(descriptor+8);elements[i][1]=MEM32(descriptor);elements[i][2]=MEM32(descriptor+4);
        }
        draw.elements=graphics_frame_inputs_asset(elements,sizeof(elements));
        pilot_gfx_shader_capture_constants(constants);draw.constants=graphics_frame_inputs_asset(constants,sizeof(constants));
    } else draw.fvf_descriptor=graphics_frame_inputs_asset(graphics_pointer(
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT),0x114),0x114);
    for(unsigned i=0;i<16;++i)if(streams&(1u<<i)) {
        uint32_t stride=up_data?up_stride:MEM32(PILOT_D3D_STREAMS+i*12),guest=0,address=up_data;
        uint64_t bytes=(uint64_t)(draw.count-1)*stride+extents[i];
        if(!up_data) {
            guest=MEM32(PILOT_D3D_STREAMS+i*12+8);struct GraphicsVertexObject *v=graphics_vertex(guest);
            if((uint64_t)draw.first*stride+bytes>v->bytes)stop("frame input stream range",i);
            address=v->data+draw.first*stride;
        }
        if(!bytes || bytes>64u*1024*1024)stop("frame input stream extent",i);
        uint32_t record[]={stride,(uint32_t)bytes,graphics_frame_inputs_asset(graphics_pointer(address,(uint32_t)bytes),(uint32_t)bytes),
            guest,address,extents[i]};memcpy(draw.streams[i],record,sizeof(record));
    }
    for(unsigned stage=0;stage<4;++stage)draw.textures[stage]=graphics_frame_inputs_sample(
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_TEXTURES+stage*4));
    graphics_frame_inputs_record(4,&draw,sizeof(draw));graphics_frame_inputs.input_sequence=draw.id;
}
static void graphics_frame_inputs_close(void)
{
    if(!graphics_frame_inputs.file)return;
    if(!graphics_frame_inputs.root_ready)stop("frame input missing root",0);
    uint32_t footer[]={graphics_frame_audit.calls,graphics_frame_inputs.draws,graphics_frame_inputs.asset_count,graphics_frame_inputs.target_count};
    graphics_frame_inputs_record(8,footer,sizeof(footer));
    if(!FlushFileBuffers(graphics_frame_inputs.file) || !CloseHandle(graphics_frame_inputs.file))stop("frame input close",GetLastError());
    graphics_frame_inputs.file=NULL;free(graphics_frame_inputs.assets);graphics_frame_inputs.assets=NULL;
}
