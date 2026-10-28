#ifndef PILOT_PIPELINE_STATE_CACHE_H
#define PILOT_PIPELINE_STATE_CACHE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One native renderer owner serializes these operations. Keys are complete,
 * initialized immutable descriptors; objects are borrowed until the next get
 * or clear on this cache. The native graphics context separately retains GPU
 * references, exactly as when the old current object was destroyed/replaced.
 * No guest state, resource contents or completion serial lives here. */
enum {PILOT_PIPELINE_CACHE_CAPACITY=64,PILOT_PIPELINE_KEY_BYTES=128};
typedef void *(*PilotPipelineCreate)(const void *key);
typedef void (*PilotPipelineDestroy)(void *object);
struct PilotPipelineCache {
    struct {unsigned char key[PILOT_PIPELINE_KEY_BYTES];size_t bytes;void *object;}
        entries[PILOT_PIPELINE_CACHE_CAPACITY];
    unsigned count,next;
};
/* Counters: requests, hits, factory attempts, evictions. They are diagnostic
 * observations, not synchronization or a measurement of game performance. */
static inline void *pilot_pipeline_cache_get(struct PilotPipelineCache *cache,
    const void *key,size_t bytes,PilotPipelineCreate create,PilotPipelineDestroy destroy,
    volatile uint64_t counters[4])
{
    if(!cache || !key || !bytes || bytes>PILOT_PIPELINE_KEY_BYTES || !create || !destroy || !counters)return NULL;
    ++counters[0];
    for(unsigned i=0;i<cache->count;++i)
        if(cache->entries[i].object && cache->entries[i].bytes==bytes &&
           !memcmp(cache->entries[i].key,key,bytes)) {
            ++counters[1];return cache->entries[i].object;
        }
    ++counters[2];void *object=create(key);
    if(!object)return NULL; /* failed creation leaves every cached owner intact */
    unsigned slot;
    if(cache->count<PILOT_PIPELINE_CACHE_CAPACITY)slot=cache->count++;
    else {
        slot=cache->next;cache->next=(slot+1)%PILOT_PIPELINE_CACHE_CAPACITY;
        destroy(cache->entries[slot].object);++counters[3];
    }
    memcpy(cache->entries[slot].key,key,bytes);cache->entries[slot].bytes=bytes;
    cache->entries[slot].object=object;return object;
}
static inline void pilot_pipeline_cache_clear(struct PilotPipelineCache *cache,PilotPipelineDestroy destroy)
{
    for(unsigned i=0;i<cache->count;++i)destroy(cache->entries[i].object);
    memset(cache,0,sizeof(*cache));
}
#endif
