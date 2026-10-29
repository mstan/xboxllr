#include "resource_mirror.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);return 1;}} while(0)
#if defined(_WIN32) && !defined(PILOT_WRITE_WATCH_PORTABLE)
#include <windows.h>
int main(void)
{
    unsigned char *allocation=VirtualAlloc(NULL,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    unsigned char *shadow_allocation=malloc(65536);
    CHECK(allocation && shadow_allocation);
    memset(allocation,0x44,65536);memset(shadow_allocation,0xee,65536);
    unsigned char *source=allocation+3,*shadow=shadow_allocation+3;
    size_t bytes=8192+8;
    memset(source,0x11,bytes);memcpy(shadow,source,bytes);
    CHECK(pilot_write_watch_initialize(allocation,65536));
    pilot_resource_mirror_diagnostics=1;
    PilotResourceIdentity key={pilot_resource_new_identity(),1,0,0,(uint32_t)bytes,1,1};
    PilotResourcePages owner={0},alias={0};
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,4,32));
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,4,32);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,4,32));
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,4093,1));
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,4080,40);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,4080,40));
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,bytes-9,9);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,bytes-9,9));
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,bytes,1));
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source+1,bytes-1,4,16));

    /* A consumed subset is unchanged, but another byte in that guest-page
     * intersection differs. It must never certify the subset as a whole page. */
    source[10]^=1;
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,100,16);
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));
    shadow[10]=source[10];
    pilot_resource_pages_invalidate(&owner,10,1);
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,100,16);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));

    PilotResourceIdentity alias_key=key;alias_key.generation=pilot_resource_new_identity();
    pilot_resource_pages_capture(&alias,&alias_key,source,shadow,bytes,100,16);
    CHECK(pilot_resource_pages_reusable(&alias,&alias_key,source,bytes,100,16));
    source[20]^=2;
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));
    CHECK(!pilot_resource_pages_reusable(&alias,&alias_key,source,bytes,100,16));
    memcpy(shadow,source,bytes);
    pilot_resource_pages_invalidate_all(&owner);
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,100,16);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));
    pilot_resource_pages_invalidate_all(&owner);
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));
    /* GPU-only mutation/refetch has its own authority even with clean CPU pages. */
    shadow[25]^=4;pilot_resource_pages_invalidate(&owner,25,1);
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,100,16);
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));
    shadow[25]=source[25];pilot_resource_pages_invalidate(&owner,25,1);
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,100,16);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,100,16));

    PilotResourceIdentity replaced=key;replaced.generation=pilot_resource_new_identity();
    CHECK(!pilot_resource_pages_reusable(&owner,&replaced,source,bytes,100,16));
    pilot_resource_pages_capture(&owner,&replaced,source,shadow,bytes,100,16);
    CHECK(pilot_resource_pages_reusable(&owner,&replaced,source,bytes,100,16));
    replaced.format=7;
    CHECK(!pilot_resource_pages_reusable(&owner,&replaced,source,bytes,100,16));

    CHECK(pilot_write_watch_native_begin(source,32));
    pilot_resource_pages_invalidate_all(&owner);
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,0,32);
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,bytes,0,32));
    source[0]=3;pilot_write_watch_native_end(source,32);shadow[0]=source[0];
    pilot_resource_pages_invalidate(&owner,0,32);
    pilot_resource_pages_capture(&owner,&key,source,shadow,bytes,0,32);
    CHECK(pilot_resource_pages_reusable(&owner,&key,source,bytes,0,32));

    PilotResourceWhole whole={0};
    PilotResourceIdentity image={pilot_resource_new_identity(),2,0,0x12,32,32,1};
    CHECK(pilot_resource_whole_begin(&whole,&image,allocation+16384,8192));
    pilot_resource_whole_end(&whole,10);
    CHECK(pilot_resource_whole_reusable(&whole,&image,allocation+16384,8192,10));
    CHECK(!pilot_resource_whole_reusable(&whole,&image,allocation+16384,8192,11));
    CHECK(!pilot_resource_whole_reusable(&whole,&image,allocation+16384,8192,0));
    PilotResourceIdentity other_image=image;other_image.subresource=1;
    CHECK(!pilot_resource_whole_reusable(&whole,&other_image,allocation+16384,8192,10));
    allocation[16400]^=1;
    CHECK(!pilot_resource_whole_reusable(&whole,&image,allocation+16384,8192,10));

    /* This target compiles the real module with a 4 KiB metadata quota. */
    PilotResourcePages bounded[64]={0};unsigned refused=0;
    memcpy(shadow,source,bytes);
    for(unsigned i=0;i<64;++i){
        PilotResourceIdentity identity=key;identity.generation=pilot_resource_new_identity();
        pilot_resource_pages_capture(&bounded[i],&identity,source,shadow,bytes,0,bytes);
        if(!bounded[i].pages)++refused;
    }
    CHECK(refused && pilot_resource_mirror_stats.metadata_refusals);
    CHECK(pilot_resource_mirror_stats.metadata_bytes<=4096);
    for(unsigned i=0;i<64;++i)pilot_resource_pages_clear(&bounded[i]);
    pilot_resource_pages_clear(&owner);pilot_resource_pages_clear(&alias);
    CHECK(!pilot_resource_mirror_stats.metadata_bytes);
    pilot_write_watch_finish();CHECK(VirtualFree(allocation,0,MEM_RELEASE));free(shadow_allocation);
    fprintf(stderr,"PASS resource mirror: unaligned owned spans, partial-consumption refusal, aliases, native lease, GPU/reset/reuse identity, layout and bounded metadata\n");
    return 0;
}
#else
int main(void)
{
    unsigned char source[8192]={0},mirror[8192]={0};
    PilotResourcePages owner={0};PilotResourceWhole whole={0};
    PilotResourceIdentity key={pilot_resource_new_identity(),1,0,0,8192,1,1};
    pilot_resource_pages_capture(&owner,&key,source,mirror,sizeof(source),0,32);
    CHECK(!pilot_resource_pages_reusable(&owner,&key,source,sizeof(source),0,32));
    CHECK(!pilot_resource_whole_begin(&whole,&key,source,sizeof(source)));
    source[1]=9;memcpy(mirror,source,sizeof(source));CHECK(!memcmp(mirror,source,sizeof(source)));
    pilot_resource_pages_clear(&owner);
    fprintf(stderr,"PASS resource mirror portable: unavailable observations retain actual byte operations\n");
    return 0;
}
#endif
