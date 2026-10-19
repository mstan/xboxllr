#include "section_mods.h"
#include <stdlib.h>
#include <string.h>

static uint32_t section_mod_u32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void section_mod_put(unsigned char *p,uint32_t x)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(x>>(8*i));}
static uint64_t section_mod_align(uint64_t x)
{return (x+4095)&~(uint64_t)4095;}

const char *pilot_section_mods_create(const unsigned char *original,size_t bytes,
    const char *image_sha256,const struct PilotSectionModPlan *plan,
    const struct PilotSectionModChecks *checks,unsigned char **view,size_t *view_bytes)
{
    if(!view || !view_bytes)return "missing section mod output";
    *view=NULL;*view_bytes=0;
    if(!original || bytes<0x178 || bytes>256u*1024*1024 || memcmp(original,"XBEH",4) ||
       !plan || plan->abi!=PILOT_SECTION_MOD_ABI || !plan->id || !*plan->id ||
       !image_sha256 || strlen(image_sha256)!=64 || !plan->image_sha256 ||
       strcmp(image_sha256,plan->image_sha256) || !plan->count || plan->count>16 ||
       !plan->sections || !checks || !checks->hash || !checks->contains_code)
        return "invalid section mod identity or checks";
    uint32_t base=section_mod_u32(original+0x104),headers=section_mod_u32(original+0x108);
    uint32_t extent=section_mod_u32(original+0x10c),count=section_mod_u32(original+0x11c);
    uint32_t table=section_mod_u32(original+0x120);
    if(headers<0x178 || headers>bytes || !count || count>4096 || table<base ||
       (uint64_t)table-base<0x178 || (uint64_t)table-base+count*56ull>headers ||
       (uint64_t)base+headers>0x04000000u || extent<headers ||
       (uint64_t)base+extent>0x04000000u)return "invalid section mod source layout";
    size_t table_at=table-base;
    uint64_t raw_end=bytes,virtual_end=section_mod_align((uint64_t)base+extent),payload_total=0;
    uint32_t addresses[16],sizes[16],raws[16];
    for(uint32_t i=0;i<count;++i) {
        const unsigned char *s=original+table_at+i*56;
        uint32_t address=section_mod_u32(s+4),size=section_mod_u32(s+8);
        uint32_t raw=section_mod_u32(s+12),length=section_mod_u32(s+16);
        if(!size || address<(uint64_t)base+headers || (uint64_t)address+size>(uint64_t)base+extent ||
           length>size || raw<headers || (uint64_t)raw+length>bytes || section_mod_u32(s+24))
            return "invalid original section range or references";
    }
    for(uint32_t i=0;i<plan->count;++i) {
        const struct PilotSectionMod *m=&plan->sections[i];
        if(m->index>=count || !m->payload || !m->bytes ||
           (payload_total+=m->bytes)>16u*1024*1024)return "invalid section mod payload";
        const unsigned char *s=original+table_at+m->index*56;
        if(memcmp(s,m->original_descriptor,56) || (section_mod_u32(s)&4) ||
           checks->contains_code(checks->context,section_mod_u32(s+4),section_mod_u32(s+8)))
            return "section mod targets code or changed descriptor";
        uint32_t cell=m->counter_offset;
        if((cell&1) || cell<table_at+count*56ull || (uint64_t)cell+4>headers ||
           section_mod_u32(original+cell))return "invalid reviewed section counter reservation";
        for(uint32_t j=0;j<count;++j) {
            const unsigned char *other=original+table_at+j*56;
            uint32_t counters[]={section_mod_u32(other+28),section_mod_u32(other+32)};
            for(unsigned c=0;c<2;++c)
                if(counters[c]<(uint64_t)base+cell+4 && (uint64_t)counters[c]+2>(uint64_t)base+cell)
                    return "section mod counter reservation already owned";
        }
        for(uint32_t j=0;j<i;++j)
            if(m->index==plan->sections[j].index ||
               (cell<(uint64_t)plan->sections[j].counter_offset+4 &&
                (uint64_t)cell+4>plan->sections[j].counter_offset))return "overlapping section mods";
        unsigned char hash[32];
        if(!checks->hash(checks->context,PILOT_SECTION_SHA256,m->payload,m->bytes,hash) ||
           memcmp(hash,m->payload_sha256,32))return "section mod payload digest mismatch";
        uint64_t size=section_mod_align(m->bytes),raw=section_mod_align(raw_end);
        if(virtual_end+size>0x04000000u || raw+m->bytes>UINT32_MAX)
            return "section mod exceeds owned image range";
        addresses[i]=(uint32_t)virtual_end;sizes[i]=(uint32_t)size;raws[i]=(uint32_t)raw;
        virtual_end+=size;raw_end=raw+m->bytes;
    }
    unsigned char *result=calloc(1,(size_t)raw_end);
    if(!result)return "section mod allocation failed";
    memcpy(result,original,bytes);
    for(uint32_t i=0;i<plan->count;++i) {
        const struct PilotSectionMod *m=&plan->sections[i];
        unsigned char *s=result+table_at+m->index*56,hash[32];
        if(!checks->hash(checks->context,PILOT_SECTION_LENGTH_SHA1,m->payload,m->bytes,hash)) {
            free(result);return "section mod section digest failed";
        }
        section_mod_put(s+4,addresses[i]);section_mod_put(s+8,sizes[i]);
        section_mod_put(s+12,raws[i]);section_mod_put(s+16,m->bytes);
        section_mod_put(s+28,base+m->counter_offset);section_mod_put(s+32,base+m->counter_offset+2);
        memcpy(s+36,hash,20);memcpy(result+raws[i],m->payload,m->bytes);
    }
    section_mod_put(result+0x10c,(uint32_t)(virtual_end-base));
    *view=result;*view_bytes=(size_t)raw_end;return NULL;
}
