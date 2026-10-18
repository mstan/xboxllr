#include "continuation_diagnostic.h"

static const PilotContSchema *schema;
static PilotContFrame frames[PILOT_CONT_MAX_FRAMES];
static unsigned count, depth, in_checkpoint;

static void clear_frame(PilotContFrame *f)
{
    f->function=f->token=f->callee=f->fa=f->fb=0;
    f->fas=f->fbs=f->flags=0;
    f->ebp=f->icall_esp=f->icall_target=0;
}
void pilot_cont_bind(const PilotContSchema *value)
{
    if(depth) { pilot_cont_fault("bind during guest execution"); return; }
    schema=value;count=0;in_checkpoint=0;
}
PilotContFrame *pilot_cont_enter(uint32_t function)
{
    if(!schema || depth>=PILOT_CONT_MAX_FRAMES) {
        pilot_cont_fault("continuation depth/schema"); return NULL;
    }
    PilotContFrame *f=&frames[depth];
    if(depth<count) {
        if(f->function!=function) {
            pilot_cont_fault("restored call target differs"); return NULL;
        }
    } else { clear_frame(f);f->function=function;++count; }
    ++depth;return f;
}
void pilot_cont_leave(void)
{
    if(!depth) { pilot_cont_fault("continuation underflow"); return; }
    count=--depth;
}
unsigned pilot_cont_depth(void) { return depth; }

static const PilotContSite *site(const PilotContFrame *f)
{
    for(size_t i=0;i<schema->site_count;++i)
        if(schema->sites[i].function==f->function && schema->sites[i].token==f->token)
            return &schema->sites[i];
    return NULL;
}
static int known_function(uint32_t function)
{
    for(size_t i=0;i<schema->site_count;++i)
        if(schema->sites[i].function==function)return 1;
    return 0;
}
void pilot_cont_at_label(PilotContFrame *f,uint32_t token,uint32_t address)
{
    if(!depth || f!=&frames[depth-1] || in_checkpoint)
        pilot_cont_fault("checkpoint outside active guest label");
    f->token=token;f->callee=0;
    const PilotContSite *s=site(f);
    if(!s || s->is_call || s->guest_address!=address)
        pilot_cont_fault("checkpoint label differs from schema");
    in_checkpoint=1;
    pilot_cont_checkpoint(f->function,address);
    in_checkpoint=0;
}
void pilot_cont_validate_call(const PilotContFrame *f)
{
    const PilotContSite *s=site(f);
    if(!s || !s->is_call || !known_function(f->callee) ||
       (s->direct_target?s->direct_target!=f->callee:f->icall_target!=f->callee))
        pilot_cont_fault("call target outside continuation schema");
}
static int valid(const PilotContFrame *f,unsigned n)
{
    if(!schema || !n || n>PILOT_CONT_MAX_FRAMES)return 0;
    for(unsigned i=0;i<n;++i) {
        const PilotContSite *s=site(f+i);
        if(!s)return 0;
        if(i+1<n) {
            if(!s->is_call || f[i].callee!=f[i+1].function ||
               (s->direct_target?s->direct_target!=f[i].callee:
                                 f[i].icall_target!=f[i].callee))return 0;
        } else if(s->is_call || f[i].callee)return 0;
    }
    return 1;
}
static void put32(uint8_t *p,uint32_t v)
{ for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t checksum(const uint8_t *p,size_t n)
{ uint32_t h=2166136261u;for(size_t i=0;i<n;++i)h=(h^p[i])*16777619u;return h; }
/* Copy signed bit patterns without out-of-range signed integer conversions. */
static uint32_t bits(int32_t v)
{ return (uint32_t)v; }
static int32_t signed_bits(uint32_t v)
{ return v<=INT32_MAX?(int32_t)v:-1-(int32_t)(UINT32_MAX-v); }

size_t pilot_cont_save(void *output,size_t capacity)
{
    size_t n=48u+(size_t)count*44u;
    if(!in_checkpoint || !output || depth!=count || capacity<n || !valid(frames,count))return 0;
    uint8_t *p=(uint8_t*)output;
    put32(p,0x54434c58u);put32(p+4,PILOT_CONT_VERSION);
    for(unsigned i=0;i<32;++i)p[8+i]=schema->identity[i];
    put32(p+40,count);
    for(unsigned i=0;i<count;++i) {
        const PilotContFrame *f=frames+i;
        uint32_t values[]={f->function,f->token,f->callee,f->fa,f->fb,
            bits(f->fas),bits(f->fbs),bits(f->flags),f->ebp,f->icall_esp,f->icall_target};
        for(unsigned j=0;j<11;++j)put32(p+44+i*44+j*4,values[j]);
    }
    put32(p+n-4,checksum(p,n-4));return n;
}
int pilot_cont_load(const void *input,size_t size)
{
    PilotContFrame staged[PILOT_CONT_MAX_FRAMES];
    const uint8_t *p=(const uint8_t*)input;
    if(!schema || depth || !p || size<48 || get32(p)!=0x54434c58u ||
       get32(p+4)!=PILOT_CONT_VERSION)return 0;
    for(unsigned i=0;i<32;++i)if(p[8+i]!=schema->identity[i])return 0;
    unsigned n=get32(p+40);
    if(!n || n>PILOT_CONT_MAX_FRAMES || size!=48u+(size_t)n*44u ||
       get32(p+size-4)!=checksum(p,size-4))return 0;
    for(unsigned i=0;i<n;++i) {
        const uint8_t *r=p+44+i*44;
        PilotContFrame *f=staged+i;
        f->function=get32(r);f->token=get32(r+4);f->callee=get32(r+8);
        f->fa=get32(r+12);f->fb=get32(r+16);
        f->fas=signed_bits(get32(r+20));f->fbs=signed_bits(get32(r+24));
        f->flags=signed_bits(get32(r+28));f->ebp=get32(r+32);
        f->icall_esp=get32(r+36);f->icall_target=get32(r+40);
    }
    if(!valid(staged,n))return 0;
    for(unsigned i=0;i<n;++i)frames[i]=staged[i];
    count=n;return 1;
}
