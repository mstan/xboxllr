/* Outcome comparison only: real native GPU vertex output versus the bounded
 * LLE CPU reference. This executable is never part of title execution. */
#include "graphics_backend.h"
#include "nv2a_vsh_emulator.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL shader line %d: %s\n",__LINE__,#c);return 1;}}while(0)
#define SAMPLES 8
#define STRIDE 512
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
int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    uint32_t pp[17]={64,64,6,1,0x11,1,0,0,1,0x2a,0,0,1};CHECK(pilot_gfx_create(pp)==0);
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
