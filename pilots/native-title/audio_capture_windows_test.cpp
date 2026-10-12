#include "audio_capture_windows.hpp"
#include <cstdio>
#include <initializer_list>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main()
{
    AudioCaptureWindows plan;
    CHECK(audio_capture_windows_parse(nullptr,30,plan));
    CHECK(!plan.explicit_windows && plan.count==1 && plan.items[0].start==0 && plan.items[0].seconds==30);
    CHECK(audio_capture_windows_parse("0:125,205:125,425:110",30,plan));
    CHECK(plan.explicit_windows && plan.count==3 && plan.items[2].start==425 && plan.items[2].seconds==110);
    for(const char *text:{"",":1","0:","-1:1","0:0","0:301","0:1,1:1","0:2,1:1",
                         "0:1,2:1,4:1,6:1","1200:1","901:300","0:1,","0:1x","4294967296:1","0:1, 2:1"}) {
        CHECK(!audio_capture_windows_parse(text,30,plan));
        CHECK(plan.count==3 && plan.items[2].start==425); // Rejection has no publication.
    }
    CHECK(audio_capture_windows_parse("900:300",30,plan));
    CHECK(!audio_capture_windows_parse(nullptr,0,plan) && !audio_capture_windows_parse(nullptr,301,plan));
    CHECK(audio_capture_remaining_ms(100,100)==0 && audio_capture_remaining_ms(101,100)==0);
    CHECK(audio_capture_remaining_ms(100,101)==1 && audio_capture_remaining_ms(100,10100)==1);
    CHECK(audio_capture_remaining_ms(100,10101)==2);
    std::puts("bounded capture schedule: one-shot, finite windows, exact deadline and nonpublication guards passed");
    return 0;
}
