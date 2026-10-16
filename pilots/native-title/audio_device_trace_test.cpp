#include "audio_device_trace.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"stage trace FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(int argc,char** argv) {
    CHECK(argc==3);const std::string mode=argv[1],path=std::string(argv[2])+"/"+mode+".bin";
    CHECK(mode=="normal" || mode=="window" || mode=="capacity" || mode=="invalid");
    std::unique_ptr<PilotAudioStageTrace> trace;
    CHECK(PilotAudioStageTrace::create(trace,path.c_str(),0,456));
    CHECK(!PilotAudioStageTrace::create(trace,path.c_str(),123,456));
    CHECK(PilotAudioStageTrace::create(trace,path.c_str(),123,456));
    const uint32_t metadata[]={11,22,33,44,55,66,77};
    const float samples[]={.25f,-.5f,0.f,1.f,-1.f,.125f};
    const xlle::ApuObservation source{xlle::ApuObservationKind::Source,96,metadata,7,samples,sizeof(samples)};
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
