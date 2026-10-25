/* Authored ISA expectations, using the actual shared generated CPU header. */
#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include "cases.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "native_sse_control.h"
uint32_t recomp_mxcsr_read(void){return pilot_native_mxcsr_read();}
ptrdiff_t g_xbox_mem_offset;
RECOMP_TLS uint32_t g_eax,g_ecx,g_edx,g_esp,g_ebx,g_esi,g_edi,g_ebp;
RECOMP_TLS uint32_t g_fs_base,g_seh_ebp,g_icall_saved_esp,g_icall_dispatch_form;
RECOMP_TLS double g_fp_stack[8];RECOMP_TLS int g_fp_top,g_fp_cmp,g_df;
RECOMP_TLS uint16_t g_fp_cc,g_fp_control_word=0x027f;
RECOMP_TLS volatile uint32_t *g_pilot_suspend_count;
void recomp_unimpl(const char *text,uint32_t address){fprintf(stderr,"unexpected guard %s %x\n",text,address);exit(90);}
void recomp_trace_enter(const char *name,uint32_t address){(void)name;(void)address;}
void recomp_trace_exit(const char *name,uint32_t address){(void)name;(void)address;}
#define CHECK(c) do{if(!(c)){fprintf(stderr,"FAIL native x87 line%d: %s\n",__LINE__,#c);return 1;}}while(0)
static unsigned parity(uint32_t value){unsigned count=0;for(unsigned i=0;i<8;++i)count+=(value>>i)&1;return !(count&1);}
int main(void)
{
    unsigned char *memory=calloc(1,0x20000);CHECK(memory);g_xbox_mem_offset=(ptrdiff_t)memory;
    const uint32_t pairs[][2]={{0,1},{1,0},{0,0},{2,0},{3,0}};unsigned runs=0;
    for(unsigned kind=0;kind<8;++kind)for(unsigned source=1;source<8;++source)
    for(unsigned top=0;top<8;++top)for(unsigned p=0;p<5;++p)for(int fcmp=-1;fcmp<=2;++fcmp) {
        uint32_t a=pairs[p][0],b=pairs[p][1];unsigned cf=a<b,zf=a==b,pf=parity(a-b);
        unsigned conditions[]={cf,zf,cf||zf,pf,!cf,!zf,!(cf||zf),!pf};
        double before[8];for(unsigned i=0;i<8;++i)before[i]=g_fp_stack[i]=4.25+i*11.5;
        g_fp_top=top;g_fp_cmp=fcmp;g_fp_cc=0x4500;g_eax=a;g_ebx=b;g_edx=0;g_esp=0x18000;
        cases[kind*7+source-1]();
        CHECK(g_fp_top==(int)top && g_fp_cmp==fcmp && g_fp_cc==0x4500 && g_esp==0x18004);
        CHECK(g_edx==(zf|(cf<<8)|(pf<<16)));
        for(unsigned i=0;i<8;++i)CHECK(g_fp_stack[i]==(i==top && conditions[kind]?before[(top+source)&7]:before[i]));
        ++runs;
    }
    CHECK(runs==8960);printf("PASS %u conditional moves: all8 CPU conditions, ST1..7,8 TOPs, true/false/unordered x87 state, preserved CF/ZF/PF and stack\n",runs);
    const double values[]={-2.0,-0.0,0.0,.008333333,1.0,INFINITY,-INFINITY,NAN};runs=0;
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<8;++j)for(unsigned maximum=0;maximum<2;++maximum) {
        double a=values[i],b=values[j];
        /* The original TEST/FCMOV predicates select A for a minimum tie, B
         * for a maximum tie, B for unordered minimum and A for unordered max. */
        double expected=maximum?((isnan(a)||isnan(b))?a:(a>b?a:b)):
            ((isnan(a)||isnan(b))?b:(a<=b?a:b));
        g_fp_top=5;g_fp_stack[5]=b;g_fp_stack[6]=a;g_esp=0x18000;
        (maximum?MAXIMUM:MINIMUM)();
        if(g_fp_top!=6 || g_esp!=0x18004 || memcmp(&g_fp_stack[6],&expected,sizeof(expected)))
            fprintf(stderr,"minmax i=%u j=%u max=%u top=%d esp=%x a=%a b=%a got=%a expected=%a\n",i,j,maximum,g_fp_top,g_esp,a,b,g_fp_stack[6],expected);
        CHECK(g_fp_top==6 && g_esp==0x18004 && !memcmp(&g_fp_stack[6],&expected,sizeof(expected)));++runs;
    }
    printf("PASS %u min/max: FCOM/FNSTSW/TEST/FCMOV idiom, signed-zero ties and actual fractional clamp input\n",runs);
    for(unsigned p=0;p<5;++p) {
        uint32_t a=pairs[p][0],b=pairs[p][1];g_eax=a;g_ebx=b;g_edx=0;g_esp=0x18000;
        g_fp_top=3;g_fp_stack[3]=2.5;g_fp_stack[5]=-7.5;JOINED();
        CHECK(g_fp_stack[3]==(a==b?-7.5:2.5) && g_edx==(a==b) && g_fp_top==3);
    }
    puts("PASS conditional move consumes flags across a basic-block edge");
    for(unsigned p=0;p<5;++p) {
        uint32_t a=pairs[p][0],b=pairs[p][1];g_eax=a;g_ebx=b;g_edx=0;g_esp=0x18000;
        double before[8];for(unsigned i=0;i<8;++i)before[i]=g_fp_stack[i]=i+1.25;
        g_fp_top=5;g_fp_cmp=-1;g_fp_cc=0xc7ff;g_fp_control_word=0x027f;CLEARED();
        CHECK(g_fp_top==5 && g_fp_cmp==-1 && g_fp_cc==0x4700 && g_fp_control_word==0x027f);
        CHECK(!memcmp(before,g_fp_stack,sizeof(before)) && g_esp==0x18004);
        CHECK(g_edx==((a==b)|((a<b)<<8)|(parity(a-b)<<16)));
    }
    puts("PASS clear exception status retains all comparison codes, TOP, values, control word and CPU flags");
    fenv_t environment;CHECK(!fegetenv(&environment));runs=0;
    const int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
    for(unsigned mode=0;mode<4;++mode) {
        CHECK(!fesetround(modes[mode]) && !feclearexcept(FE_ALL_EXCEPT));
        if(mode&1)CHECK(!feraiseexcept(FE_INVALID|FE_DIVBYZERO));
        uint32_t expected=pilot_native_mxcsr_read();CHECK(((expected>>13)&3)==mode);
        int flags=fetestexcept(FE_ALL_EXCEPT);
        for(unsigned indexed=0;indexed<2;++indexed)for(unsigned p=0;p<5;++p) {
            uint32_t a=pairs[p][0],b=pairs[p][1];g_eax=a;g_ebx=b;g_edx=0;g_esp=0x18000;g_esi=0x10ffc;
            g_fp_top=5;g_fp_cmp=2;g_fp_cc=0x4500;g_fp_control_word=0x027f;
            MEM32(0x10ffc)=MEM32(0x11004)=0xa5b6c7d8;MEM32(0x11000)=0;
            (indexed?INDEXED:STORED)();
            CHECK(MEM32(0x11000)==expected && MEM32(0x10ffc)==0xa5b6c7d8 && MEM32(0x11004)==0xa5b6c7d8);
            CHECK(g_eax==a && g_ebx==b && g_esi==0x10ffc && g_esp==0x18004);
            CHECK(g_edx==((a==b)|((a<b)<<8)|(parity(a-b)<<16)));
            CHECK(g_fp_top==5 && g_fp_cmp==2 && g_fp_cc==0x4500 && g_fp_control_word==0x027f);
            CHECK(fegetround()==modes[mode] && fetestexcept(FE_ALL_EXCEPT)==flags && pilot_native_mxcsr_read()==expected);
            ++runs;
        }
    }
    CHECK(runs==40 && !fesetenv(&environment));runs=0;
    for(unsigned source=0;source<8;++source)for(unsigned top=0;top<8;++top)
    for(unsigned p=0;p<5;++p) {
        uint32_t a=pairs[p][0],b=pairs[p][1];g_eax=a;g_ebx=b;g_edx=0;g_esp=0x18000;
        double before[8];for(unsigned i=0;i<8;++i)before[i]=g_fp_stack[i]=i+1.25;
        g_fp_top=top;g_fp_cmp=-1;g_fp_cc=0x4500;g_fp_control_word=0x027f;
        free_cases[source]();
        CHECK(g_fp_top==(int)top && g_fp_cmp==-1 && g_fp_cc==0x4500 && g_fp_control_word==0x027f);
        CHECK(!memcmp(before,g_fp_stack,sizeof(before)) && g_esp==0x18004);
        CHECK(g_edx==((a==b)|((a<b)<<8)|(parity(a-b)<<16)));++runs;
    }
    printf("PASS %u register frees: every ST/TOP, preserved values, conditions, control and CPU flags in tag-less native model\n",runs);
    free(memory);
    puts("PASS 40 SSE control stores: absolute/indexed DWORD, four rounding modes, preserved CPU flags, x87 state, native environment and adjacent bytes");
    return 0;
}
