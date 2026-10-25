#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "audio_dsp.h"
#include "audio_backend.h"
#define CHECK(c) do{if(!(c)){fprintf(stderr,"DSP test failed line %u: %s\n",__LINE__,#c);exit(1);}}while(0)
static uint64_t step(uint64_t x)
{uint32_t l=(uint32_t)x;return (x>>1)|((uint64_t)((((l*8^l*2^l)<<28)^(uint32_t)(x>>32))&0x80000000)<<32);}
static void encrypt(uint8_t *p,unsigned n,const uint8_t key[8],uint64_t initial)
{
    uint64_t s=step(initial);
    for(unsigned i=0;i<n;++i){s=step(s);uint8_t d=(uint8_t)(initial>>(8*(i%8)));p[i]=(uint8_t)((p[i]^d^(uint8_t)s)+d*key[i%8]);}
}
static unsigned fixture(uint32_t data[1024])
{
    memset(data,0,4096);
    /* Independently authored eight-word program: establish state R5, load a
     * constant and write output sample four. Contains no title bytes or key. */
    uint32_t code[]={0x65f400,0x80,0x44f400,0x123456,0x65f400,0xc00,0x021584,0x0c};
    data[0x804/4]=8;data[0x808/4]=0x838;data[0x80c/4]=5;data[0x810/4]=3;
    memcpy((uint8_t*)data+0x818,code,sizeof(code));
    uint32_t *desc=data+0x84c/4;desc[0]=1;desc[1]=0;
    uint32_t map[]={0x818,28,0x838,20,0,0,0xc000,0};memcpy(desc+2,map,sizeof(map));
    uint64_t initial=123;for(unsigned i=0;i<81;++i)initial=step(initial);
    uint8_t key[8]={3,1,4,1,5,9,2,6},k[8];
    for(unsigned i=0;i<8;++i)k[i]=(uint8_t)(initial>>(8*i));
    encrypt((uint8_t*)data+0x818,28,key,initial);
    memcpy(desc+10,key,8);encrypt((uint8_t*)(desc+10),8,k,initial);
    return 0x84c+48;
}
static void full_bank_fixture(void)
{
    /* Reach both real GP bank ends with an authored position-independent
     * graph. Temporary/output storage above X:c00 is valid initial state. */
    const unsigned words=4093,state=4096-0x80,state_at=0x818+words*4;
    const unsigned descriptor=state_at+state*4,bytes=descriptor+48;
    uint32_t *data=calloc(1,bytes);CHECK(data);
    data[0x804/4]=words;data[0x808/4]=state_at;data[0x80c/4]=state;data[0x810/4]=3;
    const uint32_t prefix[]={0x65f400,0x80,0x44f400,0x123456,0x65f400,0xc00,0x021584,
        0x44f400,0x987654,0x65f400,0xffb,0x021584};
    memcpy((uint8_t*)data+0x818,prefix,sizeof(prefix));data[(state_at-4)/4]=0x0c;
    data[(descriptor-4)/4]=0xabcdef;
    uint32_t *desc=data+descriptor/4;desc[0]=1;
    const uint32_t map[]={0x818,(4093-1)*4,state_at,(4096-0x80)*4,0,0,0xc000,0};
    memcpy(desc+2,map,sizeof(map));
    uint64_t initial=123;for(unsigned i=0;i<81;++i)initial=step(initial);
    uint8_t key[8]={2,7,1,8,2,8,1,8},seed_key[8];
    for(unsigned i=0;i<8;++i)seed_key[i]=(uint8_t)(initial>>(i*8));
    encrypt((uint8_t*)data+0x818,(words-1)*4,key,initial);
    memcpy(desc+10,key,8);encrypt((uint8_t*)(desc+10),8,seed_key,initial);
    const char *error=NULL;PilotDspImage *image=pilot_dsp_decode(data,bytes,123,&error);CHECK(image && !error);
    uint32_t capacity=pilot_dsp_storage_bytes(image);uint8_t *storage=malloc(capacity+16);CHECK(storage);
    memset(storage+capacity,0x6b,16);PilotDsp *d=pilot_dsp_create(image,storage,0x10000000);CHECK(d);
    uint32_t *actual=(uint32_t*)(storage+pilot_dsp_descriptor_offset(image));
    CHECK(actual[2]==0x10000000 && actual[4]==0x10000000+pilot_dsp_descriptor_offset(image)+40);
    uint32_t mix[1024]={0},out[192];
    for(unsigned block=0;block<3;++block) {
        pilot_dsp_process(d,mix,out);CHECK(out[4]==0x123456);
        CHECK(((uint32_t*)(storage+0x4000))[4095]==0x987654);
    }
    CHECK(pilot_dsp_blocks(d)==3);
    uint32_t tail=0xffffffff;CHECK(pilot_dsp_effect_read(d,0,(4096-0x80-1)*4,&tail,4) && tail==0xabcdef);
    /* The real program changed live X[4095]; its CPU shadow stays independent. */
    uint32_t replacement=0x1234,other=0x9876;
    struct PilotDspPatch patches[]={{(4096-0x80-1)*4,4,&replacement},{(4096-0x80)*4,4,&other}};
    CHECK(!pilot_dsp_effect_patch(d,0,2,patches));
    CHECK(((uint32_t*)(storage+0x4000))[4095]==0x987654);
    CHECK(pilot_dsp_effect_read(d,0,patches[0].offset,&tail,4) && tail==0xabcdef);
    CHECK(pilot_dsp_effect_patch(d,0,1,patches));
    CHECK(((uint32_t*)(storage+0x4000))[4095]==replacement);
    pilot_dsp_process(d,mix,out);CHECK(((uint32_t*)(storage+0x4000))[4095]==0x987654);
    CHECK(pilot_dsp_effect_read(d,0,patches[0].offset,&tail,4) && tail==replacement);
    CHECK(!pilot_dsp_effect_read(d,1,0,&tail,4) && !pilot_dsp_effect_read(d,0,1,&tail,4));
    for(unsigned i=0;i<16;++i)CHECK(storage[capacity+i]==0x6b);
    pilot_dsp_destroy(d);pilot_dsp_image_destroy(image);free(storage);
    data[0x804/4]=4094;CHECK(!pilot_dsp_decode(data,bytes,123,&error));free(data);
}
static void isolated_fixture(void)
{
    uint32_t data[1024];unsigned bytes=fixture(data);const char *error=NULL;
    PilotDspImage *image=pilot_dsp_decode(data,bytes,123,&error);CHECK(image && !error);
    unsigned size=pilot_dsp_storage_bytes(image);uint8_t *storage=malloc(size+16),*before=malloc(size);CHECK(storage && before);
    memset(storage+size,0x5a,16);
    PilotDsp *d=pilot_dsp_create_isolated(image,storage,0x10000000);CHECK(d);
    uint32_t mix[1024]={0},out[192];
    uint32_t *x=(uint32_t*)(storage+0x4000);
    for(unsigned n=0;n<100;++n) {
        mix[3]=n*137;memcpy(before,storage,size);
        pilot_dsp_begin(d);pilot_dsp_process(d,mix,out);
        CHECK(!memcmp(before,storage,size)); // Live execution never touches guest memory.
        uint32_t patch=n+77;struct PilotDspPatch rejected={0,4,&patch};
        CHECK(!pilot_dsp_effect_patch(d,0,1,&rejected));
        x[3000]=n+1; // A concurrent CPU-only write must survive publication.
        x[0xc04]=0xffffff; // The real graph's changed output publishes later.
        pilot_dsp_publish(d);
        CHECK(x[3000]==n+1 && x[0xc04]==0x123456 && x[0xc03]==mix[3]);
        CHECK(out[4]==0x123456 && out[3]==mix[3]);
    }
    // Program changes made through guest memory must invalidate native blocks.
    ((uint32_t*)(storage+0x1000))[3]=0x654321;
    pilot_dsp_begin(d);pilot_dsp_process(d,mix,out);pilot_dsp_publish(d);
    CHECK(out[4]==0x654321 && x[0xc04]==0x654321);
    uint32_t patch=0x765432,read=0;struct PilotDspPatch accepted={0,4,&patch};
    CHECK(pilot_dsp_effect_patch(d,0,1,&accepted));
    CHECK(pilot_dsp_effect_read(d,0,0,&read,4) && read==patch);
    uint32_t *desc=(uint32_t*)(storage+pilot_dsp_descriptor_offset(image));
    CHECK(*(uint32_t*)(storage+desc[4]-0x10000000)==patch);
    for(unsigned i=0;i<16;++i)CHECK(storage[size+i]==0x5a);
    pilot_dsp_destroy(d);pilot_dsp_image_destroy(image);free(before);free(storage);
}
int main(int argc,char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    full_bank_fixture();
    isolated_fixture();
    uint32_t data[1024];unsigned bytes=fixture(data);const char *error=NULL;
    PilotDspImage *image=pilot_dsp_decode(data,bytes,123,&error);CHECK(image && !error);
    unsigned storage_bytes=pilot_dsp_storage_bytes(image);uint8_t *storage=malloc(storage_bytes+16);CHECK(storage);
    memset(storage+storage_bytes,0x5a,16);PilotDsp *d=pilot_dsp_create(image,storage,0x10000000);CHECK(d);
    if(argc==2) {
        /* Mutate the owned program before first execution: exercise the linked
         * donor's guards, not merely the preparation script's string edits. */
        uint32_t *p=(uint32_t*)(storage+0x1000);
        if(!strcmp(argv[1],"debug"))p[2]=0x200;
        else if(!strcmp(argv[1],"reset"))p[2]=0x84;
        else if(!strcmp(argv[1],"wait"))p[2]=0x86;
        else if(!strcmp(argv[1],"mode")){p[2]=0x05f439;p[3]=0x20000;}
        else return 1;
        uint32_t in[1024]={0},out[192];pilot_dsp_process(d,in,out);
        fprintf(stderr,"unsupported core feature unexpectedly returned\n");return 1;
    }
    uint32_t *desc=(uint32_t*)(storage+pilot_dsp_descriptor_offset(image));
    CHECK(desc[0]==1 && desc[1]==0 && desc[2]==0x10001000 &&
          desc[4]==0x10000000+pilot_dsp_descriptor_offset(image)+40 && desc[8]==0x10016000);
    uint32_t in[1024],out[192];for(unsigned i=0;i<1024;++i)in[i]=i*103;
    for(unsigned n=0;n<100;++n){pilot_dsp_process(d,in,out);CHECK(out[4]==0x123456);for(unsigned i=0;i<192;++i)if(i!=4)CHECK(out[i]==in[i]);}
    CHECK(pilot_dsp_blocks(d)==100 && pilot_dsp_dma_nodes(d)==0);
    for(unsigned i=0;i<16;++i)CHECK(storage[storage_bytes+i]==0x5a);
    pilot_dsp_destroy(d);pilot_dsp_image_destroy(image);free(storage);
    // Native circular format-2 transport must match the hardware's literal
    // MSB-aligned byte examples, not just round-trip its own wrong packing.
    image=pilot_dsp_decode(data,bytes,123,&error);CHECK(image && !error);
    storage=malloc(pilot_dsp_storage_bytes(image));CHECK(storage);
    d=pilot_dsp_create(image,storage,0x10000000);CHECK(d);
    const uint32_t dma_code[]={0x65f400,0xffffd0,0x44f400,0x20,0x021584,
                               0x65f400,0xffffd2,0x44f400,1,0x021584,0x0c};
    memcpy(storage+0x1000,dma_code,sizeof(dma_code));
    uint32_t *x=(uint32_t*)(storage+0x4000);
    const uint32_t output_node[]={0x40,0x49d2,2,0x80,0,0,7};
    const uint32_t input_node[]={0x4000,0x49d0,2,0x90,0,0,7};
    memcpy(x+0x20,output_node,sizeof(output_node));memcpy(x+0x40,input_node,sizeof(input_node));
    x[0x80]=0x123456;x[0x81]=0xabcdef;
    memset(in,0,sizeof(in));pilot_dsp_process(d,in,out);
    const uint8_t packed[]={0,0x56,0x34,0x12,0,0xef,0xcd,0xab};
    CHECK(!memcmp(storage+0xa000,packed,sizeof(packed)));
    CHECK(x[0x90]==0x123456 && x[0x91]==0xabcdef && pilot_dsp_dma_nodes(d)==2);
    pilot_dsp_destroy(d);pilot_dsp_image_destroy(image);free(storage);
    for(unsigned cut=0;cut<bytes;++cut){image=pilot_dsp_decode(data,cut,123,&error);CHECK(!image && error);}
    image=pilot_dsp_decode(data,bytes,124,&error);CHECK(!image);
    unsigned offsets[]={0x800,0x804,0x808,0x80c,0x810,0x814,0x84c,0x850,0x854,0x858,0x85c,0x860,0x864,0x868,0x86c,0x870};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i){uint32_t *p=data+offsets[i]/4,old=*p;*p=0xffffffff;CHECK(!pilot_dsp_decode(data,bytes,123,&error));*p=old;}
    if(argc==4) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0 && n<=1024*1024);rewind(f);
        uint8_t *raw=malloc(n);CHECK(raw && fread(raw,1,n,f)==(size_t)n);fclose(f);
        image=pilot_dsp_decode(raw,(uint32_t)n,_strtoui64(argv[2],NULL,16),&error);
        if(!image)fprintf(stderr,"actual image: %s\n",error);CHECK(image);free(raw);
        storage=malloc(pilot_dsp_storage_bytes(image));CHECK(storage);d=pilot_dsp_create(image,storage,0x20000000);CHECK(d);
        /* Compare independently decoded original routines' bytes, when supplied
         * as an external private qualification artifact. Never compile assets. */
        f=fopen(argv[3],"rb");CHECK(f);raw=malloc(n);CHECK(raw && fread(raw,1,n,f)==(size_t)n);fclose(f);
        uint32_t words;memcpy(&words,raw+0x804,4);
        uint32_t *actual=(uint32_t*)(storage+pilot_dsp_descriptor_offset(image));
        CHECK(actual[2]>=0x20000000 && actual[2]-0x20000000+words*4<=0x4000);
        CHECK(!memcmp(storage+(actual[2]-0x20000000),raw+0x818,words*4));free(raw);
        memset(in,0,sizeof(in));for(unsigned c=0;c<32;++c)in[c*32]=0x100000;
        pilot_dsp_process(d,in,out);unsigned impulse=0;for(unsigned i=0;i<192;++i)impulse+=out[i]!=0;CHECK(impulse);
        memset(in,0,sizeof(in));unsigned tail=0;LARGE_INTEGER start,end,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
        for(unsigned frame=0;frame<1500;++frame){pilot_dsp_process(d,in,out);for(unsigned i=0;i<192;++i)tail+=out[i]!=0;}
        QueryPerformanceCounter(&end);CHECK(tail && pilot_dsp_dma_nodes(d)>1000);
        printf("actual graph: blocks=%llu DMA nodes=%llu impulse samples=%u tail samples=%u warm second=%.3f ms\n",
               pilot_dsp_blocks(d),pilot_dsp_dma_nodes(d),impulse,tail,(end.QuadPart-start.QuadPart)*1000.0/freq.QuadPart);
        pilot_dsp_destroy(d);free(storage);
    } else image=pilot_dsp_decode(data,bytes,123,&error);
    CHECK(image);storage=malloc(pilot_dsp_storage_bytes(image));CHECK(storage);
    d=pilot_dsp_create(image,storage,0x10000000);CHECK(d);pilot_dsp_image_destroy(image);
    CHECK(pilot_audio_open()==0);CHECK(pilot_audio_effects_install(d)==0);
    void *voice=NULL;CHECK(pilot_audio_buffer_create(512,2,44100,16,&voice)==0);
    int16_t pcm[256];for(unsigned i=0;i<256;++i)pcm[i]=(int16_t)((i%32)*16-256);
    CHECK(pilot_audio_buffer_copy(voice,0,pcm,sizeof(pcm),0)==0);
    CHECK(pilot_audio_buffer_play(voice,1)==0);Sleep(125);
    if(argc!=4) {
        uint32_t value=0x2345,observed=0;
        struct PilotDspPatch patches[]={{0,4,&value},{20,4,&value}};
        CHECK(pilot_audio_effects_patch(d,0,2,patches)<0);
        CHECK(pilot_dsp_effect_read(d,0,0,&observed,4) && !observed);
        CHECK(!pilot_audio_effects_patch(d,0,1,patches));
        CHECK(pilot_dsp_effect_read(d,0,0,&observed,4) && observed==value);
        /* This authored program ignores its state prefix. A real running
         * source/GP quantum survives the atomic update without graph reset. */
    }
    Sleep(125);
    uint32_t status=0,cursor=0,write=0;CHECK(pilot_audio_buffer_status(voice,&status)==0 && status==5);
    CHECK(pilot_audio_buffer_position(voice,&cursor,&write)==0 && cursor<512 && write<512);
    CHECK(pilot_audio_buffer_stop(voice)==0);pilot_audio_buffer_destroy(voice);pilot_audio_close();
    CHECK(pilot_dsp_blocks(d)>=100);printf("real XAudio2 effects path: %llu GP blocks in 250 ms of looping playback\n",pilot_dsp_blocks(d));
    pilot_dsp_destroy(d);free(storage);
    puts("DSP contract: real program changes output; owned relocations, persistent execution, bounds and invalid images passed");
    return 0;
}
