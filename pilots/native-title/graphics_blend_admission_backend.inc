/* Bounded semantic proof for snapshot float blending. Original shader and
 * resource bytes supply the proof; no captured draw ID or pixel observation. */
static int blend_input(const Nv2aVshInput *input,Nv2aVshRegisterType type,unsigned index,unsigned lanes)
{
    /* The decoder defines relative addressing only for context registers;
     * that member is unspecified for an input register. */
    if(input->type!=type || input->index!=index || input->is_negated ||
       (type==NV2ART_CONTEXT && input->is_relative))return 0;
    for(unsigned lane=0;lane<lanes;++lane)if(input->swizzle[lane]!=lane)return 0;
    return 1;
}
static int shader_blend_recipe(const uint32_t *code,unsigned count,const struct PilotVertexElement elements[16])
{
    static const struct PilotVertexElement declaration[]={
        {0x42,0,0},{0x40,0,16},{0x22,0,20},{0x22,0,28},{0x22,0,36},{0x22,0,44}};
    if(count!=6 || memcmp(elements,declaration,sizeof(declaration)))return 0;
    for(unsigned i=0;i<6;++i) {
        Nv2aVshStep step={0};
        if(((code[i*4+1]>>21)&15)>13 || nv2a_vsh_parse_step(&step,code+i*4)!=NV2AVPR_SUCCESS ||
           step.is_final!=(i==5) || step.ilu.opcode!=NV2AOP_NOP ||
           step.mac.opcode!=(i?NV2AOP_MOV:NV2AOP_ADD))return 0;
        const Nv2aVshOutput *out=NULL;
        for(unsigned j=0;j<2;++j) {
            if(step.ilu.outputs[j].type!=NV2ART_NONE && step.ilu.outputs[j].writemask)return 0;
            const Nv2aVshOutput *candidate=&step.mac.outputs[j];
            if(candidate->type==NV2ART_NONE || !candidate->writemask)continue;
            if(out || candidate->type!=NV2ART_OUTPUT)return 0;
            out=candidate;
        }
        if(!out || out->index!=(i==0?0:i==1?3:i+7) ||
           out->writemask!=(i<2?NV2AWM_XYZW:NV2AWM_XY) ||
           !blend_input(&step.mac.inputs[0],NV2ART_INPUT,i,i<2?4:2))return 0;
        /* The decoder presents ADD's C encoding as its second operand. */
        if(!i && !blend_input(&step.mac.inputs[1],NV2ART_CONTEXT,121,4))return 0;
    }
    return 1;
}
void pilot_gfx_shader_fixed_binding(void)
{
    native_shader_active=native_constants_bound=native_raster_bound=0;
}
int pilot_gfx_shader_blend_rectangle(const void *vertices,uint32_t bytes,unsigned stride,unsigned primitive,unsigned count)
{
    if(!native_shader_active || !native_shader_bound || !native_constants_bound || !native_raster_bound ||
       native_constants_dirty || !native_constants_buffer || !native_raster_buffer || !vertices ||
       primitive!=6 || count!=4 || stride<52 || stride>2048 || bytes<4*stride)return 0;
    struct NativeShader *shader=native_shader(native_shader_bound);
    if(!shader->blend_recipe)return 0;
    uint32_t viewport[6],one=0x3f800000u;
    if(pilot_gfx_get_viewport(viewport)<0 || viewport[0] || viewport[1] ||
       viewport[2]!=640 || viewport[3]!=480 || viewport[4] || viewport[5]!=one)return 0;
    const float expected_raster[]={2.0f/640,-2.0f/480,1.0f/16777215,1,-1,1,0,0,0,0,0,0};
    if(memcmp(native_raster_values,expected_raster,sizeof(expected_raster)))return 0;
    float constant[4];memcpy(constant,native_constants[121],sizeof(constant));
    if(constant[2]!=0 || constant[3]!=0)return 0;
    for(unsigned lane=0;lane<2;++lane)
        if(!isfinite(constant[lane]) || fabsf(constant[lane])>16 ||
           truncf(constant[lane]*32)!=constant[lane]*32)return 0;
    float position[4][2];
    for(unsigned vertex=0;vertex<4;++vertex) {
        const unsigned char *source=(const unsigned char*)vertices+vertex*stride;
        float raw[4],uv[8];memcpy(raw,source,sizeof(raw));memcpy(uv,source+20,sizeof(uv));
        if(raw[2]!=0 || raw[3]!=1)return 0;
        for(unsigned lane=0;lane<8;++lane)if(!isfinite(uv[lane]) || fabsf(uv[lane])>4096)return 0;
        for(unsigned lane=0;lane<2;++lane) {
            if(!isfinite(raw[lane]) || fabsf(raw[lane])>4096 ||
               truncf(raw[lane]*32)!=raw[lane]*32)return 0;
            /* These bounded 1/32 dyadics add exactly in binary32. Retain the
             * actual native addition/truncation order, independent of MXCSR. */
            volatile float added=raw[lane]+constant[lane];
            position[vertex][lane]=truncf(added*16)*.0625f;
            if(position[vertex][lane]<0 || position[vertex][lane]>(lane?480:640))return 0;
        }
    }
    /* Axis-aligned rectangle. Strip triangles have disjoint interiors and
     * native top-left ownership covers their shared edge once. Count alone
     * cannot establish this: duplicate/folded strips are expressly rejected. */
    return position[0][0]==position[1][0] && position[2][0]==position[3][0] &&
        position[0][1]==position[2][1] && position[1][1]==position[3][1] &&
        position[0][0]!=position[2][0] && position[0][1]!=position[1][1];
}
