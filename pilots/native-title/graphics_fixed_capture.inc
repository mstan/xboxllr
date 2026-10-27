/* Optional submitted-input observation. Fixed and programmable captures have
 * distinct magic values; program captures retain the actual bounded NV2A code.
 * No expected output or replacement program is invented for a captured draw. */
static void graphics_fixed_capture_inputs(const char *directory,unsigned sequence,uint32_t shader,
    const uint32_t args[3],uint32_t up_data,uint32_t up_stride,const void *indices,unsigned index_count,int32_t base)
{
    struct GraphicsShader *declaration=(shader&1)?graphics_shader(shader):NULL;
    if(!declaration && (shader&15)!=2 && (shader&15)!=4)return;
    unsigned program_words=declaration && !declaration->fixed?MEM32(declaration->header+8):0;
    if(program_words>136)stop("draw input capture program extent",shader);
    struct PilotVertexElement elements[16]={0};uint32_t extents[16]={0};
    if(declaration) {
        for(unsigned i=0;i<16;++i) {
            uint32_t at=declaration->header+0x14+i*16,type=MEM32(at+8);
            if(type!=2)elements[i]=(struct PilotVertexElement){type,MEM32(at),MEM32(at+4)};
        }
        pilot_gfx_shader_streams(extents);
    } else extents[0]=pilot_gfx_fvf_layout(shader,elements);
    uint32_t streams[16][3]={0};const void *data[16]={0};uint64_t total=(uint64_t)index_count*2+program_words*16;
    for(unsigned i=0;i<16;++i)if(extents[i]) {
        uint32_t stride=up_data?up_stride:MEM32(PILOT_D3D_STREAMS+i*12);
        uint64_t bytes=(uint64_t)(args[2]-1)*stride+extents[i];
        if(bytes>UINT32_MAX || (up_data && i))stop("fixed capture stream extent",i);
        streams[i][0]=stride;streams[i][1]=(uint32_t)bytes;
        if(up_data)data[i]=memory+up_data;
        else {
            streams[i][2]=MEM32(PILOT_D3D_STREAMS+i*12+8);
            data[i]=memory+graphics_vertex(streams[i][2])->data+(uint64_t)args[1]*stride;
        }
        total+=bytes;
    }
    static uint64_t captured_bytes;
    if(total>32u*1024*1024-captured_bytes)return;
    captured_bytes+=total;
    char path[1024];if(snprintf(path,sizeof(path),"%s/%s-%u.bin",directory,program_words?"program":"fixed",sequence)>=(int)sizeof(path))stop("fixed capture path",0);
    FILE *file=fopen(path,"wb");if(!file)stop("fixed capture open",0);
    uint32_t header[]={program_words?0x50444c58:0x46444c58,1,MEM32(g_esp),shader,args[0],args[1],args[2],index_count,(uint32_t)base,
        graphics_target,graphics_depth,166,128,160,16,program_words};
#define FIXED_CAPTURE(value,bytes) do{if(fwrite(value,bytes,1,file)!=1)stop("fixed capture write",0);}while(0)
    FIXED_CAPTURE(header,sizeof(header));FIXED_CAPTURE(elements,sizeof(elements));FIXED_CAPTURE(streams,sizeof(streams));
    if(!graphics_render_capture(file))stop("fixed capture render states",0);
    FIXED_CAPTURE(memory+PILOT_D3D_TEXTURE_STATES,128*4);
    FIXED_CAPTURE(memory+TRANSFORM_STATE(TRANSFORMS),160*4);
    if(program_words)FIXED_CAPTURE(declaration->program,program_words*16);
    for(unsigned i=0;i<16;++i)if(streams[i][1])FIXED_CAPTURE(data[i],streams[i][1]);
    if(index_count)FIXED_CAPTURE(indices,index_count*2);
#undef FIXED_CAPTURE
    if(fclose(file))stop("fixed capture close",0);
    fprintf(stderr,"[FIXED INPUT CAPTURE] sequence=%u caller=%08x shader=%08x vertices=%u indices=%u bytes=%llu file=%s\n",
        sequence,MEM32(g_esp),shader,args[2],index_count,(unsigned long long)total,path);
}
