#include "audio_device_trace.hpp"
#include "audio_retained_trace_trigger.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"stage trace FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static void retained_trigger_contract() {
    PilotRetainedTraceTrigger t{};
    CHECK(pilot_retained_trace_call(&t,7,1,100,PILOT_RETAINED_STOP)==0); // unused object
    CHECK(pilot_retained_trace_call(&t,7,2,100,PILOT_RETAINED_PLAY)==0);
    CHECK(pilot_retained_trace_call(&t,7,3,200,PILOT_RETAINED_STOP)==0); // other object
    CHECK(pilot_retained_trace_call(&t,7,4,100,PILOT_RETAINED_REBIND)==0);
    CHECK(pilot_retained_trace_call(&t,7,5,100,PILOT_RETAINED_STOP)==0); // new attachment
    CHECK(pilot_retained_trace_call(&t,7,6,100,PILOT_RETAINED_PLAY)==0);
    CHECK(pilot_retained_trace_call(&t,7,7,100,PILOT_RETAINED_FINAL_RELEASE)==0);
    CHECK(pilot_retained_trace_call(&t,7,8,100,PILOT_RETAINED_STOP)==0); // reused address
    CHECK(pilot_retained_trace_call(&t,7,9,100,PILOT_RETAINED_PLAY)==0);
    const auto before=t;
    CHECK(pilot_retained_trace_call(&t,8,10,100,PILOT_RETAINED_STOP)==-1);
    CHECK(!std::memcmp(&before,&t,sizeof(t)));
    CHECK(pilot_retained_trace_call(&t,7,10,100,PILOT_RETAINED_STOP)==1);
    CHECK(t.object==100 && t.prior_play==9 && t.stop==10 && t.lifetime==7);
    CHECK(pilot_retained_trace_call(&t,7,11,200,PILOT_RETAINED_STOP)==0); // one-shot
    CHECK(t.object==100 && t.prior_play==9 && t.stop==10);
    PilotRetainedTraceTrigger full{};
    for(unsigned i=0;i<32;++i)CHECK(pilot_retained_trace_call(&full,1,i+1,i+1,PILOT_RETAINED_PLAY)==0);
    const auto bounded=full;
    CHECK(pilot_retained_trace_call(&full,1,33,33,PILOT_RETAINED_PLAY)==-1);
    CHECK(!std::memcmp(&bounded,&full,sizeof(full)));
}
int main(int argc,char** argv) {
    CHECK(argc==3);const std::string mode=argv[1],path=std::string(argv[2])+"/"+mode+".bin";
    const bool retained=mode.compare(0,8,"retained")==0;
    CHECK(mode=="normal" || mode=="window" || mode=="capacity" || mode=="invalid" ||
          mode=="retained" || mode=="retained-window" || mode=="retained-capacity" || mode=="retained-untriggered");
    if(retained)retained_trigger_contract();
    std::unique_ptr<PilotAudioStageTrace> trace;
    CHECK(PilotAudioStageTrace::create(trace,path.c_str(),0,456));
    CHECK(!PilotAudioStageTrace::create(trace,path.c_str(),123,456,retained));
    CHECK(PilotAudioStageTrace::create(trace,path.c_str(),123,456));
    const uint32_t metadata[]={11,22,33,44,55,66,77};
    const float samples[]={.25f,-.5f,0.f,1.f,-1.f,.125f};
    const xlle::ApuObservation source{xlle::ApuObservationKind::Source,96,metadata,7,samples,sizeof(samples)};
    if(retained) {
        trace->record(580,source); // even real nonzero samples before Stop are excluded
        if(mode!="retained-untriggered") {
            CHECK(trace->trigger(455));CHECK(!trace->trigger(590));CHECK(trace->trigger(590));
            // Start with actual DSP input, not a nonzero-source heuristic.
            trace->record(600,{xlle::ApuObservationKind::EpInput,96,metadata,7,samples,sizeof(samples)});
            trace->record(700,source);
            if(mode=="retained-capacity")for(unsigned i=0;i<30;++i)trace->record(701+i,source);
            if(mode=="retained-window")trace->record(4500600,source);
        }
        CHECK(!trace->finish());CHECK(trace->trigger(800));
        trace->record(800,source);CHECK(!trace->finish());
        std::puts("PASS retained lifecycle trigger and bounded all-stage window");return 0;
    }
    trace->record(500,{xlle::ApuObservationKind::Mix,0,metadata,4,samples,sizeof(samples)});
    const float silent[]={0.f,-0.f,0.f,0.f,0.f,0.f};
    for(unsigned i=0;i<30;++i)trace->record(550+i,{xlle::ApuObservationKind::Source,67,metadata,7,silent,sizeof(silent)});
    trace->record(600,source);
    if(mode=="capacity")for(unsigned i=0;i<30;++i)trace->record(601+i,source);
    else if(mode=="invalid")trace->record(599,source);
    else {
        trace->record(700,{xlle::ApuObservationKind::Send,96,metadata,7,samples,sizeof(samples)});
        if(mode=="window")trace->record(4500600,source);
    }
    std::string published;
    if(mode=="window" || mode=="capacity") {
        // The device/title owner has not requested finalization. A full prefix
        // must already be independently readable while that owner stays alive.
        std::ifstream file(path+".partial",std::ios::binary);CHECK(file);
        published.assign(std::istreambuf_iterator<char>(file),{});
        CHECK(published.size()>=88 && !published.compare(0,8,"XLLESTG1") &&
              !published.compare(published.size()-24,8,"XLLEEND1"));
    }
    const char* error=trace->finish();CHECK((error!=nullptr)==(mode=="invalid"));
    CHECK(trace->finish()==error);trace->record(800,source); // finalized prefix is immutable
    if(!published.empty()) {
        std::ifstream file(path+".partial",std::ios::binary);CHECK(file);
        const std::string final{std::istreambuf_iterator<char>(file),{}};
        CHECK(final==published);
    }
    trace.reset();CHECK(PilotAudioStageTrace::create(trace,path.c_str(),123,456));
    std::puts("PASS bounded stage persistence and explicit partial state");
}
