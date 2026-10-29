/* Independent image/GP-DMA adapter. MIT mborgerson/dsp56300 executes the actual
 * program. Public register facts are behavioral references, not imported code.
 * See docs/reference/audio/NATIVE-DSP-DONOR-2026-10-07.md for qualification and timing limits. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp56300.h"
#include "audio_dsp.h"
#ifdef PILOT_SESSION_RUNTIME
#include "session_replay.h"
#endif

enum { CODE_OFFSET=0x818, P_OFFSET=0, X_OFFSET=0x4000, Y_OFFSET=0x8000,
       SCRATCH_OFFSET=0xa000, SCRATCH_PREFIX=0xc000, MAX_SCRATCH=8*1024*1024,
       GP_WORDS=4096, LARGE_GRAPH_CALL=GP_WORDS-3 };
struct PilotDspImage {
    uint8_t *decoded;
    uint32_t code_words,state_words,state_base,descriptor,count,scratch_bytes,storage_bytes;
    uint32_t program_base,call_pc;
};
struct PilotDsp {
    Dsp56300Jit *core;
    uint32_t *p,*x,*y,mix[1024];
    uint8_t *scratch;
    uint32_t scratch_bytes,next,control,irq;
    uint32_t call_pc;
    uint8_t *shadow;
    uint32_t count,ranges[64][2]; /* X-word start and byte extent. */
    uint64_t blocks,nodes;
    uint8_t *owned,*guest,*dirty,*published;
    uint32_t storage_bytes,processing;
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
    if(!code || code>LARGE_GRAPH_CALL || !state || state>GP_WORDS)return NULL;
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
    if(word(image->decoded+CODE_OFFSET)!=0x65f400 || base>GP_WORDS || state>GP_WORDS-base)goto fail;
    for(uint32_t at=CODE_OFFSET;at<state_start;at+=4)if(word(image->decoded+at)&0xff000000)goto fail;
    /* Every entry's R5 assignment must agree with its actual relocated state.
     * The image graph falls through between entries and returns once. */
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *m=raw+desc+8+32*i;
        uint32_t c=word(m),s=word(m+8);
        if(word(m+4)<8 || word(image->decoded+c)!=0x65f400 || word(image->decoded+c+4)!=base+(s-state_start)/4)goto fail;
    }
    image->code_words=code;image->state_words=state;image->state_base=base;
    /* The old 0x400 placement leaves only 3072 program words. Larger images
     * use the actual full 4096-word GP bank and a native call/STOP harness
     * above the graph, with one in-bank decoder lookahead word. No hardware
     * bank or address guard is widened.
     * Initial X state can include temporary/output buffers above 0xc00; its
     * physical bound is the end of X RAM, not the speaker-output address. */
    image->program_base=code<=3072?0x400:0;
    image->call_pc=code<=3072?0:LARGE_GRAPH_CALL;
    image->descriptor=desc;image->count=count;image->scratch_bytes=SCRATCH_PREFIX+scratch;
    image->storage_bytes=SCRATCH_OFFSET+image->scratch_bytes+8+32*count+state*4;
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
{
    fprintf(stderr,"[PILOT DSP STOP] %s: %08x\n",reason,value);fflush(stderr);
#ifdef PILOT_SESSION_RUNTIME
    pilot_session_report_stop(reason,2);
#endif
    ExitProcess(2);
}
static void mark_store(PilotDsp *d,const void *pointer,unsigned bytes)
{
    uintptr_t p=(uintptr_t)pointer,base=(uintptr_t)d->owned;
    if(!d->owned || p<base || p+bytes>base+d->storage_bytes)return;
    uint32_t offset=(uint32_t)(p-base);
    for(unsigned i=0;i<bytes;++i)d->dirty[(offset+i)/8]|=(uint8_t)(1u<<((offset+i)&7));
}
static uint32_t isolated_x_read(void *opaque,uint32_t address){PilotDsp *d=opaque;return d->x[address];}
static void isolated_x_write(void *opaque,uint32_t address,uint32_t value){PilotDsp *d=opaque;d->x[address]=value;mark_store(d,d->x+address,4);}
static uint32_t isolated_y_read(void *opaque,uint32_t address){PilotDsp *d=opaque;return d->y[address];}
static void isolated_y_write(void *opaque,uint32_t address,uint32_t value){PilotDsp *d=opaque;d->y[address]=value;mark_store(d,d->y+address,4);}
static uint32_t isolated_p_read(void *opaque,uint32_t address){PilotDsp *d=opaque;return d->p[address];}
static void isolated_p_write(void *opaque,uint32_t address,uint32_t value){PilotDsp *d=opaque;d->p[address]=value;mark_store(d,d->p+address,4);}
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
                if(control&2){*byte=(uint8_t)(sample>>(8*b));mark_store(d,byte,1);}else sample|=(uint32_t)*byte<<(8*b);
                if(++offset==size)offset=0;
            }
            if(!(control&2)){*dst=sample>>8;mark_store(d,dst,4);}
        }
        if(control&16){uint32_t *position=dma_word(d,at+4);*position=offset;mark_store(d,position,4);}
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
static PilotDsp *create(const PilotDspImage *image,void *storage,uint32_t guest_base,int isolated)
{
    if(!image || !storage || ((uintptr_t)storage&3) || (uint64_t)guest_base+image->storage_bytes>0x100000000ull)return NULL;
    PilotDsp *d=calloc(1,sizeof(*d));if(!d)return NULL;
    if(isolated) {
        d->owned=malloc((size_t)image->storage_bytes*2+(image->storage_bytes+7)/8);
        if(!d->owned){free(d);return NULL;}
        d->guest=storage;d->storage_bytes=image->storage_bytes;
        storage=d->owned;d->published=d->owned+d->storage_bytes;d->dirty=d->published+d->storage_bytes;
    }
    memset(storage,0,image->storage_bytes);
    d->p=(uint32_t*)((uint8_t*)storage+P_OFFSET);d->x=(uint32_t*)((uint8_t*)storage+X_OFFSET);
    d->y=(uint32_t*)((uint8_t*)storage+Y_OFFSET);d->scratch=(uint8_t*)storage+SCRATCH_OFFSET;
    d->scratch_bytes=image->scratch_bytes;d->control=0x20;
    memcpy(d->p+image->program_base,image->decoded+CODE_OFFSET,image->code_words*4);
    memcpy(d->x+image->state_base,image->decoded+CODE_OFFSET+image->code_words*4,image->state_words*4);
    d->call_pc=image->call_pc;
    d->p[d->call_pc]=0x0d0000|image->program_base;d->p[d->call_pc+1]=0x87; /* Real JSR/RTS then STOP. */
    uint32_t *desc=(uint32_t*)((uint8_t*)storage+pilot_dsp_descriptor_offset(image));
    d->shadow=(uint8_t*)(desc+2+8*image->count);d->count=image->count;
    memcpy(d->shadow,image->decoded+CODE_OFFSET+image->code_words*4,image->state_words*4);
    desc[0]=image->count;desc[1]=image->scratch_bytes-SCRATCH_PREFIX;
    for(uint32_t i=0;i<image->count;++i) {
        const uint8_t *m=image->decoded+image->descriptor+8+32*i;uint32_t *map=desc+2+8*i;
        for(unsigned j=0;j<8;++j)map[j]=word(m+4*j);
        map[0]=guest_base+P_OFFSET+image->program_base*4+map[0]-CODE_OFFSET;
        uint32_t state_offset=map[2]-(CODE_OFFSET+image->code_words*4);
        d->ranges[i][0]=image->state_base+state_offset/4;d->ranges[i][1]=map[3];
        map[2]=guest_base+(uint32_t)(d->shadow-(uint8_t*)storage)+state_offset;
        map[4]+=guest_base+Y_OFFSET;map[6]+=guest_base+SCRATCH_OFFSET;
    }
    Dsp56300MemoryRegion pr[]={BUFFER(0,4096,d->p),GUARD(4096,0x1000000)};
    Dsp56300MemoryRegion xr[]={BUFFER(0,4096,d->x),GUARD(4096,0x1400),BUFFER(0x1400,0x1800,d->mix),
        {0x1800,0x1000000,DSP56300_REGION_CALLBACK,{.callback={d,peripheral_read,peripheral_write}}}};
    Dsp56300MemoryRegion yr[]={BUFFER(0,2048,d->y),GUARD(2048,0x1000000)};
    if(isolated) {
        pr[0]=(Dsp56300MemoryRegion){0,4096,DSP56300_REGION_CALLBACK,{.callback={d,isolated_p_read,isolated_p_write}}};
        xr[0]=(Dsp56300MemoryRegion){0,4096,DSP56300_REGION_CALLBACK,{.callback={d,isolated_x_read,isolated_x_write}}};
        yr[0]=(Dsp56300MemoryRegion){0,2048,DSP56300_REGION_CALLBACK,{.callback={d,isolated_y_read,isolated_y_write}}};
    }
    Dsp56300CreateInfo info={.memory_map={xr,4,yr,2,pr,2}};
    d->core=dsp56300_create(&info);
    if(!d->core){free(d->owned);free(d);return NULL;}
    if(d->owned){memcpy(d->guest,storage,d->storage_bytes);memcpy(d->published,storage,d->storage_bytes);}
    return d;
}
PilotDsp *pilot_dsp_create(const PilotDspImage *image,void *storage,uint32_t guest_base)
{return create(image,storage,guest_base,0);}
PilotDsp *pilot_dsp_create_isolated(const PilotDspImage *image,void *storage,uint32_t guest_base)
{return create(image,storage,guest_base,1);}
void pilot_dsp_begin(PilotDsp *d)
{
    if(!d || !d->owned || d->processing)invalid("invalid isolated DSP begin",0);
    for(uint32_t at=0;at<d->storage_bytes;at+=4096) {
        uint32_t n=d->storage_bytes-at;if(n>4096)n=4096;
        if(!memcmp(d->guest+at,d->published+at,n))continue;
        for(uint32_t i=at;i<at+n;i+=4)
            if(word(d->guest+i)!=word(d->published+i)) {
                if(i<0x4000)dsp56300_write_memory(d->core,DSP56300_MEM_SPACE_P,i/4,word(d->guest+i));
                else memcpy(d->owned+i,d->guest+i,4);
            }
    }
    memset(d->dirty,0,(d->storage_bytes+7)/8);d->processing=1;
}
void pilot_dsp_publish(PilotDsp *d)
{
    if(!d || !d->owned || !d->processing)invalid("invalid isolated DSP publication",0);
    for(uint32_t at=0;at<d->storage_bytes;at+=4096) {
        uint32_t n=d->storage_bytes-at;if(n>4096)n=4096;
        static const uint8_t clean[512]={0};
        if(!memcmp(d->dirty+at/8,clean,(n+7)/8) && !memcmp(d->guest+at,d->published+at,n))continue;
        for(uint32_t i=at;i<at+n;i+=4) {
            unsigned mask=(d->dirty[i/8]>>(i&7))&15;
            if(mask==15)memcpy(d->guest+i,d->owned+i,4);
            else if(!mask)memcpy(d->owned+i,d->guest+i,4);
            else for(unsigned b=0;b<4;++b) {
                if(mask&(1u<<b))d->guest[i+b]=d->owned[i+b];else d->owned[i+b]=d->guest[i+b];
            }
        }
    }
    memcpy(d->published,d->guest,d->storage_bytes);d->processing=0;
}
void pilot_dsp_destroy(PilotDsp *d){if(d){dsp56300_destroy(d->core);free(d->owned);free(d);}}
void pilot_dsp_process(PilotDsp *d,const uint32_t mix[1024],uint32_t output[192])
{
    if(!d || !mix || !output)invalid("missing DSP processing storage",0);
    for(unsigned i=0;i<1024;++i)d->mix[i]=mix[i]&0xffffff;
    /* The six direct speaker bins seed the GP output, then the downloaded graph
     * adds effect returns. This is the bounded host GP-framework contract. */
    memcpy(d->x+0xc00,d->mix,192*4);
    if(d->owned)memset(d->dirty+(X_OFFSET+0xc00*4)/8,255,192*4/8);
    Dsp56300State s;dsp56300_get_state(d->core,&s);
    s.pc=d->call_pc;s.power_state=0;s.halt_requested=false;
    dsp56300_set_state(d->core,&s);dsp56300_run(d->core,1000000);dsp56300_get_state(d->core,&s);
    if(s.pc!=d->call_pc+2 || s.power_state!=2 || s.registers[DSP56300_REG_SP])invalid("DSP graph failed to return within budget",s.pc);
    memcpy(output,d->x+0xc00,192*4);++d->blocks;
}
static uint64_t fingerprint_word(uint64_t h,uint64_t value,unsigned bytes)
{for(unsigned i=0;i<bytes;++i)h=(h^(uint8_t)(value>>(i*8)))*UINT64_C(1099511628211);return h;}
uint64_t pilot_dsp_fingerprint(const PilotDsp *d)
{
    Dsp56300State s;memset(&s,0,sizeof(s));dsp56300_get_state(d->core,&s);
    uint64_t h=UINT64_C(14695981039346656037);
#define H32(v) h=fingerprint_word(h,(uint32_t)(v),4)
    H32(s.pc);H32(s.pc_advance);H32(s.pc_on_rep);H32(s.cycle_count);H32(s.cycle_budget);
    for(unsigned i=0;i<2;++i)h=fingerprint_word(h,s.interrupts.pending_bits[i],8);
    H32(s.interrupts.pipeline_stage);H32(s.interrupts.vector_addr);H32(s.interrupts.saved_pc);H32(s.interrupts.state);
    for(unsigned i=0;i<128;++i)H32((uint8_t)s.interrupts.ipl[i]);
    H32(s.interrupts.ipl_to_raise);H32(s.loop_rep);H32(s.halt_requested);H32(s.power_state);
    for(unsigned i=0;i<64;++i)H32(s.registers[i]);
    for(unsigned i=0;i<2;++i)for(unsigned j=0;j<16;++j)H32(s.stack[i][j]);
    H32(d->next);H32(d->control);H32(d->irq);
    h=fingerprint_word(h,d->blocks,8);h=fingerprint_word(h,d->nodes,8);
#undef H32
    return h;
}
uint64_t pilot_dsp_blocks(const PilotDsp *d){return d->blocks;}
uint64_t pilot_dsp_dma_nodes(const PilotDsp *d){return d->nodes;}
static int effect_range(const PilotDsp *d,unsigned effect,uint32_t offset,uint32_t bytes)
{
    return d && effect<d->count && !(offset&3) && !(bytes&3) &&
        offset<=d->ranges[effect][1] && bytes<=d->ranges[effect][1]-offset;
}
static uint32_t effect_shadow_offset(const PilotDsp *d,unsigned effect)
{return (d->ranges[effect][0]-d->ranges[0][0])*4;}
int pilot_dsp_effect_read(const PilotDsp *d,unsigned effect,uint32_t offset,void *data,uint32_t bytes)
{
    if(!data || !effect_range(d,effect,offset,bytes))return 0;
    const uint8_t *shadow=d->owned?d->guest+(d->shadow-d->owned):d->shadow;
    memcpy(data,shadow+effect_shadow_offset(d,effect)+offset,bytes);return 1;
}
int pilot_dsp_effect_patch(PilotDsp *d,unsigned effect,unsigned count,const struct PilotDspPatch *patches)
{
    if(!d || d->processing || !patches || !count || count>16)return 0;
    for(unsigned i=0;i<count;++i)
        if(!patches[i].data || !effect_range(d,effect,patches[i].offset,patches[i].bytes))return 0;
    for(unsigned i=0;i<count;++i) {
        const struct PilotDspPatch *p=&patches[i];
        memcpy(d->shadow+effect_shadow_offset(d,effect)+p->offset,p->data,p->bytes);
        memcpy((uint8_t*)(d->x+d->ranges[effect][0])+p->offset,p->data,p->bytes);
        if(d->owned) {
            memcpy(d->guest+(d->shadow-d->owned)+effect_shadow_offset(d,effect)+p->offset,p->data,p->bytes);
            memcpy(d->guest+X_OFFSET+d->ranges[effect][0]*4+p->offset,p->data,p->bytes);
        }
    }
    return 1;
}
