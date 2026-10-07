/* Xbox shader handles are odd guest addresses. Native compiled objects and
 * declaration constants remain owned here, never encoded as host pointers. */
struct GraphicsShader {
    uint32_t header,native;
    uint32_t constant_mask[6],constants[192][4];
};
static struct GraphicsShader graphics_shaders[256];
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
