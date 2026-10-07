/* Xbox shader handles are odd guest addresses. Native compiled objects and
 * declaration constants remain owned here, never encoded as host pointers. */
struct GraphicsShader {
    uint32_t header,native;
    uint32_t constant_mask[6],constants[192][4];
    uint32_t program[136][4];
};
static struct GraphicsShader graphics_shaders[256];
static struct GraphicsShader *graphics_shader(uint32_t handle)
{
    if(handle&1)for(unsigned i=0;i<256;++i)
        if(graphics_shaders[i].header==handle-1)return &graphics_shaders[i];
    stop("unowned or unqualified vertex shader binding",handle);
}
static int graphics_shader_records(void)
{
    return !(MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_RECORD_FLAGS)&PILOT_D3D_VERTEX_RECORD_DISABLE);
}
static void graphics_shader_constants(uint32_t first,uint32_t data,uint32_t words,int record)
{
    if(first>=192 || !words || (words&3) || words/4>192-first)
        stop("unqualified vertex constant range",first);
    const void *source=graphics_pointer(data,words*4);
    pilot_gfx_shader_constants(first,words/4,source);
    if(record)memmove(memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS+first*16,source,words*4);
}
static void graphics_shader_get_constants(const uint32_t *args)
{
    int64_t first=(int32_t)args[0];first+=96;
    if(first<0 || first>=192 || !args[2] || args[2]>192-(uint32_t)first)
        stop("unqualified vertex constant query",args[0]);
    memmove(graphics_pointer(args[1],args[2]*16),
        memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS+(uint32_t)first*16,args[2]*16);
}
static void graphics_shader_bind(uint32_t handle)
{
    if(!(handle&1)) {
        pilot_gfx_fvf_extent(handle);
        uint32_t object=PILOT_D3D_FVF_OBJECT,flags=2,offset=16,dimensions=0;
        FILE *capture_file=NULL;const char *capture=getenv("XLLE_PILOT_FVF_CAPTURE");
        if(capture && *capture) {
            static unsigned sequence;char path[1024];uint32_t metadata[]={handle,0x114};
            if(snprintf(path,sizeof(path),"%s/fvf-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("FVF capture path",0);
            capture_file=fopen(path,"wb");
            if(!capture_file || fwrite(metadata,sizeof(metadata),1,capture_file)!=1 ||
                fwrite(memory+object,0x114,1,capture_file)!=1)stop("FVF capture input",0);
        }
        for(unsigned i=0;i<16;++i)MEM32(object+0x1c+i*16)=2;
        MEM32(object+0x1c)=0x42;
        for(unsigned i=0;i<2;++i)if(handle&(0x40u<<i)) {
            flags|=0x400u<<i;MEM32(object+0x48+i*16)=offset;MEM32(object+0x4c+i*16)=0x40;offset+=4;
        }
        for(unsigned i=0;i<((handle>>8)&15);++i) {
            unsigned lanes=((((handle>>(16+i*2))+1)&3)+1);
            MEM32(object+0xa8+i*16)=offset;MEM32(object+0xac+i*16)=(lanes<<4)|2;
            dimensions|=lanes<<(i*8);offset+=lanes*4;
        }
        MEM32(object+4)=flags;MEM32(object+0x10)=dimensions;
        if(capture_file && (fwrite(memory+object,0x114,1,capture_file)!=1 || fclose(capture_file)))stop("FVF capture output",0);
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER)=handle;
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)=object;
        MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_START)=0;
        /* Native state is bound lazily at the draw; the FVF descriptor remains
         * queryable immediately. Original programmable constant/cache data is
         * preserved across fixed-function selection. */
        return;
    }
    struct GraphicsShader *shader=graphics_shader(handle);
    int32_t result=pilot_gfx_shader_bind(shader->native);
    if(result<0)stop("native vertex shader binding failed",(uint32_t)result);
    int record=graphics_shader_records();
    for(unsigned i=0;i<192;++i)if(shader->constant_mask[i/32]&(1u<<(i%32))) {
        pilot_gfx_shader_constants(i,1,shader->constants[i]);
        if(record)memcpy(memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_CONSTANTS+i*16,shader->constants[i],16);
    }
    if(record)memcpy(memory+PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_PROGRAM_CACHE,
        shader->program,MEM32(shader->header+8)*16);
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_SHADER)=handle;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_SNAPSHOT_VERTEX_OBJECT)=shader->header;
    MEM32(PILOT_D3D_DEVICE_OBJECT+PILOT_D3D_VERTEX_START)=0;
    /* Binding has no public shader AddRef in the original load/select path. */
}
static uint32_t graphics_shader_create(const uint32_t *args)
{
    if(args[3] || !args[0] || !args[1] || !args[2])stop("unqualified shader creation arguments",args[3]);
    graphics_pointer(args[2],4);
    uint32_t header=MEM32(args[1]),count=header>>16;
    if((header&0xffffu)!=0x2078 || !count || count>136)stop("unqualified shader program type/length",header);
    const uint32_t *code=graphics_pointer(args[1]+4,count*16);
    struct PilotVertexElement elements[16]={0};
    struct GraphicsShader shader={0};
    uint32_t stream=0,offset=0,declaration[1024],words=0;
    int ended=0;
    while(words<1024) {
        uint32_t token=MEM32(args[0]+words*4);declaration[words++]=token;
        if(token==0xffffffffu) {ended=1;break;}
        uint32_t type=token>>29;
        if(!type)continue; /* Original parser ignores all NOP-token payload bits. */
        if(type==1) {
            if(token&~0x2000000fu)stop("unqualified tessellation stream declaration",token);
            stream=token&15;offset=0;
        } else if(type==2) {
            if(token&0x10000000u) {
                if(token&~0x580f0000u)stop("unqualified vertex skip declaration",token);
                offset+=((token>>16)&15)*((token&0x08000000u)?1:4);
            } else {
                uint32_t reg=token&31,format=(token>>16)&255;
                if(reg>=16 || elements[reg].type || (token&~0x40ff001fu))stop("invalid vertex register declaration",token);
                elements[reg]=(struct PilotVertexElement){format,stream,offset};
                offset+=pilot_gfx_vertex_type_size(format);
            }
            if(offset>2048)stop("vertex declaration exceeds native stride limit",offset);
        } else if(type==4) {
            uint32_t first=token&255,registers=(token>>25)&15;
            if((token&~0x9e0000ffu) || !registers || first+registers>192 || words+registers*4>1024)
                stop("invalid declaration constant block",token);
            for(unsigned i=0;i<registers;++i) {
                shader.constant_mask[(first+i)/32]|=1u<<((first+i)%32);
                for(unsigned j=0;j<4;++j) {
                    uint32_t value=MEM32(args[0]+words*4);declaration[words++]=value;shader.constants[first+i][j]=value;
                }
            }
        } else stop("unqualified vertex declaration token",token);
    }
    if(!ended)stop("unterminated vertex declaration",args[0]);
    unsigned slot;
    for(slot=0;slot<256 && graphics_shaders[slot].header;++slot){}
    if(slot==256)return 0x8007000eu;
    uint32_t bytes=4096;
    if(vm_allocate(&shader.header,0,&bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return 0x8007000eu;
    /* Private diagnostic captures retain actual declaration/program inputs.
     * They never replace execution and are disabled in ordinary runs. */
    const char *capture=getenv("XLLE_PILOT_SHADER_CAPTURE");
    if(capture && *capture) {
        static unsigned sequence;char path[1024];
        if(snprintf(path,sizeof(path),"%s/shader-%u.bin",capture,++sequence)>=(int)sizeof(path))stop("shader input capture path too long",0);
        FILE *file=fopen(path,"wb");uint32_t metadata[]={0x56534c58,1,count,words,args[3],0,0,0};
        if(!file || fwrite(metadata,sizeof(metadata),1,file)!=1 || fwrite(declaration,words*4,1,file)!=1 ||
            fwrite(code,count*16,1,file)!=1 || fclose(file))stop("shader input capture failed",0);
    }
    int32_t result=pilot_gfx_shader_create(code,count,elements,&shader.native);
    if(result<0) {
        bytes=0;if(vm_free(&shader.header,&bytes,MEM_RELEASE))stop("shader header cleanup failed",shader.header);
        if((uint32_t)result!=0x8007000eu)stop("native shader creation failed",(uint32_t)result);
        return (uint32_t)result;
    }
    /* Retain the bounded Xbox descriptor fields: ownership, program mode,
     * instruction count and each stream/offset/format. Driver push packets
     * are private to the ORIGINAL implementation, not native shader storage. */
    MEM32(shader.header)=1;MEM32(shader.header+4)=0x10;MEM32(shader.header+8)=count;
    for(unsigned i=0;i<16;++i) {
        MEM32(shader.header+0x14+i*16)=elements[i].stream;
        MEM32(shader.header+0x18+i*16)=elements[i].offset;
        MEM32(shader.header+0x1c+i*16)=elements[i].type?elements[i].type:2;
    }
    memcpy(shader.program,code,count*16);
    graphics_shaders[slot]=shader;MEM32(args[2])=shader.header|1u;return 0;
}
static void graphics_shaders_release(void)
{
    for(unsigned i=0;i<256;++i)if(graphics_shaders[i].header) {
        pilot_gfx_shader_destroy(graphics_shaders[i].native);
        uint32_t bytes=0;
        if(vm_free(&graphics_shaders[i].header,&bytes,MEM_RELEASE))stop("shader header release failed",i);
        memset(&graphics_shaders[i],0,sizeof(graphics_shaders[i]));
    }
}
