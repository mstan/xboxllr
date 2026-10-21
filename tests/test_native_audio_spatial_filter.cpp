#define PILOT_AUDIO_DSP 1
#include "../pilots/native-title/audio_spatial_filter.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void check(bool value,const char *message)
{if(!value){std::fprintf(stderr,"%s\n",message);std::exit(1);}}
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
}
