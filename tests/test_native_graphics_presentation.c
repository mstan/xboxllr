/* Production descriptor and optional surface owner; authored data, no GPU. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphics_presentation.h"
#include "graphics_surfaces.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %u: %s\n",(unsigned)__LINE__,#x);return 1;}}while(0)
static int32_t ref(void *context,unsigned index,int acquire)
{unsigned *refs=context;if(index>=2)return -1;if(acquire)return (int32_t)++refs[index];return refs[index]?(int32_t)--refs[index]:-1;}
int main(void)
{
    uint32_t p[17]={640,480,7,1,0x2222,1,0,0,0,0,0x20,0,1,0,0,0,0};
    PilotPresentation result;CHECK(pilot_presentation_parse(p,&result));
    CHECK(pilot_presentation_profile_admits(p,1) && !pilot_presentation_profile_admits(p,0));
    CHECK(!pilot_presentation_profile_admits(p,2));
    CHECK(pilot_presentation_mask(&result)==3 && pilot_presentation_axis(&result)==2);
    uint32_t size[3];CHECK(pilot_presentation_surface(&result,0,size));
    CHECK(size[0]==1280 && size[1]==960 && size[2]==5120);
    CHECK(pilot_presentation_surface(&result,1,size) && size[0]==640 && size[1]==480 && size[2]==2560);
    CHECK(!pilot_presentation_surface(&result,2,size));
    CHECK(pilot_presentation_samples(&result,0)==0x2222 && pilot_presentation_samples(&result,1)==0x11);
    const unsigned indices[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    const uint32_t invalid[]={0,0,8,2,0x1022,2,1,1,2,0x2e,0x60,50,2,1,1,1,1};
    for(unsigned i=0;i<17;++i){uint32_t saved=p[indices[i]];p[indices[i]]=invalid[i];CHECK(!pilot_presentation_parse(p,&result));p[indices[i]]=saved;}
    p[0]=2049;CHECK(!pilot_presentation_parse(p,&result));p[0]=640;
    p[10]=0x40;CHECK(pilot_presentation_parse(p,&result));p[10]=0x20;
    CHECK(pilot_presentation_parse(p,&result));
    uint32_t device[64]={0};PilotSurfaceLayout layout={16,32,64,80,88};
    CHECK(pilot_surface_project_mask(device,sizeof(device),&layout,0x2000,0x2000,0,PILOT_SURFACE_PUBLISH,3));
    CHECK(device[4]==2 && device[8]==0x2000 && device[9]==0x2020 && !device[16] && !device[22]);
    CHECK(pilot_surface_project_mask(device,sizeof(device),&layout,0x2000,0x8000,0x9000,PILOT_SURFACE_CURRENT,3));
    CHECK(!device[16] && device[22]==0x9000); /* independent owned depth is allowed */
    uint32_t common[3]={0x010d0001,0x010d0000,0};unsigned tracked[3]={0},refs[2]={0};
    CHECK(!pilot_surface_sync_mask(common,tracked,3,ref,refs) && tracked[0]==1 && refs[0]==1);
    common[0]--;CHECK(!pilot_surface_sync_mask(common,tracked,3,ref,refs) && !refs[0]);
    common[2]=0x010d0000;CHECK(pilot_surface_sync_mask(common,tracked,3,ref,refs)==PILOT_SURFACE_BAD_COMMON);
    common[2]=0;tracked[2]=1;CHECK(pilot_surface_sync_mask(common,tracked,3,ref,refs)==PILOT_SURFACE_BAD_COMMON);
    p[4]=0x11;p[8]=1;p[9]=0x2a;p[10]=0;CHECK(pilot_presentation_parse(p,&result));
    CHECK(pilot_presentation_profile_admits(p,0) && pilot_presentation_profile_admits(p,1));
    CHECK(pilot_presentation_mask(&result)==7 && pilot_presentation_surface(&result,2,size));
    CHECK(size[0]==640 && size[1]==480 && pilot_presentation_axis(&result)==1);
    CHECK(pilot_surface_project_mask(device,sizeof(device),&layout,0x3000,0x3000,0x3040,PILOT_SURFACE_PUBLISH,7));
    CHECK(device[8]==0x3000 && device[9]==0x3020 && device[16]==0x3040 && device[22]==0x3040);
    CHECK(pilot_surface_project_mask(device,sizeof(device),&layout,0,0,0,PILOT_SURFACE_CLEAR,7));
    CHECK(!device[4] && !device[8] && !device[9] && !device[16] && !device[20] && !device[22]);
    puts("PASS guest Gaussian/legacy parameters, dimensions, descriptions, optional depth projection and reference ownership");return 0;
}
