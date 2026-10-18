#include "visual_mod_plan.h"
#include <string.h>
static int numbers_bounded(const char *text)
{
    while(*text) {
        while(*text==' ' || *text=='\t')++text;
        if(!*text)break;
        if(*text=='-' || *text=='+')return 0;
        if(*text>='0' && *text<='9') {
            uint32_t n=0;
            while(*text>='0' && *text<='9') {
                unsigned digit=(unsigned)(*text++-'0');
                if(n>(UINT32_MAX-digit)/10)return 0;
                n=n*10+digit;
            }
            if(*text && *text!=' ' && *text!='\t')return 0;
        } else while(*text && *text!=' ' && *text!='\t')++text;
    }
    return 1;
}

static int line(FILE *file,char data[256])
{
    if(!fgets(data,256,file))return 0;
    size_t n=strlen(data);
    if(n && data[n-1]!='\n' && !feof(file))return 0;
    while(n && (data[n-1]=='\n' || data[n-1]=='\r'))data[--n]=0;
    return n!=0;
}
int pilot_visual_plan_read(FILE *file,struct PilotVisualPlan *output)
{
    struct PilotVisualPlan p={0};char data[256];int consumed=0;
    pilot_visual_mods_default(&p.options);
    if(!file || !output || !line(file,data) || strcmp(data,"xlle-mod-plan 2") || !line(file,data) ||
       sscanf(data,"identity %64[0-9a-f]%n",p.identity,&consumed)!=1 ||
       strlen(p.identity)!=64 || data[consumed])return 0;
    for(unsigned count=0;count<=6;++count) {
        if(!line(file,data))return 0;
        if(!strcmp(data,"end")) {
            int c;while((c=fgetc(file))!=EOF)if(c!=' ' && c!='\t' && c!='\r' && c!='\n')return 0;
            if(ferror(file) || !pilot_visual_mods_validate(&p.options))return 0;
            *output=p;return 1;
        }
        char operation[32],version[8];consumed=0;
        if(sscanf(data,"operation %31s %7s %n",operation,version,&consumed)!=2 || strcmp(version,"1") || !consumed)return 0;
        const char *arguments=data+consumed;int end=0;unsigned bit=0;
        if(!numbers_bounded(arguments))return 0;
        if(!strcmp(operation,"aspect")) {
            if(sscanf(arguments,"%95s %u %u %u %n",p.aspect_plugin,
                &p.options.aspect_numerator,&p.options.aspect_denominator,&p.adaptive,&end)!=4 || p.adaptive>1)return 0;
            bit=PILOT_MOD_ASPECT;
        } else if(!strcmp(operation,"resolution")) {
            if(sscanf(arguments,"%u %n",&p.options.internal_height,&end)!=1 || !p.options.internal_height)return 0;
            bit=PILOT_MOD_RESOLUTION;
        } else if(!strcmp(operation,"antialiasing")) {
            if(sscanf(arguments,"%u %n",&p.options.antialiasing,&end)!=1 || !p.options.antialiasing)return 0;
            bit=PILOT_MOD_AA;
        } else if(!strcmp(operation,"supersampling")) {
            if(sscanf(arguments,"%u %n",&p.options.supersample,&end)!=1 || p.options.supersample==1)return 0;
            bit=PILOT_MOD_SUPERSAMPLING;
        } else if(!strcmp(operation,"filtering")) {
            if(sscanf(arguments,"%u %n",&p.options.anisotropy,&end)!=1 || p.options.anisotropy==1)return 0;
            bit=PILOT_MOD_FILTERING;
        } else if(!strcmp(operation,"frame-rate")) {
            if(sscanf(arguments,"%u %u %n",&p.options.frame_mode,&p.options.frame_rate,&end)!=2 || !p.options.frame_mode)return 0;
            bit=PILOT_MOD_FRAME_RATE;
        } else return 0;
        if(!end || arguments[end] || (p.operations&bit))return 0;
        p.operations|=bit;
    }
    return 0;
}
