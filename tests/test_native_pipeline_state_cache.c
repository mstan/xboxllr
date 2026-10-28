#include "pipeline_state_cache.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

struct Key {uint32_t first,second;};
struct Object {struct Key key;unsigned device,live;};
static struct Object objects[256];
static unsigned created,destroyed,live,device=1,fail_next;
static void *create(const void *data)
{
    if(fail_next){fail_next=0;return NULL;}
    assert(created<256);struct Object *object=objects+created++;
    object->key=*(const struct Key*)data;object->device=device;object->live=1;++live;
    return object;
}
static void destroy(void *pointer)
{
    struct Object *object=pointer;assert(object && object->live);
    object->live=0;++destroyed;--live;
}
int main(void)
{
    struct PilotPipelineCache cache={0};volatile uint64_t counts[4]={0};
    struct Key a={1,2},b={2,1}; /* same cheap XOR identity, different complete descriptors */
    struct Object *first=pilot_pipeline_cache_get(&cache,&a,sizeof(a),create,destroy,counts);
    struct Object *second=pilot_pipeline_cache_get(&cache,&b,sizeof(b),create,destroy,counts);
    assert(first!=second && first->key.first==1 && second->key.first==2);
    assert(pilot_pipeline_cache_get(&cache,&a,sizeof(a),create,destroy,counts)==first);
    assert(counts[0]==3 && counts[1]==1 && counts[2]==2 && counts[3]==0 && live==2);
    struct Key third={3,4};fail_next=1;
    assert(!pilot_pipeline_cache_get(&cache,&third,sizeof(third),create,destroy,counts));
    assert(first->live && second->live && destroyed==0);
    /* Changing the descriptor bytes after admission cannot mutate its key. */
    struct Object *third_object=pilot_pipeline_cache_get(&cache,&third,sizeof(third),create,destroy,counts);
    third.first=99;struct Key original_third={3,4};
    assert(pilot_pipeline_cache_get(&cache,&original_third,sizeof(original_third),create,destroy,counts)==third_object);
    struct Object *latest=NULL;
    for(unsigned i=3;i<80;++i) {
        struct Key key={i,i+1000};latest=pilot_pipeline_cache_get(&cache,&key,sizeof(key),create,destroy,counts);
        assert(latest && latest->key.second==i+1000 && live<=PILOT_PIPELINE_CACHE_CAPACITY);
    }
    assert(cache.count==PILOT_PIPELINE_CACHE_CAPACITY && counts[3]>0);
    unsigned previous_destroyed=destroyed;fail_next=1;
    assert(!pilot_pipeline_cache_get(&cache,&third,sizeof(third),create,destroy,counts));
    assert(latest->live && destroyed==previous_destroyed); /* full-cache failure retires nothing */
    pilot_pipeline_cache_clear(&cache,destroy);
    assert(!live && created==destroyed && !cache.count);
    ++device;
    struct Object *fresh=pilot_pipeline_cache_get(&cache,&a,sizeof(a),create,destroy,counts);
    assert(fresh->device==2); /* a reset cannot return an object from the old device */
    pilot_pipeline_cache_clear(&cache,destroy);assert(!live && created==destroyed);
    assert(!pilot_pipeline_cache_get(&cache,&a,0,create,destroy,counts));
    assert(!pilot_pipeline_cache_get(&cache,&a,PILOT_PIPELINE_KEY_BYTES+1,create,destroy,counts));
    puts("PASS: immutable complete keys, bounded ownership/eviction, failed creation and device retirement");
    return 0;
}
