#include "resource_mirror.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

enum { MIRROR_PAGE=4096 };
#ifndef PILOT_RESOURCE_MIRROR_BUDGET_BYTES
#define PILOT_RESOURCE_MIRROR_BUDGET_BYTES (16u*1024u*1024u)
#endif
#define MIRROR_OBJECT_LIMIT (1024u*1024u)
struct PilotResourcePage {PilotWriteSnapshot snapshot;uint64_t reset_epoch;unsigned blocked;};
int pilot_resource_mirror_diagnostics;
PilotResourceMirrorStats pilot_resource_mirror_stats;
PilotResourceTransferStats pilot_resource_transfer_stats;
static uint64_t identities;
static size_t metadata_bytes;
#define COUNT(field,amount) do {if(pilot_resource_mirror_diagnostics)pilot_resource_mirror_stats.field+=(uint64_t)(amount);} while(0)
static int identity_equal(const PilotResourceIdentity *a,const PilotResourceIdentity *b)
{
    return a->generation && a->generation==b->generation && a->kind==b->kind &&
        a->subresource==b->subresource && a->format==b->format && a->width==b->width &&
        a->height==b->height && a->levels==b->levels;
}
static int range_valid(const void *source,size_t total,size_t offset,size_t bytes)
{
    return source && total && (uintptr_t)source<=UINTPTR_MAX-total && bytes &&
        offset<=total && bytes<=total-offset;
}
uint64_t pilot_resource_new_identity(void)
{
    /* Graphics owner serializes creation; exhaustion remains unavailable. */
    if(identities==UINT64_MAX)return 0;
    return ++identities;
}
int pilot_resource_whole_reusable(PilotResourceWhole *owner,const PilotResourceIdentity *identity,
    const void *source,size_t bytes,uint64_t revision)
{
    COUNT(whole_lookups,1);
    int reusable=revision && identity_equal(&owner->identity,identity) && owner->gpu_revision==revision &&
        owner->snapshot.data==source && owner->snapshot.bytes==bytes &&
        pilot_write_watch_reusable(&owner->snapshot);
    if(reusable)COUNT(whole_hits,1);
    return reusable;
}
int pilot_resource_whole_begin(PilotResourceWhole *owner,const PilotResourceIdentity *identity,const void *source,size_t bytes)
{
    memset(owner,0,sizeof(*owner));
    if(!identity->generation || !range_valid(source,bytes,0,bytes))return 0;
    owner->identity=*identity;
    return pilot_write_watch_begin(source,bytes,&owner->snapshot);
}
void pilot_resource_whole_end(PilotResourceWhole *owner,uint64_t revision)
{
    pilot_write_watch_end(&owner->snapshot);owner->gpu_revision=revision;
    if(!revision)owner->snapshot.valid=0;
    if(owner->snapshot.valid)COUNT(whole_captures,1);
}
void pilot_resource_pages_clear(PilotResourcePages *owner)
{
    if(owner->pages){metadata_bytes-=owner->metadata_bytes;free(owner->pages);}
    memset(owner,0,sizeof(*owner));
    pilot_resource_mirror_stats.metadata_bytes=metadata_bytes;
}
static size_t page_number(const PilotResourcePages *owner,size_t offset)
{return (((uintptr_t)owner->source&(MIRROR_PAGE-1))+offset)/MIRROR_PAGE;}
static void page_span(const PilotResourcePages *owner,size_t page,size_t *offset,size_t *bytes)
{
    size_t lead=(uintptr_t)owner->source&(MIRROR_PAGE-1);
    size_t begin=page*MIRROR_PAGE;
    *offset=begin>lead?begin-lead:0;
    size_t end=begin+MIRROR_PAGE-lead;
    if(end>owner->bytes)end=owner->bytes;
    *bytes=end-*offset;
}
void pilot_resource_pages_invalidate_all(PilotResourcePages *owner)
{
    if(!owner->pages)return;
    /* Reset is independent of CPU writes. Never bless the old GPU mirror. */
    memset(owner->pages,0,owner->metadata_bytes);
    if(owner->reset_epoch==UINT64_MAX){pilot_resource_pages_clear(owner);return;}
    ++owner->reset_epoch;
}
void pilot_resource_pages_invalidate(PilotResourcePages *owner,size_t offset,size_t bytes)
{
    if(!owner->pages || !range_valid(owner->source,owner->bytes,offset,bytes))return;
    size_t first=page_number(owner,offset),last=page_number(owner,offset+bytes-1);
    for(size_t i=first;i<=last;++i)memset(&owner->pages[i],0,sizeof(owner->pages[i]));
}
static int matches(const PilotResourcePages *owner,const PilotResourceIdentity *identity,const void *source,size_t total)
{return owner->pages && owner->source==source && owner->bytes==total && identity_equal(&owner->identity,identity);}
int pilot_resource_pages_reusable(PilotResourcePages *owner,const PilotResourceIdentity *identity,
    const void *source,size_t total,size_t offset,size_t bytes)
{
    COUNT(lookups,1);COUNT(lookup_bytes,bytes);
    if(!range_valid(source,total,offset,bytes) || !matches(owner,identity,source,total))return 0;
    size_t first=page_number(owner,offset),last=page_number(owner,offset+bytes-1);
    for(size_t i=first;i<=last;++i){
        PilotResourcePage *page=owner->pages+i;
        if(page->blocked || page->reset_epoch!=owner->reset_epoch || !pilot_write_watch_reusable(&page->snapshot))return 0;
    }
    COUNT(hits,1);return 1;
}
static int prepare(PilotResourcePages *owner,const PilotResourceIdentity *identity,const void *source,size_t total)
{
    if(matches(owner,identity,source,total))return 1;
    pilot_resource_pages_clear(owner);
    if(!identity->generation || !range_valid(source,total,0,total))return 0;
    size_t lead=(uintptr_t)source&(MIRROR_PAGE-1);
    if(total>SIZE_MAX-lead-(MIRROR_PAGE-1))return 0;
    size_t count=(lead+total+MIRROR_PAGE-1)/MIRROR_PAGE;
    if(count>MIRROR_OBJECT_LIMIT/sizeof(PilotResourcePage)) {COUNT(metadata_refusals,1);return 0;}
    size_t allocation=count*sizeof(PilotResourcePage);
    if(allocation>PILOT_RESOURCE_MIRROR_BUDGET_BYTES || metadata_bytes>PILOT_RESOURCE_MIRROR_BUDGET_BYTES-allocation){COUNT(metadata_refusals,1);return 0;}
    PilotResourcePage *pages=calloc(count,sizeof(*pages));
    if(!pages){COUNT(metadata_refusals,1);return 0;}
    owner->source=source;owner->bytes=total;owner->identity=*identity;
    owner->pages=pages;owner->count=count;owner->metadata_bytes=allocation;owner->reset_epoch=1;
    metadata_bytes+=allocation;pilot_resource_mirror_stats.metadata_bytes=metadata_bytes;
    if(metadata_bytes>pilot_resource_mirror_stats.metadata_peak)pilot_resource_mirror_stats.metadata_peak=metadata_bytes;
    return 1;
}
void pilot_resource_pages_capture(PilotResourcePages *owner,const PilotResourceIdentity *identity,
    const void *source,const void *mirror,size_t total,size_t offset,size_t bytes)
{
    if(!mirror || !range_valid(source,total,offset,bytes) || !prepare(owner,identity,source,total))return;
    size_t first=page_number(owner,offset),last=page_number(owner,offset+bytes-1);
    for(size_t i=first;i<=last;++i){
        PilotResourcePage *page=owner->pages+i;size_t at,length;
        if(page->blocked)continue;
        if(page->reset_epoch==owner->reset_epoch && pilot_write_watch_reusable(&page->snapshot))continue;
        page_span(owner,i,&at,&length);COUNT(capture_attempts,1);
        if(!pilot_write_watch_begin((const unsigned char*)source+at,length,&page->snapshot)){
            page->blocked=1;COUNT(unavailable_pages,1);continue;
        }
        COUNT(compared_bytes,length);
        if(memcmp((const unsigned char*)source+at,(const unsigned char*)mirror+at,length)){
            page->snapshot.valid=0;page->blocked=1;COUNT(rejected_pages,1);continue;
        }
        pilot_write_watch_end(&page->snapshot);
        if(!page->snapshot.valid){page->blocked=1;COUNT(rejected_pages,1);continue;}
        page->reset_epoch=owner->reset_epoch;COUNT(captured_pages,1);COUNT(captured_bytes,length);
    }
}
