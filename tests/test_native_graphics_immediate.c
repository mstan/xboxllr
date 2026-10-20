#include <stdio.h>
#include <stdlib.h>
#define CHECK(condition) do { if(!(condition)) {fprintf(stderr,"FAIL immediate storage line%d: %s\n",__LINE__,#condition);return 1;} } while(0)
static int fail_allocation;
static void *allocate(void *old,size_t bytes)
{return fail_allocation?NULL:realloc(old,bytes);}
#define PILOT_IMMEDIATE_REALLOC allocate
#include "graphics_immediate.h"
int main(void)
{
    struct PilotImmediateBatch batch={0};float current[16][4]={0};
    CHECK(!pilot_immediate_append(&batch,current));
    CHECK(!pilot_immediate_begin(&batch,0) && !pilot_immediate_begin(&batch,11));
    CHECK(pilot_immediate_begin(&batch,6) && !pilot_immediate_begin(&batch,5));
    fail_allocation=1;CHECK(!pilot_immediate_append(&batch,current));
    CHECK(!batch.vertices && !batch.count && !batch.capacity && batch.primitive==6);
    fail_allocation=0;
    for(unsigned i=0;i<4097;++i) {
        current[0][0]=(float)i;current[3][0]=.1234567f;current[9][1]=(float)i*.25f;
        CHECK(pilot_immediate_append(&batch,current));
        if(batch.count==batch.capacity) {
            fail_allocation=1;CHECK(!pilot_immediate_append(&batch,current));
            CHECK(batch.count==i+1 && batch.vertices[i][0][0]==(float)i);fail_allocation=0;
        }
    }
    current[3][0]=99;
    for(unsigned i=0;i<4097;++i) {
        CHECK(batch.vertices[i][0][0]==(float)i && batch.vertices[i][3][0]==.1234567f);
        CHECK(batch.vertices[i][9][1]==(float)i*.25f);
    }
    PilotImmediateVertex *storage=batch.vertices;uint32_t capacity=batch.capacity;
    pilot_immediate_end(&batch);CHECK(!batch.count && !batch.primitive);
    CHECK(pilot_immediate_begin(&batch,5) && pilot_immediate_append(&batch,current));
    CHECK(batch.vertices==storage && batch.capacity==capacity && batch.vertices[0][3][0]==99);
    pilot_immediate_close(&batch);CHECK(!batch.vertices && !batch.count && !batch.capacity && !batch.primitive);
    puts("PASS checked immediate growth beyond4096, allocation-failure retention, independent snapshots, reuse and teardown");
    return 0;
}
