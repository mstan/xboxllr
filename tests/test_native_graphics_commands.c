#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics_commands.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"command owner line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct Observer {uint32_t method[16],value[16];unsigned count,probes,fail_apply;};
static int consume(void *opaque,uint32_t method,uint32_t value,int apply)
{
    struct Observer *o=opaque;
    if(!apply){++o->probes;return (method==0x1710 && !value) || (method>=0x300 && method<=0x30c);}
    if(o->fail_apply && o->count==o->fail_apply-1)return 0;
    CHECK(o->count<16);o->method[o->count]=method;o->value[o->count++]=value;return 1;
}
int main(void)
{
    struct PilotCommandArena a={0};struct Observer o={0};
    CHECK(pilot_command_flush(&a,NULL,0,consume,&o)==PILOT_COMMAND_RANGE);
    CHECK(pilot_command_open(&a,0xfffffff0u,64,4)==PILOT_COMMAND_RANGE);
    CHECK(!pilot_command_open(&a,0x10000,4096,516));
    uint32_t next=0xaabbccdd,limit=0x11223344;
    uint32_t commands[]={0x41710,0,0,0xc0300,1,2,3,0x4008030c,4,5};
    CHECK(!pilot_command_flush(&a,commands,0x10000+sizeof(commands),consume,&o));
    CHECK(o.count==6 && o.probes==6 && a.methods==6 && a.submissions==1);
    CHECK(o.method[0]==0x1710 && o.method[1]==0x300 && o.method[2]==0x304 && o.method[3]==0x308);
    CHECK(o.method[4]==0x30c && o.method[5]==0x30c && o.value[5]==5);
    CHECK(!pilot_command_flush(&a,NULL,0x10000+sizeof(commands),consume,&o) && o.count==6);
    CHECK(!pilot_command_reserve(&a,NULL,0x10000+sizeof(commands),512,2048,consume,&o,&next,&limit));
    CHECK(next==0x10000 && limit==0x10600-4 && !a.consumed && a.methods==6);
    // A bad later packet cannot publish any earlier method or reservation.
    const uint32_t invalid[][4]={{0x41710,0,0x43000,0},{0x41710,0,0x40310,1},
        {0x41710,0,0x800300,1},{0x41710,0,0x42000,0},{0x41710,0,0x80000000,0},
        {0x41710,0,0x41ffc,0}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);++i) {
        CHECK(!pilot_command_open(&a,0x10000,4096,516));memset(&o,0,sizeof(o));
        next=0xaabbccdd;limit=0x11223344;
        CHECK(pilot_command_reserve(&a,invalid[i],0x10010,512,2048,consume,&o,&next,&limit));
        CHECK(!o.count && !a.consumed && next==0xaabbccdd && limit==0x11223344);
        CHECK(pilot_command_flush(&a,commands,0x10008,consume,&o)==PILOT_COMMAND_FAULTED && !o.count);
    }
    CHECK(!pilot_command_open(&a,0x10000,4096,516));memset(&o,0,sizeof(o));
    CHECK(pilot_command_reserve(&a,commands,0x10008,512,4097,consume,&o,&next,&limit));
    CHECK(!o.probes && !o.count); // invalid sizing precedes all method work
    CHECK(!pilot_command_open(&a,0x10000,4096,516));memset(&o,0,sizeof(o));o.fail_apply=2;
    CHECK(pilot_command_flush(&a,commands,0x10000+sizeof(commands),consume,&o)==PILOT_COMMAND_BACKEND);
    CHECK(o.count==1 && !a.consumed);
    CHECK(pilot_command_flush(&a,commands,0x10000+sizeof(commands),consume,&o)==PILOT_COMMAND_FAULTED && o.count==1);
    CHECK(!pilot_command_open(&a,0x10000,4096,516));
    CHECK(pilot_command_flush(&a,NULL,0x10001,consume,&o)==PILOT_COMMAND_RANGE);
    CHECK(!pilot_command_open(&a,0x10000,4096,516));
    memset(&o,0,sizeof(o));
    CHECK(!pilot_command_flush(&a,commands,0x10008,consume,&o));
    CHECK(pilot_command_flush(&a,NULL,0x10004,consume,&o)==PILOT_COMMAND_RANGE);
    puts("PASS command arena: exact header decoding/order, increment/nonincrement, NOP, no duplicate flush, bounded recycle, whole-span preflight, output canaries, fault latch/no replay");
    return 0;
}
