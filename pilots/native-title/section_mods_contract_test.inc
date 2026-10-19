#include "section_mods.h"
#ifndef PILOT_SECTION_MODS
#include "section_mods_hash.inc"
#endif
static int test_section_mod_code(void *context,uint32_t address,uint32_t bytes)
{(void)address;(void)bytes;return *(int*)context;}
static void test_section_mods(int preload,uint32_t boundary_flags)
{
    image_size=0x4000;free(image_bytes);image_bytes=calloc(1,image_size);CHECK(image_bytes);
    memcpy(image_bytes,"XBEH",4);
    image_base=0x10000;header_bytes=0x1000;section_table=0x10180;section_count=3;
    uint32_t header[]={image_base,header_bytes,0x6000};memcpy(image_bytes+0x104,header,12);
    memcpy(image_bytes+0x11c,&section_count,4);memcpy(image_bytes+0x120,&section_table,4);
    uint32_t descriptors[3][14]={
        {6,0x11000,0x1000,0x1000,0x1000,0,0,0x10700,0x10702},
        {8,0x12800,0x600,0x2000,0x600,0,0,0x10702,0x10704},
        {1,0x12e00,0x200,0x2800,0x200,0,0,0x10704,0x10706}};
    descriptors[1][0]|=(preload?2:0)|boundary_flags;
    memcpy(image_bytes+0x180,descriptors,sizeof(descriptors));
    memset(image_bytes+0x1000,0xc3,0x1000);memset(image_bytes+0x2000,0x11,0x600);
    memset(image_bytes+0x2800,0x7b,0x200);
    unsigned char payload[5003],source[0x4000],hash[32];
    for(unsigned i=0;i<sizeof(payload);++i)payload[i]=(unsigned char)(i*29+3);
    memcpy(source,image_bytes,sizeof(source));
    struct PilotSectionMod mod={0};mod.index=1;mod.counter_offset=0xffc;
    memcpy(mod.original_descriptor,descriptors[1],56);mod.payload=payload;mod.bytes=sizeof(payload);
    CHECK(section_mod_hash(NULL,PILOT_SECTION_SHA256,payload,sizeof(payload),mod.payload_sha256));
    CHECK(section_mod_hash(NULL,PILOT_SECTION_SHA256,image_bytes,(uint32_t)image_size,hash));
    char identity[65];for(unsigned i=0;i<32;++i)sprintf(identity+i*2,"%02x",hash[i]);
    struct PilotSectionModPlan plan={PILOT_SECTION_MOD_ABI,1,"authored.data-mod",identity,&mod};
    int code=0;struct PilotSectionModChecks checks={section_mod_hash,test_section_mod_code,&code};
    unsigned char *view=(unsigned char*)1;size_t view_bytes=123;
    code=1;CHECK(pilot_section_mods_create(image_bytes,image_size,identity,&plan,&checks,&view,&view_bytes));
    CHECK(!view && !view_bytes);code=0;
    ++mod.payload_sha256[0];CHECK(pilot_section_mods_create(image_bytes,image_size,identity,&plan,&checks,&view,&view_bytes));
    --mod.payload_sha256[0];mod.counter_offset=0x702;
    CHECK(pilot_section_mods_create(image_bytes,image_size,identity,&plan,&checks,&view,&view_bytes));
    mod.counter_offset=0xffc;
    const char *error=pilot_section_mods_create(image_bytes,image_size,identity,&plan,&checks,&view,&view_bytes);
    CHECK(!error && view && view_bytes==0x4000+sizeof(payload));
    CHECK(!memcmp(source,image_bytes,sizeof(source)));
    /* Exact permitted header deltas; original raw bytes and every unrelated
     * descriptor/field must survive unchanged. */
    for(unsigned i=0;i<sizeof(source);++i) {
        unsigned h=0x180+56;
        int permitted=(i>=0x10c && i<0x110) || (i>=h+4 && i<h+20) || (i>=h+28 && i<h+56);
        if(!permitted)CHECK(view[i]==source[i]);
    }
    image_loader_view=view;image_loader_length=view_bytes;
    /* The common harness precommits scratch through0x20000. A cold loader
     * starts these replacement pages absent; retain its separate stack page. */
    CHECK(VirtualFree(memory+0x16000,0x2000,MEM_DECOMMIT));
    CHECK(file32(0x10c)==0x8000 && file32(0x180+56+4)==0x16000 &&
          file32(0x180+56+8)==0x2000 && file32(0x180+56+12)==0x4000 &&
          file32(0x180+56+16)==sizeof(payload));
    CHECK(section_mod_hash(NULL,PILOT_SECTION_LENGTH_SHA1,payload,sizeof(payload),hash));
    CHECK(!memcmp(view+0x180+56+36,hash,20));
    CHECK(load_image_sections()==(unsigned)(preload?2:1));sections_initialize();sections_finalize_preloads();
    uint32_t target=section_table+56,neighbor=section_table+112;
    CHECK(MEM16(0x10700)==1 && MEM16(0x10702)==1 && !MEM16(0x10704));
    CHECK(MEM32(target+4)==0x16000 && MEM32(target+28)==0x10ffc && MEM32(target+32)==0x10ffe);
    if(!preload) {
        CHECK(!MEM32(target+24) && !MEM16(0x10ffc) && !MEM16(0x10ffe));
        CHECK(test_write_faults(0x16000));CHECK(!section_change(target,1));
    }
    for(unsigned life=0;life<3;++life) {
        CHECK(!memcmp(memory+0x16000,payload,sizeof(payload)) && !MEM8(0x16000+sizeof(payload)));
        CHECK(MEM32(target+24)==1 && MEM16(0x10ffc)==1 && MEM16(0x10ffe)==1);
        CHECK(test_write_faults(0x16000)==!!(boundary_flags&0x10));
        CHECK(test_write_faults(0x17000));
        if(!(boundary_flags&0x10))MEM8(0x16000)=payload[0];
        CHECK(!section_change(target,1) && MEM32(target+24)==2);
        CHECK(!section_change(target,0) && MEM32(target+24)==1);
        CHECK(!section_change(target,0) && !MEM32(target+24));
        CHECK(!MEM16(0x10ffc) && !MEM16(0x10ffe) && test_write_faults(0x16000));
        CHECK(MEM16(0x10702)==1 && !MEM16(0x10704));
        CHECK(!section_change(neighbor,1) && MEM8(0x12e00)==0x7b);
        CHECK(!section_change(neighbor,0));
        if(life<2)CHECK(!section_change(target,1));
    }
    CHECK(!memcmp(source,image_bytes,sizeof(source)) && !memcmp(view+0x4000,payload,sizeof(payload)));
    fprintf(stderr,"PASS: %s data mod preserves original identity/code, owns relocated pages/counters and survives real section unload/reload\n",
        preload?"preloaded":"deferred");
}
