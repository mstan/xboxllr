#include "visual_mods.h"
#include "visual_mod_plan.h"
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"visual policy line %d: %s\n",__LINE__,#c);return 1;}}while(0)
static int parse(const char *operations,struct PilotVisualPlan *plan)
{
    FILE *file=tmpfile();if(!file)return 0;
    fprintf(file,"xlle-mod-plan 2\nidentity %064u\n%s",0,operations);rewind(file);
    int result=pilot_visual_plan_read(file,plan);fclose(file);return result;
}
int main(void)
{
    struct PilotVisualMods o;uint32_t size[2];pilot_visual_mods_default(&o);
    CHECK(pilot_visual_mods_size(&o,640,480,640,480,16384,size) && size[0]==640 && size[1]==480);
    o.internal_height=1080;o.aspect_numerator=7;o.aspect_denominator=3;
    CHECK(pilot_visual_mods_size(&o,640,480,640,480,16384,size) && size[0]==2520 && size[1]==1080);
    CHECK(pilot_visual_mods_size(&o,640,480,320,240,16384,size) && size[0]==1260 && size[1]==540);
    CHECK(pilot_visual_mods_target_size(&o,640,480,2,2,16384,size) && size[0]==2 && size[1]==2);
    CHECK(pilot_visual_mods_target_size(&o,640,480,256,256,16384,size) && size[0]==256 && size[1]==256);
    CHECK(pilot_visual_mods_target_size(&o,640,480,320,240,16384,size) && size[0]==1260 && size[1]==540);
    o.internal_height=1440;o.aspect_numerator=43;o.aspect_denominator=18;
    CHECK(pilot_visual_mods_size(&o,640,480,640,480,16384,size) && size[0]==3440 && size[1]==1440);
    o.internal_height=1800;o.aspect_numerator=16;o.aspect_denominator=9;
    CHECK(pilot_visual_mods_size(&o,640,480,640,480,16384,size) && size[0]==3200 && size[1]==1800);
    o.internal_height=2160;o.aspect_numerator=32;o.aspect_denominator=9;o.supersample=2;
    CHECK(pilot_visual_mods_size(&o,640,480,640,480,16384,size) && size[0]==15360 && size[1]==4320);
    o.supersample=3;CHECK(!pilot_visual_mods_size(&o,640,480,640,480,16384,size));
    o.supersample=1;CHECK(!pilot_visual_mods_size(&o,640,480,4096,4096,16384,size));
    CHECK(!pilot_visual_mods_size(&o,640,480,0xffffffffu,1,16384,size));
    struct PilotVisualMods original=o;
    for(unsigned mode=0;mode<=2;++mode){o=original;o.frame_mode=mode;o.frame_rate=mode?60:0;CHECK(pilot_visual_mods_validate(&o));}
    o.frame_mode=3;CHECK(!pilot_visual_mods_validate(&o));
    o=original;o.frame_mode=PILOT_FRAMES_ACTUAL;o.frame_rate=0;CHECK(!pilot_visual_mods_validate(&o));
    o=original;o.frame_rate=60;CHECK(!pilot_visual_mods_validate(&o));
    o=original;o.anisotropy=3;CHECK(!pilot_visual_mods_validate(&o));
    o=original;o.aspect_numerator=64;CHECK(!pilot_visual_mods_validate(&o));
    struct PilotVisualPlan p;
    CHECK(parse("operation aspect 1 example.widescreen 7 3 1\noperation resolution 1 1080\noperation frame-rate 1 1 60\nend\n",&p));
    CHECK(p.options.frame_mode==PILOT_FRAMES_INTERPOLATED && p.options.internal_height==1080 && p.adaptive==1);
    CHECK(parse("operation frame-rate 1 2 60\nend\n",&p) && p.options.frame_mode==PILOT_FRAMES_ACTUAL);
    const char *bad[]={
        "operation frame-rate 1 1 60\noperation frame-rate 1 2 60\nend\n",
        "operation frame-rate 1 3 60\nend\n", "operation frame-rate 1 0 0\nend\n",
        "operation resolution 1 4294968376\nend\n", "operation resolution 1 -4294966216\nend\n",
        "operation resolution 1 1080 extra\nend\n", "operation resolution 2 1080\nend\n",
        "operation resolution 1 1080\noperation resolution 1 1440\nend\n",
        "operation filtering 1 3\nend\n", "operation unknown 1 1\nend\n", "end\nextra\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        memset(&p,0x5a,sizeof(p));struct PilotVisualPlan before=p;
        CHECK(!parse(bad[i],&p) && !memcmp(&p,&before,sizeof(p)));
    }
    fprintf(stderr,"PASS: original canvas, resolution/aspect/resource-family scale, SSAA bounds and exclusive frame modes\n");return 0;
}
