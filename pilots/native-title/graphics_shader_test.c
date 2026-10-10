/* Outcome comparison only: real native GPU vertex output versus the bounded
 * LLE CPU reference. This executable is never part of title execution. */
#include "graphics_backend.h"
#include "d3d8_xbox.h"
#include "nv2a_vsh_emulator.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL shader line %d: %s\n",__LINE__,#c);return 1;}}while(0)
#define SAMPLES 8
#define STRIDE 512
#include "graphics_clip_depth_test.inc"
static int check_program(const uint32_t *code,unsigned count,const struct PilotVertexElement elements[16],const char *name)
{
    uint32_t handle=0;
    CHECK(pilot_gfx_shader_create(code,count,elements,&handle)==0 && handle);
    unsigned char vertices[16][SAMPLES][STRIDE]={0};
    float inputs[SAMPLES][16][4]={0},constants[192][4],gpu[SAMPLES][13][4];
    for(unsigned i=0;i<192;++i)for(unsigned j=0;j<4;++j)constants[i][j]=0.125f+(float)((i+j)%7)*0.0625f;
    for(unsigned sample=0;sample<SAMPLES;++sample)for(unsigned reg=0;reg<16;++reg) {
        unsigned type=elements[reg].type,stream=elements[reg].stream,offset=elements[reg].offset;
        if(!type)continue;
        CHECK(stream<16 && offset+pilot_gfx_vertex_type_size(type)<=STRIDE);
        unsigned char *bytes=vertices[stream][sample]+offset;
        float *value=inputs[sample][reg];value[3]=1;
        if(type==0x40) {
            uint32_t color=0x80503010u+sample*0x010203u;memcpy(bytes,&color,4);
            value[0]=((color>>16)&255)/255.0f;value[1]=((color>>8)&255)/255.0f;value[2]=(color&255)/255.0f;value[3]=(color>>24)/255.0f;
        } else if(type==0x16) {
            int x=512+(int)sample,y=-256+(int)sample,z=128-(int)sample;
            uint32_t packed=((uint32_t)x&2047)|(((uint32_t)y&2047)<<11)|(((uint32_t)z&1023)<<22);memcpy(bytes,&packed,4);
            value[0]=x/1023.0f;value[1]=y/1023.0f;value[2]=z/511.0f;
        } else if((type&15)==5 || (type&15)==1) {
            unsigned lanes=type>>4;
            for(unsigned j=0;j<lanes;++j) {
                int16_t raw=(int16_t)((int)(sample+j)*3-7);memcpy(bytes+j*2,&raw,2);
                value[j]=(type&15)==1 ? fmaxf(-1,(float)raw/32767.0f):(float)raw;
            }
        } else {
            unsigned lanes=type==0x72?3:type>>4;
            for(unsigned j=0;j<lanes;++j) {
                float raw=0.125f+(float)(sample+j)*0.03125f;memcpy(bytes+j*4,&raw,4);
                value[type==0x72 && j==2?3:j]=raw;
            }
        }
    }
    struct PilotVertexProbeStream streams[16];
    for(unsigned i=0;i<16;++i)streams[i]=(struct PilotVertexProbeStream){vertices[i],STRIDE};
    CHECK(pilot_gfx_shader_probe(handle,streams,SAMPLES,constants,&gpu[0][0][0])==0);
    if(!strcmp(name,"typed-multistream-paired-moves")) {
        const unsigned outputs[]={0,9,3,10,4},sources[]={0,1,2,3,0};
        for(unsigned sample=0;sample<SAMPLES;++sample)for(unsigned i=0;i<5;++i)for(unsigned lane=0;lane<4;++lane)
            CHECK(fabsf(gpu[sample][outputs[i]][lane]-inputs[sample][sources[i]][lane])<1e-6f);
    }
    const unsigned widths[13]={4,0,0,4,4,1,1,4,4,4,4,4,4};
    double maximum=0;unsigned comparisons=0;
    for(unsigned sample=0;sample<SAMPLES;++sample) {
        Nv2aVshCPUFullExecutionState storage={0};Nv2aVshExecutionState state=nv2a_vsh_emu_initialize_full_execution_state(&storage);
        for(unsigned reg=0;reg<13;++reg)storage.output_regs[reg*4+3]=1;
        memcpy(storage.input_regs,inputs[sample],sizeof(inputs[sample]));memcpy(storage.context_regs,constants,sizeof(constants));
        for(unsigned i=0;i<count;++i) {
            Nv2aVshStep step={0};CHECK(nv2a_vsh_parse_step(&step,code+i*4)==NV2AVPR_SUCCESS);
            const Nv2aVshOperation *ops[]={&step.mac,&step.ilu};
            for(unsigned k=0;k<2;++k)if(ops[k]->opcode)for(unsigned p=0;p<3;++p) {
                const Nv2aVshInput *input=&ops[k]->inputs[p];
                if(input->type==NV2ART_CONTEXT) {
                    int reg=(int)input->index+(input->is_relative?(int)storage.address_reg[0]:0);
                    CHECK(reg>=0 && reg<192);
                }
            }
            nv2a_vsh_emu_apply(&state,&step);
        }
        for(unsigned reg=0;reg<13;++reg)for(unsigned j=0;j<widths[reg];++j) {
            float reference=storage.output_regs[reg*4+j],native=gpu[sample][reg][j];
            if(isnan(reference)) {CHECK(isnan(native));continue;}
            if(isinf(reference)) {CHECK(isinf(native) && signbit(reference)==signbit(native));continue;}
            double scale=fmax(1,fabs(reference)),error=fabs((double)reference-native)/scale;
            if(!isfinite(native) || error>2e-4) {
                fprintf(stderr,"MISMATCH %s sample=%u o%u.%u reference=%g native=%g relative=%g\n",name,sample,reg,j,reference,native,error);return 1;
            }
            if(error>maximum)maximum=error;++comparisons;
        }
    }
    pilot_gfx_shader_destroy(handle);
    fprintf(stderr,"PASS shader %s: %u slots, %u finite outputs, maximum scaled error %.9g\n",name,count,comparisons,maximum);
    return 0;
}
static int check_capture(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);
    uint32_t header[8],declaration[1024],code[136*4];
    CHECK(fread(header,sizeof(header),1,file)==1 && header[0]==0x56534c58 && header[1]==1 && header[2]<=136 && header[3]<=1024);
    CHECK(fread(declaration,header[3]*4,1,file)==1 && fread(code,header[2]*16,1,file)==1 && fgetc(file)==EOF);fclose(file);
    struct PilotVertexElement elements[16]={0};unsigned stream=0,offset=0;
    for(unsigned i=0;i<header[3];++i) {
        uint32_t token=declaration[i];if(token==0xffffffffu)break;
        unsigned type=token>>29;
        if(type==1) {stream=token&15;offset=0;}
        if(type==2) {
            if(token&0x10000000u)offset+=((token>>16)&15)*((token&0x08000000u)?1:4);
            else {unsigned reg=token&31;CHECK(reg<16);elements[reg]=(struct PilotVertexElement){(token>>16)&255,stream,offset};offset+=pilot_gfx_vertex_type_size(elements[reg].type);}
        }
        if(type==4)i+=((token>>25)&15)*4;
    }
    return check_program(code,header[2],elements,path);
}
#include "graphics_draw_reference_test.inc"
#include "graphics_fvf_reference_test.inc"
#include "graphics_arl_reference_test.inc"
static int check_vertex_fetch_reference(const char *path)
{
    /* The file carries authored byte inputs and mathematical expectations,
     * never normalized values derived from the maintained LLE decoder. */
    FILE *file=fopen(path,"rb");CHECK(file);
    uint32_t header[4];CHECK(fread(header,sizeof(header),1,file)==1);
    CHECK(header[0]==0x46564c58 && header[1]==2 && header[2] && header[2]<=65536);
    uint32_t previous[4]={0},handle=0;unsigned comparisons=0;
    uint32_t vertices[3];
    for(unsigned i=0;i<3;++i)CHECK(pilot_gfx_vertex_create(i?6*48:6*32,&vertices[i])==0);
    pilot_gfx_vertex_bind(0,vertices[0],32);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_DISABLE);pilot_gfx_pixel_fixed();
    unsigned bound_stream=0,bound_buffer=0,bound_stride=0;
    for(unsigned sample=0;sample<header[2];++sample) {
        uint32_t metadata[8];unsigned char payload[16];float expected[4],output[13][4];
        CHECK(fread(metadata,sizeof(metadata),1,file)==1 && fread(payload,sizeof(payload),1,file)==1 && fread(expected,sizeof(expected),1,file)==1);
        unsigned type=metadata[0],slot=metadata[1],stream=metadata[2],offset=metadata[3];
        unsigned stride=metadata[4],first=metadata[5],buffer=metadata[6],persistent=metadata[7];
        CHECK(slot>0 && slot<16 && stream>0 && stream<16 && offset+pilot_gfx_vertex_type_size(type)<=32);
        CHECK((stride==32 || stride==48) && first>0 && first+3<=6 && buffer<2 && persistent<=1);
        if(!handle || memcmp(previous,metadata,sizeof(previous))) {
            if(handle)pilot_gfx_shader_destroy(handle);
            struct PilotVertexElement elements[16]={0};
            elements[0]=(struct PilotVertexElement){0x42,0,0};
            elements[slot]=(struct PilotVertexElement){type,stream,offset};
            const uint32_t code[]={0,0x0020001b,0x08000000,0x0000f800,
                0,0x0020001b|(slot<<9),0x08000000,0x0000f819};
            CHECK(pilot_gfx_shader_create(code,2,elements,&handle)==0);
            memcpy(previous,metadata,sizeof(previous));
            CHECK(pilot_gfx_shader_bind(handle)==0);
            const float scale[]={1.0f/32,-1.0f/32,1.0f/16777215,1},raster_offset[]={-1,1,0,0};
            CHECK(pilot_gfx_shader_raster(scale,raster_offset)==0);
        }
        unsigned char positions[32]={0},attributes[32];memset(attributes,0xcc,sizeof(attributes));
        const float xyzw[]={8,8,10,1};memcpy(positions,xyzw,sizeof(xyzw));
        memcpy(attributes+offset,payload,pilot_gfx_vertex_type_size(type));
        struct PilotVertexProbeStream streams[16]={0};
        streams[0]=(struct PilotVertexProbeStream){positions,32};
        streams[stream]=(struct PilotVertexProbeStream){attributes,32};
        float constants[192][4]={0};
        CHECK(pilot_gfx_shader_probe(handle,streams,1,constants,&output[0][0])==0);
        for(unsigned lane=0;lane<4;++lane) {
            float actual=output[3][lane];
            if(!isfinite(actual) || fabsf(actual-expected[lane])>2e-6f*fmaxf(1,fabsf(expected[lane]))) {
                fprintf(stderr,"FETCH mismatch sample=%u format=%x slot=%u stream=%u lane=%u actual=%g expected=%g\n",
                    sample,type,slot,stream,lane,actual,expected[lane]);
                CHECK(0);
            }
            ++comparisons;
        }
        /* Exercise the shipping draw backend with persistent buffers too.
         * The raw probe preserves the immediate bindings used by this draw. */
        unsigned char draw_positions[6*32],draw_attributes[2][6*48],pixels[64*64*4];
        memset(draw_positions,0xcc,sizeof(draw_positions));memset(draw_attributes,0xcc,sizeof(draw_attributes));
        for(unsigned i=0;i<6;++i) {
            const float sentinel[]={-200,-200,10,1};memcpy(draw_positions+i*32,sentinel,16);
        }
        const float triangle[3][4]={{8,8,10,1},{56,8,10,1},{8,56,10,1}};
        for(unsigned i=0;i<3;++i) {
            memcpy(draw_positions+(first+i)*32,triangle[i],16);
            memcpy(draw_attributes[buffer]+(first+i)*stride+offset,payload,pilot_gfx_vertex_type_size(type));
        }
        pilot_gfx_vertex_upload(vertices[0],draw_positions,sizeof(draw_positions));
        for(unsigned i=0;i<2;++i)pilot_gfx_vertex_upload(vertices[i+1],draw_attributes[i],sizeof(draw_attributes[i]));
        if(persistent)CHECK(bound_stream==stream && bound_buffer==buffer && bound_stride==stride);
        else {
            if(bound_stream && bound_stream!=stream)pilot_gfx_vertex_bind(bound_stream,0,0);
            pilot_gfx_vertex_bind(stream,vertices[buffer+1],stride);
            bound_stream=stream;bound_buffer=buffer;bound_stride=stride;
        }
        CHECK(pilot_gfx_clear(D3DCLEAR_TARGET,0xff102030,1,0)==0);
        CHECK(pilot_gfx_draw_vertices(5,first,3)==0 && pilot_gfx_readback(pixels,sizeof(pixels))==0);
        unsigned pixel=(16*64+16)*4;
        for(unsigned lane=0;lane<3;++lane) {
            int want=(int)lroundf(fminf(1,fmaxf(0,expected[lane]))*255);
            if(abs((int)pixels[pixel+lane]-want)>1) {
                fprintf(stderr,"FETCH pixel mismatch sample=%u format=%x lane=%u actual=%u expected=%d\n",
                    sample,type,lane,pixels[pixel+lane],want);CHECK(0);
            }
        }
        CHECK(pixels[0]==0x10 && pixels[1]==0x20 && pixels[2]==0x30);
    }
    CHECK(fgetc(file)==EOF);fclose(file);pilot_gfx_shader_destroy(handle);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_bind(bound_stream,0,0);
    for(unsigned i=0;i<3;++i)pilot_gfx_vertex_destroy(vertices[i]);
    fprintf(stderr,"PASS: %u independently specified native vertex fetch cases, %u output lanes and persistent-buffer draws\n",header[2],comparisons);
    return 0;
}
#include "graphics_arithmetic_reference_test.inc"
#include "graphics_fog_reference_test.inc"
#include "graphics_texture_reference_test.inc"
#include "graphics_scene_reference_test.inc"
static int check_shader_constant_reference(const char *path)
{
    FILE *file=fopen(path,"rb");CHECK(file);uint32_t header[4];
    CHECK(fread(header,sizeof(header),1,file)==1);
    CHECK(header[0]==0x43564c58 && header[1]==1 && header[2] && header[2]<=65536);
    unsigned char positions[4*32]={0};
    const float triangle[4][4]={{-200,-200,10,1},{8,8,10,1},{56,8,10,1},{8,56,10,1}};
    for(unsigned i=0;i<4;++i)memcpy(positions+i*32,triangle[i],16);
    uint32_t vertex,handle=0;unsigned previous=192,comparisons=0;
    CHECK(pilot_gfx_vertex_create(sizeof(positions),&vertex)==0);
    pilot_gfx_vertex_upload(vertex,positions,sizeof(positions));pilot_gfx_vertex_bind(0,vertex,32);
    pilot_gfx_set_state(D3DRS_CULLMODE,D3DCULL_NONE);pilot_gfx_set_state(D3DRS_ZENABLE,0);
    pilot_gfx_set_state(D3DRS_ZWRITEENABLE,0);pilot_gfx_set_state(D3DRS_ALPHABLENDENABLE,0);
    pilot_gfx_set_state(D3DRS_ALPHATESTENABLE,0);pilot_gfx_set_state(D3DRS_SPECULARENABLE,0);
    pilot_gfx_set_texture_state(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    pilot_gfx_set_texture_state(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    pilot_gfx_set_texture_state(1,D3DTSS_COLOROP,D3DTOP_DISABLE);pilot_gfx_pixel_fixed();
    for(unsigned sample=0;sample<header[2];++sample) {
        uint32_t metadata[4];float preliminary[32][4],values[32][4],expected[4],output[13][4];
        CHECK(fread(metadata,sizeof(metadata),1,file)==1);
        unsigned first=metadata[0],count=metadata[1],observed=metadata[2];
        CHECK(first<192 && count && count<=32 && count<=192-first && observed>=first && observed<first+count && !metadata[3]);
        CHECK(fread(preliminary,16,count,file)==count && fread(values,16,count,file)==count && fread(expected,16,1,file)==1);
        if(observed!=previous) {
            if(handle)pilot_gfx_shader_destroy(handle);
            struct PilotVertexElement elements[16]={0};elements[0]=(struct PilotVertexElement){0x42,0,0};
            const uint32_t code[]={0,0x0020001b,0x08000000,0x0000f800,
                0,0x0020001b|(observed<<13),0x0c000000,0x0000f819};
            CHECK(pilot_gfx_shader_create(code,2,elements,&handle)==0 && pilot_gfx_shader_bind(handle)==0);
            const float scale[]={1.0f/32,-1.0f/32,1.0f/16777215,1},offset[]={-1,1,0,0};
            CHECK(pilot_gfx_shader_raster(scale,offset)==0);previous=observed;
        }
        unsigned uploads=pilot_gfx_shader_uploads();
        pilot_gfx_shader_constants(first,count,preliminary);
        pilot_gfx_shader_constants(first,count,values);
        pilot_gfx_shader_constants(first,count,values);
        CHECK(pilot_gfx_shader_uploads()==uploads);
        struct PilotVertexProbeStream streams[16]={0};
        streams[0]=(struct PilotVertexProbeStream){positions+32,32};
        /* NULL probes the actually bound production constant buffer. */
        CHECK(pilot_gfx_shader_probe(handle,streams,1,NULL,&output[0][0])==0);
        CHECK(pilot_gfx_shader_uploads()==uploads+1);
        for(unsigned lane=0;lane<4;++lane) {
            if(!isfinite(output[3][lane]) || fabsf(output[3][lane]-expected[lane])>2e-6f*fmaxf(1,fabsf(expected[lane]))) {
                fprintf(stderr,"CONSTANT mismatch sample=%u register=%u lane=%u actual=%g expected=%g\n",
                    sample,observed,lane,output[3][lane],expected[lane]);CHECK(0);
            }
            ++comparisons;
        }
        unsigned char pixels[64*64*4];
        CHECK(pilot_gfx_clear(D3DCLEAR_TARGET,0xff102030,1,0)==0 && pilot_gfx_draw_vertices(5,1,3)==0);
        CHECK(pilot_gfx_readback(pixels,sizeof(pixels))==0);
        CHECK(pilot_gfx_shader_uploads()==uploads+1); /* draw reuses the upload */
        for(unsigned lane=0;lane<3;++lane) {
            int want=(int)lroundf(fminf(1,fmaxf(0,expected[lane]))*255);
            CHECK(abs((int)pixels[(16*64+16)*4+lane]-want)<=1);
        }
        CHECK(pixels[0]==0x10 && pixels[1]==0x20 && pixels[2]==0x30);
    }
    CHECK(fgetc(file)==EOF);fclose(file);pilot_gfx_shader_destroy(handle);
    pilot_gfx_vertex_bind(0,0,0);pilot_gfx_vertex_destroy(vertex);
    fprintf(stderr,"PASS: %u native constant cases, %u raw output lanes and GPU draws; three writes coalesce to one upload per case\n",header[2],comparisons);
    return 0;
}
int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    uint32_t pp[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};CHECK(pilot_gfx_create(pp)==0);
    if(argc==2 && !strcmp(argv[1],"--clip-depth-reference"))return check_clip_depth_reference();
    if(argc==4 && !strcmp(argv[1],"--scene-reference-observe")) {
        CHECK(observe_scene_reference(argv[2],argv[3])==0);return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--fog-reference")) {
        CHECK(check_fog_reference(argv[2])==0);pilot_gfx_close();return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--texture-reference")) {
        CHECK(check_texture_reference(argv[2])==0);pilot_gfx_close();return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--shader-arithmetic-reference")) {
        CHECK(check_shader_arithmetic_reference(argv[2])==0);pilot_gfx_close();return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--shader-constant-reference")) {
        CHECK(check_shader_constant_reference(argv[2])==0);pilot_gfx_close();return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--vertex-fetch-reference")) {
        CHECK(check_vertex_fetch_reference(argv[2])==0);pilot_gfx_close();return 0;
    }
    if(argc==3 && !strcmp(argv[1],"--fvf-reference")) {
        CHECK(check_fvf_reference(argv[2])==0);pilot_gfx_close();return 0;
    }
    if(argc>2 && !strcmp(argv[1],"--draw-capture")) {
        for(int i=2;i<argc;++i)CHECK(check_draw_capture(argv[i])==0);
        pilot_gfx_close();return 0;
    }
    if(argc==2 && !strncmp(argv[1],"--",2)) {
        uint32_t code[]={0,0x0020001b,0x08000000,0x0000f801,0,0x0020001b,0x08000000,0x0000f801},handle=0;
        struct PilotVertexElement elements[16]={0};elements[0]=(struct PilotVertexElement){0x42,0,0};
        if(!strcmp(argv[1],"--bad-opcode"))code[1]=15u<<21;
        else if(strcmp(argv[1],"--early-end"))return 1;
        pilot_gfx_shader_create(code,!strcmp(argv[1],"--early-end")?2:1,elements,&handle);return 0;
    }
    if(argc>1)for(int i=1;i<argc;++i){if(check_capture(argv[i]))return 1;}
    else {
        CHECK(check_scalar_address_reference()==0);
        /* ISA-encoded moves plus paired MOV R2,v1 + MOV oD1,R2. The second
         * operation must consume R2 from before the pair's first write. */
        const uint32_t code[]={
            0,0x0020001b,0x08000000,0x0000f800,
            0,0x0020021b,0x08000000,0x0000f848,
            0,0x0020041b,0x08000000,0x0000f818,
            0,0x0020061b,0x08000000,0x0000f850,
            0,0x0020001b,0x08000000,0x0f200000,
            0,0x0220021b,0x0800006c,0x9f20f825};
        struct PilotVertexElement elements[16]={0};
        elements[0]=(struct PilotVertexElement){0x42,0,0};elements[1]=(struct PilotVertexElement){0x16,0,16};
        elements[2]=(struct PilotVertexElement){0x40,0,20};elements[3]=(struct PilotVertexElement){0x25,1,0};
        CHECK(check_program(code,6,elements,"typed-multistream-paired-moves")==0);
    }
    pilot_gfx_close();return 0;
}
