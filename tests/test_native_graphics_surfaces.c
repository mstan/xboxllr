/* Production portable helper, authored owned-resource backend. No GPU/title. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics_surfaces.h"
static void check(int condition,const char *message){if(!condition){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
struct Backend { unsigned refs[3],calls,adds,releases; };
static int32_t reference(void *context,unsigned index,int acquire)
{
    struct Backend *backend=context;++backend->calls;
    if(index>=3 || !backend->refs[index] || (!acquire && backend->refs[index]==1))return -1;
    if(acquire){++backend->refs[index];++backend->adds;}
    else {--backend->refs[index];++backend->releases;}
    return (int32_t)(backend->refs[index]-1); /* Base device ownership stays live. */
}
int main(void)
{
    uint32_t device[64];for(unsigned i=0;i<64;++i)device[i]=0xcccccccc;
    const PilotSurfaceLayout layout={16,32,64,80,88};
    check(pilot_surface_project(device,sizeof(device),&layout,0x2000,0x2000,0x2040,PILOT_SURFACE_PUBLISH),"initial projection");
    check(device[4]==2 && device[8]==0x2000 && device[9]==0x2020 && device[16]==0x2040,"two stable colors and separate depth");
    check(device[20]==0x2000 && device[22]==0x2040,"index0 is initial current color");
    check(pilot_surface_project(device,sizeof(device),&layout,0x2000,0x9000,0xa000,PILOT_SURFACE_CURRENT),"offscreen current targets");
    check(device[8]==0x2000 && device[9]==0x2020 && device[16]==0x2040 && device[20]==0x9000 && device[22]==0xa000,"SetTargets preserves presentation");
    check(pilot_surface_project(device,sizeof(device),&layout,0x2000,0x9000,0,PILOT_SURFACE_CURRENT) && !device[22],"NULL depth projection");
    check(pilot_surface_project(device,sizeof(device),&layout,0x2000,0x2000,0x2040,PILOT_SURFACE_PUBLISH),"reset republishes");
    check(device[20]==0x2000 && device[22]==0x2040 && device[9]==0x2020,"reset current and stable headers");
    uint32_t saved[64];memcpy(saved,device,sizeof(saved));PilotSurfaceLayout bad=layout;bad.current_depth=bad.depth;
    check(!pilot_surface_project(device,sizeof(device),&bad,0x2000,0,0,PILOT_SURFACE_PUBLISH) && !memcmp(saved,device,sizeof(saved)),"overlap rejects before writes");
    bad=layout;bad.depth=sizeof(device);check(!pilot_surface_project(device,sizeof(device),&bad,0x2000,0,0,PILOT_SURFACE_PUBLISH),"bounds guard");

    struct Backend backend={{1,1,1},0,0,0};unsigned tracked[3]={0};
    uint32_t common[3]={0x010d0000,0x010d0000,0x010d0000};
    common[0]+=2;check(!pilot_surface_sync_set(common,tracked,reference,&backend),"adopt inline increases");
    check(tracked[0]==2 && backend.refs[0]==3 && backend.adds==2,"real backend refs adopted");
    common[0]-=2;check(!pilot_surface_sync_set(common,tracked,reference,&backend),"adopt inline decreases");
    check(!tracked[0] && backend.refs[0]==1 && backend.releases==2,"inline releases retain base owner");
    ++common[1];check(!pilot_surface_adjust(1,&common[1],&tracked[1],1,reference,&backend),"named AddRef after inline AddRef");
    check(common[1]==0x010d0002 && tracked[1]==2 && backend.refs[1]==3,"no lost inline reference");
    check(!pilot_surface_adjust(1,&common[1],&tracked[1],-1,reference,&backend) && common[1]==0x010d0001,"named Release exact guest count");
    --common[1];check(!pilot_surface_sync_set(common,tracked,reference,&backend) && backend.refs[1]==1,"deferred inline decrement reconciled at boundary");
    unsigned calls=backend.calls;common[0]=0x010dffff;
    check(pilot_surface_adjust(0,&common[0],&tracked[0],1,reference,&backend)==PILOT_SURFACE_OVERFLOW && backend.calls==calls,"overflow guards before adoption/backend mutation");
    common[0]=0x010e0000;check(pilot_surface_sync_set(common,tracked,reference,&backend)==PILOT_SURFACE_BAD_COMMON && backend.calls==calls,"high Common corruption");
    common[0]=0x010d0000;check(pilot_surface_adjust(0,&common[0],&tracked[0],-1,reference,&backend)==PILOT_SURFACE_UNDERFLOW && backend.calls==calls,"release underflow");
    common[0]++;common[2]=0x000d0000;
    check(pilot_surface_sync_set(common,tracked,reference,&backend)==PILOT_SURFACE_BAD_COMMON && backend.calls==calls,"preflight all headers before any mutation");
    common[2]=0x010d0000;check(!pilot_surface_sync_set(common,tracked,reference,&backend) && tracked[0]==1,"device lifetime sees inline-held reference");
    check(tracked[0] || tracked[1] || tracked[2],"outstanding ownership blocks Reset/device retirement");
    --common[0];check(!pilot_surface_sync_set(common,tracked,reference,&backend),"release outstanding inline reference");
    check(!tracked[0] && !tracked[1] && !tracked[2] && backend.adds==backend.releases,"all acquired references balanced");
    check(pilot_surface_project(device,sizeof(device),&layout,0,0,0,PILOT_SURFACE_CLEAR),"clear before freeing owned headers");
    check(!device[4] && !device[8] && !device[9] && !device[16] && !device[20] && !device[22] && device[10]==0xcccccccc,"all stale pointers cleared, neighbors unchanged");
    for(unsigned i=0;i<3;++i){check(backend.refs[i]==1,"base owner survives guest reference lifetime");--backend.refs[i];}
    puts("PASS production surface projection, inline references, backend balance and lifetime guards; no GPU");return 0;
}
