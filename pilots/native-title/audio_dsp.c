/* Independent image/GP-DMA adapter. MIT mborgerson/dsp56300 executes the actual
 * program. Public register facts are behavioral references, not imported code.
 * See docs/NATIVE-DSP-DONOR-2026-10-07.md for qualification and timing limits. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp56300.h"
#include "audio_dsp.h"

enum { CODE_OFFSET=0x818, P_OFFSET=0, X_OFFSET=0x4000, Y_OFFSET=0x8000,
       SCRATCH_OFFSET=0xa000, SCRATCH_PREFIX=0xc000, MAX_SCRATCH=8*1024*1024 };
struct PilotDspImage {
    uint8_t *decoded;
    uint32_t code_words,state_words,state_base,descriptor,count,scratch_bytes,storage_bytes;
};
struct PilotDsp {
    Dsp56300Jit *core;
    uint32_t *p,*x,*y,mix[1024];
    uint8_t *scratch;
    uint32_t scratch_bytes,next,control,irq;
    uint64_t blocks,nodes;
};
static uint32_t word(const void *p) {uint32_t v;memcpy(&v,p,4);return v;}
static int range(uint32_t start,uint32_t bytes,uint32_t begin,uint32_t end)
{return start>=begin && start<=end && bytes<=end-start;}
static uint64_t key_step(uint64_t s)
{
    uint32_t lo=(uint32_t)s,hi=(uint32_t)(s>>32);
    uint32_t feedback=((((lo<<3)^(lo<<1)^lo)<<28)^hi)&0x80000000u;
    return (s>>1)|((uint64_t)feedback<<32);
}
static void decode_bytes(uint8_t *data,uint32_t bytes,const uint8_t key[8],uint64_t initial)
{
    uint64_t stream=key_step(initial);
    for(uint32_t i=0;i<bytes;++i) {
        stream=key_step(stream);uint8_t d=(uint8_t)(initial>>(8*(i&7)));
        data[i]=d^(uint8_t)stream^(uint8_t)(data[i]-d*key[i&7]);
    }
}
PilotDspImage *pilot_dsp_decode(const void *source,uint32_t bytes,uint64_t seed,const char **error)
{
    const uint8_t *raw=source;
    if(error)*error="invalid DSP image extent";
    if(!raw || bytes<CODE_OFFSET+12 || bytes>1024*1024 || bytes%4)return NULL;
    uint32_t code=word(raw+0x804),state=word(raw+0x80c);
    if(!code || code>3072 || !state || state>3072)return NULL;
    uint32_t state_start=CODE_OFFSET+code*4,desc=state_start+state*4;
    if(error)*error="unqualified DSP image header";
    if(word(raw+0x800)!=0 || word(raw+0x808)!=state_start || word(raw+0x810)!=3 || word(raw+0x814)!=0 || desc+8>bytes)return NULL;
    uint32_t count=word(raw+desc),scratch=word(raw+desc+4);
    if(error)*error="invalid DSP image descriptor";
    if(!count || count>64 || scratch>MAX_SCRATCH || desc+8+40*count!=bytes)return NULL;
    uint32_t key_offset=desc+8+32*count,code_end=CODE_OFFSET,state_end=state_start;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *m=raw+desc+8+32*i;
        uint32_t c=word(m),cn=word(m+4),s=word(m+8),sn=word(m+12);
        uint32_t y=word(m+16),yn=word(m+20),ext=word(m+24),en=word(m+28);
        if((c|cn|s|sn|y|yn|ext|en)&3 || !cn || !sn || c!=code_end || s!=state_end ||
           !range(c,cn,CODE_OFFSET,state_start-4) || !range(s,sn,state_start,desc) ||
           !range(y,yn,0,8192) || !range(ext,en,SCRATCH_PREFIX,SCRATCH_PREFIX+scratch))return NULL;
        code_end=c+cn;state_end=s+sn;
    }
    if(code_end!=state_start-4 || state_end>desc || word(raw+state_start-4)!=0x0c)return NULL;
    PilotDspImage *image=calloc(1,sizeof(*image));
    if(!image){if(error)*error="DSP image allocation failed";return NULL;}
    image->decoded=malloc(bytes);
    if(!image->decoded){free(image);if(error)*error="DSP image allocation failed";return NULL;}
    memcpy(image->decoded,raw,bytes);
    for(unsigned i=0;i<81;++i)seed=key_step(seed);
    uint8_t initial[8];for(unsigned i=0;i<8;++i)initial[i]=(uint8_t)(seed>>(8*i));
    uint8_t *keys=image->decoded+key_offset;
    decode_bytes(keys,count*8,initial,seed);
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *m=raw+desc+8+32*i;
        decode_bytes(image->decoded+word(m),word(m+4),keys+8*i,seed);
    }
    uint32_t base=word(image->decoded+CODE_OFFSET+4);
    if(error)*error="unqualified decoded DSP program/state mapping";
    if(word(image->decoded+CODE_OFFSET)!=0x65f400 || base>0xc00 || state>0xc00-base)goto fail;
    for(uint32_t at=CODE_OFFSET;at<state_start;at+=4)if(word(image->decoded+at)&0xff000000)goto fail;
    /* Every entry's R5 assignment must agree with its actual relocated state.
     * The image graph falls through between entries and returns once. */
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *m=raw+desc+8+32*i;
        uint32_t c=word(m),s=word(m+8);
        if(word(m+4)<8 || word(image->decoded+c)!=0x65f400 || word(image->decoded+c+4)!=base+(s-state_start)/4)goto fail;
    }
    image->code_words=code;image->state_words=state;image->state_base=base;
    image->descriptor=desc;image->count=count;image->scratch_bytes=SCRATCH_PREFIX+scratch;
    image->storage_bytes=SCRATCH_OFFSET+image->scratch_bytes+8+32*count;
    if(error)*error=NULL;
    return image;
fail:
    pilot_dsp_image_destroy(image);return NULL;
}
void pilot_dsp_image_destroy(PilotDspImage *image)
{if(image){free(image->decoded);free(image);}}
uint32_t pilot_dsp_storage_bytes(const PilotDspImage *image){return image->storage_bytes;}
uint32_t pilot_dsp_descriptor_offset(const PilotDspImage *image){return SCRATCH_OFFSET+image->scratch_bytes;}
static void invalid(const char *reason,uint32_t value)
{fprintf(stderr,"[PILOT DSP STOP] %s: %08x\n",reason,value);fflush(stderr);ExitProcess(2);}
static uint32_t *dma_word(PilotDsp *d,uint32_t address)
{
    if(address<4096)return d->x+address;
    if(address>=0x1400 && address<0x1800)return d->mix+address-0x1400;
    if(address>=0x1800 && address<0x2000)return d->y+address-0x1800;
    invalid("DMA address",address);return NULL;
}
static void transfer(PilotDsp *d)
{
    for(unsigned nodes=0;!(d->next&0x4000);++nodes) {
        if(nodes==1024 || d->next&~0x3fff)invalid("DMA list cycle/address",d->next);
        uint32_t at=d->next,n[7];for(unsigned i=0;i<7;++i)n[i]=*dma_word(d,at+i)&0xffffff;
        unsigned control=n[1],count=n[2],ring=(control>>5)&15,stride=control>>14;
        if((control&0x200d) || ((control>>10)&7)!=2 || ring!=14 || stride!=1 || !count || count>4096)
            invalid("DMA format/stride",control);
        uint32_t size=n[6]+1,offset=n[4];
        if(offset>=size || (uint64_t)n[5]+size>d->scratch_bytes)invalid("DMA scratch bounds",n[5]);
        /* Validate the entire descriptor before a single byte is transferred. */
        for(unsigned i=0;i<count;++i)dma_word(d,n[3]+i);
        for(unsigned i=0;i<count;++i) {
            uint32_t *dst=dma_word(d,n[3]+i),sample=(control&2)?(*dst&0xffffff)<<8:0;
            for(unsigned b=0;b<4;++b) {
                uint8_t *byte=d->scratch+n[5]+offset;
                if(control&2)*byte=(uint8_t)(sample>>(8*b));else sample|=(uint32_t)*byte<<(8*b);
                if(++offset==size)offset=0;
            }
            if(!(control&2))*dst=sample>>8;
        }
        if(control&16)*dma_word(d,at+4)=offset;
        d->next=n[0];++d->nodes;
    }
    if(d->next&~0x7fff)invalid("DMA terminal address",d->next);
    d->irq|=0x80; /* Only after real copies, with the engine armed until STOP. */
}
static uint32_t peripheral_read(void *opaque,uint32_t address)
{
    PilotDsp *d=opaque;
    if(address==0xffffd6)return d->control;
    if(address==0xffffd4)return d->next;
    if(address==0xffffc5)return d->irq;
    invalid("unqualified X read",address);return 0;
}
static void peripheral_write(void *opaque,uint32_t address,uint32_t value)
{
    PilotDsp *d=opaque;
    if(address==0xffffd4){d->next=value;return;}
    if(address==0xffffc5 && !(value&~0x80)){d->irq&=~value;return;}
    if(address==0xffffd6) {
        switch(value) {
        case 1:
            if(d->control&8)invalid("starting frozen DMA engine",value);
            d->control=0x10;transfer(d);return;
        case 2:d->control=(d->control&8)|0x20;return;
        /* Transfers finish within START in this batched contract. A later
         * freeze/unfreeze changes the idle/armed engine state, losing no work. */
        case 3:d->control|=8;return;
        case 4:d->control&=~8u;return;
        }
    }
    invalid("unqualified X write",address);
}
static uint32_t unmapped_read(void *opaque,uint32_t address)
{(void)opaque;invalid("unmapped DSP memory read",address);return 0;}
static void unmapped_write(void *opaque,uint32_t address,uint32_t value)
{(void)opaque;(void)value;invalid("unmapped DSP memory write",address);}
#define BUFFER(a,b,c) {a,b,DSP56300_REGION_BUFFER,{.buffer={c,0}}}
#define GUARD(a,b) {a,b,DSP56300_REGION_CALLBACK,{.callback={NULL,unmapped_read,unmapped_write}}}
PilotDsp *pilot_dsp_create(const PilotDspImage *image,void *storage,uint32_t guest_base)
{
    if(!image || !storage || ((uintptr_t)storage&3) || (uint64_t)guest_base+image->storage_bytes>0x100000000ull)return NULL;
    PilotDsp *d=calloc(1,sizeof(*d));if(!d)return NULL;
    memset(storage,0,image->storage_bytes);
    d->p=(uint32_t*)((uint8_t*)storage+P_OFFSET);d->x=(uint32_t*)((uint8_t*)storage+X_OFFSET);
    d->y=(uint32_t*)((uint8_t*)storage+Y_OFFSET);d->scratch=(uint8_t*)storage+SCRATCH_OFFSET;
    d->scratch_bytes=image->scratch_bytes;d->control=0x20;
    memcpy(d->p+0x400,image->decoded+CODE_OFFSET,image->code_words*4);
    memcpy(d->x+image->state_base,image->decoded+CODE_OFFSET+image->code_words*4,image->state_words*4);
    d->p[0]=0x0d0400;d->p[1]=0x87; /* JSR graph; STOP only after its real RTS. */
    uint32_t *desc=(uint32_t*)((uint8_t*)storage+pilot_dsp_descriptor_offset(image));
    desc[0]=image->count;desc[1]=image->scratch_bytes-SCRATCH_PREFIX;
    for(uint32_t i=0;i<image->count;++i) {
        const uint8_t *m=image->decoded+image->descriptor+8+32*i;uint32_t *map=desc+2+8*i;
        for(unsigned j=0;j<8;++j)map[j]=word(m+4*j);
        map[0]=guest_base+P_OFFSET+0x1000+map[0]-CODE_OFFSET;
        map[2]=guest_base+X_OFFSET+image->state_base*4+map[2]-(CODE_OFFSET+image->code_words*4);
        map[4]+=guest_base+Y_OFFSET;map[6]+=guest_base+SCRATCH_OFFSET;
    }
    Dsp56300MemoryRegion pr[]={BUFFER(0,4096,d->p),GUARD(4096,0x1000000)};
    Dsp56300MemoryRegion xr[]={BUFFER(0,4096,d->x),GUARD(4096,0x1400),BUFFER(0x1400,0x1800,d->mix),
        {0x1800,0x1000000,DSP56300_REGION_CALLBACK,{.callback={d,peripheral_read,peripheral_write}}}};
    Dsp56300MemoryRegion yr[]={BUFFER(0,2048,d->y),GUARD(2048,0x1000000)};
    Dsp56300CreateInfo info={.memory_map={xr,4,yr,2,pr,2}};
    d->core=dsp56300_create(&info);
    if(!d->core){free(d);return NULL;}
    return d;
}
void pilot_dsp_destroy(PilotDsp *d){if(d){dsp56300_destroy(d->core);free(d);}}
void pilot_dsp_process(PilotDsp *d,const uint32_t mix[1024],uint32_t output[192])
{
    if(!d || !mix || !output)invalid("missing DSP processing storage",0);
    for(unsigned i=0;i<1024;++i)d->mix[i]=mix[i]&0xffffff;
    /* The six direct speaker bins seed the GP output, then the downloaded graph
     * adds effect returns. This is the bounded host GP-framework contract. */
    memcpy(d->x+0xc00,d->mix,192*4);
    Dsp56300State s;dsp56300_get_state(d->core,&s);
    s.pc=0;s.power_state=0;s.halt_requested=false;
    dsp56300_set_state(d->core,&s);dsp56300_run(d->core,1000000);dsp56300_get_state(d->core,&s);
    if(s.pc!=2 || s.power_state!=2 || s.registers[DSP56300_REG_SP])invalid("DSP graph failed to return within budget",s.pc);
    memcpy(output,d->x+0xc00,192*4);++d->blocks;
}
uint64_t pilot_dsp_blocks(const PilotDsp *d){return d->blocks;}
uint64_t pilot_dsp_dma_nodes(const PilotDsp *d){return d->nodes;}
