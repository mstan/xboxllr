/* Native-float HLSL, using the same permissive NV2A decoder as the LLE floor.
 * The MIT sp00nznet translator supplies the HLSL register/operation approach;
 * declarations, separate destination masks and paired issue are handled here.
 * No instruction-history or bit-exact arithmetic requirement is imposed on HLE. */
#include "nv2a_vsh_disassembler.h"
#include <stdarg.h>
struct ShaderText { char *data; size_t length,capacity; };
static void shader_text(struct ShaderText *text,const char *format,...)
{
    va_list args;va_start(args,format);
    int count=vsnprintf(text->data+text->length,text->capacity-text->length,format,args);
    va_end(args);
    if(count<0 || (size_t)count>=text->capacity-text->length)pilot_d3d_backend_stop("shader source capacity exceeded");
    text->length+=(size_t)count;
}
struct ShaderFormat { uint32_t type,bytes; DXGI_FORMAT native; const char *hlsl; };
static const struct ShaderFormat shader_formats[]={
    {0x12,4,DXGI_FORMAT_R32_FLOAT,"float"},{0x22,8,DXGI_FORMAT_R32G32_FLOAT,"float2"},
    {0x32,12,DXGI_FORMAT_R32G32B32_FLOAT,"float3"},{0x42,16,DXGI_FORMAT_R32G32B32A32_FLOAT,"float4"},
    {0x40,4,DXGI_FORMAT_R8G8B8A8_UNORM,"float4"},{0x16,4,DXGI_FORMAT_R32_UINT,"uint"},
    {0x15,2,DXGI_FORMAT_R16_SINT,"int"},{0x25,4,DXGI_FORMAT_R16G16_SINT,"int2"},
    {0x45,8,DXGI_FORMAT_R16G16B16A16_SINT,"int4"},
    {0x11,2,DXGI_FORMAT_R16_SNORM,"float"},{0x21,4,DXGI_FORMAT_R16G16_SNORM,"float2"},
    {0x41,8,DXGI_FORMAT_R16G16B16A16_SNORM,"float4"},{0x72,12,DXGI_FORMAT_R32G32B32_FLOAT,"float3"}
};
static const struct ShaderFormat *shader_format(uint32_t type)
{
    for(unsigned i=0;i<sizeof(shader_formats)/sizeof(shader_formats[0]);++i)
        if(shader_formats[i].type==type)return &shader_formats[i];
    pilot_d3d_backend_stop("unqualified vertex declaration format");
}
uint32_t pilot_gfx_vertex_type_size(uint32_t type){return shader_format(type)->bytes;}
static const char *shader_semantics[13]={"SV_POSITION",NULL,NULL,"COLOR","COLOR","FOG","PSIZE","TEXCOORD","TEXCOORD","TEXCOORD","TEXCOORD","TEXCOORD","TEXCOORD"};
static const unsigned shader_semantic_indices[13]={0,0,0,0,1,0,0,4,5,0,1,2,3};
static const unsigned shader_output_widths[13]={4,0,0,4,4,1,1,4,4,4,4,4,4};
static unsigned shader_arity(unsigned op)
{
    static const unsigned arity[]={0,1,2,2,3,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1};
    if(op>=sizeof(arity)/sizeof(arity[0]))pilot_d3d_backend_stop("invalid decoded shader operation");
    return arity[op];
}
static void shader_operand(struct ShaderText *text,const Nv2aVshInput *input,unsigned *used)
{
    shader_text(text,input->is_negated?"(-":"(");
    switch(input->type) {
    case NV2ART_INPUT:
        if(input->index>=16)pilot_d3d_backend_stop("invalid shader input register");
        *used|=1u<<input->index;shader_text(text,"v%u",input->index);break;
    case NV2ART_TEMPORARY:
        if(input->index>12)pilot_d3d_backend_stop("invalid shader temporary register");
        shader_text(text,input->index==12?"O[0]":"R[%u]",input->index);break;
    case NV2ART_CONTEXT:
        if(input->index>=192)pilot_d3d_backend_stop("invalid shader constant register");
        shader_text(text,input->is_relative?"c[a0+%u]":"c[%u]",input->index);break;
    default:pilot_d3d_backend_stop("shader operation has no input operand");
    }
    shader_text(text,").%c%c%c%c","xyzw"[input->swizzle[0]],"xyzw"[input->swizzle[1]],
        "xyzw"[input->swizzle[2]],"xyzw"[input->swizzle[3]]);
}
static void shader_value(struct ShaderText *text,const Nv2aVshOperation *op,const char *name,unsigned *used)
{
    /* Expressions follow the donor's native HLSL operations. RCC preserves
     * reciprocal sign/range; EXP/LOG retain all four components from the LLE
     * operation contract instead of the donor's scalar replication. */
    static const char *expressions[]={NULL,"a","a*b","a+b","a*b+d",
        "dot(a.xyz,b.xyz).xxxx","(dot(a.xyz,b.xyz)+b.w).xxxx","dot(a,b).xxxx",
        "float4(1,a.y*b.y,a.z,b.w)","min(a,b)","max(a,b)","float4(a<b)","float4(a>=b)",
        "floor(a.x+0.001).xxxx","(1/a.x).xxxx","nv_rcc(a.x).xxxx","rsqrt(abs(a.x)).xxxx",
        "float4(exp2(floor(a.x)),frac(a.x),exp2(a.x),1)","nv_log(a.x)","nv_lit(a)"};
    unsigned arity=shader_arity(op->opcode);
    if(!arity)return;
    shader_text(text,"float4 %s;{\n",name);
    static const char *names[]={"a","b","d"};
    for(unsigned i=0;i<arity;++i) {
        shader_text(text,"float4 %s=",names[i]);shader_operand(text,&op->inputs[i],used);shader_text(text,";\n");
    }
    shader_text(text,"%s=%s;}\n",name,expressions[op->opcode]);
}
static void shader_store(struct ShaderText *text,const Nv2aVshOperation *op,const char *name)
{
    if(op->opcode==NV2AOP_NOP)return;
    for(unsigned i=0;i<2;++i) {
        const Nv2aVshOutput *out=&op->outputs[i];
        if(out->type==NV2ART_NONE)continue;
        if(out->type==NV2ART_ADDRESS) {shader_text(text,"a0=(int)%s.x;\n",name);continue;}
        if(!out->writemask)continue;
        if(out->type==NV2ART_OUTPUT) {
            if(out->index>=13 || !shader_output_widths[out->index])pilot_d3d_backend_stop("invalid shader output register");
            shader_text(text,"O[%u]",out->index);
        } else if(out->type==NV2ART_TEMPORARY) {
            if(out->index>12)pilot_d3d_backend_stop("invalid shader temporary destination");
            shader_text(text,out->index==12?"O[0]":"R[%u]",out->index);
        } else pilot_d3d_backend_stop("vertex state program requires persistent constant-write contract");
        char mask[5]={0};unsigned count=0;
        for(unsigned lane=0;lane<4;++lane)if(out->writemask&(8u>>lane))mask[count++]="xyzw"[lane];
        shader_text(text,".%s=%s.%s;\n",mask,name,mask);
    }
}
static char *shader_source(const uint32_t *code,unsigned count,const struct PilotVertexElement elements[16],unsigned *used)
{
    struct ShaderText body={calloc(1,262144),0,262144},source={calloc(1,262144),0,262144};
    if(!body.data || !source.data) {free(body.data);free(source.data);return NULL;}
    *used=0;
    for(unsigned i=0;i<count;++i) {
        const uint32_t *slot=code+i*4;
        /* Protect the borrowed decoder's opcode lookup before it indexes. */
        if(((slot[1]>>21)&15)>13 || !!(slot[3]&1)!=(i+1==count))
            pilot_d3d_backend_stop("invalid shader opcode/end boundary");
        Nv2aVshStep step={0};
        if(nv2a_vsh_parse_step(&step,slot)!=NV2AVPR_SUCCESS)pilot_d3d_backend_stop("NV2A shader decode failed");
        shader_text(&body,"{ /* slot %u */\n",i);
        /* Both operations consume the old register values before either writes. */
        shader_value(&body,&step.mac,"mac",used);shader_value(&body,&step.ilu,"ilu",used);
        shader_store(&body,&step.mac,"mac");shader_store(&body,&step.ilu,"ilu");shader_text(&body,"}\n");
    }
    shader_text(&source,"cbuffer Constants:register(b1){float4 c[192];};\n"
        "float nv_rcc(float x){float r=1/x;return (asuint(r)&0x80000000u)?-clamp(abs(r),5.42101e-20,1.8446744e19):clamp(r,5.42101e-20,1.8446744e19);}\n"
        "float4 nv_log(float x){float a=abs(x);uint u=asuint(a);if(a==0)return float4(asfloat(0xff800000u),1,asfloat(0xff800000u),1);"
        "if(isinf(a))return float4(a,1,a,1);return float4((int)((u>>23)&255)-127,asfloat((u&0x7fffffu)|0x3f800000u),log2(a),1);}\n"
        "float4 nv_lit(float4 a){return float4(1,max(a.x,0),(a.x>0&&a.y>0)?pow(a.y,clamp(a.w,-127.9961,127.9961)):0,1);}\n"
        "struct Input{\n");
    for(unsigned i=0;i<16;++i)if(*used&(1u<<i)) {
        const struct ShaderFormat *format=shader_format(elements[i].type);
        shader_text(&source,"%s v%u:ATTR%u;\n",format->hlsl,i,i);
    }
    shader_text(&source,"};struct Output{\n");
    for(unsigned i=0;i<13;++i)if(shader_output_widths[i])
        shader_text(&source,"%s o%u:%s%u;\n",shader_output_widths[i]==1?"float":"float4",i,shader_semantics[i],shader_semantic_indices[i]);
    shader_text(&source,"};Output main(Input input){float4 R[12];float4 O[13];int a0=0;\n"
        "[unroll]for(int j=0;j<12;++j)R[j]=0;[unroll]for(int j=0;j<13;++j)O[j]=0;\n");
    for(unsigned i=0;i<16;++i)if(*used&(1u<<i)) {
        unsigned type=elements[i].type;
        shader_text(&source,"float4 v%u=",i);
        if(type==0x16)shader_text(&source,"float4(max(float3((int)(input.v%u<<21)>>21,(int)(input.v%u<<10)>>21,(int)input.v%u>>22)/float3(1023,1023,511),-1),1)",i,i,i);
        else if(type==0x40)shader_text(&source,"input.v%u.bgra",i);
        else if(type==0x72)shader_text(&source,"float4(input.v%u.xy,0,input.v%u.z)",i,i);
        else if((type>>4)==1)shader_text(&source,"float4(input.v%u,0,0,1)",i);
        else if((type>>4)==2)shader_text(&source,"float4(input.v%u,0,1)",i);
        else if((type>>4)==3)shader_text(&source,"float4(input.v%u,1)",i);
        else shader_text(&source,"float4(input.v%u)",i);
        shader_text(&source,";\n");
    }
    shader_text(&source,"%s\nOutput result;\n",body.data);
    for(unsigned i=0;i<13;++i)if(shader_output_widths[i])
        shader_text(&source,"result.o%u=O[%u]%s;\n",i,i,shader_output_widths[i]==1?".x":"");
    shader_text(&source,"return result;}\n");free(body.data);return source.data;
}
