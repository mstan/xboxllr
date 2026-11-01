#define PILOT_AUDIO_DSP 1
#include "../pilots/native-title/audio_spatial_filter.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cfenv>
#include <vector>
#if (defined(_M_X64) || defined(__x86_64__)) && !defined(SPATIAL_TEST_PORTABLE)
#include <immintrin.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#define SPATIAL_TEST_SSE 1
#endif

static void check(bool value,const char *message)
{if(!value){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
static unsigned flags()
{
    unsigned value=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef SPATIAL_TEST_SSE
    value|=(_mm_getcsr()&63)<<16;
#endif
    return value;
}
static void clear_flags()
{
    std::feclearexcept(FE_ALL_EXCEPT);
#ifdef SPATIAL_TEST_SSE
    _mm_setcsr(_mm_getcsr()&~63u);
#endif
}
static void same(const PilotSpatialFilter &a,const PilotSpatialFilter &b)
{
    check(!memcmp(a.history,b.history,sizeof(a.history)) &&
          !memcmp(a.coefficients,b.coefficients,sizeof(a.coefficients)) &&
          !memcmp(&a.delay,&b.delay,sizeof(a.delay)) && !memcmp(&a.direct,&b.direct,sizeof(a.direct)) &&
          !memcmp(&a.room,&b.room,sizeof(a.room)) && a.cursor==b.cursor && a.initialized==b.initialized,
          "pristine silent block changed smoothing or signal state");
}
static void pristine_blocks()
{
    fenv_t saved;std::fegetenv(&saved);
#ifdef SPATIAL_TEST_SSE
    unsigned saved_csr=_mm_getcsr();
    alignas(16) unsigned char saved_fx[512];_fxsave(saved_fx);
    unsigned csr_mask;memcpy(&csr_mask,saved_fx+28,sizeof(csr_mask));
#endif
    unsigned fast=0,blocks=0,skipped=0;const unsigned counts[]={0,1,7,31,32,127,480,511,9,65,17,3};
    for(int round:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO})for(unsigned mode=0;mode<4;++mode) {
        check(!std::fesetround(round),"rounding mode unavailable");
#ifdef SPATIAL_TEST_SSE
        unsigned control=(_mm_getcsr()&~(0x8040u|63u))|((mode&1)?0x8000u:0)|((mode&2)?0x40u:0);
        if((csr_mask && (control&~csr_mask)) || (!csr_mask && mode)){++skipped;continue;}
        _mm_setcsr(control);
        // One frame matters: a later smoothing step could mask the missing
        // denormal status by reading the subnormal coefficient itself.
        for(int target:{0,-100}) {
            PilotSpatialFilter ordinary,optimized;ordinary.initialized=optimized.initialized=true;
            ordinary.coefficients[1][30]=optimized.coefficients[1][30]=std::numeric_limits<float>::min();
            PilotAudioSpatialState p{};p.frequency_ratio=1;p.taps[1][30]=target*std::numeric_limits<float>::min();
            float a[6],b[6]{};volatile float zero=0;
            clear_flags();ordinary.sample(zero,p,a);unsigned expected=flags();clear_flags();
            if(round==FE_TONEAREST && optimized.silent_pristine_supported(p))optimized.silent_pristine(1,p);
            else optimized.sample(zero,p,b);
            check(flags()==expected,"one-frame normal to subnormal transition changed FP status");
            check(!memcmp(a,b,sizeof(a)),"one-frame pristine output changed");same(ordinary,optimized);
        }
#endif
        for(unsigned scenario=0;scenario<8;++scenario) {
            PilotSpatialFilter ordinary,optimized;
            for(unsigned batch=0;batch<12;++batch) {
                PilotAudioSpatialState p{};p.frequency_ratio=1;
                p.delay=(batch&1)?-3.25f:2.5f;p.direct_pole=.25f;p.room_pole=.75f;
                for(unsigned ear=0;ear<2;++ear)for(unsigned k=0;k<31;++k)
                    p.taps[ear][k]=((int)((k*7+ear*3+batch+scenario)%31)-15)/32.0f;
                if(scenario==1)p.delay=-0.0f;
                if(scenario==2)p.taps[1][4]=std::numeric_limits<float>::denorm_min();
                if(scenario==3)p.delay=std::numeric_limits<float>::denorm_min();
                if(batch==7){ordinary.reset();optimized.reset();}
                unsigned count=counts[batch];
                std::vector<float> a(count*6),b(count*6);
                clear_flags();
                for(unsigned frame=0;frame<count;++frame)ordinary.sample(0,p,a.data()+frame*6);
                unsigned expected=flags();clear_flags();
                bool eligible=false;
#ifdef SPATIAL_TEST_SSE
                eligible=round==FE_TONEAREST && optimized.silent_pristine_supported(p);
#endif
                if(eligible){optimized.silent_pristine(count,p);++fast;}
                else for(unsigned frame=0;frame<count;++frame)optimized.sample(0,p,b.data()+frame*6);
                unsigned actual=flags();
                if(actual!=expected)std::fprintf(stderr,"FP mismatch round=%d mode=%u scenario=%u batch=%u eligible=%u expected=%x actual=%x\n",round,mode,scenario,batch,(unsigned)eligible,expected,actual);
                check(actual==expected,"pristine silent block changed FP status");
                check(a.empty() || !memcmp(a.data(),b.data(),a.size()*sizeof(float)),"pristine silent block changed output bits");
                same(ordinary,optimized);++blocks;
            }
            PilotAudioSpatialState p{};p.frequency_ratio=1;p.taps[0][0]=.75f;p.taps[1][0]=-.5f;
            p.delay=1.25f;p.direct_pole=.5f;p.room_pole=.875f;
            for(unsigned frame=0;frame<256;++frame) {
                float a[6],b[6],input=frame<8?(frame&1?.5f:-.25f):0;
                ordinary.sample(input,p,a);optimized.sample(input,p,b);
                check(!memcmp(a,b,sizeof(a)),"first signal or audible tail changed after pristine blocks");same(ordinary,optimized);
            }
        }
    }
#ifdef SPATIAL_TEST_SSE
    check(fast>0,"no pristine fast block executed");
#endif
    std::fesetenv(&saved);
#ifdef SPATIAL_TEST_SSE
    _mm_setcsr(saved_csr);
#endif
    std::printf("Pristine spatial differential: %u blocks, %u fast blocks, %u unsupported controls skipped; smoothing, first signal, tails, reset and FP passed\n",blocks,fast,skipped);
}
int main()
{
    PilotAudioSpatialState p{};p.taps[0][0]=p.taps[1][0]=1;p.frequency_ratio=1;
    check(pilot_spatial_state_valid(p),"identity snapshot rejected");
    PilotSpatialFilter f;float out[6];f.sample(1,p,out);
    for(float value:out)check(value==1,"identity lost a direct/rear/room sample");
    f.sample(0,p,out);for(float value:out)check(value==0,"identity has an unexpected tail");
    PilotSpatialFilter delayed;p.delay=2;delayed.sample(1,p,out);
    check(out[0]==0 && out[1]==1 && out[2]==0 && out[3]==1,"ITD must delay only the selected ear");
    delayed.sample(0,p,out);delayed.sample(0,p,out);
    check(out[0]==1 && out[1]==0,"delayed ear lost its original sample");
    delayed.reset();delayed.sample(0,p,out);for(float value:out)check(value==0,"transport reset retained samples");
    PilotSpatialFilter hf;p.delay=0;p.direct_pole=.5f;p.room_pole=.75f;hf.sample(1,p,out);
    check(out[0]==.5f && out[2]==.5f && out[4]==.25f && out[5]==.5f,"room and direct filters must stay separate");
    p.delay=43;check(!pilot_spatial_state_valid(p),"out-of-range ITD accepted");
    p.delay=0;p.matrix[0]=std::numeric_limits<float>::quiet_NaN();
    check(!pilot_spatial_state_valid(p),"nonfinite send accepted");
    std::puts("Spatial filter impulse, ITD, room/direct and reset checks passed");
    pristine_blocks();
}
