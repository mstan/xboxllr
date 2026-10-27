/* Private bounded observation of an actually submitted indexed program draw.
 * The paired state record supplies original constants; outputs come from the
 * native GPU probe and are observations, never manufactured expectations. */
static int observe_submitted_program(const char *input,const char *state_path,const char *output)
{
    FILE *file=fopen(input,"rb");CHECK(file);
    uint32_t header[16],streams_meta[16][3],render[166],texture[128],code[136*4];
    struct PilotVertexElement elements[16];float matrices[160],constants[192][4];
    CHECK(fread(header,sizeof(header),1,file)==1 && header[0]==0x50444c58 && header[1]==1);
    CHECK(header[6] && header[6]<=1024 && header[7]<=8192 && header[15] && header[15]<=136);
    CHECK(header[11]==166 && header[12]==128 && header[13]==160 && header[14]==16);
    CHECK(fread(elements,sizeof(elements),1,file)==1 && fread(streams_meta,sizeof(streams_meta),1,file)==1);
    CHECK(fread(render,sizeof(render),1,file)==1 && fread(texture,sizeof(texture),1,file)==1);
    CHECK(fread(matrices,sizeof(matrices),1,file)==1 && fread(code,header[15]*16,1,file)==1);
    unsigned char *streams_data[16]={0};struct PilotVertexProbeStream streams[16]={0};
    uint64_t bytes=0;
    for(unsigned i=0;i<16;++i)if(streams_meta[i][1]) {
        unsigned stride=streams_meta[i][0];CHECK(stride && stride<=2048);
        CHECK(streams_meta[i][1]<=header[6]*stride && streams_meta[i][1]>(header[6]-1)*stride);
        bytes+=header[6]*stride;CHECK(bytes<=32u*1024*1024);
        streams_data[i]=calloc(header[6],stride);CHECK(streams_data[i]);
        CHECK(fread(streams_data[i],streams_meta[i][1],1,file)==1);
        streams[i]=(struct PilotVertexProbeStream){streams_data[i],stride};
    }
    uint16_t indices[8192];CHECK(!header[7] || fread(indices,header[7]*2,1,file)==1);
    CHECK(fgetc(file)==EOF);fclose(file);
    file=fopen(state_path,"rb");CHECK(file);uint32_t state_header[16];
    CHECK(fread(state_header,sizeof(state_header),1,file)==1 && state_header[0]==0x53444c58 && state_header[1]==1);
    CHECK(state_header[4]==header[3] && state_header[6]==header[4] && state_header[7]==header[5] && state_header[8]==header[6]);
    CHECK(fseek(file,(166+128)*4,SEEK_CUR)==0 && fread(constants,sizeof(constants),1,file)==1);fclose(file);
    uint32_t shader=0;CHECK(!pilot_gfx_shader_create(code,header[15],elements,&shader));
    float *values=malloc(header[6]*13*16);CHECK(values);
    CHECK(!pilot_gfx_shader_probe(shader,streams,header[6],constants,values));
    file=fopen(output,"wb");CHECK(file);uint32_t shape[]={0x4f444c58,1,header[6],13*4};
    CHECK(fwrite(shape,sizeof(shape),1,file)==1 && fwrite(values,header[6]*13*16,1,file)==1 && !fclose(file));
    pilot_gfx_shader_destroy(shader);free(values);
    for(unsigned i=0;i<16;++i)free(streams_data[i]);
    fprintf(stderr,"Observed submitted program: %u actual vertices, %u instructions, paired constants, raw native GPU output\n",header[6],header[15]);
    return 0;
}
